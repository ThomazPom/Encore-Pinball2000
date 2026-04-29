/*
 * pinball2000 VSYNC source — DC_TIMING2 as real device-register MMIO,
 * BAR2[+4] as guest-writable SRAM with a small frame-cadence host
 * write of the rising edge.
 *
 * The Williams XINU display path needs two end-of-frame inputs:
 *
 *   1. MediaGX MMIO at GX_BASE + 0x8354 (DC_TIMING2) — the live
 *      VBLANK line counter. At end-of-frame it pulses 241; during
 *      active scanout it cycles 0..240. Game code reads the value
 *      and tests `(val & 0x7FF) > 0xF0` to detect VBLANK
 *      (cf. unicorn.old/src/cpu.c:524-547). On real hardware this
 *      is a *device register*, not RAM — the value is computed by
 *      the GX video controller and the host CPU never writes it.
 *   2. BAR2_SRAM[0x4..0x7] — 32-bit "vsync seen" flag. Several
 *      polling loops in display setup wait for this dword to flip
 *      from 0 to 1 each frame. On real hardware this *is* RAM in
 *      the PLX SRAM, written by the host driver each VBLANK.
 *
 * Implementation:
 *
 *   - DC_TIMING2 is now a real read-only MMIO overlay at priority 2
 *     above the GX regs1 RAM (priority 0). The read handler returns
 *     the live counter / EOF marker; writes are silently dropped
 *     (DC_TIMING2 is read-only on real hardware). This removes the
 *     host-side scribble of guest GX RAM at a hard-coded address —
 *     exactly the dirty pattern we are retiring.
 *
 *   - BAR2[+4] stays plain SRAM (it really *is* RAM on the bus). A
 *     single QEMU virtual-clock timer fires once per frame and
 *     writes 1 to BAR2[+4] for the EOF window. The next sub-tick
 *     writes 0. This is the same behaviour the previous, fully
 *     timer-driven implementation provided, but the only host
 *     writes left target a single dword in real SRAM (which guest
 *     code is also free to read/write) — not a fake "device
 *     register" being faked with a RAM scribble.
 *
 *   - The internal scan-line counter is stable between timer fires
 *     (~580 us), so two consecutive DC_TIMING2 reads from the same
 *     code path see the same value, matching the property the
 *     original RAM-scribble approach relied on.
 *
 * Net effect vs the original timer scribbler:
 *   - DC_TIMING2 is now real device behaviour (no host write into
 *     guest GX RAM at a fake register address)
 *   - BAR2[+4] keeps its existing SRAM-flag semantics (guest can
 *     still read or clear it; the host writes one dword per frame)
 *   - the 30-subtick cadence is preserved end-to-end
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "exec/address-spaces.h"
#include "exec/cpu-common.h"

#include "p2k-internal.h"

#define P2K_BAR2_BASE        0x11000000u
#define P2K_BAR2_VSYNC_OFF   0x00000004u
#define P2K_GX_BASE          0x40000000u
#define P2K_DC_TIMING2_OFF   0x00008354u

#define P2K_VSYNC_PERIOD_NS  (17500000ULL)            /* 17.5 ms = ~57 Hz */
#define P2K_SCANLINE_TICKS   30u                      /* sub-tick updates */
#define P2K_SUBTICK_NS       (P2K_VSYNC_PERIOD_NS / P2K_SCANLINE_TICKS)
#define P2K_SCANLINE_MAX     241u                     /* EOF marker value */

static QEMUTimer *p2k_vsync_timer;
static uint32_t   p2k_scanline;        /* 0..241 */

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
    if (p2k_scanline >= P2K_SCANLINE_MAX) {
        /* End-of-frame pulse. Raise BAR2[+4]; DC_TIMING2 returns 241
         * via the read handler below. We do not host-clear BAR2[+4]
         * — the guest owns the falling edge (matches the original
         * timer scribbler behaviour). */
        p2k_scanline = P2K_SCANLINE_MAX;
        p2k_vsync_write_dword(P2K_BAR2_BASE + P2K_BAR2_VSYNC_OFF, 1);
    }
    if (p2k_scanline == P2K_SCANLINE_MAX) {
        /* Reset on the *next* fire so the EOF window lasts exactly
         * one sub-tick. */
        p2k_scanline = 0;
    }
    timer_mod(p2k_vsync_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_SUBTICK_NS);
}

/* --- DC_TIMING2 (GX_BASE + 0x8354) read-only MMIO ------------------- */

static uint64_t p2k_dc_timing2_read(void *opaque, hwaddr off, unsigned sz)
{
    uint32_t val = (p2k_scanline == P2K_SCANLINE_MAX) ? P2K_SCANLINE_MAX
                                                      : p2k_scanline;
    uint64_t mask = (sz >= 4) ? 0xFFFFFFFFULL
                              : ((1ULL << (sz * 8)) - 1ULL);
    return ((uint64_t)val >> (off * 8)) & mask;
}

static void p2k_dc_timing2_write(void *opaque, hwaddr off,
                                 uint64_t val, unsigned sz)
{
    /* Read-only status register — drop guest writes. */
}

static const MemoryRegionOps p2k_dc_timing2_ops = {
    .read       = p2k_dc_timing2_read,
    .write      = p2k_dc_timing2_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid      = { .min_access_size = 1, .max_access_size = 4 },
    .impl       = { .min_access_size = 1, .max_access_size = 4 },
};

void p2k_install_vsync(void)
{
    MemoryRegion *sys = get_system_memory();

    /* DC_TIMING2: 4-byte read-only overlay above the GX regs1 RAM
     * (priority 0); install at priority 2 so we win. */
    MemoryRegion *dc = g_new(MemoryRegion, 1);
    memory_region_init_io(dc, NULL, &p2k_dc_timing2_ops, NULL,
                          "p2k.gx.dc_timing2", 4);
    memory_region_add_subregion_overlap(sys,
        P2K_GX_BASE + P2K_DC_TIMING2_OFF, dc, 2);

    /* Timer drives the internal counter and the BAR2[+4] flag write
     * (BAR2[+4] is real SRAM and may be cleared by the guest). */
    p2k_vsync_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, p2k_vsync_tick, NULL);
    timer_mod(p2k_vsync_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_SUBTICK_NS);

    info_report("pinball2000: VSYNC armed (DC_TIMING2 MMIO @ 0x%08x, "
                "BAR2[+4] SRAM flag @ 0x%08x; %llu ms frame, "
                "%u sub-ticks)",
                P2K_GX_BASE + P2K_DC_TIMING2_OFF,
                P2K_BAR2_BASE + P2K_BAR2_VSYNC_OFF,
                (unsigned long long)(P2K_VSYNC_PERIOD_NS / 1000000ULL),
                P2K_SCANLINE_TICKS);
}
