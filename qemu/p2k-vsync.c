/*
 * pinball2000 VSYNC ticker.
 *
 * Williams Pinball 2000 game code waits on two display-timing inputs
 * during init and steady-state:
 *
 *   1. BAR2_SRAM[0x4..0x7] — 32-bit "vsync seen" flag.  Several poll
 *      loops in XINU display setup wait for this dword to flip from 0
 *      to 1 each frame before continuing.
 *   2. MediaGX MMIO at GX_BASE + 0x8354 (DC_TIMING2) — the current
 *      VBLANK line counter.  At end-of-frame it pulses 241 (= 480/2 +
 *      1, encoded scanline); during active scanout it cycles 0..240.
 *      Game code reads this register to gate vertical-retrace-only
 *      writes (palette swap, layer flip, sample-counter increment).
 *
 * Reference: unicorn.old/src/cpu.c:524-547.  We do NOT port the
 * unicorn timing/vticks/budget heuristics — only the wall-clock vsync
 * cadence (~57 Hz, 17.5 ms period).
 *
 * Implementation: a single QEMU virtual-clock timer.  At each fire:
 *   - Increment internal scanline counter by 8.  When it reaches 241,
 *     write 241 to DC_TIMING2 and write 1 to BAR2_SRAM[4]; then reset
 *     to 0 on the next fire.  Timer always re-arms 17.5/30 ms ahead so
 *     we get ~30 sub-vsync DC_TIMING2 updates per real frame, matching
 *     the expected scanout granularity.  This is simpler than running
 *     two timers (one slow vsync, one fast scan-line).
 *
 * Both BAR2 and GX_REGS2 are plain RAM regions — use
 * cpu_physical_memory_write for the bus write so QEMU's IOMMU/dirty
 * tracking is honoured.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "exec/cpu-common.h"

#include "p2k-internal.h"

#define P2K_BAR2_BASE        0x11000000u
#define P2K_GX_BASE          0x40000000u
#define P2K_DC_TIMING2_OFF   0x00008354u

#define P2K_VSYNC_PERIOD_NS  (17500000ULL)            /* 17.5 ms = ~57 Hz */
#define P2K_SCANLINE_TICKS   30u                      /* updates per frame */
#define P2K_SUBTICK_NS       (P2K_VSYNC_PERIOD_NS / P2K_SCANLINE_TICKS)

static QEMUTimer *p2k_vsync_timer;
static uint32_t   p2k_scanline;        /* 0..240, increments by 8 */

static void p2k_vsync_write_dword(hwaddr addr, uint32_t v)
{
    uint8_t le[4] = {
        (uint8_t)(v & 0xff),
        (uint8_t)((v >> 8) & 0xff),
        (uint8_t)((v >> 16) & 0xff),
        (uint8_t)((v >> 24) & 0xff),
    };
    cpu_physical_memory_write(addr, le, 4);
}

static void p2k_vsync_tick(void *opaque)
{
    p2k_scanline += 8;
    if (p2k_scanline >= 241) {
        /* End-of-frame pulse: assert vsync flag and DC_TIMING2=241. */
        p2k_vsync_write_dword(P2K_BAR2_BASE + 4, 1);
        p2k_vsync_write_dword(P2K_GX_BASE + P2K_DC_TIMING2_OFF, 241);
        p2k_scanline = 0;
    } else {
        /* Active line counter; vsync flag stays 0 here so polling
         * loops only see the rising edge once per frame. */
        p2k_vsync_write_dword(P2K_GX_BASE + P2K_DC_TIMING2_OFF,
                              p2k_scanline);
    }

    timer_mod(p2k_vsync_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_SUBTICK_NS);
}

void p2k_install_vsync(void)
{
    p2k_vsync_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                   p2k_vsync_tick, NULL);
    timer_mod(p2k_vsync_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_SUBTICK_NS);
    info_report("pinball2000: VSYNC ticker armed (~57 Hz frame, "
                "%u sub-tick updates @ DC_TIMING2)",
                P2K_SCANLINE_TICKS);
}
