/*
 * pinball2000 PLX9054 BAR2/BAR3/BAR4 RAM stubs.
 *
 * The PRISM card exposes several MMIO BARs:
 *
 *   BAR2 @ 0x11000000  256 KiB battery-backed SRAM (+ char display +
 *                      watchdog @ +0x420 + DC_TIMING2 @ +0x3F4)
 *   BAR3 @ 0x12000000  4 MiB update flash (Pin2000 update mechanism)
 *   BAR4 @ 0x13000000  16 MiB DCS audio board window
 *
 * Without ANY backing for BAR3, PRISM's update-flash validator
 * (0x85578 / 0x855D4 / 0x85618 in option ROM) reads the header back as
 * 0xFF, computes a count of 0xFFFFFFFF, and spins ~4 billion iterations
 * through a checksum loop — that is the hot loop we observed at PCs
 * 0x85654 / 0x855F2.
 *
 * For first-boot purposes we provide all three BARs as plain zero-init
 * RAM.  That makes:
 *   - BAR3[0x7d00] == 0   ->   PRISM takes the "[ UPDATE DISABLED ]"
 *                              branch and skips the whole update path.
 *   - BAR2 is writable so the watchdog/char-display register pokes do
 *     not fault.  (Watchdog poll-back semantics will be added later in
 *     a dedicated p2k-bar2-sram.c when we know which addresses must
 *     read back specific magic values; for now zeros are enough to get
 *     past PRISM init.)
 *   - BAR4 is writable so DCS audio register pokes do not fault.
 *
 * This is a TEMPORARY catch-all — once we know which sub-regions need
 * real semantics (per unicorn.old/src/io.c + bar.c) those will be
 * carved out into their own MemoryRegions with custom ops.  Until
 * then, plain RAM is the minimum that lets PRISM continue.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"

#include "pinball2000.h"
#include "p2k-internal.h"

#define P2K_BAR2_BASE        0x11000000u
#define P2K_BAR3_BASE        0x12000000u
#define P2K_BAR3_SIZE        0x00400000u   /* 4 MiB */
#define P2K_BAR4_BASE        0x13000000u
#define P2K_BAR4_SIZE        0x01000000u   /* 16 MiB */

static void map_ram(MemoryRegion *sm, const char *name,
                    hwaddr base, uint64_t size)
{
    MemoryRegion *mr = g_new(MemoryRegion, 1);
    memory_region_init_ram(mr, NULL, name, size, &error_abort);
    /* Initialize to zero (the QEMU default) — required so PRISM
     * sees [BAR3+0x7d00]==0 and takes the no-update path. */
    memory_region_add_subregion(sm, base, mr);
    info_report("pinball2000: mapped %-20s @ 0x%08" HWADDR_PRIx
                " (%" PRIu64 " KiB, RAM)", name, base, size >> 10);
}

void p2k_install_plx_bars(void)
{
    MemoryRegion *sm = get_system_memory();
    map_ram(sm, "p2k.bar2-sram",   P2K_BAR2_BASE, P2K_BAR2_SIZE);
    /* BAR3 (update flash) is loaded by p2k_install_bar3_flash() */
    /* BAR4 (DCS audio) is now an MMIO state-machine in p2k-dcs.c */
}
