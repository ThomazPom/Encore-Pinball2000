/*
 * pinball2000: back-to-back IRQ0 replay (P2K_TCG_CLKINT_HOTLOOP).
 *
 * HOTLOOP promptly reasserts IRQ0 after the guest consumes it, while avoiding
 * duplicate pending or in-service requests. The target is real-time XINU clock
 * progress with measurable delivery and bounded inter-arrival gaps.
 *
 * Representative PIC observations during development:
 *
 *   pic0: irr=01 imr=60 isr=00     325 samples  (81.3 %)
 *   pic0: irr=01 imr=ff isr=00      52           (13.0 %) [inside disable() window]
 *   pic0: irr=00 imr=60 isr=00      10            (2.5 %)
 *   pic0: irr=01 imr=60 isr=01       7            (1.8 %)
 *   pic0: irr=00 imr=ff isr=00       5            (1.3 %)
 *   pic0: irr=00 imr=60 isr=01       1            (0.3 %)
 *
 * -> IRQ0 in the master PIC's IRR is asserted ~96 % of the time.
 *    ELCR=00 (edge-triggered), so this is NOT a level-high line but
 *    something that re-asserts IRQ0 promptly whenever it has just
 *    been consumed.
 * -> Inter-arrival of "Servicing hardware INT=0x20" events is
 *    tight (p50=p99=20 log-blocks, max=60) -- no bursts (max is
 *    only 3x the minimum, unlike Encore's strict path where max is
 *    62x p50).
 *
 * Mechanism this mode implements
 * ------------------------------
 * At every TB boundary (via p2k_tcg_cflags_override, same hook the
 * direct-clkint fast mode already uses), we look at the master
 * i8259:
 *
 *   if IRQ0 has been fully consumed (IRR bit 0 clear AND ISR bit 0
 *   clear), AND the guest is willing to take an IRQ (IF=1, IMR bit
 *   0 clear, no interrupt-shadow, not V86, not currently in clkint):
 *
 *     -> set master IRR bit 0
 *     -> mark last_irr low->high edge
 *     -> cpu_interrupt(CPU_INTERRUPT_HARD)
 *
 * "irr=01 nearly always" state: raise IRQ0 as soon as the previous
 * one has finished, and never before. There is no burst, no
 * multi-tick catch-up, no synthetic ISR-bit-forcing -- one IRQ0 in
 * flight at a time, back-to-back. If the guest's clkint handler
 * takes wall time T to run, this mode delivers ~1/T IRQ0 per second,
 * which is exactly the property that lets a real 486 board on the
 * cabinet reach the 4 kHz nominal PIT rate: on that hardware the
 * handler takes ~250 us, so back-to-back replay = 4 kHz.
 *
 * We deliberately do NOT touch the natural i8254 path. In this mode
 * the stock i8254 keeps ticking at 4 kHz and setting IRR too --
 * that's harmless because our own raise is idempotent (i8259 IRR is
 * a bit set, not a counter). The important effect is that after each
 * INTACK+EOI, our next TB-boundary check re-asserts IRR immediately
 * instead of waiting on the next PIT edge callback (which is the
 * source of Encore's natural coalescing).
 *
 * What this mode does NOT try to do (compared to direct-clkint):
 *   - it does NOT bypass the IDT[0x20] gate or upstream's interrupt
 *     entry path -- delivery uses the standard x86 CPU_INTERRUPT_HARD
 *     -> pic_read_irq -> do_interrupt_x86_hardirq flow
 *   - it does NOT synthesise multi-tick bursts to catch up on wall
 *     time (that is what upsets a real board's LPT byte cadence,
 *     per the user's cabinet observations)
 *   - it does NOT install a QEMUTimer at PIT rate; the natural
 *     i8254 remains the primary source, this hook just closes the
 *     coalescing gap
 *
 * Off by default; enable with P2K_TCG_CLKINT_HOTLOOP=1. Independent
 * from P2K_TCG_DIRECT_CLKINT.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/timer.h"
#include <stdlib.h>
#include "exec/cpu-common.h"
#include "hw/core/cpu.h"
#include "hw/intc/i8259.h"
#include "hw/isa/i8259_internal.h"
#include "system/system.h"
#include "target/i386/cpu.h"

#include "p2k-internal.h"

static int s_hotloop_state = -1;  /* -1 undecided, 0 off, 1 on */
static int64_t s_hotloop_min_gap_ns = -1;  /* -1 uninit, 0 = no throttle, >0 = min ns between raises */
static int64_t s_hotloop_last_raise_ns;

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
    if (!cs || !isa_pic) {
        return;
    }

    PICCommonState *m = (PICCommonState *)isa_pic;
    CPUX86State *env = &X86_CPU(cs)->env;

    /* One IRQ0 in flight at a time: never raise while previous is
     * still pending in IRR or being serviced (ISR). This is the
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
