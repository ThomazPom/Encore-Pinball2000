/*
 * Pinball 2000 IRQ0 pacing.
 *
 * HOTLOOP replaces PIT ports 0x40-0x43 with the game's fixed-clock control
 * stub and pulses the normal i8259 IRQ0 input from a host-clock timer.  The
 * requested delay adapts to callback overhead so delivered guest clock ticks
 * remain at the selected speed without catch-up bursts.  Setting
 * P2K_HOTLOOP_HOST_TIMER=0 retains the older TB-boundary implementation for
 * controlled regression tests.  Strict mode continues to use QEMU's i8254.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/timer.h"
#include <stdlib.h>
#include "exec/cpu-common.h"
#include "hw/core/cpu.h"
#include "hw/intc/i8259.h"
#include "hw/isa/i8259_internal.h"
#include "hw/irq.h"
#include "system/system.h"
#include "target/i386/cpu.h"

#include "p2k-internal.h"

static int s_hotloop_state = -1;  /* -1 undecided, 0 off, 1 on */
static int64_t s_hotloop_min_gap_ns = -1;  /* -1 uninit, 0 = no throttle, >0 = min ns between raises */
static int64_t s_hotloop_last_raise_ns;
static QEMUTimer *s_hotloop_host_timer;
static qemu_irq s_hotloop_irq0;
static int s_hotloop_pit_programmed;
static int s_hotloop_host_timer_state = -1;
static double s_hotloop_host_period_us;
static int64_t s_hotloop_host_sample_ns;
static uint64_t s_hotloop_host_sample_clkints;

/* Small counters surfaced through the audit panel for honest reporting. */
static uint64_t s_hotloop_reraises;
static uint64_t s_hotloop_skipped_pending;
static uint64_t s_hotloop_skipped_isr_busy;
static uint64_t s_hotloop_skipped_imr;
static uint64_t s_hotloop_skipped_if0;
static uint64_t s_hotloop_skipped_shadow;
static uint64_t s_hotloop_skipped_in_clkint;
static uint64_t s_hotloop_skipped_min_gap;
static uint64_t s_hotloop_swallowed_edges;

/* Adaptive PI controller state.
 * Target: hold clkint_entered at nominal PIT rate (4003.97 Hz), i.e.
 * hold delivery=100% so guest game clock tracks wall time exactly.
 *
 * Every s_pi_period_ns of virtual time, sample clkint_entered_count and
 * measure achieved rate. Adjust s_hotloop_min_gap_ns proportionally:
 *   error = target_rate - measured_rate
 *   gap_new = gap_cur * (measured_rate / target_rate)
 *   clamped to [P2K_HOTLOOP_MIN_GAP_LOW, P2K_HOTLOOP_MIN_GAP_HIGH]
 *
 * Disabled by default (fixed gap from env or 145000 default is used).
 * Enable with P2K_TCG_CLKINT_HOTLOOP_ADAPTIVE=1.
 */
static bool     s_hotloop_adaptive;
static int64_t  s_pi_period_ns = 500000000; /* 500 ms sample period */
static int64_t  s_pi_last_sample_ns;
static uint64_t s_pi_last_clkint_count;
static int64_t  s_pi_gap_low  =  50000;  /*  20 kHz  ceiling on rate */
static int64_t  s_pi_gap_high = 300000;  /* NO_PIT default; combo is set below */
static double   s_pi_target_hz = 4003.97;
static double   s_pi_last_measured_hz;  /* for audit */

/* Inter-arrival jitter tracking. On each successful raise we compute
 * gap_ns = now - last_raise, and update running statistics: min, max,
 * count, sum, sum_sq. Reported in the audit panel.
 *
 * Auto-reset at boot warmup end so steady-state stats are not polluted
 * by long IF=0 windows during DCS init / early boot. Warmup threshold
 * is P2K_TCG_CLKINT_HOTLOOP_JITTER_WARMUP_S seconds of virtual time
 * (default 30). Set to 0 to keep all-time stats. */
static int64_t  s_jitter_min_ns = INT64_MAX;
static int64_t  s_jitter_max_ns;
static uint64_t s_jitter_count;
static uint64_t s_jitter_sum_ns;
static uint64_t s_jitter_sum_sq_us;  /* sum of (gap/us)^2 to avoid overflow */
static int64_t  s_jitter_reset_at_ns = -1;  /* -1 uninit; 0 = disabled; >0 = deadline */
static bool     s_jitter_reset_done;

static bool p2k_hotloop_env_on(void)
{
    if (unlikely(s_hotloop_state < 0)) {
        /* HOTLOOP is ON by default. Explicitly disable with
         * P2K_TCG_CLKINT_HOTLOOP=0 to fall back to strict natural
         * i8254 + i8259 delivery (44% delivery, sleep 10 = 23 s). */
        const char *v = getenv("P2K_TCG_CLKINT_HOTLOOP");
        if (v && *v) {
            s_hotloop_state = (v[0] == '1') ? 1 : 0;
        } else {
            s_hotloop_state = 1;
        }
        /* Default rate-limit: 145 µs (6.9 kHz raw) at boot, throttled
         * by the adaptive PI controller to 250 µs (4003.97 Hz nominal
         * PIT) within the first 500 ms. Empirically 145 µs initial
         * gives 0/23 wedge on NO_PIT (default) while 250 µs gives
         * 3/15. The extra initial rate lets HOTLOOP push through
         * XINU's early-init tight loops before the adaptive controller
         * has a chance to sample; without it, some boots stall in
         * pre-DCS init and never establish steady state.
         *
         * Combo mode (--with-pit) prefers 250 µs at boot (0/15 vs
         * 2/15 at 145 µs) because natural PIT is already firing at
         * 4 kHz; adding a 6.9 kHz HOTLOOP source produces 10.9 kHz
         * bursts that hit XINU's vector-install critical section.
         * Override with P2K_TCG_CLKINT_HOTLOOP_MIN_GAP_NS=250000 if
         * using --with-pit for A/B testing. */
        const char *g = getenv("P2K_TCG_CLKINT_HOTLOOP_MIN_GAP_NS");
        if (g && *g) {
            s_hotloop_min_gap_ns = (int64_t)strtoll(g, NULL, 10);
            if (s_hotloop_min_gap_ns < 0) {
                s_hotloop_min_gap_ns = 0;
            }
        } else {
            s_hotloop_min_gap_ns = 145000; /* NO_PIT-optimal boot pace */
        }

        const char *a = getenv("P2K_TCG_CLKINT_HOTLOOP_ADAPTIVE");
        /* Adaptive PI controller is ON by default -- delivers tighter
         * real-time pacing than any static gap and self-tunes to host
         * speed. Explicitly disable with P2K_TCG_CLKINT_HOTLOOP_ADAPTIVE=0
         * to fall back to the fixed MIN_GAP_NS above. */
        if (a && *a) {
            s_hotloop_adaptive = (a[0] == '1');
        } else {
            s_hotloop_adaptive = true;
        }
        const char *plo = getenv("P2K_TCG_CLKINT_HOTLOOP_GAP_LOW_NS");
        if (plo && *plo) s_pi_gap_low = strtoll(plo, NULL, 10);
        const char *phi = getenv("P2K_TCG_CLKINT_HOTLOOP_GAP_HIGH_NS");
        if (phi && *phi) {
            s_pi_gap_high = strtoll(phi, NULL, 10);
        } else {
            const char *no_pit = getenv("P2K_TCG_CLKINT_HOTLOOP_NO_PIT");
            if (!(no_pit && no_pit[0] == '1')) {
                /* In combo mode the natural PIT may already provide the
                 * complete 4 kHz clock.  A 300 us ceiling still forces at
                 * least ~3.3k HOTLOOP opportunities/s and overclocks such a
                 * host.  Permit the proportional controller to back off to
                 * an effectively dormant 100 ms cadence.  On slower hosts
                 * it converges to the shorter gap needed to fill only the
                 * PIT delivery deficit. */
                s_pi_gap_high = 100000000;
            }
        }
        const char *pp = getenv("P2K_TCG_CLKINT_HOTLOOP_ADAPTIVE_PERIOD_MS");
        if (pp && *pp) s_pi_period_ns = strtoll(pp, NULL, 10) * 1000000LL;
        const char *pt = getenv("P2K_TCG_CLKINT_HOTLOOP_TARGET_HZ");
        if (pt && *pt) {
            double target = strtod(pt, NULL);
            if (target >= 1.0 && target <= 100000.0) {
                s_pi_target_hz = target;
            }
        }
    }
    return s_hotloop_state == 1;
}

bool p2k_clkint_hotloop_enabled(void)
{
    return p2k_hotloop_env_on();
}

bool p2k_clkint_hotloop_no_pit(void)
{
    /* One-shot env read cached alongside the other hotloop knobs. */
    static int s_no_pit_state = -1;
    if (unlikely(s_no_pit_state < 0)) {
        const char *v = getenv("P2K_TCG_CLKINT_HOTLOOP_NO_PIT");
        s_no_pit_state = (v && v[0] == '1') ? 1 : 0;
    }
    return s_no_pit_state == 1 && p2k_hotloop_env_on();
}

/* The machine's paced IRQ0 source.  Use the host clock and pulse the normal
 * i8259 input line; the PIT output level gates delivery.  This keeps timing
 * independent of translated-block boundaries while preserving the complete
 * QEMU IRQ path.  The former TB-polling implementation remains available for
 * controlled regression testing with P2K_HOTLOOP_HOST_TIMER=0. */
static bool p2k_hotloop_host_timer_enabled(void)
{
    if (unlikely(s_hotloop_host_timer_state < 0)) {
        const char *v = getenv("P2K_HOTLOOP_HOST_TIMER");
        s_hotloop_host_timer_state = (!v || v[0] != '0') ? 1 : 0;
    }
    return s_hotloop_host_timer_state == 1;
}

bool p2k_clkint_hotloop_uses_host_timer(void)
{
    return p2k_hotloop_env_on() && p2k_hotloop_host_timer_enabled();
}

static int64_t p2k_hotloop_host_nominal_period_us(void)
{
    double percent = p2k_speed_target_percent();
    int64_t period = (int64_t)(25000.0 / percent + 0.5);
    return MAX(period, 1);
}

static int64_t p2k_hotloop_host_current_period_us(void)
{
    if (s_hotloop_host_period_us == 0.0) {
        s_hotloop_host_period_us = p2k_hotloop_host_nominal_period_us();
    }
    return MAX((int64_t)(s_hotloop_host_period_us + 0.5), 1);
}

static void p2k_hotloop_host_adapt(int64_t now_ns)
{
    uint64_t clkints = p2k_audit_clkint_entered_count();

    if (!s_hotloop_adaptive) {
        return;
    }
    if (!s_hotloop_host_sample_ns) {
        s_hotloop_host_sample_ns = now_ns;
        s_hotloop_host_sample_clkints = clkints;
        return;
    }

    int64_t elapsed_ns = now_ns - s_hotloop_host_sample_ns;
    if (elapsed_ns < s_pi_period_ns) {
        return;
    }

    /* A debugger stop, host suspend, or scheduler freeze is not a pacing
     * sample.  Rebasing avoids a full-gain correction from a window in which
     * neither the timer nor the guest could run. */
    if (elapsed_ns > s_pi_period_ns * 2) {
        s_hotloop_host_sample_ns = now_ns;
        s_hotloop_host_sample_clkints = clkints;
        return;
    }

    double measured_hz = (double)(clkints - s_hotloop_host_sample_clkints) *
                         1e9 / (double)elapsed_ns;
    double target_hz = 4003.97 * p2k_speed_target_percent() / 100.0;
    if (measured_hz > 1.0) {
        double nominal = p2k_hotloop_host_nominal_period_us();
        s_hotloop_host_period_us *= measured_hz / target_hz;
        s_hotloop_host_period_us = CLAMP(s_hotloop_host_period_us,
                                         nominal * 0.5, nominal * 2.0);
        s_hotloop_min_gap_ns = p2k_hotloop_host_current_period_us() * 1000;
        s_pi_last_measured_hz = measured_hz;
    }
    s_hotloop_host_sample_ns = now_ns;
    s_hotloop_host_sample_clkints = clkints;
}

static void p2k_hotloop_host_timer_cb(void *opaque)
{
    int64_t now_us = qemu_clock_get_us(QEMU_CLOCK_HOST);

    p2k_hotloop_host_adapt(now_us * 1000);
    timer_mod(s_hotloop_host_timer,
              now_us + p2k_hotloop_host_current_period_us());
    if (!p2k_hotloop_env_on() || !s_hotloop_irq0 ||
        !s_hotloop_pit_programmed) {
        return;
    }

    p2k_timing_audit_note_irq0_raised();
    qemu_irq_pulse(s_hotloop_irq0);
    s_hotloop_reraises++;

    int64_t now_ns = now_us * 1000;
    if (s_hotloop_last_raise_ns && now_ns > s_hotloop_last_raise_ns) {
        int64_t gap_ns = now_ns - s_hotloop_last_raise_ns;
        s_jitter_min_ns = MIN(s_jitter_min_ns, gap_ns);
        s_jitter_max_ns = MAX(s_jitter_max_ns, gap_ns);
        s_jitter_sum_ns += gap_ns;
        uint64_t gap_us = gap_ns / 1000;
        s_jitter_sum_sq_us += gap_us * gap_us;
        s_jitter_count++;
    }
    s_hotloop_last_raise_ns = now_ns;
}

void p2k_clkint_hotloop_connect_irq(qemu_irq irq0)
{
    s_hotloop_irq0 = irq0;
    if (p2k_hotloop_host_timer_enabled() && !s_hotloop_host_timer) {
        s_hotloop_host_timer = timer_new_us(QEMU_CLOCK_HOST,
                                            p2k_hotloop_host_timer_cb, NULL);
        timer_mod(s_hotloop_host_timer,
                  qemu_clock_get_us(QEMU_CLOCK_HOST) +
                  p2k_hotloop_host_current_period_us());
    }
}

bool p2k_clkint_hotloop_uses_pit_stub(void)
{
    return p2k_clkint_hotloop_uses_host_timer();
}

void p2k_clkint_hotloop_pit_write(hwaddr addr, uint64_t value)
{
    if (addr != 0) {
        return;
    }

    /* The guest writes channel 0 low byte then high byte.  A value of one
     * enables the fixed clock source; any channel-0 write while enabled
     * disables it first.  Thus the normal divisor sequence finishes enabled
     * without retaining a second PIT waveform. */
    if (s_hotloop_pit_programmed) {
        s_hotloop_pit_programmed = 0;
    } else if ((value & 0xff) == 1) {
        s_hotloop_pit_programmed = 1;
    }
}

/* Priming removed 2026-07-10. Empirical wedge study proved priming
 * does not fix the combo-mode disable()/enable() race (7/8 wedged
 * with PRIME_N=800 vs 0/23 wedged with NO_PIT + no prime). NO_PIT
 * is now the default so priming has no useful purpose. See git
 * history for the old p2k_hotloop_primed() implementation. */

/* Adaptive PI controller: called opportunistically from maybe_raise().
 * Every s_pi_period_ns of virtual time, measure achieved clkint rate
 * over the last window and update s_hotloop_min_gap_ns to hold the rate
 * at nominal PIT (4003.97 Hz), i.e. delivery = 100%. */
static void p2k_hotloop_adaptive_step(int64_t now_ns)
{
    if (!s_hotloop_adaptive) return;
    if (s_pi_last_sample_ns == 0) {
        s_pi_last_sample_ns = now_ns;
        s_pi_last_clkint_count = p2k_audit_clkint_entered_count();
        return;
    }
    int64_t dt_ns = now_ns - s_pi_last_sample_ns;
    if (dt_ns < s_pi_period_ns) return;

    uint64_t now_ct = p2k_audit_clkint_entered_count();
    uint64_t dct = now_ct - s_pi_last_clkint_count;
    double dt_s = (double)dt_ns / 1e9;
    double measured_hz = (double)dct / dt_s;
    s_pi_last_measured_hz = measured_hz;

    /* Target = nominal PIT rate. Adjust gap by ratio (measured/target).
     * If measured > target: gap grows (rate falls).
     * If measured < target: gap shrinks (rate rises).
     * Gain 1.0 (full proportional) -- previously used 0.5 (damped) but
     * that undershoots on average (94% delivery instead of 100%).
     * The measurement window is 500 ms so a full-proportional step
     * settles within 1-2 windows without oscillation. */
    const double target_hz = s_pi_target_hz;
    if (measured_hz > 1.0) {
        double ratio = measured_hz / target_hz;
        double correction = ratio;  /* full proportional */
        int64_t new_gap = (int64_t)((double)s_hotloop_min_gap_ns * correction);
        if (new_gap < s_pi_gap_low)  new_gap = s_pi_gap_low;
        if (new_gap > s_pi_gap_high) new_gap = s_pi_gap_high;
        s_hotloop_min_gap_ns = new_gap;
    }

    s_pi_last_sample_ns = now_ns;
    s_pi_last_clkint_count = now_ct;
}

/* Called from the TB-boundary hook (p2k_tcg_cflags_override) once per
 * TB lookup. Cheap fast-path when the mode is off. */
void p2k_clkint_hotloop_maybe_raise(CPUState *cs)
{
    if (likely(!p2k_hotloop_env_on())) {
        return;
    }
    if (p2k_hotloop_host_timer_enabled()) {
        return;
    }
    if (!cs || !isa_pic) {
        return;
    }

    PICCommonState *m = (PICCommonState *)isa_pic;
    CPUX86State *env = &X86_CPU(cs)->env;

    /* One IRQ0 in flight at a time: never raise while previous is
     * still pending in IRR or being serviced (ISR). */
    if (m->irr & 0x01) {
        s_hotloop_skipped_pending++;
        return;
    }
    if (m->isr & 0x01) {
        s_hotloop_skipped_isr_busy++;
        return;
    }

    /* Respect the guest's own state. All of these mirror x86's own
     * pending-interrupt gate (target/i386/cpu.c
     * x86_cpu_pending_interrupt). Skipping avoids ever handing the
     * guest an IRQ0 in a context the real hardware would refuse.
     *
     * NO_PIT MODE EXCEPTION: in HOTLOOP-only mode we are the SOLE
     * source of IRR bit 0. Natural i8254 does exactly what we do
     * (level-triggered latch of IRR bit 0) WITHOUT gating on IF or
     * shadow -- those are the CPU's job (x86_cpu_pending_interrupt
     * checks them at TB boundaries when deciding to service). If we
     * gate here, guest IF=0 windows swallow all our raises and we
     * cannot match PIT's level-triggered coverage. So in NO_PIT
     * mode we skip only the PIC-level gates (pending, ISR, IMR)
     * and let the CPU handle IF and shadow itself. */
    if (!p2k_clkint_hotloop_no_pit()) {
        if (!(env->eflags & IF_MASK)) {
            s_hotloop_skipped_if0++;
            return;
        }
        if (env->hflags & HF_INHIBIT_IRQ_MASK) {
            s_hotloop_skipped_shadow++;
            return;
        }
        if (env->eflags & VM_MASK) {
            return;
        }
    }
    if (m->imr & 0x01) {
        s_hotloop_skipped_imr++;
        return;
    }
    if (!p2k_clkint_hotloop_no_pit() && p2k_audit_in_clkint()) {
        s_hotloop_skipped_in_clkint++;
        return;
    }

    /* Rate-limit: on a host that services clkint faster than real 486
     * (~250 us per clkint on the cabinet), un-throttled hotloop
     * over-delivers (game clock races 2-3x). Default gap is calibrated
     * empirically; adaptive mode retunes it every 500 ms based on
     * measured clkint rate vs nominal 4003.97 Hz. */
    int64_t now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    p2k_hotloop_adaptive_step(now_ns);
    if (s_hotloop_min_gap_ns > 0 &&
        s_hotloop_last_raise_ns != 0 &&
        (now_ns - s_hotloop_last_raise_ns) < s_hotloop_min_gap_ns) {
        s_hotloop_skipped_min_gap++;
        return;
    }

    /* Raise IRQ0. We ONLY set IRR bit 0 -- we deliberately do NOT touch
     * last_irr, so that the natural i8254 raise/lower cycles from stock
     * hw/timer/i8254.c continue to see fresh low->high transitions. If
     * we set last_irr here, the next natural raise would be treated as
     * "already latched" and no new IRR bit would ever be set -- observed
     * empirically to drop delivery from 32 % strict to 12 % hotloop. */
    m->irr |= 0x01;

    if (!bql_locked()) {
        bql_lock();
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
        bql_unlock();
    } else {
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    }
    s_hotloop_reraises++;
    if (s_hotloop_last_raise_ns) {
        /* Warmup reset: at first raise, compute deadline. Then when
         * vtime crosses deadline, zero the jitter accumulators once. */
        if (unlikely(s_jitter_reset_at_ns < 0)) {
            const char *w = getenv("P2K_TCG_CLKINT_HOTLOOP_JITTER_WARMUP_S");
            int64_t warm_s = (w && *w) ? strtoll(w, NULL, 10) : 30;
            if (warm_s <= 0) {
                s_jitter_reset_at_ns = 0;  /* disabled */
                s_jitter_reset_done = true;
            } else {
                s_jitter_reset_at_ns = now_ns + warm_s * 1000000000LL;
            }
        }
        if (!s_jitter_reset_done && s_jitter_reset_at_ns > 0 &&
            now_ns >= s_jitter_reset_at_ns) {
            s_jitter_min_ns    = INT64_MAX;
            s_jitter_max_ns    = 0;
            s_jitter_count     = 0;
            s_jitter_sum_ns    = 0;
            s_jitter_sum_sq_us = 0;
            s_jitter_reset_done = true;
        }

        int64_t gap_ns = now_ns - s_hotloop_last_raise_ns;
        if (gap_ns > 0) {
            if (gap_ns < s_jitter_min_ns) s_jitter_min_ns = gap_ns;
            if (gap_ns > s_jitter_max_ns) s_jitter_max_ns = gap_ns;
            s_jitter_sum_ns += (uint64_t)gap_ns;
            uint64_t g_us = (uint64_t)gap_ns / 1000ull;
            s_jitter_sum_sq_us += g_us * g_us;
            s_jitter_count++;
        }
    }
    s_hotloop_last_raise_ns = now_ns;
}

uint64_t p2k_clkint_hotloop_count_reraises(void)   { return s_hotloop_reraises; }
uint64_t p2k_clkint_hotloop_count_skipped_pending(void) { return s_hotloop_skipped_pending; }
uint64_t p2k_clkint_hotloop_count_skipped_isr(void)     { return s_hotloop_skipped_isr_busy; }
uint64_t p2k_clkint_hotloop_count_skipped_imr(void)     { return s_hotloop_skipped_imr; }
uint64_t p2k_clkint_hotloop_count_skipped_if0(void)     { return s_hotloop_skipped_if0; }
uint64_t p2k_clkint_hotloop_count_skipped_shadow(void)  { return s_hotloop_skipped_shadow; }
uint64_t p2k_clkint_hotloop_count_skipped_in_clkint(void){return s_hotloop_skipped_in_clkint;}
uint64_t p2k_clkint_hotloop_count_skipped_min_gap(void) { return s_hotloop_skipped_min_gap; }
int64_t  p2k_clkint_hotloop_current_gap_ns(void) { return s_hotloop_min_gap_ns; }
double   p2k_clkint_hotloop_measured_hz(void)    { return s_pi_last_measured_hz; }
bool     p2k_clkint_hotloop_adaptive_enabled(void){ return s_hotloop_adaptive; }
int64_t  p2k_clkint_hotloop_jitter_min_ns(void) { return s_jitter_count ? s_jitter_min_ns : 0; }
int64_t  p2k_clkint_hotloop_jitter_max_ns(void) { return s_jitter_max_ns; }
uint64_t p2k_clkint_hotloop_jitter_count(void)  { return s_jitter_count; }
uint64_t p2k_clkint_hotloop_jitter_mean_ns(void)
{
    return s_jitter_count ? (s_jitter_sum_ns / s_jitter_count) : 0;
}
uint64_t p2k_clkint_hotloop_jitter_stddev_us(void)
{
    if (s_jitter_count < 2) return 0;
    uint64_t mean_us = (s_jitter_sum_ns / s_jitter_count) / 1000ull;
    uint64_t mean_sq = (s_jitter_sum_sq_us / s_jitter_count);
    uint64_t sq_mean = mean_us * mean_us;
    if (mean_sq <= sq_mean) return 0;
    uint64_t var = mean_sq - sq_mean;
    /* integer sqrt */
    uint64_t x = var, r = 0;
    while (x >= (r + 1) * (r + 1)) r++;
    return r;
}

/* Called from pinball2000.c IRQ0 tap when NO_PIT mode is active and
 * the natural PIT-driven edge is being suppressed. Counts natural
 * PIT raises that we chose to swallow (so audit still knows how many
 * "expected" PIT edges came in). */
void p2k_hotloop_note_swallowed_edge(void)
{
    s_hotloop_swallowed_edges++;
}

uint64_t p2k_hotloop_swallowed_edges_count(void)
{
    return s_hotloop_swallowed_edges;
}
