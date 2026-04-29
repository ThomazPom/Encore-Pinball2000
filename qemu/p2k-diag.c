/*
 * pinball2000 read-only diagnostic sampler.
 *
 * Goal: see what the GUEST is actually doing with PIT/PIC/RTC/IDT,
 * so we can replace the remaining symptom patches (p2k-pic-fixup,
 * p2k-mem-detect) with real device or CPU behavior. Activates only
 * when env P2K_DIAG=1 (or via `run-qemu.sh -v`). Off by default to
 * keep routine boots quiet.
 *
 * Samples on a virtual-clock periodic tick (default 100 ms):
 *   - i8254 PIT channel 0/1/2: mode + initial_count + computed Hz + gate
 *   - i8259 master IMR/IRR/ISR + slave IMR/IRR/ISR + irq_base
 *   - Last RTC index seen on port 0x70 (debug peek of CMOS chip is not
 *     part of our build; we only log delta when the master IMR or RTC
 *     mask changes — RTC index port goes through QEMU's MC146818 if
 *     present, otherwise our isa-stubs CMOS handler)
 *   - IDT base/limit and IDT[0x20] / IDT[0x28] gate offset
 *
 * Logs ONLY on first observation and on every change. No effect on
 * timing — pure observer.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h"
#include "exec/cpu-common.h"
#include "exec/address-spaces.h"
#include "hw/intc/i8259.h"
#include "hw/isa/i8259_internal.h"
#include "hw/timer/i8254.h"
#include "hw/timer/i8254_internal.h"
#include "hw/isa/isa.h"
#include "target/i386/cpu.h"

#include "p2k-internal.h"

#define P2K_DIAG_POLL_NS  (100ull * 1000ull * 1000ull)   /* 100 ms */

/* XINU scheduler globals in v2.10 swe1 (resolved from symbols.rom).
 * Used to detect scheduler liveness. We only read; we never write. */
#define P2K_XINU_EXEC_PASS        0x33c450u  /* exec_proc inc per loop */
#define P2K_XINU_EXEC_XPID        0x33c454u  /* XPid of exec_proc */
#define P2K_XINU_LAST_EXEC_PASS   0x354ef8u  /* interval_0_25ms snapshot */
#define P2K_XINU_MS_SINCE_EXEC    0x33c490u  /* fatals at 0xBB8 */
#define P2K_XINU_CLKRUN           0x2fe660u  /* sleep1000 gates on this */
#define P2K_XINU_SLNONEMPTY       0x2fe670u  /* sleep delta-queue active */
#define P2K_XINU_SLQHEAD_PTR      0x2fe66cu  /* &queue[head] */
#define P2K_XINU_INTERRUPT_DEPTH  0x2fe944u  /* >0 inside ISR */
#define P2K_XINU_CURRPID          0x303468u  /* running XPid */
#define P2K_XINU_PROCTAB          0x303470u  /* base of process table */
#define P2K_XINU_PROCTAB_STRIDE   0xE8u      /* (1+7*4)*8 per disasm */
#define P2K_XINU_PROC_STATE_OFF   0x00u      /* state byte at +0 (per ready() disasm) */

typedef struct DiagSample {
    int      pit_mode[3];
    uint16_t pit_count[3];
    int      pit_gate[3];
    uint8_t  master_imr, master_irr, master_isr;
    uint8_t  slave_imr,  slave_irr,  slave_isr;
    uint8_t  master_irq_base, slave_irq_base;
    uint32_t idt_base;
    uint16_t idt_limit;
    uint32_t idt20_off, idt28_off;
    uint16_t idt20_sel, idt28_sel;
    uint8_t  idt20_type, idt28_type;
    bool     valid;
} DiagSample;

static QEMUTimer *p2k_diag_timer;
static DiagSample p2k_diag_last;
static uint64_t   p2k_diag_ticks;
static Pinball2000MachineState *p2k_diag_state;

static void p2k_diag_read_idt_gate(uint32_t base, int vec,
                                   uint32_t *off, uint16_t *sel, uint8_t *type)
{
    uint8_t g[8];
    cpu_physical_memory_read(base + vec * 8, g, sizeof(g));
    *off  = g[0] | (g[1] << 8) | (g[6] << 16) | (g[7] << 24);
    *sel  = g[2] | (g[3] << 8);
    *type = g[5];
}

static bool p2k_diag_pick_cpu(CPUX86State **out)
{
    CPUState *cs;
    CPU_FOREACH(cs) {
        *out = &X86_CPU(cs)->env;
        return true;
    }
    return false;
}

static void p2k_diag_collect(DiagSample *s)
{
    memset(s, 0, sizeof(*s));

    /* PIT — use the public accessor; no internal state poking. */
    if (p2k_diag_state && p2k_diag_state->pit) {
        ISADevice *pit = (ISADevice *)p2k_diag_state->pit;
        for (int i = 0; i < 3; i++) {
            PITChannelInfo info;
            pit_get_channel_info(pit, i, &info);
            s->pit_mode[i]  = info.mode;
            s->pit_count[i] = info.initial_count & 0xffff;
            s->pit_gate[i]  = info.gate;
        }
    }

    /* PIC */
    if (isa_pic) {
        PICCommonState *m = (PICCommonState *)isa_pic;
        s->master_imr = m->imr;
        s->master_irr = m->irr;
        s->master_isr = m->isr;
        s->master_irq_base = m->irq_base;
        /* Slave PIC has no public accessor; skipping. */
    }

    /* IDT */
    CPUX86State *env;
    if (p2k_diag_pick_cpu(&env) && env->idt.base) {
        s->idt_base  = env->idt.base;
        s->idt_limit = env->idt.limit;
        if (env->idt.limit >= 0x20 * 8 + 7) {
            p2k_diag_read_idt_gate(env->idt.base, 0x20,
                                   &s->idt20_off, &s->idt20_sel, &s->idt20_type);
        }
        if (env->idt.limit >= 0x28 * 8 + 7) {
            p2k_diag_read_idt_gate(env->idt.base, 0x28,
                                   &s->idt28_off, &s->idt28_sel, &s->idt28_type);
        }
    }
    s->valid = true;
}

static void p2k_diag_log_pit(const DiagSample *cur, const DiagSample *prev)
{
    for (int i = 0; i < 3; i++) {
        bool changed = !prev->valid ||
                       cur->pit_mode[i]  != prev->pit_mode[i] ||
                       cur->pit_count[i] != prev->pit_count[i] ||
                       cur->pit_gate[i]  != prev->pit_gate[i];
        if (!changed) continue;
        unsigned cnt = cur->pit_count[i] ? cur->pit_count[i] : 0x10000;
        double hz = (double)PIT_FREQ / (double)cnt;
        info_report("p2k-diag: PIT ch%d mode=%d gate=%d count=%u (%.2f Hz)",
                    i, cur->pit_mode[i], cur->pit_gate[i],
                    (unsigned)cur->pit_count[i], hz);
    }
}

static void p2k_diag_log_pic(const DiagSample *cur, const DiagSample *prev)
{
    bool m = !prev->valid ||
             cur->master_imr != prev->master_imr ||
             cur->master_irq_base != prev->master_irq_base;
    if (m) {
        info_report("p2k-diag: PIC master imr=%02x irr=%02x isr=%02x base=%02x",
                    cur->master_imr, cur->master_irr, cur->master_isr,
                    cur->master_irq_base);
    }
    bool sl = false;
    if (sl) {
        info_report("p2k-diag: PIC slave  imr=%02x irr=%02x isr=%02x base=%02x",
                    cur->slave_imr, cur->slave_irr, cur->slave_isr,
                    cur->slave_irq_base);
    }
}

static void p2k_diag_log_idt(const DiagSample *cur, const DiagSample *prev)
{
    bool b = !prev->valid ||
             cur->idt_base  != prev->idt_base ||
             cur->idt_limit != prev->idt_limit;
    if (b) {
        info_report("p2k-diag: IDT base=0x%08x limit=0x%04x",
                    cur->idt_base, cur->idt_limit);
    }
    if (!prev->valid ||
        cur->idt20_off != prev->idt20_off ||
        cur->idt20_sel != prev->idt20_sel ||
        cur->idt20_type != prev->idt20_type) {
        info_report("p2k-diag: IDT[0x20] sel=0x%04x off=0x%08x type=0x%02x"
                    " (IRQ0/PIT)", cur->idt20_sel, cur->idt20_off,
                    cur->idt20_type);
    }
    if (!prev->valid ||
        cur->idt28_off != prev->idt28_off ||
        cur->idt28_sel != prev->idt28_sel ||
        cur->idt28_type != prev->idt28_type) {
        info_report("p2k-diag: IDT[0x28] sel=0x%04x off=0x%08x type=0x%02x"
                    " (IRQ8/RTC)", cur->idt28_sel, cur->idt28_off,
                    cur->idt28_type);
    }
}

/* ----- XINU scheduler state sampler (for "exec is hung" investigation) -- */

typedef struct SchedSample {
    uint32_t exec_pass, last_exec_pass, ms_since_exec, exec_xpid;
    uint32_t clkrun, slnonempty, slqhead_ptr, slqhead_key;
    uint32_t intr_depth, currpid;
    uint8_t  exec_state, curr_state;
    bool     valid;
} SchedSample;

static SchedSample p2k_diag_sched_last;
static bool p2k_diag_sched_alarmed;

static uint32_t p2k_rd32(uint32_t pa)
{
    uint8_t b[4]; cpu_physical_memory_read(pa, b, 4);
    return b[0] | (b[1]<<8) | (b[2]<<16) | (b[3]<<24);
}
static uint8_t p2k_rd8(uint32_t pa)
{
    uint8_t b; cpu_physical_memory_read(pa, &b, 1); return b;
}

static uint8_t p2k_proc_state(uint32_t xpid)
{
    if (xpid >= 0x216) return 0xff;
    return p2k_rd8(P2K_XINU_PROCTAB + xpid * P2K_XINU_PROCTAB_STRIDE
                   + P2K_XINU_PROC_STATE_OFF);
}

static void p2k_diag_sched_collect(SchedSample *s)
{
    memset(s, 0, sizeof(*s));
    s->exec_pass       = p2k_rd32(P2K_XINU_EXEC_PASS);
    s->last_exec_pass  = p2k_rd32(P2K_XINU_LAST_EXEC_PASS);
    s->ms_since_exec   = p2k_rd32(P2K_XINU_MS_SINCE_EXEC);
    s->exec_xpid       = p2k_rd32(P2K_XINU_EXEC_XPID);
    s->clkrun          = p2k_rd32(P2K_XINU_CLKRUN);
    s->slnonempty      = p2k_rd32(P2K_XINU_SLNONEMPTY);
    s->slqhead_ptr     = p2k_rd32(P2K_XINU_SLQHEAD_PTR);
    s->slqhead_key     = (s->slqhead_ptr >= 0x100000 &&
                          s->slqhead_ptr < 0x01000000)
                         ? p2k_rd32(s->slqhead_ptr) : 0xdeadbeef;
    s->intr_depth      = p2k_rd32(P2K_XINU_INTERRUPT_DEPTH);
    s->currpid         = p2k_rd32(P2K_XINU_CURRPID);
    s->exec_state      = (s->exec_xpid && s->exec_xpid < 0x216)
                         ? p2k_proc_state(s->exec_xpid) : 0xff;
    s->curr_state      = (s->currpid && s->currpid < 0x216)
                         ? p2k_proc_state(s->currpid) : 0xff;
    s->valid = true;
}

static void p2k_diag_sched_log(const SchedSample *cur, const SchedSample *prev)
{
    /* Always log first time we see a non-zero exec_xpid or currpid. */
    bool changed_exec = !prev->valid ||
                        cur->exec_xpid != prev->exec_xpid ||
                        cur->exec_state != prev->exec_state;
    bool changed_curr = !prev->valid ||
                        cur->currpid != prev->currpid ||
                        cur->curr_state != prev->curr_state;
    bool exec_progressed = !prev->valid ||
                           cur->exec_pass != prev->exec_pass;
    bool intr = cur->intr_depth != prev->intr_depth;
    bool sl   = cur->slnonempty != prev->slnonempty ||
                cur->slqhead_ptr != prev->slqhead_ptr;

    if (changed_exec) {
        info_report("p2k-diag: exec_xpid=%u state=0x%02x (1=CURR 2=skip "
                    "3=READY 5=PR_SLEEP 6=SUSP 9=FREE)",
                    cur->exec_xpid, cur->exec_state);
    }
    if (changed_curr) {
        info_report("p2k-diag: currpid=%u state=0x%02x exec_pass=0x%x "
                    "ms_since_exec=0x%x", cur->currpid, cur->curr_state,
                    cur->exec_pass, cur->ms_since_exec);
    }
    if (sl || intr) {
        info_report("p2k-diag: clkrun=%u slnonempty=%u qhead_ptr=0x%08x "
                    "qhead_key=0x%x intr_depth=%u",
                    cur->clkrun, cur->slnonempty, cur->slqhead_ptr,
                    cur->slqhead_key, cur->intr_depth);
    }
    /* Hot watch: as ms_since_exec_proc approaches 0xBB8 (=Fatal threshold),
     * dump full state once per 100ms tick so the post-mortem is in the log. */
    if (cur->ms_since_exec >= 0x900 && !p2k_diag_sched_alarmed) {
        p2k_diag_sched_alarmed = true;
        info_report("p2k-diag: !!! exec is starving: ms_since=0x%x "
                    "(Fatal at 0xBB8)", cur->ms_since_exec);
        /* One-shot dump of the HookExec chain at 0x33c45c (head) */
        uint32_t hk = p2k_rd32(0x33c45c);
        int n = 0;
        while (hk >= 0x100000 && hk < 0x01000000 && n < 64) {
            uint32_t fn  = p2k_rd32(hk + 0);
            uint32_t nxt = p2k_rd32(hk + 4);
            uint32_t arg = p2k_rd32(hk + 0xc);
            info_report("p2k-diag:   HookExec[%d] node=0x%08x fn=0x%08x "
                        "arg=0x%08x next=0x%08x", n, hk, fn, arg, nxt);
            hk = nxt; n++;
        }
        /* Also dump exec proctab entry (saved SP probably at +0x14/+0x18) */
        uint32_t pent = P2K_XINU_PROCTAB +
                        cur->exec_xpid * P2K_XINU_PROCTAB_STRIDE;
        uint8_t buf[0x40];
        cpu_physical_memory_read(pent, buf, sizeof(buf));
        char hex[3*sizeof(buf)+1]; int o=0;
        for (size_t k=0;k<sizeof(buf);k++) o+=snprintf(hex+o,4,"%02x ",buf[k]);
        info_report("p2k-diag:   exec proctab[0x%x..]: %s", pent, hex);
        /* Dump exec's saved stack — walk for return-EIPs in code range
         * [0x100000..0x300000] to identify wait() callers. */
        uint32_t esp = p2k_rd32(pent + 0x08);
        uint32_t pstkbase = p2k_rd32(pent + 0x24);
        if (esp >= 0x100000 && esp < 0x10000000 && pstkbase > esp) {
            uint32_t used = pstkbase - esp;
            if (used > 0x400) used = 0x400;
            uint8_t *sb = g_malloc(used);
            cpu_physical_memory_read(esp, sb, used);
            info_report("p2k-diag:   exec saved esp=0x%08x pstkbase=0x%08x "
                        "used=0x%x — code-range entries:",
                        esp, pstkbase, used);
            for (uint32_t k = 0; k+4 <= used; k += 4) {
                uint32_t v = sb[k] | (sb[k+1]<<8) | (sb[k+2]<<16) | (sb[k+3]<<24);
                if (v >= 0x100000 && v < 0x300000) {
                    info_report("p2k-diag:     [esp+0x%03x]=0x%08x", k, v);
                }
            }
            g_free(sb);
        }
    }
    if (cur->ms_since_exec >= 0x900) {
        /* In wait() (state 7), proctab[xpid]+0x10 holds the sem id.
         * Sem table at 0x30aa4c, stride 0x30; field +0=alloc,
         * +4=count, +0xc=qhead. */
        uint32_t pent = P2K_XINU_PROCTAB +
                        cur->exec_xpid * P2K_XINU_PROCTAB_STRIDE;
        uint32_t psem = (cur->exec_xpid && cur->exec_xpid < 0x216)
                        ? p2k_rd32(pent + 0x10) : 0xffffffff;
        uint32_t sem_alloc=0, sem_count=0, sem_qhead=0;
        if (psem < 0xc8) {
            uint32_t sb = 0x30aa4c + psem * 0x30;
            sem_alloc = p2k_rd8(sb);
            sem_count = p2k_rd32(sb + 4);
            sem_qhead = p2k_rd32(sb + 0xc);
        }
        info_report("p2k-diag: STARVING ms=0x%x exec_pass=0x%x last=0x%x "
                    "exec_xpid=%u exec_state=0x%02x currpid=%u "
                    "curr_state=0x%02x intr_depth=%u sln=%u qhead=0x%x "
                    "qkey=0x%x exec_psem=%u (alloc=%u count=%d qh=0x%x)",
                    cur->ms_since_exec, cur->exec_pass, cur->last_exec_pass,
                    cur->exec_xpid, cur->exec_state, cur->currpid,
                    cur->curr_state, cur->intr_depth, cur->slnonempty,
                    cur->slqhead_ptr, cur->slqhead_key,
                    psem, sem_alloc, (int32_t)sem_count, sem_qhead);
    }
    /* Reset alarm if exec resumes. */
    if (cur->ms_since_exec < 0x100 && p2k_diag_sched_alarmed) {
        info_report("p2k-diag: exec recovered (ms_since=0x%x exec_pass=0x%x)",
                    cur->ms_since_exec, cur->exec_pass);
        p2k_diag_sched_alarmed = false;
    }
    (void)exec_progressed;
}

static void p2k_diag_tick(void *opaque)
{
    DiagSample cur;
    p2k_diag_collect(&cur);
    p2k_diag_log_pit(&cur, &p2k_diag_last);
    p2k_diag_log_pic(&cur, &p2k_diag_last);
    p2k_diag_log_idt(&cur, &p2k_diag_last);
    p2k_diag_last = cur;

    /* XINU scheduler liveness — only valid once guest has reached protected
     * mode and populated currpid (>0). Skip otherwise to avoid log noise. */
    if (cur.idt_base) {
        SchedSample sched;
        p2k_diag_sched_collect(&sched);
        if (sched.currpid != 0 || sched.exec_xpid != 0) {
            p2k_diag_sched_log(&sched, &p2k_diag_sched_last);
            p2k_diag_sched_last = sched;
        }
    }
    p2k_diag_ticks++;

    timer_mod(p2k_diag_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_DIAG_POLL_NS);
}

void p2k_install_diag(Pinball2000MachineState *s)
{
    /* Default OFF: diag sampler is opt-in via env P2K_DIAG=1
     * (the run-qemu.sh wrapper exports P2K_DIAG=1 when -v is passed).
     * Set unset/0/empty -> silent.
     * The change-only logger is cheap, but the install message and
     * timer scheduling are noise during routine boots. */
    const char *on = getenv("P2K_DIAG");
    if (!on || !*on || on[0] == '0') {
        return;
    }
    p2k_diag_state = s;
    p2k_diag_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, p2k_diag_tick, NULL);
    timer_mod(p2k_diag_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_DIAG_POLL_NS);
    info_report("pinball2000: diag sampler ON (PIT/PIC/IDT/XINU-sched, "
                "every %llu ms; unset P2K_DIAG to silence)",
                (unsigned long long)(P2K_DIAG_POLL_NS / 1000000ull));
}
