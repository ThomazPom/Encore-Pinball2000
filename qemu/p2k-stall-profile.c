/*
 * pinball2000 IRQ0 stall profiler & missed-window classifier.
 *
 * Question this module answers:
 *   "When the guest is failing to keep up with PIT edges (raised >>
 *    serviced), WHERE is the CPU stuck and WHY did the edge miss its
 *    ~250 µs delivery window?"
 *
 * How:
 *   - On every PIT-tap rising edge the machine init in pinball2000.c
 *     calls p2k_stall_profile_sample(deficit) where
 *         deficit = irq0_raised - irq0_serviced.
 *   - When deficit ≥ threshold (default 1) we snapshot the i386 CPU
 *     state and PIC state and classify the miss into one of:
 *
 *       (a) IF=0 / guest CLI               -- halted=0, IF=0
 *       (b) IMR masks IRQ0                 -- master imr & 0x01
 *       (c) ISR busy / prev IRQ0 in flight -- master isr & 0x01
 *       (d) HLT not waking                 -- halted=1, IF=1, irr=01
 *       (e) polling loop (CLI + tight)     -- IF=0 + same EIP repeats
 *           (folded into (a); the per-class top-EIP table names it)
 *       (f) TCG/TB boundary delay          -- halted=0, IF=1, irr=01,
 *                                             interrupt_request HARD set
 *       (g) other                          -- everything else
 *
 *   - Per-class cumulative + delta counters; per-class small EIP
 *     histogram so we can name the dominant offender.
 *
 *   - Dumped on every audit snapshot (initial @3s, every 5s with
 *     P2K_DIAG=1, plus a final summary at exit).
 *
 * Cost: when disabled, one integer compare per PIT raise. When enabled,
 * one BQL-context CPUState read per raise above threshold. Effectively
 * zero on the guest hot path.
 *
 * Off by default. Enable with P2K_PROFILE_STALLS=1. Threshold tunable
 * via P2K_PROFILE_STALL_GAP (default 1 -- every miss is classified).
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/typedefs.h"
#include "hw/core/cpu.h"
#include "exec/cpu-interrupt.h"
#include "hw/intc/i8259.h"
#include "hw/isa/i8259_internal.h"
#include "target/i386/cpu.h"

#include "p2k-internal.h"

typedef enum {
    CLS_IF0_CLI = 0,    /* halted=0, IF=0 (includes polling loops -- top EIPs name them) */
    CLS_IMR_MASK,       /* master IMR has bit 0 set */
    CLS_ISR_BUSY,       /* master ISR has bit 0 set (prev IRQ0 still in service) */
    CLS_HLT_NOWAKE,     /* halted=1, IF=1, IRR&1, !ISR&1 -- HLT didn't wake */
    CLS_TB_DELAY,       /* halted=0, IF=1, !ISR&1, interrupt_request HARD pending */
    CLS_OTHER,
    CLS_COUNT
} StallClass;

static const char *cls_name[CLS_COUNT] = {
    "IF0_CLI",
    "IMR_MASK",
    "ISR_BUSY",
    "HLT_NOWAKE",
    "TB_DELAY",
    "OTHER",
};

#define P2K_STALL_EIP_BUCKETS 32u

typedef struct EipBucket {
    uint32_t eip;
    uint32_t count;
} EipBucket;

static bool        p2k_stall_enabled;
/* Default = 2: classify a sample only when at least one *prior* raise is
 * still unserved (this raise itself accounts for +1 in deficit). gap=1
 * tags every PIC edge as a "miss" even when delivery happens immediately
 * after, which massively inflates CLS_TB_DELAY. Override via
 * P2K_PROFILE_STALL_GAP=N. */
static unsigned    p2k_stall_gap_threshold = 2;

static uint64_t    cls_total[CLS_COUNT];
static uint64_t    cls_delta[CLS_COUNT];
static uint64_t    cls_total_samples;
static uint64_t    cls_delta_samples;
static uint32_t    cls_deficit_max_total;
static uint32_t    cls_deficit_max_delta;

static EipBucket   cls_eip_total[CLS_COUNT][P2K_STALL_EIP_BUCKETS];
static EipBucket   cls_eip_delta[CLS_COUNT][P2K_STALL_EIP_BUCKETS];

void p2k_stall_profile_init(void)
{
    const char *e = getenv("P2K_PROFILE_STALLS");
    p2k_stall_enabled = e && *e && strcmp(e, "0") != 0;
    if (!p2k_stall_enabled) {
        return;
    }
    const char *g = getenv("P2K_PROFILE_STALL_GAP");
    if (g && *g) {
        unsigned v = (unsigned)strtoul(g, NULL, 0);
        if (v > 0 && v < 1024) {
            p2k_stall_gap_threshold = v;
        }
    }
    info_report("pinball2000: stall-profiler armed (gap≥%u; "
                "P2K_PROFILE_STALLS=1; classes: %s/%s/%s/%s/%s/%s)",
                p2k_stall_gap_threshold,
                cls_name[0], cls_name[1], cls_name[2],
                cls_name[3], cls_name[4], cls_name[5]);
}

bool p2k_stall_profile_active(void)
{
    return p2k_stall_enabled;
}

/* Update one EIP histogram bucket: prefer existing entry, else weakest
 * slot is replaced. Coarse but cheap; the dump reads top-3 by count. */
static void eip_bucket_add(EipBucket *tbl, uint32_t eip)
{
    unsigned weakest = 0;
    uint32_t weakest_n = UINT32_MAX;
    for (unsigned i = 0; i < P2K_STALL_EIP_BUCKETS; i++) {
        if (tbl[i].eip == eip && tbl[i].count) {
            tbl[i].count++;
            return;
        }
        if (tbl[i].count == 0) {
            tbl[i].eip = eip;
            tbl[i].count = 1;
            return;
        }
        if (tbl[i].count < weakest_n) {
            weakest_n = tbl[i].count;
            weakest = i;
        }
    }
    /* Table full. Replace the weakest entry with this new EIP. */
    tbl[weakest].eip = eip;
    tbl[weakest].count = 1;
}

static StallClass classify(uint8_t imr, uint8_t irr, uint8_t isr,
                           bool halted, bool if_set,
                           uint32_t interrupt_request)
{
    /* Order matters: each class is mutually exclusive -- we report the
     * *primary* reason this raise was missed. */
    if (imr & 0x01) {
        return CLS_IMR_MASK;
    }
    if (isr & 0x01) {
        return CLS_ISR_BUSY;
    }
    if (halted) {
        if (if_set && (irr & 0x01)) {
            return CLS_HLT_NOWAKE;
        }
        return CLS_OTHER;
    }
    if (!if_set) {
        return CLS_IF0_CLI;
    }
    /* halted=0, IF=1, ISR clear: interrupt is pending in IRR; if
     * CPU_INTERRUPT_HARD is asserted on the CPU but TCG hasn't taken
     * it yet, that's the TB-boundary delay class. */
    if ((irr & 0x01) && (interrupt_request & CPU_INTERRUPT_HARD)) {
        return CLS_TB_DELAY;
    }
    return CLS_OTHER;
}

void p2k_stall_profile_sample(uint32_t deficit)
{
    if (!p2k_stall_enabled || deficit < p2k_stall_gap_threshold) {
        return;
    }
    CPUState *cs = qemu_get_cpu(0);
    if (!cs) {
        return;
    }
    CPUX86State *env = &X86_CPU(cs)->env;

    uint8_t imr = 0, irr = 0, isr = 0;
    if (isa_pic) {
        PICCommonState *m = (PICCommonState *)isa_pic;
        imr = m->imr; irr = m->irr; isr = m->isr;
    }
    bool if_set = (env->eflags & IF_MASK) != 0;
    bool halted = cs->halted != 0;
    uint32_t eip = (uint32_t)env->eip;
    uint32_t ireq = cs->interrupt_request;

    StallClass c = classify(imr, irr, isr, halted, if_set, ireq);
    cls_total[c]++;
    cls_delta[c]++;
    cls_total_samples++;
    cls_delta_samples++;
    if (deficit > cls_deficit_max_total) cls_deficit_max_total = deficit;
    if (deficit > cls_deficit_max_delta) cls_deficit_max_delta = deficit;

    eip_bucket_add(cls_eip_total[c], eip);
    eip_bucket_add(cls_eip_delta[c], eip);
}

/* Find top-N EIPs in tbl; write up to max_out entries of "0xPC×N"
 * into out (NUL-terminated). */
static void format_top_eips(const EipBucket *tbl, char *out, size_t outsz)
{
    EipBucket sorted[P2K_STALL_EIP_BUCKETS];
    memcpy(sorted, tbl, sizeof(sorted));
    /* Selection-sort top 3 by count desc. */
    for (unsigned i = 0; i < 3 && i < P2K_STALL_EIP_BUCKETS; i++) {
        unsigned best = i;
        for (unsigned j = i + 1; j < P2K_STALL_EIP_BUCKETS; j++) {
            if (sorted[j].count > sorted[best].count) {
                best = j;
            }
        }
        if (best != i) {
            EipBucket t = sorted[i]; sorted[i] = sorted[best]; sorted[best] = t;
        }
    }
    int off = 0;
    out[0] = '\0';
    for (unsigned i = 0; i < 3; i++) {
        if (sorted[i].count == 0) break;
        int w = snprintf(out + off, outsz - off, " 0x%08x×%u",
                         sorted[i].eip, sorted[i].count);
        if (w < 0 || (size_t)w >= outsz - off) break;
        off += w;
    }
}

void p2k_stall_profile_dump(void)
{
    if (!p2k_stall_enabled) {
        return;
    }
    if (cls_total_samples == 0) {
        info_report("p2k-stall: no missed windows yet "
                    "(deficit < %u every PIT raise so far)",
                    p2k_stall_gap_threshold);
        return;
    }

    /* Per-class summary line: cumulative + delta counts + percent of total. */
    char line[1024];
    int  off = 0;
    off += snprintf(line + off, sizeof(line) - off,
                    "p2k-class total=%llu (Δ=%llu) max_def=%u (Δ=%u) |",
                    (unsigned long long)cls_total_samples,
                    (unsigned long long)cls_delta_samples,
                    cls_deficit_max_total, cls_deficit_max_delta);
    for (unsigned i = 0; i < CLS_COUNT; i++) {
        double pct  = cls_total_samples
                    ? 100.0 * cls_total[i] / cls_total_samples : 0.0;
        double dpct = cls_delta_samples
                    ? 100.0 * cls_delta[i] / cls_delta_samples : 0.0;
        int w = snprintf(line + off, sizeof(line) - off,
                         " %s=%llu(%.0f%%,Δ%.0f%%)",
                         cls_name[i],
                         (unsigned long long)cls_total[i], pct, dpct);
        if (w < 0 || (size_t)w >= sizeof(line) - off) break;
        off += w;
    }
    info_report("%s", line);

    /* Per-class top-3 EIPs (cumulative) for the actionable classes. */
    static const StallClass interesting[] = {
        CLS_IF0_CLI, CLS_HLT_NOWAKE, CLS_OTHER, CLS_ISR_BUSY, CLS_TB_DELAY,
    };
    for (unsigned k = 0; k < ARRAY_SIZE(interesting); k++) {
        StallClass c = interesting[k];
        if (cls_total[c] == 0) continue;
        char buf[256];
        format_top_eips(cls_eip_total[c], buf, sizeof(buf));
        if (buf[0]) {
            info_report("p2k-class %s top_eips:%s", cls_name[c], buf);
        }
    }

    /* Reset per-snapshot delta counters (cumulative kept). */
    for (unsigned i = 0; i < CLS_COUNT; i++) {
        cls_delta[i] = 0;
        memset(cls_eip_delta[i], 0, sizeof(cls_eip_delta[i]));
    }
    cls_delta_samples = 0;
    cls_deficit_max_delta = 0;
}
