/*
 * pinball2000 PLX9054 BAR2 (battery-backed NVRAM SRAM @ 0x11000000).
 *
 * The PRISM card exposes a 128 KiB SRAM through BAR2.  XINU reads the
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
 * IMPORTANT (unicorn.old/src/bar.c:526-535): the full BAR2 PCI window
 * is 16 MiB.  The first 128 KiB is the real SRAM; reads above it must
 * return 0xFFFFFFFF.  This is the "Phase 3 channel scan empty
 * sentinel" — DCS-2's channel-init walks the upper window looking for
 * an empty slot, and only after finding one does it call regres()
 * with the 8-space ID that unblocks the XINA resource-lookup loop.
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
#define P2K_BAR2_SRAM_SIZE   0x00020000u   /* 128 KiB SRAM (matches encore.h BAR2_SIZE) */
#define P2K_BAR2_WINDOW_SIZE 0x01000000u   /* 16 MiB full PCI BAR2 window */

static uint64_t p2k_bar2_sentinel_read(void *opaque, hwaddr off, unsigned sz)
{
    /* Phase 3 channel scan empty sentinel — must read all-ones. */
    if (sz >= 8) return 0xFFFFFFFFFFFFFFFFULL;
    return (1ULL << (sz * 8)) - 1ULL;
}

static void p2k_bar2_sentinel_write(void *opaque, hwaddr off,
                                    uint64_t val, unsigned sz)
{
    /* Writes above 128 KiB are silently dropped (no SRAM there). */
}

static const MemoryRegionOps p2k_bar2_sentinel_ops = {
    .read  = p2k_bar2_sentinel_read,
    .write = p2k_bar2_sentinel_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
};

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
    size_t n = fread(host, 1, P2K_BAR2_SRAM_SIZE, fp);
    fclose(fp);
    info_report("pinball2000: BAR2 SRAM seeded from %s (%zu of %u bytes)",
                path, n, P2K_BAR2_SRAM_SIZE);
}

void p2k_install_plx_bars(Pinball2000MachineState *s)
{
    MemoryRegion *sm = get_system_memory();

    /* Phase-3 sentinel container covers the whole 16 MiB BAR2 window
     * and returns 0xFFFFFFFF for every address.  Lower-priority. */
    MemoryRegion *sentinel = g_new(MemoryRegion, 1);
    memory_region_init_io(sentinel, NULL, &p2k_bar2_sentinel_ops, NULL,
                          "p2k.bar2-sentinel", P2K_BAR2_WINDOW_SIZE);
    memory_region_add_subregion_overlap(sm, P2K_BAR2_BASE, sentinel, 0);

    /* Real 128 KiB SRAM overlaid on top at offset 0 of the window. */
    MemoryRegion *bar2 = g_new(MemoryRegion, 1);
    memory_region_init_ram(bar2, NULL, "p2k.bar2-sram",
                           P2K_BAR2_SRAM_SIZE, &error_abort);
    memory_region_add_subregion_overlap(sm, P2K_BAR2_BASE, bar2, 1);
    info_report("pinball2000: mapped %-20s @ 0x%08x (%u KiB SRAM + 16 MiB sentinel)",
                "p2k.bar2-sram", P2K_BAR2_BASE, P2K_BAR2_SRAM_SIZE >> 10);

    p2k_seed_bar2_from_nvram(bar2, s);

    /* BAR3 (update flash) is loaded by p2k_install_bar3_flash() */
    /* BAR4 (DCS audio) is now an MMIO state-machine in p2k-dcs.c */
}
