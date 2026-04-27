/*
 * p2k-gx.c — Cyrix MediaGX GX_BASE stub (16 MiB @ 0x40000000).
 *
 * Region layout (per unicorn.old/include/encore.h:65-99 and
 * unicorn.old/src/bar.c:455-700):
 *
 *   0x40000000 .. 0x407FFFFF  registers (GP, DC, BC) — 8 MiB plain RAM
 *                              [+0x20000] = BC_DRAM_TOP, must be 0x007FFFFF
 *                              so PRISM's BIOS path sees 8 MiB installed.
 *   0x40800000 .. 0x40BFFFFF  framebuffer — alias into system RAM 0x800000.
 *                              Per unicorn.old/src/bar.c:699-704 every write
 *                              into the GX FB window is mirrored to physical
 *                              RAM at 0x800000, and per display.c:323 the
 *                              display reads from that same RAM address.
 *                              GP BLT engine and the display path both go
 *                              through 0x800000 — keeping a single backing
 *                              store avoids divergence.
 *   0x40C00000 .. 0x40FFFFFF  more register space — 4 MiB plain RAM.
 *
 * Real device semantics (DC_TIMING2 vsync counter, GP BLT engine, display
 * presentation) belong in their own files later — this file only owns the
 * memory shape so the game can scribble freely without faulting.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "hw/boards.h"

#include "p2k-internal.h"

#define GX_BASE              0x40000000u
#define GX_REGS1_OFF         0x00000000u
#define GX_REGS1_SIZE        0x00800000u    /* 8 MiB regs */
#define GX_FB_OFF            0x00800000u
#define GX_FB_SIZE           0x00400000u    /* 4 MiB framebuffer */
#define GX_REGS2_OFF         0x00C00000u
#define GX_REGS2_SIZE        0x00400000u    /* 4 MiB regs */

#define GX_BC_DRAM_TOP_OFF   0x20000u
#define GX_BC_DRAM_TOP_VAL   0x007FFFFFu    /* 8 MiB - 1 */

#define GX_FB_RAM_MIRROR     0x00800000u    /* system-RAM offset of FB */

void p2k_install_gx_stub(void)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    MemoryRegion *sys = get_system_memory();
    MemoryRegion *regs1 = g_new(MemoryRegion, 1);
    MemoryRegion *regs2 = g_new(MemoryRegion, 1);
    MemoryRegion *fb_alias = g_new(MemoryRegion, 1);
    void         *host;

    /* Regs1 (8 MiB) — covers GP @ +0x8100, DC @ +0x8300, BC @ +0x20000. */
    memory_region_init_ram(regs1, NULL, "p2k.gx.regs1",
                           GX_REGS1_SIZE, &error_abort);
    memory_region_add_subregion(sys, GX_BASE + GX_REGS1_OFF, regs1);

    /* Pre-seed BC_DRAM_TOP = 8 MiB - 1.  Per unicorn.old/src/bar.c:425-427
     * the guest BIOS reads this to discover installed RAM size. */
    host = memory_region_get_ram_ptr(regs1);
    *(uint32_t *)((uint8_t *)host + GX_BC_DRAM_TOP_OFF) = GX_BC_DRAM_TOP_VAL;

    /* Framebuffer (4 MiB) — alias into system RAM at 0x800000.  Both GP
     * blits and the display read this same backing store, matching the
     * unicorn.old observation that the FB window is a mirror of physical
     * RAM 0x800000.  Requires -m at least 16 MiB so 0x800000+0x400000 fits. */
    if (ms->ram_size < GX_FB_RAM_MIRROR + GX_FB_SIZE) {
        error_report("pinball2000: -m must be >= 16 (need RAM through 0xC00000 "
                     "for GX framebuffer mirror)");
        exit(1);
    }
    memory_region_init_alias(fb_alias, NULL, "p2k.gx.fb",
                             ms->ram, GX_FB_RAM_MIRROR, GX_FB_SIZE);
    memory_region_add_subregion(sys, GX_BASE + GX_FB_OFF, fb_alias);

    /* Regs2 (4 MiB) — upper register window. */
    memory_region_init_ram(regs2, NULL, "p2k.gx.regs2",
                           GX_REGS2_SIZE, &error_abort);
    memory_region_add_subregion(sys, GX_BASE + GX_REGS2_OFF, regs2);
}
