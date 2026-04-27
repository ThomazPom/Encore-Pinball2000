/*
 * pinball2000 ROM loader.
 *
 * Bank 0 = two physical 8 MiB chips (u100, u101) interleaved as 16-bit pairs
 * stepped by 4 bytes (so the linear bank is 16 MiB).  Per unicorn.old/src/rom.c
 * interleave_file(): chip c writes pair[0..1] at base + c*2, then steps +4.
 *
 * Files are looked up under <roms-dir>/<game>_u<NN>.{rom,bin}.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"

#include "p2k-internal.h"

static int p2k_load_chip(uint8_t *bank, int which_chip,
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

    /* 16-bit-pair interleave per unicorn.old/src/rom.c:interleave_file().
     * For each 2 bytes read from the chip, write them to bank[base+0..1],
     * then step base by 4. */
    uint8_t pair[2];
    uint8_t *ptr = bank + which_chip * 2;
    size_t total = 0;
    while (fread(pair, 1, 2, fp) == 2) {
        if ((size_t)(ptr - bank) + 2 > P2K_BANK_SIZE) break;
        ptr[0] = pair[0];
        ptr[1] = pair[1];
        ptr += 4;
        total += 2;
    }
    fclose(fp);
    if (total == 0) {
        error_report("pinball2000: empty ROM chip %s", path);
        return -1;
    }
    info_report("pinball2000: loaded %s (%zu bytes -> bank0 chip%d 16-bit lane)",
                path, total, which_chip);
    return 0;
}

int p2k_load_bank0(Pinball2000MachineState *s)
{
    s->bank0 = g_malloc0(P2K_BANK_SIZE);
    if (p2k_load_chip(s->bank0, 0, s->roms_dir, s->game, 100) < 0) return -1;
    if (p2k_load_chip(s->bank0, 1, s->roms_dir, s->game, 101) < 0) return -1;
    return 0;
}
