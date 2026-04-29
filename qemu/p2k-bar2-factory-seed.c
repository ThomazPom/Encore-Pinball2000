/*
 * p2k-bar2-factory-seed.c — STRICTLY GATED BAR2 SRAM (NVRAM2) seeder
 *                          for --update none / P2K_NO_AUTO_UPDATE.
 *
 * Goal of this file: when the user explicitly opts out of the update
 * bundle (`--update none`), we run on the pure BASE v0.40 ROM path.
 * In that mode there is no calibrated NVRAM in BAR2 SRAM, so XINU
 * always enters "Automatic Factory Reset underway" and stays there
 * (the cabinet only emits the recovery chime S03CE).
 *
 * On a real Williams cabinet, BAR2 SRAM is battery-backed: it holds
 * post-factory-calibration values (audit counters, settings table,
 * SuperIOType, audio settings, coin meters, etc.) so subsequent boots
 * skip Factory Reset and proceed to attract / game.
 *
 * Our regular savedata/<game>.nvram2 is loaded by p2k-bars.c
 * (p2k_seed_bar2_from_nvram), but in practice it is essentially empty
 * (only [0x4]=1, which p2k-vsync.c re-pokes every frame anyway), so it
 * does NOT lift the guest out of Factory Reset. This file lets a user
 * drop a separate "post-Factory-Reset" capture next to the regular
 * savedata under savedata/<game>.factory.nvram2 and have it overlaid
 * on top of the zero/empty BAR2 SRAM strictly when --update none is in
 * effect.
 *
 * Strict gating — by design:
 *
 *   - Activates ONLY when env P2K_NO_AUTO_UPDATE is set
 *     (scripts/run-qemu.sh exports it for `--update none`).
 *   - Silent / inert on normal update boots: no read, no overlay, no
 *     log noise.
 *   - Strictly opt-in even within --update none: if no
 *     savedata/<game>.factory.nvram2 file is present, we just log
 *     "no factory seed available" and do NOTHING. No synthetic field
 *     guesses, no pattern poking, no guest .text writes.
 *   - One-line install banner explicitly says "compatibility seed"
 *     so it cannot be confused with clean device behavior.
 *
 * Why a separate file from p2k-bars.c:
 *   - p2k-bars.c is the real-device BAR2 SRAM mapper and must remain
 *     update-path-safe. We do not want any --update-none-only
 *     compatibility behavior bleeding into the normal-boot codepath.
 *   - This is the same containment style as p2k-probe-cell-shim.c.
 *
 * Format of savedata/<game>.factory.nvram2:
 *   - First 128 KiB (0x20000 bytes) = BAR2 SRAM contents.
 *   - Trailing bytes (the 6-byte pad+checksum trailer that
 *     unicorn.old/src/rom.c:928-952 writes) are tolerated and ignored.
 *   - Anything shorter than 128 KiB is also accepted (we copy what we
 *     have and leave the tail at whatever p2k-bars.c initialised it to).
 *
 * Removal condition:
 *   - Drop this file once XINU on --update none either:
 *       (a) reaches a non-Factory-Reset state without any pre-seeded
 *           BAR2 SRAM (i.e. the BASE v0.40 ROM is genuinely happy), or
 *       (b) we model whatever device/RTC/SEEPROM read is actually used
 *           to decide "skip Factory Reset" so that no NVRAM seed is
 *           required.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "exec/cpu-common.h"
#include <glib.h>

#include "p2k-internal.h"
#include "pinball2000.h"

/* Defined in pinball2000.h: P2K_BAR2_SIZE = 0x40000 (256 KiB window
 * documentation), but the actual SRAM mapped by p2k-bars.c is the lower
 * 128 KiB window (P2K_BAR2_SRAM_SIZE in p2k-bars.c). Mirror that here
 * so we never overrun the real RAM region. */
#define P2K_BAR2_FACTORY_SRAM_BASE  0x11000000u
#define P2K_BAR2_FACTORY_SRAM_SIZE  0x00020000u   /* 128 KiB */

/* Try to open the factory NVRAM file in this priority order:
 *   1. $P2K_FACTORY_NVRAM (explicit absolute / relative path)
 *   2. savedata/<game>.factory.nvram2 (matches normal --update path cwd)
 *   3. <roms_dir>/../savedata/<game>.factory.nvram2 (works under
 *      --no-savedata where cwd is /tmp/...)
 * Returns the FILE* and writes the resolved path into out_path. */
static FILE *open_factory_nvram(const char *roms_dir, const char *game,
                                char *out_path, size_t out_path_sz)
{
    const char *env = getenv("P2K_FACTORY_NVRAM");
    if (env && *env) {
        FILE *fp = fopen(env, "rb");
        if (fp) {
            g_strlcpy(out_path, env, out_path_sz);
            return fp;
        }
    }

    snprintf(out_path, out_path_sz, "savedata/%s.factory.nvram2", game);
    FILE *fp = fopen(out_path, "rb");
    if (fp) return fp;

    if (roms_dir && *roms_dir) {
        char *parent = g_path_get_dirname(roms_dir);
        snprintf(out_path, out_path_sz,
                 "%s/savedata/%s.factory.nvram2", parent, game);
        g_free(parent);
        fp = fopen(out_path, "rb");
        if (fp) return fp;
    }

    return NULL;
}

void p2k_install_bar2_factory_seed(Pinball2000MachineState *s)
{
    /* Hard gate: only run under --update none. Auto-update path is
     * untouched and stays clean. */
    if (getenv("P2K_NO_AUTO_UPDATE") == NULL) {
        return;
    }

    const char *game = s->game ? s->game : "swe1";
    char path[1024] = {0};
    FILE *fp = open_factory_nvram(s->roms_dir, game, path, sizeof(path));
    if (!fp) {
        info_report("p2k-bar2-factory-seed: --update none active but no "
                    "factory NVRAM found "
                    "($P2K_FACTORY_NVRAM, savedata/%s.factory.nvram2, "
                    "<roms>/../savedata/%s.factory.nvram2) — BAR2 SRAM "
                    "left as-is (XINU will run Automatic Factory Reset). "
                    "[gated compatibility seed, NOT clean device behavior]",
                    game, game);
        return;
    }

    uint8_t buf[P2K_BAR2_FACTORY_SRAM_SIZE];
    memset(buf, 0, sizeof(buf));
    size_t n = fread(buf, 1, sizeof(buf), fp);
    fclose(fp);

    if (n == 0) {
        warn_report("p2k-bar2-factory-seed: %s is empty; no overlay "
                    "applied", path);
        return;
    }

    cpu_physical_memory_write(P2K_BAR2_FACTORY_SRAM_BASE, buf, n);

    info_report("p2k-bar2-factory-seed: overlaid %zu bytes from %s onto "
                "BAR2 SRAM @ 0x%08x [gated --update none compatibility "
                "seed; auto-update boots are unaffected]",
                n, path, P2K_BAR2_FACTORY_SRAM_BASE);
}
