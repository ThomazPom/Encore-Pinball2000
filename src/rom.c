/*
 * rom.c — ROM loading, auto-detection, and savedata persistence.
 *
 * ROM-agnostic: scans roms_dir for *_bank0.bin files, reads game ID
 * at offset 0x803C (50069=SWE1, 50070=RFM) to auto-detect game.
 *
 * Savedata: loads/saves .flash, .nvram2, .see, .ems between sessions.
 */
#include "encore.h"
#include <dirent.h>

/* ===== File helpers ===== */

static uint8_t *load_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0) { fclose(f); return NULL; }

    uint8_t *buf = malloc(sz);
    if (!buf) { fclose(f); return NULL; }

    size_t rd = fread(buf, 1, sz, f);
    fclose(f);

    if (rd != (size_t)sz) { free(buf); return NULL; }
    *out_size = (size_t)sz;
    return buf;
}

static int save_file(const char *path, const void *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t wr = fwrite(data, 1, size, f);
    fclose(f);
    return (wr == size) ? 0 : -1;
}

/* ===== ROM Auto-Detection ===== */

#define GAME_ID_OFFSET  0x0000803Cu
#define GAME_ID_SWE1    50069u
#define GAME_ID_RFM     50070u

int rom_detect_game(void)
{
    /* If user specified --game, use that — but still need game_id and game_id_str */
    if (g_emu.game_prefix[0] && strcmp(g_emu.game_prefix, "auto") != 0) {
        LOG("rom", "Game override: %s\n", g_emu.game_prefix);
        /* Still fall through to read game_id from bank0 and detect game_id_str */
    } else {

    /* Scan roms_dir for *_bank0.bin files */
    DIR *d = opendir(g_emu.roms_dir);
    if (!d) {
        fprintf(stderr, "[rom] Cannot open ROM dir: %s\n", g_emu.roms_dir);
        return -1;
    }

    char found_prefix[16] = {0};
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        char *p = strstr(ent->d_name, "_bank0.bin");
        if (p) {
            size_t len = p - ent->d_name;
            if (len > 0 && len < sizeof(found_prefix)) {
                strncpy(found_prefix, ent->d_name, len);
                found_prefix[len] = '\0';
                break;
            }
        }
    }
    closedir(d);

    if (!found_prefix[0]) {
        fprintf(stderr, "[rom] No *_bank0.bin found in %s\n", g_emu.roms_dir);
        return -1;
    }

    strncpy(g_emu.game_prefix, found_prefix, sizeof(g_emu.game_prefix) - 1);
    LOG("rom", "Auto-detected ROM prefix: %s\n", g_emu.game_prefix);

    } /* end of auto-detect block */

    /* Load bank0 temporarily to read game ID */
    char path[512];
    snprintf(path, sizeof(path), "%s/%s_bank0.bin", g_emu.roms_dir, g_emu.game_prefix);
    FILE *f = fopen(path, "rb");
    if (f) {
        uint32_t gid = 0;
        fseek(f, GAME_ID_OFFSET, SEEK_SET);
        if (fread(&gid, 4, 1, f) == 1) {
            g_emu.game_id = gid;
            if (gid == GAME_ID_SWE1)
                LOG("rom", "Game ID: %u → Star Wars Episode I\n", gid);
            else if (gid == GAME_ID_RFM)
                LOG("rom", "Game ID: %u → Revenge From Mars\n", gid);
            else
                LOG("rom", "Game ID: %u (unknown game)\n", gid);
        }
        fclose(f);
    }

    /* Build game_id_str for savedata naming (e.g. "swe1_14", "rfm_15").
     * Scan savedata dir for files matching prefix to get version suffix. */
    {
        bool found_id = false;
        DIR *sd = opendir(g_emu.savedata_dir);
        if (sd) {
            struct dirent *se;
            while ((se = readdir(sd)) != NULL) {
                /* Look for {prefix}_*.nvram2 or {prefix}_*.see */
                size_t plen = strlen(g_emu.game_prefix);
                if (strncmp(se->d_name, g_emu.game_prefix, plen) == 0 &&
                    se->d_name[plen] == '_') {
                    char *dot = strchr(se->d_name + plen, '.');
                    if (dot) {
                        size_t idlen = dot - se->d_name;
                        if (idlen < sizeof(g_emu.game_id_str)) {
                            strncpy(g_emu.game_id_str, se->d_name, idlen);
                            g_emu.game_id_str[idlen] = '\0';
                            found_id = true;
                            break;
                        }
                    }
                }
            }
            closedir(sd);
        }
        if (!found_id)
            strncpy(g_emu.game_id_str, g_emu.game_prefix, sizeof(g_emu.game_id_str) - 1);
    }
    LOG("rom", "Save data ID: %s\n", g_emu.game_id_str);

    return 0;
}

/* Forward declarations */
static void rom_load_update_flash(void);

/* ===== ROM Loading ===== */

int rom_load_all(void)
{
    char path[512];

    /* Auto-detect game if needed */
    if (rom_detect_game() != 0) return -1;

    /* Load ROM banks 0-3 */
    for (int b = 0; b < 4; b++) {
        snprintf(path, sizeof(path), "%s/%s_bank%d.bin",
                 g_emu.roms_dir, g_emu.game_prefix, b);
        g_emu.rom_banks[b] = load_file(path, &g_emu.rom_sizes[b]);
        if (g_emu.rom_banks[b]) {
            LOG("rom", "Bank %d: %s (%lu MB)\n", b, path,
                (unsigned long)(g_emu.rom_sizes[b] >> 20));
        } else {
            LOG("rom", "Bank %d: %s — NOT FOUND\n", b, path);
            if (b == 0) {
                fprintf(stderr, "[rom] Bank 0 is required!\n");
                return -1;
            }
        }
    }

    /* Load DCS sound ROM (bank4) */
    snprintf(path, sizeof(path), "%s/%s_bank4_dcs.bin",
             g_emu.roms_dir, g_emu.game_prefix);
    g_emu.dcs_rom = load_file(path, &g_emu.dcs_rom_size);
    if (g_emu.dcs_rom) {
        LOG("rom", "DCS  : %s (%lu MB)\n", path,
            (unsigned long)(g_emu.dcs_rom_size >> 20));
    } else {
        LOG("rom", "DCS  : %s — not found (sound disabled)\n", path);
    }

    /* Load BIOS — try multiple paths */
    const char *bios_paths[] = {
        "../emulator/P2K-runtime/roms/bios.bin",
        "emulator/P2K-runtime/roms/bios.bin",
        NULL
    };
    /* Build relative path from roms_dir */
    snprintf(path, sizeof(path), "%s/../emulator/P2K-runtime/roms/bios.bin", g_emu.roms_dir);
    g_emu.bios = load_file(path, &g_emu.bios_size);
    for (int i = 0; !g_emu.bios && bios_paths[i]; i++)
        g_emu.bios = load_file(bios_paths[i], &g_emu.bios_size);

    if (g_emu.bios) {
        LOG("rom", "BIOS : loaded (%lu KB)\n", (unsigned long)(g_emu.bios_size >> 10));
    } else {
        fprintf(stderr, "[rom] BIOS not found!\n");
        return -1;
    }

    /* Verify PRISM option ROM signature */
    if (g_emu.rom_banks[0] && g_emu.rom_sizes[0] >= 3) {
        uint8_t *r = g_emu.rom_banks[0];
        if (r[0] == 0x55 && r[1] == 0xAA)
            LOG("rom", "PRISM option ROM: 55 AA, size=%d×512=%d bytes\n", r[2], r[2]*512);
    }

    /* Allocate update flash (4MB, all 0xFF = erased) */
    g_emu.flash = malloc(FLASH_SIZE);
    if (g_emu.flash)
        memset(g_emu.flash, 0xFF, FLASH_SIZE);

    /* SEEPROM defaults set by seeprom_init_default() in bar_init() */

    /* Clear EMS */
    memset(g_emu.ems, 0, sizeof(g_emu.ems));

    /* Load savedata if available */
    savedata_load();

    /* Load update ROM files into flash (BAR3).
     * The .flash savedata is usually erased (all 0xFF at the beginning).
     * For the PRISM option ROM to boot, flash must contain:
     *   bootdata.rom + im_flsh0.rom + game.rom + symbols.rom
     * concatenated at flash offset 0. This replicates what the x64 POC does. */
    if (g_emu.flash) {
        rom_load_update_flash();
    }

    fflush(stdout);
    return 0;
}

/* ===== Update Flash Loading ===== */

/*
 * Load V1.x update ROM files into flash (BAR3 at 0x12000000).
 * The option ROM (Init2) checks flash for a valid update. If the flash bootdata
 * at offset 0 starts with a valid date string and offset 0x48 contains a game entry
 * point, Init2 boots from flash. Otherwise it falls through to the no-update path
 * which hangs.
 *
 * Update file search order:
 *   1. Pre-concatenated update.bin (simplest)
 *   2. Individual ROM files: bootdata + im_flsh0 + game + symbols (concatenated)
 *
 * Search directories:
 *   - ../emulator/P2K-runtime/update/{game_id_str}/
 *   - {roms_dir}/ for {prefix}_upd_{game_id_str}_update.bin
 */
static void rom_load_update_flash(void)
{
    char path[512];
    const char *id = g_emu.game_id_str;  /* e.g. "rfm_15" or "swe1_14" */
    uint32_t game_num = 0;

    /* Determine game number for filename matching */
    if (g_emu.game_id == GAME_ID_RFM) game_num = 50070;
    else if (g_emu.game_id == GAME_ID_SWE1) game_num = 50069;

    /* Check if flash already has valid data (from .flash savedata) */
    if (g_emu.flash[0] != 0xFF || g_emu.flash[1] != 0xFF) {
        /* Check if offset 0x48 has a valid game entry point */
        uint32_t entry = *(uint32_t *)(g_emu.flash + 0x48);
        if (entry != 0xFFFFFFFF && entry >= 0x100000 && entry < 0x1000000) {
            LOG("flash", "Flash already has valid data (entry=0x%08x), skipping update load\n", entry);
            return;
        }
    }

    /* Strategy 1: Try pre-concatenated update.bin */
    {
        const char *upd_dirs[] = {
            "../emulator/P2K-runtime/update",
            "emulator/P2K-runtime/update",
            NULL
        };

        for (int di = 0; upd_dirs[di]; di++) {
            snprintf(path, sizeof(path), "%s/%s/%s_update.bin", upd_dirs[di], id, id);
            size_t sz = 0;
            uint8_t *data = load_file(path, &sz);
            if (data && sz > 0x50 && sz <= FLASH_SIZE) {
                memcpy(g_emu.flash, data, sz);
                uint32_t entry = *(uint32_t *)(g_emu.flash + 0x48);
                LOG("flash", "Update loaded: %s (%lu KB, entry=0x%08x)\n",
                    path, (unsigned long)(sz >> 10), entry);
                free(data);
                return;
            }
            free(data);
        }

        /* Also try {prefix}_upd_{id}_update.bin in roms_dir */
        snprintf(path, sizeof(path), "%s/%s_upd_%s_update.bin",
                 g_emu.roms_dir, g_emu.game_prefix, id);
        size_t sz = 0;
        uint8_t *data = load_file(path, &sz);
        if (data && sz > 0x50 && sz <= FLASH_SIZE) {
            memcpy(g_emu.flash, data, sz);
            uint32_t entry = *(uint32_t *)(g_emu.flash + 0x48);
            LOG("flash", "Update loaded: %s (%lu KB, entry=0x%08x)\n",
                path, (unsigned long)(sz >> 10), entry);
            free(data);
            return;
        }
        free(data);
    }

    /* Strategy 2: Concatenate individual ROM files (bootdata + im_flsh0 + game + symbols).
     * Try multiple version numbers (newest first) */
    if (game_num > 0) {
        const char *upd_dirs[] = {
            "../emulator/P2K-runtime/update",
            "emulator/P2K-runtime/update",
            NULL
        };
        /* Version numbers to try, newest first */
        const char *versions[] = { "0180", "0160", "0150", "0120", NULL };
        const char *rom_suffixes[] = { "bootdata", "im_flsh0", "game", "symbols", NULL };

        for (int di = 0; upd_dirs[di]; di++) {
            for (int vi = 0; versions[vi]; vi++) {
                /* Check if bootdata exists for this version */
                snprintf(path, sizeof(path), "%s/%s/pin2000_%u_%s_bootdata.rom",
                         upd_dirs[di], id, game_num, versions[vi]);
                FILE *test = fopen(path, "rb");
                if (!test) continue;
                fclose(test);

                /* Found a version — concatenate all ROM files */
                uint32_t flash_off = 0;
                int loaded = 0;
                for (int ri = 0; rom_suffixes[ri]; ri++) {
                    snprintf(path, sizeof(path), "%s/%s/pin2000_%u_%s_%s.rom",
                             upd_dirs[di], id, game_num, versions[vi], rom_suffixes[ri]);
                    size_t sz = 0;
                    uint8_t *data = load_file(path, &sz);
                    if (data && flash_off + sz <= FLASH_SIZE) {
                        memcpy(g_emu.flash + flash_off, data, sz);
                        LOG("flash", "  %s: %lu bytes at flash+0x%x\n",
                            rom_suffixes[ri], (unsigned long)sz, flash_off);
                        flash_off += (uint32_t)sz;
                        loaded++;
                    } else if (!data) {
                        LOG("flash", "  %s: NOT FOUND (%s)\n", rom_suffixes[ri], path);
                    }
                    free(data);
                }

                if (loaded >= 2) { /* At least bootdata + game */
                    uint32_t entry = *(uint32_t *)(g_emu.flash + 0x48);
                    LOG("flash", "Update concatenated: v%s, %u bytes total, entry=0x%08x\n",
                        versions[vi], flash_off, entry);
                    return;
                }
                /* Reset flash if not enough files */
                memset(g_emu.flash, 0xFF, FLASH_SIZE);
            }
        }
    }

    LOG("flash", "WARNING: No update data found — Init2 may hang at 'NO UPDATE' path\n");
    LOG("flash", "  Expected: ../emulator/P2K-runtime/update/%s/{id}_update.bin\n", id);
    LOG("flash", "  Or individual ROM files: bootdata + im_flsh0 + game + symbols\n");
}

/* ===== Savedata Persistence ===== */

int savedata_load(void)
{
    char path[512];
    const char *sd = g_emu.savedata_dir;
    const char *id = g_emu.game_id_str;

    if (!sd[0] || !id[0]) return -1;

    /* Create savedata dir if needed */
    mkdir(sd, 0755);

    /* 1. Flash (4MB) — load from .flash.bak (pristine) or .flash */
    if (g_emu.flash) {
        size_t sz = 0;
        uint8_t *data = NULL;

        snprintf(path, sizeof(path), "%s/%s.flash.bak", sd, id);
        data = load_file(path, &sz);
        if (!data) {
            snprintf(path, sizeof(path), "%s/%s.flash", sd, id);
            data = load_file(path, &sz);
        }
        if (data && sz <= FLASH_SIZE) {
            memcpy(g_emu.flash, data, sz);
            LOG("save", "Flash loaded: %s (%lu bytes)\n", path, (unsigned long)sz);
        }
        free(data);
    }

    /* 2. NVRAM (128KB) — prefer .nvram2, fallback .nvram1, .nvram */
    {
        const char *nvram_exts[] = { ".nvram2", ".nvram1", ".nvram", NULL };
        for (int i = 0; nvram_exts[i]; i++) {
            snprintf(path, sizeof(path), "%s/%s%s", sd, id, nvram_exts[i]);
            size_t sz = 0;
            uint8_t *data = load_file(path, &sz);
            if (data) {
                size_t copy_sz = (sz > BAR2_SIZE) ? BAR2_SIZE : sz;
                memcpy(g_emu.bar2_sram, data, copy_sz);
                free(data);

                /* Verify checksum if file has trailer */
                if (sz >= BAR2_SIZE + 6) {
                    /* Checksum is last 2 bytes: sum of first 0x20000 data bytes */
                    LOG("save", "NVRAM loaded: %s (%lu bytes, has checksum trailer)\n",
                        path, (unsigned long)sz);
                } else {
                    LOG("save", "NVRAM loaded: %s (%lu bytes)\n", path, (unsigned long)sz);
                }
                break;
            }
        }
    }

    /* 3. SEEPROM (128 bytes = 64 × uint16) */
    snprintf(path, sizeof(path), "%s/%s.see", sd, id);
    {
        size_t sz = 0;
        uint8_t *data = load_file(path, &sz);
        if (data && sz >= 128) {
            memcpy(g_emu.seeprom, data, 128);
            LOG("save", "SEEPROM loaded: %s\n", path);
        }
        free(data);
    }

    /* 4. EMS (16 bytes = 4 × uint32) */
    snprintf(path, sizeof(path), "%s/%s.ems", sd, id);
    {
        size_t sz = 0;
        uint8_t *data = load_file(path, &sz);
        if (data && sz >= 16) {
            memcpy(g_emu.ems, data, 16);
            LOG("save", "EMS loaded: %s\n", path);
        }
        free(data);
    }

    fflush(stdout);
    return 0;
}

int savedata_save(void)
{
    char path[512];
    const char *sd = g_emu.savedata_dir;
    const char *id = g_emu.game_id_str;

    if (!sd[0] || !id[0]) return -1;

    mkdir(sd, 0755);

    /* 1. SEEPROM — always save */
    snprintf(path, sizeof(path), "%s/%s.see", sd, id);
    if (save_file(path, g_emu.seeprom, 128) == 0)
        LOG("save", "SEEPROM saved: %s\n", path);

    /* 2. EMS — always save */
    snprintf(path, sizeof(path), "%s/%s.ems", sd, id);
    if (save_file(path, g_emu.ems, 16) == 0)
        LOG("save", "EMS saved: %s\n", path);

    /* 3. NVRAM — save with checksum trailer (format: 128KB data + 4B pad + 2B checksum) */
    {
        size_t total = BAR2_SIZE + 6;
        uint8_t *buf = calloc(1, total);
        if (buf) {
            /* Read current NVRAM from guest memory if UC is available */
            if (g_emu.uc)
                uc_mem_read(g_emu.uc, WMS_BAR2, buf, BAR2_SIZE);
            else
                memcpy(buf, g_emu.bar2_sram, BAR2_SIZE);

            /* Compute checksum: sum of all 128K data bytes */
            uint16_t cksum = 0;
            for (size_t i = 0; i < BAR2_SIZE; i++)
                cksum += buf[i];
            buf[BAR2_SIZE + 4] = cksum & 0xFF;
            buf[BAR2_SIZE + 5] = (cksum >> 8) & 0xFF;

            snprintf(path, sizeof(path), "%s/%s.nvram2", sd, id);
            if (save_file(path, buf, total) == 0)
                LOG("save", "NVRAM saved: %s (%lu bytes, cksum=0x%04x)\n",
                    path, (unsigned long)total, cksum);
            free(buf);
        }
    }

    /* 4. Flash — save if modified (check if not all 0xFF) */
    if (g_emu.flash) {
        bool has_data = false;
        for (uint32_t i = 0; i < FLASH_SIZE && !has_data; i += 4096)
            if (g_emu.flash[i] != 0xFF) has_data = true;

        if (has_data) {
            snprintf(path, sizeof(path), "%s/%s.flash", sd, id);
            if (save_file(path, g_emu.flash, FLASH_SIZE) == 0)
                LOG("save", "Flash saved: %s\n", path);
        }
    }

    fflush(stdout);
    return 0;
}
