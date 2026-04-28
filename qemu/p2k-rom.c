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

static int p2k_load_chip_sized(uint8_t *bank, int which_chip,
                               const char *roms_dir, const char *game,
                               int chipnum, size_t bank_size)
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
        return -1;
    }

    uint8_t pair[2];
    uint8_t *ptr = bank + which_chip * 2;
    size_t total = 0;
    while (fread(pair, 1, 2, fp) == 2) {
        if ((size_t)(ptr - bank) + 2 > bank_size) break;
        ptr[0] = pair[0];
        ptr[1] = pair[1];
        ptr += 4;
        total += 2;
    }
    fclose(fp);
    if (total == 0) {
        return -1;
    }
    info_report("pinball2000: loaded %s (%zu bytes -> chip%d 16-bit lane)",
                path, total, which_chip);
    return 0;
}

static int p2k_load_chip(uint8_t *bank, int which_chip,
                         const char *roms_dir, const char *game, int chipnum)
{
    int rc = p2k_load_chip_sized(bank, which_chip, roms_dir, game, chipnum,
                                 P2K_BANK_SIZE);
    if (rc < 0) {
        error_report("pinball2000: missing ROM chip %s_u%d.{rom,bin} in %s",
                     game, chipnum, roms_dir);
    }
    return rc;
}

int p2k_load_bank0(Pinball2000MachineState *s)
{
    s->bank0 = g_malloc0(P2K_BANK_SIZE);
    if (p2k_load_chip(s->bank0, 0, s->roms_dir, s->game, 100) < 0) return -1;
    if (p2k_load_chip(s->bank0, 1, s->roms_dir, s->game, 101) < 0) return -1;
    return 0;
}

/* Bank N = chips u(100+2N), u(101+2N) interleaved.  Best-effort:
 * if either chip is missing, leave the bank pointer NULL (matches
 * unicorn.old/src/rom.c:load_raw_rom_banks). */
static uint8_t *p2k_try_load_bank(const char *roms_dir, const char *game,
                                  int chip_a, int chip_b, int bank_idx)
{
    uint8_t *bank = g_malloc0(P2K_BANK_SIZE);
    if (p2k_load_chip(bank, 0, roms_dir, game, chip_a) < 0 ||
        p2k_load_chip(bank, 1, roms_dir, game, chip_b) < 0) {
        info_report("pinball2000: bank%d (chips u%d/u%d) absent — skipping",
                    bank_idx, chip_a, chip_b);
        g_free(bank);
        return NULL;
    }
    return bank;
}

void p2k_load_extra_banks(Pinball2000MachineState *s)
{
    s->bank1 = p2k_try_load_bank(s->roms_dir, s->game, 102, 103, 1);
    s->bank2 = p2k_try_load_bank(s->roms_dir, s->game, 104, 105, 2);
    s->bank3 = p2k_try_load_bank(s->roms_dir, s->game, 106, 107, 3);
}

void p2k_load_dcs_rom(Pinball2000MachineState *s)
{
    /* DCS sound ROM = chips u109 + u110 interleaved into 8 MiB. */
    s->dcs_rom = g_malloc0(P2K_DCS_BANK_SIZE);
    if (p2k_load_chip_sized(s->dcs_rom, 0, s->roms_dir, s->game, 109,
                            P2K_DCS_BANK_SIZE) < 0 ||
        p2k_load_chip_sized(s->dcs_rom, 1, s->roms_dir, s->game, 110,
                            P2K_DCS_BANK_SIZE) < 0) {
        info_report("pinball2000: DCS sound ROM (u109/u110) absent — silent");
        g_free(s->dcs_rom);
        s->dcs_rom = NULL;
    }
}
