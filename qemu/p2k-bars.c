/*
 * pinball2000 PLX9054 BAR2 (battery-backed NVRAM SRAM @ 0x11000000).
 *
 * The PRISM card exposes 192 KiB of battery-backed SRAM through BAR2.  XINU reads the
 * factory settings, machine audits, and several resource lookup tables
 * out of this NVRAM during early init.  When the SRAM is uninitialised
 * (all zeros) the resource table comes back empty and XINU spins
 * forever printing
 *
 *     *** NonFatal: Retrieve Resource (get &) Failed, ID=
 *
 * from savedata/<game>.nvram2 into its bar2_sram buffer.
 *
 * IMPORTANT: the full BAR2 PCI window is 16 MiB.  The first 192 KiB is
 * the real SRAM; reads above it must return 0xFFFFFFFF.  This is the
 * "Phase 3 channel scan empty sentinel" — DCS-2's channel-init walks the
 * upper window looking for an empty slot, and only after finding one does
 * it call regres() with the 8-space ID that unblocks the XINA resource-
 * lookup loop.
 *
 * BAR3 (update flash) is loaded by p2k-bar3-flash.c.  BAR4 (DCS audio)
 * is an MMIO state machine in p2k-dcs.c.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/notify.h"
#include "qapi/error.h"
#include "system/system.h"
#include "p2k-qemu-compat.h"
#include <errno.h>

#include "pinball2000.h"
#include "p2k-internal.h"

#define P2K_BAR2_BASE        0x11000000u
/* XINA's CMOS test reaches the complete 0x30000-byte persisted range.
 * Exposing only 128 KiB incorrectly forces it onto temporary system RAM. */
#define P2K_BAR2_SRAM_SIZE   P2K_BAR2_SIZE
#define P2K_BAR2_WINDOW_SIZE 0x01000000u   /* 16 MiB full PCI BAR2 window */

static MemoryRegion *s_bar2_mr;
static char          s_bar2_save_path[1024];

static uint64_t p2k_bar2_sentinel_read(void *opaque, hwaddr off, unsigned sz)
{
    /* Phase 3 channel scan empty sentinel — must read all-ones. */
    if (sz >= 8) return 0xFFFFFFFFFFFFFFFFULL;
    return (1ULL << (sz * 8)) - 1ULL;
}

static void p2k_bar2_sentinel_write(void *opaque, hwaddr off,
                                    uint64_t val, unsigned sz)
{
    /* Writes above 192 KiB are silently dropped (no SRAM there). */
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
    snprintf(s_bar2_save_path, sizeof(s_bar2_save_path), "%s/%s.nvram2",
             s->savedata_dir, s->game);
    if (p2k_no_savedata_enabled() || p2k_fresh_savedata_enabled()) {
        if (p2k_fresh_savedata_enabled()) {
            info_report("pinball2000: fresh run — BAR2 SRAM starts empty");
        }
        return;
    }
    FILE *fp = fopen(s_bar2_save_path, "rb");
    if (!fp) {
        warn_report("pinball2000: %s not found; BAR2 SRAM left zero "
                    "(XINU resource lookups will likely fail)",
                    s_bar2_save_path);
        return;
    }
    void *host = memory_region_get_ram_ptr(mr);
    size_t n = fread(host, 1, P2K_BAR2_SRAM_SIZE, fp);
    fclose(fp);
    info_report("pinball2000: BAR2 SRAM seeded from %s (%zu of %u bytes)",
                s_bar2_save_path, n, P2K_BAR2_SRAM_SIZE);
}

/* Flush BAR2 SRAM (battery-backed NVRAM) back to savedata at exit so
 * audits, high scores, and service-mode settings persist across runs.
 * Atomic write via tmp+rename. Disable with P2K_NO_SAVEDATA=1. */
static void p2k_bar2_save_cb(Notifier *notifier, void *data)
{
    if (!s_bar2_mr || !s_bar2_save_path[0]) return;
    if (p2k_no_savedata_enabled()) {
        info_report("pinball2000: P2K_NO_SAVEDATA set — discarding BAR2 NVRAM changes");
        return;
    }
    void *host = memory_region_get_ram_ptr(s_bar2_mr);
    if (!host) return;
    char tmp[1100];
    snprintf(tmp, sizeof(tmp), "%s.tmp", s_bar2_save_path);
    FILE *fp = fopen(tmp, "wb");
    if (!fp) {
        warn_report("pinball2000: BAR2 save: fopen(%s) failed: %s",
                    tmp, strerror(errno));
        return;
    }
    size_t w = fwrite(host, 1, P2K_BAR2_SRAM_SIZE, fp);
    fclose(fp);
    if (w != P2K_BAR2_SRAM_SIZE) {
        warn_report("pinball2000: BAR2 save: short write %zu/%u",
                    w, P2K_BAR2_SRAM_SIZE);
        unlink(tmp);
        return;
    }
    if (rename(tmp, s_bar2_save_path) != 0) {
        warn_report("pinball2000: BAR2 save: rename failed: %s",
                    strerror(errno));
        unlink(tmp);
        return;
    }
    info_report("pinball2000: BAR2 NVRAM saved to %s (%zu bytes)",
                s_bar2_save_path, w);
}

static Notifier p2k_bar2_exit_notifier = {
    .notify = p2k_bar2_save_cb,
};

void p2k_install_plx_bars(Pinball2000MachineState *s)
{
    static bool no_savedata_reported;
    MemoryRegion *sm = get_system_memory();

    if (p2k_no_savedata_enabled() && !no_savedata_reported) {
        info_report("pinball2000: P2K_NO_SAVEDATA set — running in read-only savedata mode (no seed, no save)");
        no_savedata_reported = true;
    } else if (p2k_fresh_savedata_enabled() && !no_savedata_reported) {
        info_report("pinball2000: P2K_FRESH_SAVEDATA set — ignoring saved "
                    "device seeds and saving new state on exit");
        no_savedata_reported = true;
    }

    /* Phase-3 sentinel container covers the whole 16 MiB BAR2 window
     * and returns 0xFFFFFFFF for every address.  Lower-priority. */
    MemoryRegion *sentinel = g_new(MemoryRegion, 1);
    memory_region_init_io(sentinel, NULL, &p2k_bar2_sentinel_ops, NULL,
                          "p2k.bar2-sentinel", P2K_BAR2_WINDOW_SIZE);
    memory_region_add_subregion_overlap(sm, P2K_BAR2_BASE, sentinel, 0);

    /* Real 192 KiB SRAM overlaid on top at offset 0 of the window. */
    MemoryRegion *bar2 = g_new(MemoryRegion, 1);
    memory_region_init_ram(bar2, NULL, "p2k.bar2-sram",
                           P2K_BAR2_SRAM_SIZE, &error_abort);
    memory_region_add_subregion_overlap(sm, P2K_BAR2_BASE, bar2, 1);
    info_report("pinball2000: mapped %-20s @ 0x%08x (%u KiB SRAM + 16 MiB sentinel)",
                "p2k.bar2-sram", P2K_BAR2_BASE, P2K_BAR2_SRAM_SIZE >> 10);

    s_bar2_mr = bar2;
    p2k_seed_bar2_from_nvram(bar2, s);
    qemu_add_exit_notifier(&p2k_bar2_exit_notifier);

    /* BAR3 (update flash) is loaded by p2k_install_bar3_flash() */
    /* BAR4 (DCS audio) is now an MMIO state-machine in p2k-dcs.c */
}
