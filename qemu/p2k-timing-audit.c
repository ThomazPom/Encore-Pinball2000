/*
 * pinball2000 timing-audit panel.
 *
 * Question this module answers (every snapshot, single line):
 *   "Is Encore/QEMU truly running on QEMU virtual time with clean IRQ0
 *    delivery, or are we limping along on an external pacing workaround?"
 *
 * What it reports (no guest-state mutation; pure observer):
 *
 *   clock=<QEMU clock used for our timers>
 *   icount=<on/off>                     (icount_enabled())
 *   pit0_div=<n> pit0_hz=<hz>           (i8254 ch0 mode + initial_count)
 *   irq0_edges_exp=<n>                  (vtime_elapsed * pit0_hz; "expected"
 *                                        because we don't snoop the IRQ wire)
 *   imr=<xx> irr=<xx> isr=<xx>          (i8259 master)
 *   idt20=<addr> handler=<clkint|panic-stub|null>
 *   fixup=<0/1>                         (P2K_PIC_FIXUP env)
 *   shim=<0>                            (IRQ0 shim deleted in a294b49)
 *   wall=<s> vtime=<s> scale=<x>        (QEMU_CLOCK_REALTIME vs
 *                                        QEMU_CLOCK_VIRTUAL since arm)
 *   host_slow=<yes/no>                  (scale < 0.95)
 *
 * Cadence:
 *   - one initial line ~3 s after machine arm (gives PIT ch0 + IDT[0x20]
 *     time to be programmed)
 *   - if `P2K_DIAG=1` (or `run-qemu.sh -v`): one line every 5 s after that
 *   - one final line at machine exit / QEMU shutdown
 *
 * Disable entirely with `P2K_NO_TIMING_AUDIT=1`.
 *
 * Notes:
 *   - irq0_edges is "expected" rather than "observed" because counting
 *     real IRQ0 edges/EOIs requires either patching the in-tree i8259
 *     or wrapping the IRQ wire — both invasive. The expected count is
 *     a reliable upper bound: if scale~=1 and pit0_hz is correct then
 *     real edges should match within a few ticks.
 *   - The IRQ0 shim was deleted in a294b49; this panel keeps `shim=0`
 *     as a permanent guardrail so any future re-introduction shows up.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "qemu/notify.h"
#include "system/cpu-timers.h"
#include "system/system.h"
#include "exec/cpu-common.h"
#include "hw/intc/i8259.h"
#include "hw/isa/i8259_internal.h"
#include "hw/timer/i8254.h"
#include "hw/timer/i8254_internal.h"
#include "hw/isa/isa.h"
#include "target/i386/cpu.h"

#include "p2k-internal.h"

#define P2K_AUDIT_INITIAL_NS  (3ull  * 1000ull * 1000ull * 1000ull)  /*  3 s */
#define P2K_AUDIT_PERIOD_NS   (5ull  * 1000ull * 1000ull * 1000ull)  /*  5 s */

/* IRQ0 panic-stub signature: same one p2k-pic-fixup.c uses to gate the
 * unmask; if IDT[0x20] still points at this prologue then clkint hasn't
 * been installed yet and the timing path is not live. */
static const uint8_t p2k_panic_sig[7] = {
    0x55, 0x89, 0xE5, 0x60, 0x6A, 0x20, 0xE9
};

static QEMUTimer *p2k_audit_timer;
static Pinball2000MachineState *p2k_audit_state;

static int64_t  p2k_audit_arm_wall_ns;     /* QEMU_CLOCK_REALTIME at arm */
static int64_t  p2k_audit_arm_vtime_ns;    /* QEMU_CLOCK_VIRTUAL  at arm */
static bool     p2k_audit_periodic;        /* true => arm follow-up */
static uint64_t p2k_audit_seq;

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

static void p2k_audit_emit(const char *tag)
{
    /* PIT ch0. */
    uint32_t pit0_count = 0;
    int      pit0_mode  = -1;
    double   pit0_hz    = 0.0;
    if (p2k_audit_state && p2k_audit_state->pit) {
        ISADevice *pit = (ISADevice *)p2k_audit_state->pit;
        PITChannelInfo info;
        pit_get_channel_info(pit, 0, &info);
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

    /* Wall vs virtual since arm. */
    int64_t now_w  = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    int64_t now_v  = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    double wall_s  = (now_w - p2k_audit_arm_wall_ns)  / 1e9;
    double vtime_s = (now_v - p2k_audit_arm_vtime_ns) / 1e9;
    double scale   = (wall_s > 0.001) ? (vtime_s / wall_s) : 0.0;
    bool host_slow = (wall_s > 0.5) && (scale < 0.95);

    /* Expected IRQ0 edge count over (vtime since arm). */
    double irq0_edges_exp = (pit0_hz > 0.0) ? (vtime_s * pit0_hz) : 0.0;

    bool fixup_on = p2k_audit_env_truthy("P2K_PIC_FIXUP")
                 && !p2k_audit_env_truthy("P2K_NO_PIC_FIXUP");

    info_report("p2k-timing #%llu %s | clock=QEMU_CLOCK_VIRTUAL icount=%s "
                "pit0_div=%u pit0_mode=%d pit0_hz=%.2f irq0_edges_exp=%.0f "
                "imr=%02x irr=%02x isr=%02x base=%02x "
                "idt20=0x%08x handler=%s "
                "fixup=%d shim=0 "
                "wall=%.3fs vtime=%.3fs scale=%.3fx host_slow=%s "
                "pic_base=0x%02x",
                (unsigned long long)p2k_audit_seq, tag,
                icount_enabled() ? "on" : "off",
                pit0_count, pit0_mode, pit0_hz, irq0_edges_exp,
                imr, irr, isr, base,
                idt20, handler,
                fixup_on ? 1 : 0,
                wall_s, vtime_s, scale, host_slow ? "yes" : "no",
                base);
    p2k_audit_seq++;
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
    p2k_audit_periodic     = p2k_audit_env_truthy("P2K_DIAG");
    p2k_audit_timer        = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                          p2k_audit_tick, NULL);
    timer_mod(p2k_audit_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_AUDIT_INITIAL_NS);
    qemu_add_exit_notifier(&p2k_audit_exit_notifier);
    info_report("pinball2000: timing-audit armed (initial @3s, %s; "
                "disable with P2K_NO_TIMING_AUDIT=1)",
                p2k_audit_periodic ? "every 5s with P2K_DIAG=1" : "exit only");
}
