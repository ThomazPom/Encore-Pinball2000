/*
 * pinball2000 timing-audit panel.
 *
 * Question this module answers (every snapshot, single line):
 *   "Is Encore/QEMU running on QEMU virtual time, and do the observed
 *    IRQ0/clkint counters agree with PIT expectations?"
 *
 * What it reports (observer only; no guest-state mutation):
 *
 *   clock=<QEMU clock used for our timers>
 *   icount=<on/off>                     (icount_enabled())
 *   expected: pit0_div=<n> pit0_hz=<hz>
 *             irq0_edges_pit_expected=<n>
 *             (vtime_elapsed * pit0_hz; PIT sanity, not delivery proof)
 *   observed: irq0_raised=<n>           (PIT ch0 line rising edges into i8259)
 *             clkint_entered=<n>        (TCG helper at IDT[0x20] entry)
 *             eoi_seen=<n>              (master PIC EOI writes clearing IRQ0)
 *             delivery=<pct>            (clkint_entered / irq0_raised
 *                                        since audit arm)
 *             current_delivery=<pct>    (same ratio over latest report
 *                                        interval only)
 *             current_irq0_raised=<n> current_clkint_entered=<n>
 *   imr=<xx> irr=<xx> isr=<xx>          (i8259 master)
 *   idt20=<addr> handler=<clkint|panic-stub|null>
 *   shim=<0>                            (IRQ0 shim deleted in a294b49)
 *   wall=<s> vtime=<s> scale=<x>        (QEMU_CLOCK_REALTIME vs
 *                                        QEMU_CLOCK_VIRTUAL since arm)
 *   host_slow=<yes/no>                  (scale < 0.95)
 *
 * Cadence:
 *   - one initial line ~3 s after machine arm (gives PIT ch0 + IDT[0x20]
 *     time to be programmed)
 *   - if `P2K_DIAG=1` (or `run-qemu.sh -v`): a full report every 3 s
 *   - if `P2K_TIMING_SNAPSHOTS=1`: a lightweight benchmark report every 3 s
 *   - one final line at machine exit / QEMU shutdown
 *
 * Disable entirely with `P2K_NO_TIMING_AUDIT=1`.
 *
 * Notes:
 *   - irq0_edges_pit_expected is only the PIT-derived expected edge count.
 *     It must not be cited as proof that IRQ0 was delivered. Delivery proof
 *     comes from the observed irq0_raised/clkint_entered/eoi_seen counters.
 *   - The IRQ0 shim was deleted in a294b49; this panel keeps `shim=0`
 *     as a permanent guardrail so any future re-introduction shows up.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "qemu/notify.h"
#include <math.h>
#include <stdlib.h>
#include "system/cpu-timers.h"
#include "system/system.h"
#include "exec/cpu-common.h"
#if __has_include("exec/icount.h")
#include "exec/icount.h"
#endif
#include "hw/core/cpu.h"
#include "exec/translation-block.h"
#include "hw/intc/i8259.h"
#include "hw/isa/i8259_internal.h"
#include "hw/timer/i8254.h"
#include "hw/timer/i8254_internal.h"
#include "hw/isa/isa.h"
#include "target/i386/cpu.h"

#include "p2k-internal.h"
#include "p2k-qemu-compat.h"

#define P2K_AUDIT_INITIAL_NS  (3ull  * 1000ull * 1000ull * 1000ull)  /*  3 s */
#define P2K_AUDIT_PERIOD_NS   (3ull  * 1000ull * 1000ull * 1000ull)  /*  3 s */

/* IRQ0 panic-stub signature: if IDT[0x20] still points at this prologue
 * then clkint hasn't been installed yet and the timing path is not live. */
static const uint8_t p2k_panic_sig[7] = {
    0x55, 0x89, 0xE5, 0x60, 0x6A, 0x20, 0xE9
};

static QEMUTimer *p2k_audit_timer;
static Pinball2000MachineState *p2k_audit_state;

static bool p2k_pit_deadline_arm_enabled(void);
static void p2k_pit_deadline_arm(int64_t now_v);


static int64_t  p2k_audit_arm_wall_ns;     /* QEMU_CLOCK_REALTIME at arm */
static int64_t  p2k_audit_arm_vtime_ns;    /* QEMU_CLOCK_VIRTUAL  at arm */
static bool     p2k_audit_periodic;        /* true => arm follow-up */
static bool     p2k_audit_light_snapshots; /* benchmark-safe periodic subset */
static uint64_t p2k_audit_seq;

static uint64_t p2k_audit_irq0_raised;
static uint64_t p2k_audit_clkint_entered;
static uint64_t p2k_audit_eoi_seen;
static uint32_t p2k_audit_clkint_pc;

/* Handler-entry cadence, independent of which source raised IRQ0. This fills
 * the same steady timing fields for strict, HOTLOOP, and HOTLOOP+PIT. */
static int64_t  p2k_clkint_entry_last_wall_ns;
static uint64_t p2k_clkint_entry_n;
static uint64_t p2k_clkint_entry_sum_ns;
static long double p2k_clkint_entry_sum_sq_us;
static int64_t  p2k_clkint_entry_min_ns = INT64_MAX;
static int64_t  p2k_clkint_entry_max_ns;

/* Raise -> clkint latency tracking.
 *
 * Earlier versions of this file used a 256-entry raise-timestamp ring
 * indexed by clkint_count; with the i8259 IRR coalescing PIT edges
 * (raised - serviced regularly > 1000) the lookup index drifted into
 * stale slots and the reported "latency" was many PIT periods instead
 * of true intack delay. We now keep a single monotonic latest_raise_ns
 * and compute (entry_ns - latest_raise_ns) on every clkint entry: this
 * is the real "delay between the most recent PIT edge and the CPU
 * actually executing the handler", and is meaningful regardless of
 * coalescence. */
static int64_t  p2k_audit_latest_raise_ns;
#define P2K_AUDIT_LAT_WINDOW  4096u
static uint32_t p2k_audit_lat_us[P2K_AUDIT_LAT_WINDOW];
static uint32_t p2k_audit_lat_n;          /* total samples (saturates) */
static uint32_t p2k_audit_lat_idx;        /* next write slot in ring */
static uint32_t p2k_audit_lat_max_total;  /* cumulative max us */

/* Per-segment dwell tracking. Each segment is independently histogrammed
 * with the same fixed window; sample sizes are equal because every
 * complete IRQ0 cycle produces one of each. */
#define P2K_AUDIT_DWELL_WINDOW  4096u
typedef struct {
    uint32_t buf[P2K_AUDIT_DWELL_WINDOW];
    uint32_t n;
    uint32_t idx;
    uint32_t max_total;
} P2KDwell;

static P2KDwell p2k_dw_raise_intack;   /* PIC raise -> CPU intack       */
static P2KDwell p2k_dw_intack_entry;   /* CPU intack -> first handler insn */
static P2KDwell p2k_dw_entry_eoi;      /* handler entry -> PIC EOI write   */
static P2KDwell p2k_dw_eoi_iret;       /* PIC EOI -> IRET                  */
static P2KDwell p2k_dw_iret_raise;     /* IRET -> next PIT rising edge     */
/* Sub-segments of iret_raise (techlead 2026-05-21):
 *   iret_firsttb  = IRET -> first TB looked up by cpu_exec after IRET
 *                   (TCG re-enter overhead: cpu_exit, main-loop bounce,
 *                    pic_update_irq poll, TB cache lookup)
 *   firsttb_raise = first TB after IRET -> next PIT rising edge
 *                   (guest execution time until i8254 model raises its line)
 * Sum = iret_raise. The split tells us whether iret_raise is dominated by
 * QEMU re-enter overhead or by genuine TCG-executed guest code waiting
 * for the next PIT deadline. */
static P2KDwell p2k_dw_iret_firsttb;
static P2KDwell p2k_dw_firsttb_raise;

static int64_t  p2k_ts_intack_ns;
static int64_t  p2k_ts_entry_ns;
static int64_t  p2k_ts_eoi_ns;
static int64_t  p2k_ts_iret_ns;
static int64_t  p2k_ts_first_tb_after_iret_ns;
static bool     p2k_first_tb_pending;
static bool     p2k_in_clkint;
static uint64_t p2k_intack_seen_irq0;
static uint64_t p2k_iret_seen_after_clkint;

/* EOI EIP histogram (top hot EIPs at the moment of `out 0x20, 0x20`).
 * Reveals where in the clkint body the EOI is written: useful to
 * confirm the handler shape in the disassembly. Tiny fixed table of
 * 16 distinct EIPs keyed by exact match. */
#define P2K_AUDIT_EOI_EIP_SLOTS 16
static struct { uint32_t eip; uint64_t count; } p2k_eoi_eip[P2K_AUDIT_EOI_EIP_SLOTS];

/* One-shot guest-memory dump of the clkint handler. When env
 * P2K_DUMP_CLKINT is set (any non-empty / non-"0" value), the very
 * first call to p2k_timing_audit_note_clkint_enter() reads 1024 bytes
 * starting at idt20 from guest physical memory and writes them to
 * <P2K_DUMP_CLKINT> (path). Used to feed objdump for the clkint
 * disassembly audit. */
static bool p2k_clkint_dump_done;

/* PDB 0x05 (driver-board lamp/data refresh) gap tracker. The cabinet
 * lamp matrix sweep is paced by guest writes to LPT opcode 0x05; the
 * figure-of-merit for "is the guest meeting its display contract?" is
 * the gap between two consecutive 0x05 writes.
 *
 * Cabinet safety is WALL TIME, not vtime: real solenoids and lamps see
 * wall time. We therefore record both wall and vtime gaps into
 * independent dwell rings and report p50 / p95 / p99 / max.
 *
 * Two rings per axis:
 *   - "_total"  rolling 4096-deep window (proxy for cumulative)
 *   - "_delta"  cleared after every audit emit (current-window percentiles)
 */
static int64_t  p2k_audit_pdb05_last_vtime_ns;
static int64_t  p2k_audit_pdb05_last_wall_ns;
static uint64_t p2k_audit_pdb05_count;
static P2KDwell p2k_dw_pdb05_wall_total;
static P2KDwell p2k_dw_pdb05_wall_delta;
static P2KDwell p2k_dw_pdb05_vtime_total;
static P2KDwell p2k_dw_pdb05_vtime_delta;

/* Opt-in rare PDB-gap recorder. No event is printed on the hot path: the
 * fixed ring is dumped only at exit, so profiling cannot manufacture the
 * next gap through stderr I/O. Thread/process CPU clocks distinguish a vCPU
 * that was executing from one that was descheduled or blocked. */
#define P2K_PDB_GAP_EVENTS 64u
#define P2K_PDB_GAP_TB_BUCKETS 32u
#define P2K_PDB_GAP_TB_TOP 3u
#define P2K_PDB_GAP_TB_TAIL 64u
typedef struct P2KPdbGapTbBucket {
    uint32_t eip;
    uint32_t count;
} P2KPdbGapTbBucket;

typedef struct P2KPdbGapTbDuration {
    uint32_t eip;
    uint32_t wall_us;
} P2KPdbGapTbDuration;

typedef struct P2KPdbGapEvent {
    uint64_t at_wall_us;
    uint64_t wall_us;
    uint64_t vtime_us;
    uint64_t thread_cpu_us;
    uint64_t process_cpu_us;
    uint64_t irq_raised;
    uint64_t irq_serviced;
    uint64_t eoi_seen;
    uint64_t data_writes;
    uint64_t dispatches;
    uint64_t frames;
    uint32_t eip;
    uint32_t esp;
    uint32_t eflags;
    uint32_t interrupt_request;
    uint8_t imr;
    uint8_t irr;
    uint8_t isr;
    bool halted;
    bool in_clkint;
    uint64_t tb_count;
    P2KPdbGapTbBucket tb_top[P2K_PDB_GAP_TB_TOP];
    P2KPdbGapTbDuration tb_slowest[P2K_PDB_GAP_TB_TOP];
    uint32_t tb_tail[P2K_PDB_GAP_TB_TAIL];
    uint8_t tb_tail_count;
    P2KDcsProfileSnapshot dcs;
} P2KPdbGapEvent;

static bool p2k_pdb_gap_profile_enabled;
static uint64_t p2k_pdb_gap_threshold_us = 1000;
static uint64_t p2k_pdb_gap_after_us;
static uint64_t p2k_pdb_gap_thread_cpu_last_us;
static uint64_t p2k_pdb_gap_process_cpu_last_us;
static uint64_t p2k_pdb_gap_irq_raised_last;
static uint64_t p2k_pdb_gap_irq_serviced_last;
static uint64_t p2k_pdb_gap_eoi_last;
static uint64_t p2k_pdb_gap_data_last;
static uint64_t p2k_pdb_gap_dispatch_last;
static uint64_t p2k_pdb_gap_frames_last;
static P2KPdbGapEvent p2k_pdb_gap_events[P2K_PDB_GAP_EVENTS];
static unsigned p2k_pdb_gap_event_count;
static uint64_t p2k_pdb_gap_observed;
static uint64_t p2k_pdb_gap_tb_count;
static P2KPdbGapTbBucket p2k_pdb_gap_tb_buckets[P2K_PDB_GAP_TB_BUCKETS];
static uint32_t p2k_pdb_gap_tb_tail[P2K_PDB_GAP_TB_TAIL];
static unsigned p2k_pdb_gap_tb_tail_count;
static unsigned p2k_pdb_gap_tb_tail_next;
static P2KPdbGapTbDuration p2k_pdb_gap_tb_slowest[P2K_PDB_GAP_TB_TOP];
static int64_t p2k_pdb_gap_tb_last_wall_ns;
static uint32_t p2k_pdb_gap_tb_last_eip;

static void p2k_pdb_gap_add_tb_duration(uint32_t eip, uint64_t wall_us)
{
    uint32_t bounded = wall_us > UINT32_MAX ? UINT32_MAX : wall_us;
    for (unsigned i = 0; i < P2K_PDB_GAP_TB_TOP; i++) {
        if (bounded > p2k_pdb_gap_tb_slowest[i].wall_us) {
            for (unsigned j = P2K_PDB_GAP_TB_TOP - 1; j > i; j--) {
                p2k_pdb_gap_tb_slowest[j] = p2k_pdb_gap_tb_slowest[j - 1];
            }
            p2k_pdb_gap_tb_slowest[i] = (P2KPdbGapTbDuration) {
                .eip = eip,
                .wall_us = bounded,
            };
            break;
        }
    }
}

/* Opt-in only: retain the guest path between two PDB completions. Recording
 * one EIP and timestamp per TB avoids timers, logging, and locks on the vCPU
 * path. The state is discarded every normal ~250 us interval and copied to
 * an event only when that interval later proves to be unusually long. The
 * clock-read cost is why this profiler must not supply headline benchmarks. */
static void p2k_pdb_gap_note_tb(void)
{
    if (!p2k_pdb_gap_profile_enabled || !first_cpu) {
        return;
    }
    int64_t now_w = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    uint32_t eip = X86_CPU(first_cpu)->env.eip;
    if (p2k_pdb_gap_tb_last_wall_ns &&
        now_w > p2k_pdb_gap_tb_last_wall_ns) {
        p2k_pdb_gap_add_tb_duration(
            p2k_pdb_gap_tb_last_eip,
            (uint64_t)(now_w - p2k_pdb_gap_tb_last_wall_ns) / 1000ull);
    }
    p2k_pdb_gap_tb_last_wall_ns = now_w;
    p2k_pdb_gap_tb_last_eip = eip;
    p2k_pdb_gap_tb_count++;
    p2k_pdb_gap_tb_tail[p2k_pdb_gap_tb_tail_next] = eip;
    p2k_pdb_gap_tb_tail_next =
        (p2k_pdb_gap_tb_tail_next + 1) % P2K_PDB_GAP_TB_TAIL;
    if (p2k_pdb_gap_tb_tail_count < P2K_PDB_GAP_TB_TAIL) {
        p2k_pdb_gap_tb_tail_count++;
    }

    unsigned weakest = 0;
    uint32_t weakest_count = UINT32_MAX;
    for (unsigned i = 0; i < P2K_PDB_GAP_TB_BUCKETS; i++) {
        P2KPdbGapTbBucket *bucket = &p2k_pdb_gap_tb_buckets[i];
        if (bucket->count && bucket->eip == eip) {
            bucket->count++;
            return;
        }
        if (!bucket->count) {
            bucket->eip = eip;
            bucket->count = 1;
            return;
        }
        if (bucket->count < weakest_count) {
            weakest = i;
            weakest_count = bucket->count;
        }
    }
    p2k_pdb_gap_tb_buckets[weakest] = (P2KPdbGapTbBucket) {
        .eip = eip,
        .count = 1,
    };
}

static void p2k_pdb_gap_reset_tb(void)
{
    p2k_pdb_gap_tb_count = 0;
    memset(p2k_pdb_gap_tb_buckets, 0, sizeof(p2k_pdb_gap_tb_buckets));
    p2k_pdb_gap_tb_tail_count = 0;
    p2k_pdb_gap_tb_tail_next = 0;
    memset(p2k_pdb_gap_tb_slowest, 0, sizeof(p2k_pdb_gap_tb_slowest));
}

/* PDB completion can occur inside the current translated block's MMIO OUT
 * helper, before the next TB lookup. Close that partial block here so the
 * helper's elapsed time belongs to the interval that actually contained it.
 * Move the origin to completion to avoid counting the same time twice at the
 * next TB boundary. */
static void p2k_pdb_gap_close_tb(int64_t now_w)
{
    if (!p2k_pdb_gap_profile_enabled || !p2k_pdb_gap_tb_last_wall_ns ||
        now_w <= p2k_pdb_gap_tb_last_wall_ns) {
        return;
    }
    p2k_pdb_gap_add_tb_duration(
        p2k_pdb_gap_tb_last_eip,
        (uint64_t)(now_w - p2k_pdb_gap_tb_last_wall_ns) / 1000ull);
    p2k_pdb_gap_tb_last_wall_ns = now_w;
}

static void p2k_pdb_gap_copy_tb(P2KPdbGapEvent *event)
{
    P2KPdbGapTbBucket sorted[P2K_PDB_GAP_TB_BUCKETS];
    memcpy(sorted, p2k_pdb_gap_tb_buckets, sizeof(sorted));
    for (unsigned i = 0; i < P2K_PDB_GAP_TB_TOP; i++) {
        unsigned best = i;
        for (unsigned j = i + 1; j < P2K_PDB_GAP_TB_BUCKETS; j++) {
            if (sorted[j].count > sorted[best].count) {
                best = j;
            }
        }
        if (best != i) {
            P2KPdbGapTbBucket tmp = sorted[i];
            sorted[i] = sorted[best];
            sorted[best] = tmp;
        }
        event->tb_top[i] = sorted[i];
    }
    event->tb_count = p2k_pdb_gap_tb_count;
    memcpy(event->tb_slowest, p2k_pdb_gap_tb_slowest,
           sizeof(event->tb_slowest));
    event->tb_tail_count = p2k_pdb_gap_tb_tail_count;
    unsigned oldest = p2k_pdb_gap_tb_tail_count < P2K_PDB_GAP_TB_TAIL
        ? 0 : p2k_pdb_gap_tb_tail_next;
    for (unsigned i = 0; i < p2k_pdb_gap_tb_tail_count; i++) {
        event->tb_tail[i] =
            p2k_pdb_gap_tb_tail[(oldest + i) % P2K_PDB_GAP_TB_TAIL];
    }
}

static uint64_t p2k_cpu_clock_us(clockid_t clock_id)
{
    struct timespec ts;
    if (clock_gettime(clock_id, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000ull +
           (uint64_t)ts.tv_nsec / 1000ull;
}

static void p2k_pdb_gap_record(uint64_t wall_us, uint64_t vtime_us,
                               int64_t now_w)
{
    uint64_t thread_cpu_us = 0;
    uint64_t process_cpu_us = 0;
#ifdef CLOCK_THREAD_CPUTIME_ID
    thread_cpu_us = p2k_cpu_clock_us(CLOCK_THREAD_CPUTIME_ID);
#endif
#ifdef CLOCK_PROCESS_CPUTIME_ID
    process_cpu_us = p2k_cpu_clock_us(CLOCK_PROCESS_CPUTIME_ID);
#endif

    uint64_t irq_raised = p2k_audit_irq0_raised;
    uint64_t irq_serviced = p2k_audit_clkint_entered;
    uint64_t eoi_seen = p2k_audit_eoi_seen;
    uint64_t data_writes = p2k_lpt_get_data_writes();
    uint64_t dispatches = p2k_lpt_get_dispatches();
    uint64_t frames = p2k_display_get_frames();

    bool eligible = p2k_audit_arm_wall_ns &&
        (uint64_t)(now_w - p2k_audit_arm_wall_ns) >= p2k_pdb_gap_after_us * 1000ull;
    if (eligible && wall_us >= p2k_pdb_gap_threshold_us) {
        p2k_pdb_gap_observed++;
        if (p2k_pdb_gap_event_count < P2K_PDB_GAP_EVENTS) {
            P2KPdbGapEvent *event =
                &p2k_pdb_gap_events[p2k_pdb_gap_event_count++];
            event->at_wall_us = (now_w - p2k_audit_arm_wall_ns) / 1000ull;
            event->wall_us = wall_us;
            event->vtime_us = vtime_us;
            event->thread_cpu_us = p2k_pdb_gap_thread_cpu_last_us
                ? thread_cpu_us - p2k_pdb_gap_thread_cpu_last_us : 0;
            event->process_cpu_us = p2k_pdb_gap_process_cpu_last_us
                ? process_cpu_us - p2k_pdb_gap_process_cpu_last_us : 0;
            event->irq_raised = irq_raised - p2k_pdb_gap_irq_raised_last;
            event->irq_serviced = irq_serviced - p2k_pdb_gap_irq_serviced_last;
            event->eoi_seen = eoi_seen - p2k_pdb_gap_eoi_last;
            event->data_writes = data_writes - p2k_pdb_gap_data_last;
            event->dispatches = dispatches - p2k_pdb_gap_dispatch_last;
            event->frames = frames - p2k_pdb_gap_frames_last;
            event->in_clkint = p2k_in_clkint;
            p2k_pdb_gap_copy_tb(event);

            CPUState *cs = qemu_get_cpu(0);
            if (cs) {
                CPUX86State *env = &X86_CPU(cs)->env;
                event->eip = env->eip;
                event->esp = env->regs[R_ESP];
                event->eflags = env->eflags;
                event->interrupt_request = cs->interrupt_request;
                event->halted = cs->halted != 0;
            }
            if (isa_pic) {
                PICCommonState *m = (PICCommonState *)isa_pic;
                event->imr = m->imr;
                event->irr = m->irr;
                event->isr = m->isr;
            }
            p2k_dcs_adsp_profile_snapshot(&event->dcs);
        }
    }

    p2k_pdb_gap_thread_cpu_last_us = thread_cpu_us;
    p2k_pdb_gap_process_cpu_last_us = process_cpu_us;
    p2k_pdb_gap_irq_raised_last = irq_raised;
    p2k_pdb_gap_irq_serviced_last = irq_serviced;
    p2k_pdb_gap_eoi_last = eoi_seen;
    p2k_pdb_gap_data_last = data_writes;
    p2k_pdb_gap_dispatch_last = dispatches;
    p2k_pdb_gap_frames_last = frames;
}

static void p2k_pdb_gap_profile_dump(void)
{
    if (!p2k_pdb_gap_profile_enabled) {
        return;
    }
    info_report("p2k-pdb-gap-profile threshold=%lluus after=%lluus "
                "observed=%llu stored=%u",
                (unsigned long long)p2k_pdb_gap_threshold_us,
                (unsigned long long)p2k_pdb_gap_after_us,
                (unsigned long long)p2k_pdb_gap_observed,
                p2k_pdb_gap_event_count);
    for (unsigned i = 0; i < p2k_pdb_gap_event_count; i++) {
        const P2KPdbGapEvent *e = &p2k_pdb_gap_events[i];
        int64_t host_wait_us = (int64_t)e->wall_us -
                               (int64_t)e->thread_cpu_us;
        info_report("p2k-pdb-gap #%u at=%.3fms wall=%lluus vtime=%lluus "
                    "vcpu=%lluus process=%lluus host_wait=%lldus "
                    "irq=%llu/%llu eoi=%llu data=%llu dispatch=%llu frames=%llu "
                    "eip=%08x esp=%08x flags=%08x halted=%d in_clkint=%d "
                    "imr=%02x irr=%02x isr=%02x ireq=%08x "
                    "dcs=%d state_busy=%d core_busy=%d worker=%d/%d "
                    "host_boot=%d hybrid=%d commands=%u ring=%u rate=%d "
                    "cycles=%llu pcm=%llu",
                    i, e->at_wall_us / 1000.0,
                    (unsigned long long)e->wall_us,
                    (unsigned long long)e->vtime_us,
                    (unsigned long long)e->thread_cpu_us,
                    (unsigned long long)e->process_cpu_us,
                    (long long)host_wait_us,
                    (unsigned long long)e->irq_raised,
                    (unsigned long long)e->irq_serviced,
                    (unsigned long long)e->eoi_seen,
                    (unsigned long long)e->data_writes,
                    (unsigned long long)e->dispatches,
                    (unsigned long long)e->frames,
                    e->eip, e->esp, e->eflags, e->halted, e->in_clkint,
                    e->imr, e->irr, e->isr, e->interrupt_request,
                    e->dcs.available, e->dcs.state_lock_busy,
                    e->dcs.core_lock_busy, e->dcs.worker_started,
                    e->dcs.worker_run, e->dcs.host_boot,
                    e->dcs.hybrid_engine, e->dcs.command_count,
                    e->dcs.ring_frames, e->dcs.output_rate,
                    (unsigned long long)e->dcs.cycles,
                    (unsigned long long)e->dcs.pcm_frames);
        char trace[2048];
        size_t off = 0;
        off += snprintf(trace + off, sizeof(trace) - off,
                        "p2k-pdb-gap-trace #%u tb=%llu top=",
                        i, (unsigned long long)e->tb_count);
        for (unsigned j = 0; j < P2K_PDB_GAP_TB_TOP &&
                             off < sizeof(trace); j++) {
            if (!e->tb_top[j].count) {
                continue;
            }
            off += snprintf(trace + off, sizeof(trace) - off,
                            "%s%08x:%u", j ? "," : "",
                            e->tb_top[j].eip, e->tb_top[j].count);
        }
        off += snprintf(trace + off, sizeof(trace) - off, " slow=");
        for (unsigned j = 0; j < P2K_PDB_GAP_TB_TOP &&
                             off < sizeof(trace); j++) {
            if (!e->tb_slowest[j].wall_us) {
                continue;
            }
            off += snprintf(trace + off, sizeof(trace) - off,
                            "%s%08x:%uus", j ? "," : "",
                            e->tb_slowest[j].eip,
                            e->tb_slowest[j].wall_us);
        }
        off += snprintf(trace + off, sizeof(trace) - off, " tail=");
        for (unsigned j = 0; j < e->tb_tail_count &&
                             off < sizeof(trace); j++) {
            off += snprintf(trace + off, sizeof(trace) - off,
                            "%s%08x", j ? "," : "", e->tb_tail[j]);
        }
        info_report("%s", trace);
    }
}

/* XINU time-advance / drift tracking (synthetic).
 *
 * The guest's tick count is incremented once per IRQ0 service, i.e.
 * once per clkint entry, and the PIT is programmed for a fixed period
 * (typically 250 µs). XINU's notion of elapsed time is therefore
 *
 *   xinu_advance_ms = clkint_entered * (1000.0 / pit0_hz)
 *
 * which we compare against wall_elapsed_ms to derive the drift ratio
 * (1.0 = perfect; ~0.42 = guest only sees 42% of real time). Both
 * cumulative (since arm) and delta (since last snapshot) are emitted.
 */
static uint64_t p2k_audit_prev_clkint_entered;
static int64_t  p2k_audit_prev_wall_ns;
static int64_t  p2k_audit_prev_vtime_ns;

static void p2k_dwell_push(P2KDwell *d, uint64_t us)
{
    if (us > UINT32_MAX) us = UINT32_MAX;
    d->buf[d->idx] = (uint32_t)us;
    d->idx = (d->idx + 1) % P2K_AUDIT_DWELL_WINDOW;
    if (d->n < P2K_AUDIT_DWELL_WINDOW) d->n++;
    if (us > d->max_total) d->max_total = (uint32_t)us;
}

static void p2k_dwell_reset(P2KDwell *d)
{
    d->n = 0;
    d->idx = 0;
    d->max_total = 0;
}

void p2k_timing_audit_note_irq0_raised(void)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    /* iret -> raise gap (closes a complete IRQ0 cycle). Only push when
     * we have a previous IRET timestamp; the first raise after boot
     * has no predecessor. */
    if (p2k_ts_iret_ns) {
        if (now > p2k_ts_iret_ns) {
            p2k_dwell_push(&p2k_dw_iret_raise,
                           (uint64_t)(now - p2k_ts_iret_ns) / 1000ull);
        }
        /* Consume so we don't keep crediting the same IRET to every
         * subsequent raise during a long deficit. */
        p2k_ts_iret_ns = 0;
    }
    /* iret_raise sub-segment: gap between first TB after IRET and this raise. */
    if (p2k_ts_first_tb_after_iret_ns && now > p2k_ts_first_tb_after_iret_ns) {
        p2k_dwell_push(&p2k_dw_firsttb_raise,
                       (uint64_t)(now - p2k_ts_first_tb_after_iret_ns) / 1000ull);
    }
    p2k_ts_first_tb_after_iret_ns = 0;
    p2k_first_tb_pending = false;
    p2k_audit_latest_raise_ns = now;
    p2k_audit_irq0_raised++;
}

uint64_t p2k_timing_audit_get_irq0_raised(void)
{
    return p2k_audit_irq0_raised;
}

uint64_t p2k_timing_audit_get_irq0_serviced(void)
{
    return p2k_audit_clkint_entered;
}

void p2k_timing_audit_note_intack(int intno)
{
    if (intno != 0x20) {
        return;
    }
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    p2k_intack_seen_irq0++;
    /* raise -> intack: time the PIC + CPU took to actually accept the
     * already-pending edge. Uses the LATEST raise timestamp, which is
     * the most-recent edge regardless of coalescence. */
    if (p2k_audit_latest_raise_ns && now > p2k_audit_latest_raise_ns) {
        p2k_dwell_push(&p2k_dw_raise_intack,
                       (uint64_t)(now - p2k_audit_latest_raise_ns) / 1000ull);
    }
    p2k_ts_intack_ns = now;
}

static void p2k_eoi_eip_bump(uint32_t eip)
{
    int free = -1;
    for (int i = 0; i < P2K_AUDIT_EOI_EIP_SLOTS; i++) {
        if (p2k_eoi_eip[i].count == 0 && free < 0) free = i;
        if (p2k_eoi_eip[i].eip == eip && p2k_eoi_eip[i].count) {
            p2k_eoi_eip[i].count++;
            return;
        }
    }
    if (free >= 0) {
        p2k_eoi_eip[free].eip = eip;
        p2k_eoi_eip[free].count = 1;
    }
}

static void p2k_clkint_dump_oneshot(uint32_t base)
{
    if (p2k_clkint_dump_done) return;
    const char *path = getenv("P2K_DUMP_CLKINT");
    if (!path || !*path || !strcmp(path, "0")) return;
    /* Default 1024 bytes. P2K_DUMP_CLKINT_LEN can override (decimal). */
    size_t len = 1024;
    const char *lenenv = getenv("P2K_DUMP_CLKINT_LEN");
    if (lenenv && *lenenv) {
        unsigned long v = strtoul(lenenv, NULL, 0);
        if (v >= 16 && v <= (1u<<20)) len = v;
    }
    /* P2K_DUMP_CLKINT_BASE can override the base address (hex/dec/0x...).
     * Useful to dump clkint_body (called by the IDT stub) instead of the
     * 11-byte stub itself. When unset, dumps from the IDT entry. */
    uint32_t dump_base = base;
    const char *baseenv = getenv("P2K_DUMP_CLKINT_BASE");
    if (baseenv && *baseenv) {
        dump_base = (uint32_t)strtoul(baseenv, NULL, 0);
    }
    uint8_t *buf = g_malloc(len);
    cpu_physical_memory_read(dump_base, buf, len);
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(buf, 1, len, f);
        fclose(f);
        info_report("p2k-timing: clkint dumped %zu bytes @ 0x%08x to %s",
                    len, dump_base, path);
    }
    g_free(buf);
    p2k_clkint_dump_done = true;
}

void p2k_timing_audit_note_pic_eoi(bool master, int irq, uint8_t ocw2)
{
    if (master && irq == 0) {
        int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        p2k_audit_eoi_seen++;

        /* entry -> EOI dwell (handler work before EOI). */
        if (p2k_in_clkint && p2k_ts_entry_ns && now >= p2k_ts_entry_ns) {
            p2k_dwell_push(&p2k_dw_entry_eoi,
                           (uint64_t)(now - p2k_ts_entry_ns) / 1000ull);
        }
        p2k_ts_eoi_ns = now;

        /* Capture EIP at the moment the EOI is written. current_cpu is
         * the CPU thread that issued the I/O write into the i8259, i.e.
         * the guest CPU running clkint (BQL is held). */
        if (current_cpu) {
            CPUX86State *env = &X86_CPU(current_cpu)->env;
            p2k_eoi_eip_bump((uint32_t)env->eip);
        }
    }
}

void p2k_timing_audit_note_clkint_enter(uint64_t eip)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int64_t now_wall = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

    if (p2k_audit_arm_wall_ns && p2k_clkint_entry_last_wall_ns &&
        now_wall >= p2k_audit_arm_wall_ns + 30000000000LL) {
        int64_t gap_ns = now_wall - p2k_clkint_entry_last_wall_ns;
        if (gap_ns > 0) {
            uint64_t gap_us = gap_ns / 1000;
            p2k_clkint_entry_n++;
            p2k_clkint_entry_sum_ns += gap_ns;
            p2k_clkint_entry_sum_sq_us +=
                (long double)gap_us * (long double)gap_us;
            p2k_clkint_entry_min_ns = MIN(p2k_clkint_entry_min_ns, gap_ns);
            p2k_clkint_entry_max_ns = MAX(p2k_clkint_entry_max_ns, gap_ns);
        }
    }
    p2k_clkint_entry_last_wall_ns = now_wall;

    /* intack -> entry dwell (CPU frame-push + CS:EIP load + first
     * instruction). Only valid when we observed the corresponding
     * intack for this cycle. */
    if (p2k_ts_intack_ns && now >= p2k_ts_intack_ns) {
        p2k_dwell_push(&p2k_dw_intack_entry,
                       (uint64_t)(now - p2k_ts_intack_ns) / 1000ull);
        p2k_ts_intack_ns = 0;
    }

    /* Legacy raise -> entry latency (now meaningful: latest_raise_ns is
     * monotonic, never stale). */
    if (p2k_audit_latest_raise_ns && now > p2k_audit_latest_raise_ns) {
        uint64_t lat_us = (uint64_t)(now - p2k_audit_latest_raise_ns) / 1000ull;
        if (lat_us > UINT32_MAX) lat_us = UINT32_MAX;
        p2k_audit_lat_us[p2k_audit_lat_idx] = (uint32_t)lat_us;
        p2k_audit_lat_idx = (p2k_audit_lat_idx + 1) % P2K_AUDIT_LAT_WINDOW;
        if (p2k_audit_lat_n < P2K_AUDIT_LAT_WINDOW) p2k_audit_lat_n++;
        if (lat_us > p2k_audit_lat_max_total) {
            p2k_audit_lat_max_total = (uint32_t)lat_us;
        }
    }

    p2k_ts_entry_ns = now;
    p2k_in_clkint = true;
    p2k_clkint_dump_oneshot((uint32_t)eip);
    p2k_audit_clkint_entered++;
}

void p2k_timing_audit_note_iret(uint32_t eip)
{
    if (!p2k_in_clkint) {
        return;
    }
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    /* EOI -> IRET dwell (handler work after EOI + return path). */
    if (p2k_ts_eoi_ns && now >= p2k_ts_eoi_ns) {
        p2k_dwell_push(&p2k_dw_eoi_iret,
                       (uint64_t)(now - p2k_ts_eoi_ns) / 1000ull);
    }
    p2k_ts_eoi_ns = 0;
    p2k_ts_iret_ns = now;
    p2k_first_tb_pending = true;
    p2k_ts_first_tb_after_iret_ns = 0;
    p2k_in_clkint = false;
    p2k_iret_seen_after_clkint++;

    /* After the IRQ0 IRET, arm a short virtual-clock timer aimed at the
     * next projected i8254 deadline (latest_raise_ns + pit_period_ns) and
     * cpu_exit() the vCPU when it fires, so QEMU's main loop re-polls right
     * as the i8254 model is about to raise the next IRQ0. We do not inject
     * a synthetic edge — the i8254 still drives its own line; this only
     * rendezvous's the vCPU with the iothread at the right moment.
     *
     * Measured to lift natural delivery ~+1.8pp / drift +0.018 vs upstream
     * default (see docs/12). On by default; an internal off-switch
     * (P2K_NO_IRQ0_PIT_DEADLINE_TIMER=1) exists only for regression A/B. */
    if (p2k_pit_deadline_arm_enabled()) {
        p2k_pit_deadline_arm(now);
    }
}

/* Hook called from accel/tcg/cpu-exec.c for every TB the main scheduler
 * loop is about to look up. During the post-IRQ0-IRET window we OR-in
 * CF_NO_GOTO_TB | CF_NO_GOTO_PTR so the just-translated TB returns to
 * cpu_exec rather than chaining to the next TB via goto_tb / goto_ptr.
 * Outside the window this is the identity function — and the upstream
 * patch ships a __weak default with the same semantics so plain QEMU
 * builds without the p2k object link cleanly. */
/* Hook called from accel/tcg/cpu-exec.c for every TB the main scheduler
 * loop is about to look up. It captures the first-TB-after-IRET timestamp
 * for the iret_raise segment audit and, when direct-clkint dispatch owes
 * the guest a tick, forces the TB to return to cpu_exec_loop so the
 * dispatcher runs at TB granularity. Outside those cases it is the
 * identity function — the upstream patch ships a __weak default with the
 * same semantics so plain QEMU builds without the p2k object link
 * cleanly. */
uint32_t p2k_tcg_cflags_override(uint32_t base);
uint32_t p2k_tcg_cflags_override(uint32_t base)
{
    if (unlikely(p2k_guest_extensions_pending)) {
        p2k_guest_extensions_maybe_install();
    }
    if (unlikely(p2k_pdb_gap_profile_enabled)) {
        p2k_pdb_gap_note_tb();
    }
    /* iret_raise sub-segment: capture the timestamp of the first TB
     * lookup performed after IRET. Cheap: one boolean test on the
     * fast path, one timestamp + push when pending. */
    if (unlikely(p2k_first_tb_pending)) {
        int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        if (p2k_ts_iret_ns && now > p2k_ts_iret_ns) {
            p2k_dwell_push(&p2k_dw_iret_firsttb,
                           (uint64_t)(now - p2k_ts_iret_ns) / 1000ull);
        }
        p2k_ts_first_tb_after_iret_ns = now;
        p2k_first_tb_pending = false;
    }
    /* Diagnostic only (off by default): unconditionally forbid TB chaining
     * on every TB, with NO synthetic tick injection at all -- i.e. the
     * plain hardware-faithful natural i8259/i8254 IRQ0 path, but with the
     * TCG scheduler-check window shrunk to a single TB instead of a whole
     * goto_tb/goto_ptr chain. Tests whether the natural clkint delivery
     * ceiling (~42-45%) is caused by long TB chains hiding the pending-
     * interrupt check (this hook's own theory) rather than by a deeper
     * PIC/PIT modelling gap. Costs raw TCG dispatch throughput; never
     * enable this for real play. See P2K_DIAG_ALWAYS_NOCHAIN in docs/12. */
    static int s_always_nochain = -1;
    if (unlikely(s_always_nochain < 0)) {
        const char *e = getenv("P2K_DIAG_ALWAYS_NOCHAIN");
        s_always_nochain = (e && e[0] == '1') ? 1 : 0;
    }
    if (s_always_nochain && base != (uint32_t)-1) {
        return base | CF_NO_GOTO_TB | CF_NO_GOTO_PTR;
    }

    /* HOTLOOP IRQ0 delivery. See qemu/p2k-clkint-hotloop.c. This
     * runs at every TB boundary; cheap fast-path when the env is unset.
     * We pass first_cpu since this hook is called from the vCPU thread's
     * cpu_exec_loop and the guest has a single CPU on this machine. */
    if (unlikely(p2k_clkint_hotloop_enabled())) {
        p2k_clkint_hotloop_maybe_raise(first_cpu);
    }
    return base;
}

void p2k_timing_audit_note_pdb05(void)
{
    int64_t now_v = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int64_t now_w = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    uint64_t gap_v_us = 0;
    uint64_t gap_w_us = 0;
    if (p2k_audit_pdb05_last_vtime_ns != 0 && now_v > p2k_audit_pdb05_last_vtime_ns) {
        gap_v_us = (uint64_t)(now_v - p2k_audit_pdb05_last_vtime_ns) / 1000ull;
        p2k_dwell_push(&p2k_dw_pdb05_vtime_total, gap_v_us);
        p2k_dwell_push(&p2k_dw_pdb05_vtime_delta, gap_v_us);
    }
    if (p2k_audit_pdb05_last_wall_ns != 0 && now_w > p2k_audit_pdb05_last_wall_ns) {
        gap_w_us = (uint64_t)(now_w - p2k_audit_pdb05_last_wall_ns) / 1000ull;
        p2k_dwell_push(&p2k_dw_pdb05_wall_total, gap_w_us);
        p2k_dwell_push(&p2k_dw_pdb05_wall_delta, gap_w_us);
    }
    if (p2k_pdb_gap_profile_enabled && gap_w_us) {
        p2k_pdb_gap_close_tb(now_w);
        p2k_pdb_gap_record(gap_w_us, gap_v_us, now_w);
    }
    if (p2k_pdb_gap_profile_enabled) {
        p2k_pdb_gap_reset_tb();
    }
    p2k_audit_pdb05_last_vtime_ns = now_v;
    p2k_audit_pdb05_last_wall_ns  = now_w;
    p2k_audit_pdb05_count++;
}

static int p2k_audit_lat_cmp(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a;
    uint32_t y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static void p2k_audit_lat_percentiles(uint32_t *p50, uint32_t *p95,
                                      uint32_t *p99, uint32_t *pmax)
{
    *p50 = *p95 = *p99 = *pmax = 0;
    if (p2k_audit_lat_n == 0) {
        return;
    }
    uint32_t n = p2k_audit_lat_n;
    uint32_t *copy = g_new(uint32_t, n);
    memcpy(copy, p2k_audit_lat_us, sizeof(uint32_t) * n);
    qsort(copy, n, sizeof(uint32_t), p2k_audit_lat_cmp);
    *p50  = copy[(n * 50)  / 100];
    *p95  = copy[(n * 95)  / 100 < n ? (n * 95)  / 100 : n - 1];
    *p99  = copy[(n * 99)  / 100 < n ? (n * 99)  / 100 : n - 1];
    *pmax = copy[n - 1];
    g_free(copy);
}

static void p2k_dwell_percentiles(const P2KDwell *d, uint32_t *p50,
                                  uint32_t *p95, uint32_t *p99,
                                  uint32_t *pmax)
{
    *p50 = *p95 = *p99 = *pmax = 0;
    if (d->n == 0) return;
    uint32_t n = d->n;
    uint32_t *copy = g_new(uint32_t, n);
    memcpy(copy, d->buf, sizeof(uint32_t) * n);
    qsort(copy, n, sizeof(uint32_t), p2k_audit_lat_cmp);
    *p50  = copy[(n * 50)  / 100];
    *p95  = copy[(n * 95)  / 100 < n ? (n * 95)  / 100 : n - 1];
    *p99  = copy[(n * 99)  / 100 < n ? (n * 99)  / 100 : n - 1];
    *pmax = copy[n - 1];
    g_free(copy);
}

static const char *p2k_audit_handler_class(uint32_t off)
{
    if (off == 0) {
        return "null";
    }
    if (off < 0x100000u) {
        return "low-stub";
    }
    uint8_t buf[7];
    cpu_physical_memory_read(off, buf, sizeof(buf));
    if (memcmp(buf, p2k_panic_sig, sizeof(buf)) == 0) {
        return "panic-stub";
    }
    return "clkint";
}

static uint32_t p2k_audit_read_idt20(void)
{
    CPUState *cs;
    CPU_FOREACH(cs) {
        CPUX86State *env = &X86_CPU(cs)->env;
        if (env->idt.base == 0 || env->idt.limit < 0x20 * 8 + 7) {
            return 0;
        }
        uint8_t g[8];
        cpu_physical_memory_read(env->idt.base + 0x20 * 8, g, sizeof(g));
        uint16_t lo = g[0] | (g[1] << 8);
        uint16_t hi = g[6] | (g[7] << 8);
        return ((uint32_t)hi << 16) | lo;
    }
    return 0;
}

static bool p2k_audit_env_truthy(const char *name)
{
    const char *v = getenv(name);
    return v && v[0] && v[0] != '0';
}

/* PIT-deadline rendezvous timer (on by default). Arm a virtual-clock
 * QEMUTimer at the projected next i8254 deadline so the main loop wakes
 * the vCPU just in time to take the next IRQ0 raise. Measured to lift
 * natural delivery ~+1.8pp vs upstream default. The off-switch
 * P2K_NO_IRQ0_PIT_DEADLINE_TIMER=1 exists only for regression A/B. */
static int        p2k_pit_deadline_arm_state = -1;
static QEMUTimer *p2k_pit_deadline_timer;
static uint64_t   p2k_pit_period_ns_cached;
static uint64_t   p2k_pit_deadline_arms;
static uint64_t   p2k_pit_deadline_fires;

static bool p2k_pit_deadline_arm_enabled(void)
{
    if (p2k_pit_deadline_arm_state < 0) {
        p2k_pit_deadline_arm_state =
            p2k_audit_env_truthy("P2K_NO_IRQ0_PIT_DEADLINE_TIMER") ? 0 : 1;
        info_report("pinball2000: irq0 pit-deadline timer %s "
                    "(set P2K_NO_IRQ0_PIT_DEADLINE_TIMER=1 to disable)",
                    p2k_pit_deadline_arm_state ? "ENABLED" : "disabled");
    }
    return p2k_pit_deadline_arm_state == 1;
}

static uint64_t p2k_pit_period_ns(void)
{
    if (p2k_pit_period_ns_cached) {
        return p2k_pit_period_ns_cached;
    }
    if (p2k_audit_state && p2k_audit_state->pit) {
        ISADevice *pit = (ISADevice *)p2k_audit_state->pit;
        PITChannelInfo info;
        pit_get_channel_info(P2K_PIT_STATE(pit), 0, &info);
        unsigned cnt = (info.initial_count & 0xffff);
        if (cnt == 0) {
            cnt = 0x10000;
        }
        /* period_ns = cnt * 1e9 / PIT_FREQ */
        p2k_pit_period_ns_cached =
            ((uint64_t)cnt * 1000000000ull) / (uint64_t)PIT_FREQ;
    }
    if (!p2k_pit_period_ns_cached) {
        p2k_pit_period_ns_cached = 250000ull;  /* fallback ~4 kHz */
    }
    return p2k_pit_period_ns_cached;
}

static void p2k_pit_deadline_cb(void *opaque)
{
    p2k_pit_deadline_fires++;
    if (first_cpu) {
        cpu_exit(first_cpu);
    }
}

static void p2k_pit_deadline_arm(int64_t now_v)
{
    if (!p2k_pit_deadline_timer) {
        return;
    }
    uint64_t period = p2k_pit_period_ns();
    int64_t base = p2k_audit_latest_raise_ns ? p2k_audit_latest_raise_ns
                                             : now_v;
    int64_t deadline = base + (int64_t)period;
    /* If the projected deadline is already in the past (handler took
     * longer than a period, or first arm), aim for now + half-period. */
    if (deadline <= now_v) {
        deadline = now_v + (int64_t)(period / 2);
    }
    timer_mod(p2k_pit_deadline_timer, deadline);
    p2k_pit_deadline_arms++;
}

/* Accessors for sibling p2k modules (e.g. p2k-clkint-hotloop.c) that
 * need to know whether the guest is currently inside the IRQ0 handler
 * and what PIT period the i8254 model is currently programmed for.
 * Both are vCPU-thread / iothread safe to read (single-writer, racy
 * read at worst yields a stale value by one cycle, never corrupted). */
bool p2k_audit_in_clkint(void)
{
    return p2k_in_clkint;
}

uint64_t p2k_audit_pit_period_ns(void)
{
    return p2k_pit_period_ns();
}

uint64_t p2k_audit_clkint_entered_count(void)
{
    return p2k_audit_clkint_entered;
}

bool p2k_clkint_tcg_match_pc(uint64_t pc, uint64_t cs_base)
{
    if (!p2k_audit_state || pc < cs_base) {
        return false;
    }

    uint64_t eip = pc - cs_base;
    if (p2k_audit_clkint_pc != 0) {
        return eip == p2k_audit_clkint_pc;
    }

    uint32_t idt20 = p2k_audit_read_idt20();
    if (eip == idt20 && strcmp(p2k_audit_handler_class(idt20), "clkint") == 0) {
        p2k_audit_clkint_pc = idt20;
        return true;
    }
    return false;
}

static void p2k_audit_update_clkint_hook(uint32_t idt20, const char *handler)
{
    if (strcmp(handler, "clkint") != 0 || idt20 == 0 ||
        p2k_audit_clkint_pc == idt20) {
        return;
    }

    /* HISTORICAL BUG (fixed here): this used to call tb_flush() (deferred
     * via async_safe_run_on_cpu, itself a fix for an even more direct
     * earlier crash) every time this module first learned the real clkint
     * entry PC, so that p2k_clkint_tcg_match_pc()'s cached PC stayed
     * "fresh" for any future TCG-time consumer. That consumer
     * (p2k_clkint_tcg_match_pc) is DEAD CODE — grep confirms it is
     * declared and defined but never called anywhere in the tree, a
     * leftover from the removed direct-clkint TCG-hook mechanism. So the
     * flush bought nothing except a 100%-reproducible
     * `cpu_io_recompile: could not find TB for pc=<host retaddr>` fatal:
     * even the "safe", exclusive-context-deferred tb_flush() still
     * invalidates the TranslationBlock whose HOST code is on the call
     * stack of whatever MMIO/PIO helper is *currently* unwinding back into
     * cpu_io_recompile()'s tcg_tb_lookup(host_retaddr) — which then finds
     * nothing, because tb_flush() just erased it. This module's own file
     * header promises "observer only; no guest-state mutation"; a
     * TB-cache-wide flush triggered by a periodic diagnostic timer was
     * never consistent with that promise. Fix: track p2k_audit_clkint_pc
     * as pure host-side bookkeeping for the `clkint_hook=0x%08x` log
     * field below — no flush, no guest-visible effect, matching the
     * documented invariant. */
    p2k_audit_clkint_pc = idt20;
}

static void p2k_audit_emit(const char *tag)
{
    /* PIT ch0. */
    uint32_t pit0_count = 0;
    int      pit0_mode  = -1;
    double   pit0_hz    = 0.0;
    if (p2k_audit_state && p2k_audit_state->pit) {
        ISADevice *pit = (ISADevice *)p2k_audit_state->pit;
        PITChannelInfo info;
        pit_get_channel_info(P2K_PIT_STATE(pit), 0, &info);
        pit0_count = info.initial_count & 0xffff;
        pit0_mode  = info.mode;
        unsigned cnt = pit0_count ? pit0_count : 0x10000;
        pit0_hz = (double)PIT_FREQ / (double)cnt;
    }

    /* PIC master. */
    uint8_t imr = 0xff, irr = 0, isr = 0, base = 0;
    if (isa_pic) {
        PICCommonState *m = (PICCommonState *)isa_pic;
        imr = m->imr; irr = m->irr; isr = m->isr; base = m->irq_base;
    }

    /* IDT[0x20]. */
    uint32_t idt20 = p2k_audit_read_idt20();
    const char *handler = p2k_audit_handler_class(idt20);
    p2k_audit_update_clkint_hook(idt20, handler);

    /* Wall vs virtual since arm. */
    int64_t now_w  = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    int64_t now_v  = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    double wall_s  = (now_w - p2k_audit_arm_wall_ns)  / 1e9;
    double vtime_s = (now_v - p2k_audit_arm_vtime_ns) / 1e9;
    double scale   = (wall_s > 0.001) ? (vtime_s / wall_s) : 0.0;
    bool host_slow = (wall_s > 0.5) && (scale < 0.95);

    /* Expected IRQ0 edge count over (vtime since arm). */
    double irq0_edges_pit_expected = (pit0_hz > 0.0) ? (vtime_s * pit0_hz) : 0.0;
    uint64_t serviced = p2k_audit_clkint_entered;
    double delivery = (p2k_audit_irq0_raised > 0)
        ? (100.0 * (double)serviced / (double)p2k_audit_irq0_raised)
        : 0.0;

    /* Delivery over only the interval since the previous audit report.
     * Keep the historical `delivery=` field as the since-arm average for
     * log/parser compatibility, but surface this window explicitly so boot
     * and other old phases do not hide a current regression.  On the first
     * report the window begins at audit arm, so current and total naturally
     * cover the same interval. */
    static uint64_t prev_delivery_raised;
    static uint64_t prev_delivery_serviced;
    static int64_t prev_delivery_wall_ns;
    uint64_t delivery_raised_delta =
        (p2k_audit_irq0_raised >= prev_delivery_raised)
        ? p2k_audit_irq0_raised - prev_delivery_raised : 0;
    uint64_t delivery_serviced_delta =
        (serviced >= prev_delivery_serviced)
        ? serviced - prev_delivery_serviced : 0;
    double current_delivery = (delivery_raised_delta > 0)
        ? (100.0 * (double)delivery_serviced_delta /
           (double)delivery_raised_delta)
        : 0.0;
    double delivery_window_s = prev_delivery_wall_ns
        ? (double)(now_w - prev_delivery_wall_ns) / 1.0e9 : wall_s;
    double current_clkint_hz = delivery_window_s > 0.001
        ? (double)delivery_serviced_delta / delivery_window_s : 0.0;
    double speed_target_pct = p2k_speed_target_percent();
    double current_speed_pct = 100.0 * current_clkint_hz / 4003.966443;
    prev_delivery_raised = p2k_audit_irq0_raised;
    prev_delivery_serviced = serviced;
    prev_delivery_wall_ns = now_w;

    info_report("p2k-timing #%llu %s | clock=QEMU_CLOCK_VIRTUAL icount=%s "
                "expected: pit0_div=%u pit0_mode=%d pit0_hz=%.2f "
                "irq0_edges_pit_expected=%.0f "
                "observed: irq0_raised=%llu "
                "clkint_entered=%llu "
                "eoi_seen=%llu delivery=%.1f%% current_delivery=%.1f%% "
                "current_irq0_raised=%llu current_clkint_entered=%llu "
                "speed_target=%.2f%% current_clkint_hz=%.1f current_speed=%.2f%% "
                "imr=%02x irr=%02x isr=%02x base=%02x "
                "idt20=0x%08x handler=%s clkint_hook=0x%08x "
                "shim=0 "
                "wall=%.3fs vtime=%.3fs scale=%.3fx host_slow=%s "
                "pic_base=0x%02x",
                (unsigned long long)p2k_audit_seq, tag,
                icount_enabled() ? "on" : "off",
                pit0_count, pit0_mode, pit0_hz, irq0_edges_pit_expected,
                (unsigned long long)p2k_audit_irq0_raised,
                (unsigned long long)p2k_audit_clkint_entered,
                (unsigned long long)p2k_audit_eoi_seen,
                delivery, current_delivery,
                (unsigned long long)delivery_raised_delta,
                (unsigned long long)delivery_serviced_delta,
                speed_target_pct, current_clkint_hz, current_speed_pct,
                imr, irr, isr, base,
                idt20, handler, p2k_audit_clkint_pc,
                wall_s, vtime_s, scale, host_slow ? "yes" : "no",
                base);

    /* The full diagnostic report sorts twelve 4096-entry timing rings and
     * emits many log lines. That is useful interactively, but doing it on
     * the emulator thread every three seconds creates the very PDB tail a
     * benchmark is trying to measure. P2K_TIMING_SNAPSHOTS keeps only the
     * five fields consumed by the comparison tool and sorts one ring.
     * Exit still takes the complete report, after timing has stopped being
     * performance-sensitive. */
    if (p2k_audit_light_snapshots && strcmp(tag, "snap") == 0) {
        uint32_t s50, s95, s99, smax;
        p2k_dwell_percentiles(&p2k_dw_pdb05_wall_delta,
                              &s50, &s95, &s99, &smax);
        info_report("p2k-latency %s | pdb05 count=%llu",
                    tag, (unsigned long long)p2k_audit_pdb05_count);
        info_report("p2k-pdb05 %s | %-18s p50=%uus p95=%uus p99=%uus "
                    "max=%uus max_total=%uus n=%u",
                    tag, "pdb05_wall_delta", s50, s95, s99, smax,
                    p2k_dw_pdb05_wall_delta.max_total,
                    p2k_dw_pdb05_wall_delta.n);
        p2k_dwell_reset(&p2k_dw_pdb05_wall_delta);

        double clkint_period_ms = 1000.0 / 4003.966443;
        double wall_ms_delta = 0.0;
        double vtime_ms_delta = 0.0;
        double xinu_ms_delta = 0.0;
        double drift_delta = 0.0;
        if (p2k_audit_prev_wall_ns) {
            wall_ms_delta = (now_w - p2k_audit_prev_wall_ns) / 1.0e6;
            vtime_ms_delta = (now_v - p2k_audit_prev_vtime_ns) / 1.0e6;
            uint64_t clkint_delta = p2k_audit_clkint_entered -
                                    p2k_audit_prev_clkint_entered;
            xinu_ms_delta = (double)clkint_delta * clkint_period_ms;
            drift_delta = wall_ms_delta > 0.001
                ? xinu_ms_delta / wall_ms_delta : 0.0;
        }
        p2k_audit_prev_wall_ns = now_w;
        p2k_audit_prev_vtime_ns = now_v;
        p2k_audit_prev_clkint_entered = p2k_audit_clkint_entered;
        info_report("p2k-xinu-drift %s | period_ms=%.3f | "
                    "delta: wall=%.1fms vtime=%.1fms xinu=%.1fms "
                    "drift=%.3f",
                    tag, clkint_period_ms, wall_ms_delta, vtime_ms_delta,
                    xinu_ms_delta, drift_delta);

        static uint64_t light_prev_data;
        static uint64_t light_prev_ctrl;
        static uint64_t light_prev_disp;
        uint64_t data = p2k_lpt_get_data_writes();
        uint64_t ctrl = p2k_lpt_get_ctrl_writes();
        uint64_t disp = p2k_lpt_get_dispatches();
        double dt_s = wall_ms_delta / 1000.0;
        double data_hz = dt_s > 0.001
            ? (data - light_prev_data) / dt_s : 0.0;
        double ctrl_hz = dt_s > 0.001
            ? (ctrl - light_prev_ctrl) / dt_s : 0.0;
        double disp_hz = dt_s > 0.001
            ? (disp - light_prev_disp) / dt_s : 0.0;
        info_report("p2k-lpt-hz %s | data=%llu (+%.0f/s) "
                    "ctrl=%llu (+%.0f/s) dispatch=%llu (+%.0f/s) "
                    "target_driverboard_hz=16000",
                    tag, (unsigned long long)data, data_hz,
                    (unsigned long long)ctrl, ctrl_hz,
                    (unsigned long long)disp, disp_hz);
        light_prev_data = data;
        light_prev_ctrl = ctrl;
        light_prev_disp = disp;
        if (p2k_clkint_hotloop_enabled()) {
            info_report("p2k-clkint-hotloop %s | jitter: n=%llu "
                        "max_us=%lld",
                        tag,
                        (unsigned long long)
                            p2k_clkint_hotloop_jitter_count(),
                        (long long)
                            (p2k_clkint_hotloop_jitter_max_ns() / 1000));
        }
        p2k_audit_seq++;
        return;
    }

    /* Latency histogram + PDB05 multi-axis percentiles. The pdb05 lines
     * are the cabinet-safety figure-of-merit: real lamps and solenoids
     * see WALL TIME, not vtime. */
    uint32_t p50_us = 0, p95_us = 0, p99_us = 0, pmax_us = 0;
    p2k_audit_lat_percentiles(&p50_us, &p95_us, &p99_us, &pmax_us);
    info_report("p2k-latency %s | raise->clkint p50=%uus p95=%uus "
                "p99=%uus max=%uus max_total=%uus n=%u | "
                "pdb05 count=%llu",
                tag, p50_us, p95_us, p99_us, pmax_us,
                p2k_audit_lat_max_total, p2k_audit_lat_n,
                (unsigned long long)p2k_audit_pdb05_count);

    uint64_t entry_mean_ns = p2k_clkint_entry_n
        ? p2k_clkint_entry_sum_ns / p2k_clkint_entry_n : 0;
    long double entry_mean_us = (long double)entry_mean_ns / 1000.0L;
    long double entry_variance = p2k_clkint_entry_n
        ? p2k_clkint_entry_sum_sq_us / p2k_clkint_entry_n -
          entry_mean_us * entry_mean_us : 0.0L;
    if (entry_variance < 0.0L) entry_variance = 0.0L;
    info_report("p2k-clkint-entry %s | n=%llu mean_us=%llu min_us=%lld "
                "max_us=%lld stddev_us=%llu",
                tag, (unsigned long long)p2k_clkint_entry_n,
                (unsigned long long)(entry_mean_ns / 1000),
                (long long)(p2k_clkint_entry_n ?
                            p2k_clkint_entry_min_ns / 1000 : 0),
                (long long)(p2k_clkint_entry_max_ns / 1000),
                (unsigned long long)sqrtl(entry_variance));

    struct { const char *name; P2KDwell *d; bool reset_after; } pdb05_dwells[] = {
        { "pdb05_wall_delta",  &p2k_dw_pdb05_wall_delta,  true  },
        { "pdb05_wall_total",  &p2k_dw_pdb05_wall_total,  false },
        { "pdb05_vtime_delta", &p2k_dw_pdb05_vtime_delta, true  },
        { "pdb05_vtime_total", &p2k_dw_pdb05_vtime_total, false },
    };
    for (size_t i = 0; i < sizeof(pdb05_dwells)/sizeof(pdb05_dwells[0]); i++) {
        uint32_t s50, s95, s99, smax;
        p2k_dwell_percentiles(pdb05_dwells[i].d, &s50, &s95, &s99, &smax);
        info_report("p2k-pdb05 %s | %-18s p50=%uus p95=%uus p99=%uus "
                    "max=%uus max_total=%uus n=%u",
                    tag, pdb05_dwells[i].name, s50, s95, s99, smax,
                    pdb05_dwells[i].d->max_total, pdb05_dwells[i].d->n);
        if (pdb05_dwells[i].reset_after) {
            p2k_dwell_reset(pdb05_dwells[i].d);
        }
    }

    /* XINU advances by one fixed 4003.97 Hz game tick per clkint. Deliberately
     * scaling the emulated PIT changes how often that fixed tick executes; it
     * does not change the amount of game time advanced by the handler. */
    double clkint_period_ms = 1000.0 / 4003.966443;
    double wall_ms_total    = wall_s * 1000.0;
    double vtime_ms_total   = vtime_s * 1000.0;
    double xinu_ms_total    = (double)p2k_audit_clkint_entered * clkint_period_ms;
    double drift_total      = (wall_ms_total > 0.001)
                              ? (xinu_ms_total / wall_ms_total) : 0.0;

    int64_t now_w_for_delta = now_w;
    int64_t now_v_for_delta = now_v;
    double  wall_ms_delta   = 0.0;
    double  vtime_ms_delta  = 0.0;
    double  xinu_ms_delta   = 0.0;
    double  drift_delta     = 0.0;
    if (p2k_audit_prev_wall_ns) {
        wall_ms_delta  = (now_w_for_delta - p2k_audit_prev_wall_ns)  / 1.0e6;
        vtime_ms_delta = (now_v_for_delta - p2k_audit_prev_vtime_ns) / 1.0e6;
        uint64_t clkint_delta = p2k_audit_clkint_entered - p2k_audit_prev_clkint_entered;
        xinu_ms_delta  = (double)clkint_delta * clkint_period_ms;
        drift_delta    = (wall_ms_delta > 0.001)
                         ? (xinu_ms_delta / wall_ms_delta) : 0.0;
    }
    p2k_audit_prev_wall_ns         = now_w_for_delta;
    p2k_audit_prev_vtime_ns        = now_v_for_delta;
    p2k_audit_prev_clkint_entered  = p2k_audit_clkint_entered;

    info_report("p2k-xinu-drift %s | period_ms=%.3f | "
                "total: wall=%.1fms vtime=%.1fms xinu=%.1fms drift=%.3f | "
                "delta: wall=%.1fms vtime=%.1fms xinu=%.1fms drift=%.3f",
                tag, clkint_period_ms,
                wall_ms_total, vtime_ms_total, xinu_ms_total, drift_total,
                wall_ms_delta, vtime_ms_delta, xinu_ms_delta, drift_delta);

    /* Per-segment dwell table. Five segments named per the audit plan;
     * each printed on its own line so log post-processing is trivial.
     * intack_seen / iret_seen are coverage counters: they should track
     * clkint_entered when the patches' weak hooks are linked. */
    struct { const char *name; const P2KDwell *d; } segs[] = {
        { "raise_intack",  &p2k_dw_raise_intack  },
        { "intack_entry",  &p2k_dw_intack_entry  },
        { "entry_eoi",     &p2k_dw_entry_eoi     },
        { "eoi_iret",      &p2k_dw_eoi_iret      },
        { "iret_raise",    &p2k_dw_iret_raise    },
        { "iret_firsttb",  &p2k_dw_iret_firsttb  },
        { "firsttb_raise", &p2k_dw_firsttb_raise },
    };
    for (size_t i = 0; i < sizeof(segs) / sizeof(segs[0]); i++) {
        uint32_t s50, s95, s99, smax;
        p2k_dwell_percentiles(segs[i].d, &s50, &s95, &s99, &smax);
        info_report("p2k-segment %s | %-13s p50=%uus p95=%uus p99=%uus "
                    "max=%uus max_total=%uus n=%u",
                    tag, segs[i].name, s50, s95, s99, smax,
                    segs[i].d->max_total, segs[i].d->n);
    }
    info_report("p2k-segment %s | coverage intack_seen=%llu "
                "iret_seen=%llu (vs clkint_entered=%llu)",
                tag,
                (unsigned long long)p2k_intack_seen_irq0,
                (unsigned long long)p2k_iret_seen_after_clkint,
                (unsigned long long)p2k_audit_clkint_entered);

    info_report("p2k-pit-deadline %s | arms=%llu fires=%llu period_ns=%llu",
                tag,
                (unsigned long long)p2k_pit_deadline_arms,
                (unsigned long long)p2k_pit_deadline_fires,
                (unsigned long long)p2k_pit_period_ns_cached);

    if (p2k_clkint_hotloop_enabled()) {
        uint64_t hotloop_reraises = p2k_clkint_hotloop_count_reraises();
        uint64_t clkint_entered = p2k_audit_clkint_entered_count();
        uint64_t pit_driven = (clkint_entered > hotloop_reraises)
                              ? clkint_entered - hotloop_reraises : 0;
        info_report("p2k-clkint-hotloop %s | "
                    "reraises=%llu (PIT-driven irq0 => %llu) | "
                    "adaptive=%s gap_ns=%lld measured_hz=%.1f | "
                    "jitter: n=%llu mean_us=%llu min_us=%lld max_us=%lld stddev_us=%llu | "
                    "skipped: pending=%llu isr=%llu imr=%llu if0=%llu "
                    "shadow=%llu in_clkint=%llu min_gap=%llu",
                    tag,
                    (unsigned long long)hotloop_reraises,
                    (unsigned long long)pit_driven,
                    p2k_clkint_hotloop_adaptive_enabled() ? "on" : "off",
                    (long long)p2k_clkint_hotloop_current_gap_ns(),
                    p2k_clkint_hotloop_measured_hz(),
                    (unsigned long long)p2k_clkint_hotloop_jitter_count(),
                    (unsigned long long)(p2k_clkint_hotloop_jitter_mean_ns() / 1000),
                    (long long)(p2k_clkint_hotloop_jitter_min_ns() / 1000),
                    (long long)(p2k_clkint_hotloop_jitter_max_ns() / 1000),
                    (unsigned long long)p2k_clkint_hotloop_jitter_stddev_us(),
                    (unsigned long long)p2k_clkint_hotloop_count_skipped_pending(),
                    (unsigned long long)p2k_clkint_hotloop_count_skipped_isr(),
                    (unsigned long long)p2k_clkint_hotloop_count_skipped_imr(),
                    (unsigned long long)p2k_clkint_hotloop_count_skipped_if0(),
                    (unsigned long long)p2k_clkint_hotloop_count_skipped_shadow(),
                    (unsigned long long)p2k_clkint_hotloop_count_skipped_in_clkint(),
                    (unsigned long long)p2k_clkint_hotloop_count_skipped_min_gap());
    }

    /* LPT/driverboard activity. Per Erikie's pinside msg #36, the rate
     * of LPT writes hitting the driverboard (target ~16 kHz) is the
     * only metric that ultimately matters for cabinet behaviour. We
     * report monotonic totals plus per-interval Hz (using wall delta
     * so the figure matches what a scope on the real LPT would see). */
    {
        static uint64_t s_prev_data, s_prev_ctrl, s_prev_disp;
        uint64_t data = p2k_lpt_get_data_writes();
        uint64_t ctrl = p2k_lpt_get_ctrl_writes();
        uint64_t disp = p2k_lpt_get_dispatches();
        double   dt_s = wall_ms_delta / 1000.0;
        double   data_hz = (dt_s > 0.001) ? ((data - s_prev_data) / dt_s) : 0.0;
        double   ctrl_hz = (dt_s > 0.001) ? ((ctrl - s_prev_ctrl) / dt_s) : 0.0;
        double   disp_hz = (dt_s > 0.001) ? ((disp - s_prev_disp) / dt_s) : 0.0;
        info_report("p2k-lpt-hz %s | data=%llu (+%.0f/s) ctrl=%llu (+%.0f/s) "
                    "dispatch=%llu (+%.0f/s) target_driverboard_hz=16000",
                    tag,
                    (unsigned long long)data, data_hz,
                    (unsigned long long)ctrl, ctrl_hz,
                    (unsigned long long)disp, disp_hz);
        s_prev_data = data;
        s_prev_ctrl = ctrl;
        s_prev_disp = disp;
    }

    /* Frame submission counter. Reports per-interval FPS derived from
     * the display module's dpy_gfx_update_full hook. */
    {
        static uint64_t s_prev_frames;
        uint64_t frames = p2k_display_get_frames();
        double   dt_s   = wall_ms_delta / 1000.0;
        double   fps    = (dt_s > 0.001) ? ((frames - s_prev_frames) / dt_s) : 0.0;
        info_report("p2k-fps %s | frames=%llu fps=%.1f",
                    tag, (unsigned long long)frames, fps);
        s_prev_frames = frames;
    }

    /* Top-N EOI EIPs (where in the handler the EOI write fired). */
    uint64_t total_eoi_eips = 0;
    for (int i = 0; i < P2K_AUDIT_EOI_EIP_SLOTS; i++) {
        total_eoi_eips += p2k_eoi_eip[i].count;
    }
    if (total_eoi_eips > 0) {
        char buf[512];
        size_t off = 0;
        for (int i = 0; i < P2K_AUDIT_EOI_EIP_SLOTS && off < sizeof(buf) - 32; i++) {
            if (!p2k_eoi_eip[i].count) continue;
            off += snprintf(buf + off, sizeof(buf) - off,
                            "0x%08x:%llu ", p2k_eoi_eip[i].eip,
                            (unsigned long long)p2k_eoi_eip[i].count);
        }
        info_report("p2k-eoi-eip %s | %s", tag, buf);
    }

    /* Optional guest-clock probe: read up to 8 32-bit values from guest
     * physical memory at every audit dump. Address list comes from
     * comma-separated env P2K_GUEST_CLOCK_ADDRS (hex/dec/0x..). Reports
     * raw value plus delta and rate-per-wall-second. Used to compare
     * guest-visible time advance against wall time independently of
     * clkint_entered. */
    {
        const char *addrs = getenv("P2K_GUEST_CLOCK_ADDRS");
        if (addrs && *addrs) {
            static uint32_t s_prev_val[8];
            static int64_t  s_prev_wall_ns;
            static bool     s_have_prev;
            uint32_t addr[8] = {0};
            uint32_t val[8]  = {0};
            int n = 0;
            const char *p = addrs;
            while (n < 8 && *p) {
                char *endp;
                unsigned long v = strtoul(p, &endp, 0);
                if (endp == p) break;
                addr[n++] = (uint32_t)v;
                p = endp;
                while (*p == ',' || *p == ' ') p++;
            }
            for (int i = 0; i < n; i++) {
                uint32_t pval = 0, vval = 0;
                bool vok = false;
                cpu_physical_memory_read(addr[i], &pval, 4);
                if (first_cpu) {
                    if (cpu_memory_rw_debug(first_cpu, addr[i],
                                            (uint8_t *)&vval, 4, false) == 0) {
                        vok = true;
                    }
                }
                /* Prefer the virtual (paged) read when available; some kernels
                 * (e.g. XINU on a 386) have paging enabled with an identity
                 * map for kernel data, but the linker references can still
                 * differ from raw physical when CR3 swaps to a user space. */
                val[i] = vok ? vval : pval;
            }
            char line[768];
            size_t off = 0;
            uint32_t cr0 = 0, cr3 = 0;
            if (first_cpu) {
                CPUX86State *env = &X86_CPU(first_cpu)->env;
                cr0 = env->cr[0];
                cr3 = env->cr[3];
            }
            off += snprintf(line + off, sizeof(line) - off,
                            "p2k-guest-clock %s | cr0=0x%08x cr3=0x%08x",
                            tag, cr0, cr3);
            double dt_w = 0.0;
            if (s_have_prev) {
                dt_w = (now_w - s_prev_wall_ns) / 1.0e9;
            }
            for (int i = 0; i < n && off < sizeof(line) - 64; i++) {
                int32_t delta = s_have_prev
                    ? (int32_t)(val[i] - s_prev_val[i]) : 0;
                double rate = (dt_w > 0.001) ? ((double)delta / dt_w) : 0.0;
                off += snprintf(line + off, sizeof(line) - off,
                                " [0x%08x]=0x%08x(%u) d=%d rate=%.2f/s",
                                addr[i], val[i], val[i], delta, rate);
                s_prev_val[i] = val[i];
            }
            s_prev_wall_ns = now_w;
            s_have_prev    = true;
            info_report("%s", line);
        }
    }

    p2k_audit_seq++;
    p2k_stall_profile_dump();
    if (strcmp(tag, "exit") == 0) {
        p2k_pdb_gap_profile_dump();
    }
}

static void p2k_audit_tick(void *opaque)
{
    p2k_audit_emit("snap");
    if (p2k_audit_periodic) {
        timer_mod(p2k_audit_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_AUDIT_PERIOD_NS);
    }
}

static void p2k_audit_exit_cb(Notifier *n, void *opaque)
{
    if (p2k_audit_timer) {
        timer_del(p2k_audit_timer);
    }
    p2k_audit_emit("exit");
}

static Notifier p2k_audit_exit_notifier = {
    .notify = p2k_audit_exit_cb,
};

void p2k_install_timing_audit(Pinball2000MachineState *s)
{
    if (p2k_audit_env_truthy("P2K_NO_TIMING_AUDIT")) {
        info_report("pinball2000: timing-audit disabled "
                    "(P2K_NO_TIMING_AUDIT=1)");
        return;
    }
    p2k_audit_state      = s;
    p2k_audit_arm_wall_ns  = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    p2k_audit_arm_vtime_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    p2k_audit_light_snapshots =
        p2k_audit_env_truthy("P2K_TIMING_SNAPSHOTS");
    p2k_audit_periodic = p2k_audit_env_truthy("P2K_DIAG") ||
                         p2k_audit_light_snapshots;
    p2k_pdb_gap_profile_enabled =
        p2k_audit_env_truthy("P2K_PROFILE_PDB_GAPS");
    if (p2k_pdb_gap_profile_enabled) {
        const char *threshold = getenv("P2K_PROFILE_PDB_GAP_US");
        const char *after = getenv("P2K_PROFILE_PDB_AFTER_MS");
        if (threshold && *threshold) {
            uint64_t value = strtoull(threshold, NULL, 0);
            if (value >= 250 && value <= 1000000) {
                p2k_pdb_gap_threshold_us = value;
            }
        }
        if (after && *after) {
            uint64_t value = strtoull(after, NULL, 0);
            if (value <= 3600000) {
                p2k_pdb_gap_after_us = value * 1000ull;
            }
        }
        info_report("pinball2000: PDB-gap profiler armed "
                    "(threshold=%lluus after=%llums, exit-only dump)",
                    (unsigned long long)p2k_pdb_gap_threshold_us,
                    (unsigned long long)(p2k_pdb_gap_after_us / 1000ull));
    }
    p2k_audit_timer        = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                          p2k_audit_tick, NULL);
    p2k_pit_deadline_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                          p2k_pit_deadline_cb, NULL);
    timer_mod(p2k_audit_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_AUDIT_INITIAL_NS);
    qemu_add_exit_notifier(&p2k_audit_exit_notifier);
    p2k_stall_profile_init();
    info_report("pinball2000: timing-audit armed (initial @3s, %s; "
                "disable with P2K_NO_TIMING_AUDIT=1)",
                p2k_audit_light_snapshots ?
                    "light snapshots every 3s" :
                p2k_audit_periodic ?
                    "full snapshots every 3s" : "exit only");
}
