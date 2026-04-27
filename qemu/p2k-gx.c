/*
 * p2k-gx.c — Cyrix MediaGX GX_BASE stub (16 MiB @ 0x40000000).
 *
 * Region layout (per unicorn.old/include/encore.h:65-99 and
 * unicorn.old/src/bar.c:455-700):
 *   0x40000000 .. 0x40007FFF  unused / scratch
 *   0x40008000 .. 0x40008FFF  DC display-controller registers
 *   0x40020000 .. 0x40020FFF  BC bus-controller registers
 *                              [+0x20000] = BC_DRAM_TOP, must be 0x007FFFFF
 *                              so PRISM's BIOS path sees 8 MiB installed.
 *   0x40800000 .. 0x40BFFFFF  framebuffer (4 MiB)
 *
 * For now this is a single zero-init RAM region: the game can scribble
 * registers and framebuffer freely without faulting.  Real device
 * semantics (DC_TIMING2 vsync counter, GP BLT engine, framebuffer
 * presentation) belong in their own files later.
 *
 * The single seeded value is BC_DRAM_TOP at offset 0x20000 — required
 * for guest BIOS to detect the correct RAM size (matches behavior in
 * unicorn.old/src/bar.c:425-427).
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"

#include "p2k-internal.h"

#define GX_BASE       0x40000000u
#define GX_SIZE       0x01000000u   /* 16 MiB */
#define GX_BC_DRAM_TOP_OFF  0x20000u
#define GX_BC_DRAM_TOP_VAL  0x007FFFFFu  /* 8 MiB - 1 */

void p2k_install_gx_stub(void)
{
    MemoryRegion *mr = g_new(MemoryRegion, 1);
    void         *host;

    memory_region_init_ram(mr, NULL, "p2k.gx", GX_SIZE, &error_abort);
    memory_region_add_subregion(get_system_memory(), GX_BASE, mr);

    /* Pre-seed BC_DRAM_TOP = 8 MiB - 1.  Per unicorn.old/src/bar.c the
     * guest BIOS reads this to discover installed RAM size; without it
     * the boot path may either pick a wrong RAM ceiling or busy-spin. */
    host = memory_region_get_ram_ptr(mr);
    *(uint32_t *)((uint8_t *)host + GX_BC_DRAM_TOP_OFF) = GX_BC_DRAM_TOP_VAL;
}
