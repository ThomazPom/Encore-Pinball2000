/*
 * pinball2000 ROM loader.
 *
 * Bank 0 is two physical 27C040-class chips byte-interleaved on the PLX
 * ROM window: chip u100 supplies even-offset bytes, chip u101 supplies odd
 * bytes.  We rebuild the linear 1 MiB image here so later code can treat
 * bank0 as a flat array.
 *
 * Files are looked up under <roms-dir>/<game>_u<NN>.{rom,bin}.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"

#include "p2k-internal.h"

static int p2k_load_chip(uint8_t *bank, int which_byte,
                         const char *roms_dir, const char *game, int chipnum)
{
    char path[512];
    static const char *suffixes[] = { ".rom", ".bin", NULL };
    FILE *fp = NULL;

    for (int i = 0; suffixes[i] && !fp; i++) {
        snprintf(path, sizeof(path), "%s/%s_u%d%s",
                 roms_dir, game, chipnum, suffixes[i]);
        fp = fopen(path, "rb");
    }
    if (!fp) {
        error_report("pinball2000: missing ROM chip %s_u%d.{rom,bin} in %s",
                     game, chipnum, roms_dir);
        return -1;
    }

    /* Each chip is at most 512 KiB; interleave into the 1 MiB bank. */
    uint8_t buf[512 * 1024];
    size_t n = fread(buf, 1, sizeof(buf), fp);
    fclose(fp);
    if (n == 0) {
        error_report("pinball2000: empty ROM chip %s", path);
        return -1;
    }
    for (size_t i = 0; i < n; i++) {
        bank[2 * i + which_byte] = buf[i];
    }
    info_report("pinball2000: loaded %s (%zu bytes -> bank0 %s)",
                path, n, which_byte ? "odd" : "even");
    return 0;
}

int p2k_load_bank0(Pinball2000MachineState *s)
{
    s->bank0 = g_malloc0(P2K_BANK_SIZE);
    if (p2k_load_chip(s->bank0, 0, s->roms_dir, s->game, 100) < 0) return -1;
    if (p2k_load_chip(s->bank0, 1, s->roms_dir, s->game, 101) < 0) return -1;
    return 0;
}
