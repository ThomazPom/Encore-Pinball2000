/*
 * pinball2000 read-only diagnostic sampler.
 *
 * Goal: see what the GUEST is actually doing with PIT/PIC/RTC/IDT,
 * so we can replace the symptom patches (p2k-irq0-shim, p2k-pic-fixup,
 * etc.) with real device behavior. Activates only when env P2K_DIAG=1.
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

static void p2k_diag_tick(void *opaque)
{
    DiagSample cur;
    p2k_diag_collect(&cur);
    p2k_diag_log_pit(&cur, &p2k_diag_last);
    p2k_diag_log_pic(&cur, &p2k_diag_last);
    p2k_diag_log_idt(&cur, &p2k_diag_last);
    p2k_diag_last = cur;
    p2k_diag_ticks++;

    timer_mod(p2k_diag_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_DIAG_POLL_NS);
}

void p2k_install_diag(Pinball2000MachineState *s)
{
    const char *en = getenv("P2K_DIAG");
    if (!en || !*en || en[0] == '0') {
        return;
    }
    p2k_diag_state = s;
    p2k_diag_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, p2k_diag_tick, NULL);
    timer_mod(p2k_diag_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_DIAG_POLL_NS);
    info_report("pinball2000: P2K_DIAG enabled — PIT/PIC/IDT delta sampler "
                "active (every %llu ms)",
                (unsigned long long)(P2K_DIAG_POLL_NS / 1000000ull));
}
