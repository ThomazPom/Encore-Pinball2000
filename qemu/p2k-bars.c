/*
 * pinball2000 PLX9054 BAR2 (battery-backed NVRAM SRAM @ 0x11000000).
 *
 * The PRISM card exposes a 256 KiB SRAM through BAR2.  XINU reads the
 * factory settings, machine audits, and several resource lookup tables
 * out of this NVRAM during early init.  When the SRAM is uninitialised
 * (all zeros) the resource table comes back empty and XINU spins
 * forever printing
 *
 *     *** NonFatal: Retrieve Resource (get &) Failed, ID=
 *
 * with garbage IDs (mostly spaces).  Mirrors what
 * unicorn.old/src/rom.c:savedata_load() loads from
 * savedata/<game>.nvram2 into g_emu.bar2_sram.
 *
 * BAR3 (update flash) is loaded by p2k-bar3-flash.c.  BAR4 (DCS audio)
 * is an MMIO state machine in p2k-dcs.c.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"

#include "pinball2000.h"
#include "p2k-internal.h"

#define P2K_BAR2_BASE        0x11000000u

static void p2k_seed_bar2_from_nvram(MemoryRegion *mr,
                                     Pinball2000MachineState *s)
{
    char path[1024];
    snprintf(path, sizeof(path), "savedata/%s.nvram2", s->game);
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        warn_report("pinball2000: %s not found; BAR2 SRAM left zero "
                    "(XINU resource lookups will likely fail)", path);
        return;
    }
    void *host = memory_region_get_ram_ptr(mr);
    size_t n = fread(host, 1, P2K_BAR2_SIZE, fp);
    fclose(fp);
    info_report("pinball2000: BAR2 SRAM seeded from %s (%zu of %u bytes)",
                path, n, P2K_BAR2_SIZE);
}

void p2k_install_plx_bars(Pinball2000MachineState *s)
{
    MemoryRegion *sm = get_system_memory();

    MemoryRegion *bar2 = g_new(MemoryRegion, 1);
    memory_region_init_ram(bar2, NULL, "p2k.bar2-sram",
                           P2K_BAR2_SIZE, &error_abort);
    memory_region_add_subregion(sm, P2K_BAR2_BASE, bar2);
    info_report("pinball2000: mapped %-20s @ 0x%08x (%u KiB, RAM)",
                "p2k.bar2-sram", P2K_BAR2_BASE, P2K_BAR2_SIZE >> 10);

    p2k_seed_bar2_from_nvram(bar2, s);

    /* BAR3 (update flash) is loaded by p2k_install_bar3_flash() */
    /* BAR4 (DCS audio) is now an MMIO state-machine in p2k-dcs.c */
}
