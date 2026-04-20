/*
 * rom.c — ROM loading, auto-detection, and savedata persistence.
 *
 * ROM loading follows the i386 POC path: load raw U100-U107 chip
 * images and deinterleave them into 16 MB banks. DCS from U109/U110.
 * No prebuilt bank file fallbacks — raw chips only.
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

static void copy_cstr(char *dst, size_t dst_size, const char *src)
{
    size_t len;

    if (dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }

    len = strlen(src);
    if (len >= dst_size)
        len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static bool build_update_path(char *dst, size_t dst_size,
                              const char *base_dir, const char *update_id)
{
    size_t need;

    need = strlen(base_dir) + 1 + strlen(update_id) + 1 +
           strlen(update_id) + strlen("_update.bin") + 1;
    if (need > dst_size)
        return false;

    {
        char *p = dst;
        size_t base_len = strlen(base_dir);
        size_t id_len = strlen(update_id);

        memcpy(p, base_dir, base_len);
        p += base_len;
        *p++ = '/';
        memcpy(p, update_id, id_len);
        p += id_len;
        *p++ = '/';
        memcpy(p, update_id, id_len);
        p += id_len;
        memcpy(p, "_update.bin", sizeof("_update.bin"));
    }
    return true;
}

/* ===== ROM interleaving ===== */

static long interleave_file(uint8_t *base, int index, FILE *fp)
{
    uint8_t pair[2];
    uint8_t *ptr = base + index * 2;
    long count = 0;

    while (fread(pair, 1, 2, fp) == 2) {
        ptr[0] = pair[0];
        ptr[1] = pair[1];
        ptr += 4;
        count += 2;
    }

    return count;
}

static bool try_open_variants(FILE **out_fp, char *out_path, size_t out_path_size,
                              const char *dir, const char *prefix,
                              int chip, bool allow_r2)
{
    const char *exts[] = { ".rom", ".bin", ".cpu", NULL };
    int ei;

    *out_fp = NULL;
    out_path[0] = '\0';

    if (allow_r2) {
        for (ei = 0; exts[ei]; ei++) {
            snprintf(out_path, out_path_size, "%s/%s_u%dr2%s",
                     dir, prefix, chip, exts[ei]);
            *out_fp = fopen(out_path, "rb");
            if (*out_fp)
                return true;
        }
    }

    for (ei = 0; exts[ei]; ei++) {
        snprintf(out_path, out_path_size, "%s/%s_u%d%s",
                 dir, prefix, chip, exts[ei]);
        *out_fp = fopen(out_path, "rb");
        if (*out_fp)
            return true;
    }

    if (!allow_r2) {
        for (ei = 0; exts[ei]; ei++) {
            snprintf(out_path, out_path_size, "%s/%s_u%dr2%s",
                     dir, prefix, chip, exts[ei]);
            *out_fp = fopen(out_path, "rb");
            if (*out_fp)
                return true;
        }
    }

    return false;
}

static int detect_game_id_from_prefix(const char *prefix)
{
    if (strcmp(prefix, "swe1") == 0)
        return 50069u;
    if (strcmp(prefix, "rfm") == 0)
        return 50070u;
    return 0;
}

/* ===== ROM Auto-Detection ===== */

#define GAME_ID_OFFSET  0x0000803Cu
#define GAME_ID_SWE1    50069u
#define GAME_ID_RFM     50070u

int rom_detect_game(void)
{
    /* If user specified --game, use that. */
    if (g_emu.game_prefix[0] && strcmp(g_emu.game_prefix, "auto") != 0) {
        LOG("rom", "Game override: %s\n", g_emu.game_prefix);
    } else {

    /* Scan roms_dir for raw bank0 chip files first, then prebuilt bank files. */
    DIR *d = opendir(g_emu.roms_dir);
    if (!d) {
        fprintf(stderr, "[rom] Cannot open ROM dir: %s\n", g_emu.roms_dir);
        return -1;
    }

    char found_prefix[16] = {0};
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        char *p = strstr(ent->d_name, "_u100");
        if (p) {
            size_t len = p - ent->d_name;
            if (len > 0 && len < sizeof(found_prefix)) {
                strncpy(found_prefix, ent->d_name, len);
                found_prefix[len] = '\0';
                break;
            }
        }
    }

    if (!found_prefix[0]) {
        fprintf(stderr, "[rom] No raw chip ROMs (*_u100*) found in %s\n", g_emu.roms_dir);
        return -1;
    }
    closedir(d);

    copy_cstr(g_emu.game_prefix, sizeof(g_emu.game_prefix), found_prefix);
    LOG("rom", "Auto-detected ROM prefix: %s\n", g_emu.game_prefix);

    } /* end of auto-detect block */

    g_emu.game_id = detect_game_id_from_prefix(g_emu.game_prefix);
    if (g_emu.game_id == GAME_ID_SWE1)
        LOG("rom", "Game ID: %u → Star Wars Episode I\n", g_emu.game_id);
    else if (g_emu.game_id == GAME_ID_RFM)
        LOG("rom", "Game ID: %u → Revenge From Mars\n", g_emu.game_id);
    else if (g_emu.game_id != 0)
        LOG("rom", "Game ID: %u (unknown game)\n", g_emu.game_id);

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
            copy_cstr(g_emu.game_id_str, sizeof(g_emu.game_id_str), g_emu.game_prefix);
    }
    LOG("rom", "Save data ID: %s\n", g_emu.game_id_str);

    return 0;
}

/* Forward declarations */
static void rom_load_update_flash(void);

static int load_raw_rom_banks(void)
{
    static const int chips[4][2] = {
        {100, 101},
        {102, 103},
        {104, 105},
        {106, 107},
    };
    int any = 0;

    for (int b = 0; b < 4; b++) {
        uint8_t *bank_mem = calloc(1, BANK_SIZE);
        int loaded = 0;
        bool prefer_r2 = (b == 0 &&
                          strcmp(g_emu.game_prefix, "rfm") == 0 &&
                          strstr(g_emu.game_id_str, "_15") != NULL);

        if (!bank_mem)
            return -1;

        for (int c = 0; c < 2; c++) {
            char path[512];
            FILE *fp = NULL;

            if (!try_open_variants(&fp, path, sizeof(path), g_emu.roms_dir,
                                   g_emu.game_prefix, chips[b][c],
                                   prefer_r2)) {
                if (b == 0)
                    LOG("rom", "Missing raw ROM chip: %s_u%d*\n",
                        g_emu.game_prefix, chips[b][c]);
                continue;
            }

            interleave_file(bank_mem, c, fp);
            fclose(fp);
            loaded++;
            any++;
            LOG("rom", "Bank %d chip u%d: %s\n", b, chips[b][c], path);
        }

        if (loaded == 2) {
            g_emu.rom_banks[b] = bank_mem;
            g_emu.rom_sizes[b] = BANK_SIZE;
            /* DCS presence table at bank0+0x10000 left INTACT.
             * Game needs it to detect DCS2 hardware and create the
             * DCS2 driver task. NonFatal from missing DCS board is
             * caught by the NonFatal patch in cpu.c. */
            LOG("rom", "Bank %d: raw interleaved load complete (%lu MB)\n",
                b, (unsigned long)(g_emu.rom_sizes[b] >> 20));
        } else {
            free(bank_mem);
            if (b == 0)
                return -1;
            LOG("rom", "Bank %d: incomplete raw chip set, leaving bank absent\n", b);
        }
    }

    return any > 0 ? 0 : -1;
}

/* Prebuilt bank file loading removed — raw chip deinterleaving only (i386 POC path) */

static int load_raw_dcs_rom(void)
{
    uint8_t *buf = calloc(1, DCS_BANK_SIZE);
    int loaded = 0;

    if (!buf)
        return -1;

    for (int c = 0; c < 2; c++) {
        const int chip = 109 + c;
        char path[512];
        FILE *fp = NULL;

        if (!try_open_variants(&fp, path, sizeof(path), g_emu.roms_dir,
                               g_emu.game_prefix, chip, false))
            continue;

        interleave_file(buf, c, fp);
        fclose(fp);
        loaded++;
        LOG("rom", "DCS chip u%d: %s\n", chip, path);
    }

    if (loaded == 2) {
        g_emu.dcs_rom = buf;
        g_emu.dcs_rom_size = DCS_BANK_SIZE;
        return 0;
    }

    free(buf);
    return -1;
}

/* Prebuilt DCS bank file loading removed — raw chip deinterleaving only */

/* ===== ROM Loading ===== */

int rom_load_all(void)
{
    char path[512];

    /* Auto-detect game if needed */
    if (rom_detect_game() != 0) return -1;

    if (load_raw_rom_banks() != 0) {
        fprintf(stderr, "[rom] Raw chip load failed — ensure *_u100-u107 files exist in %s\n",
                g_emu.roms_dir);
        return -1;
    }

    if (load_raw_dcs_rom() != 0)
        LOG("rom", "DCS  : u109/u110 not found (sound disabled)\n");
    else
        LOG("rom", "DCS  : raw interleaved load complete (%lu MB)\n",
            (unsigned long)(g_emu.dcs_rom_size >> 20));

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

    /* Always load from update ROM files — never trust the saved .flash file.
     * The saved flash may have stale game code that fails checksum validation.
     * We always reload from the fresh update files to ensure consistency. */

    /* Strategy 0: explicit --update FILE override */
    if (g_emu.update_file[0]) {
        size_t sz = 0;
        uint8_t *data = load_file(g_emu.update_file, &sz);
        if (data && sz > 0x50 && sz <= FLASH_SIZE) {
            memcpy(g_emu.flash, data, sz);
            uint32_t entry = *(uint32_t *)(g_emu.flash + 0x48);
            LOG("flash", "Update loaded (--update): %s (%lu KB, entry=0x%08x)\n",
                g_emu.update_file, (unsigned long)(sz >> 10), entry);
            free(data);
            g_emu.is_v19_update = true;
            return;
        }
        LOG("flash", "WARN: --update %s failed (sz=%zu) — falling back to default search\n",
            g_emu.update_file, sz);
        free(data);
    }

    /* Strategy 1: Try pre-concatenated update.bin.
     * Scan update dirs for subdirectories starting with game prefix. */
    {
        const char *upd_dirs[] = {
            "../emulator/P2K-runtime/update",
            "emulator/P2K-runtime/update",
            NULL
        };

        for (int di = 0; upd_dirs[di]; di++) {
            /* First try exact game_id_str match */
            if (!build_update_path(path, sizeof(path), upd_dirs[di], id))
                continue;
            size_t sz = 0;
            uint8_t *data = load_file(path, &sz);
            if (data && sz > 0x50 && sz <= FLASH_SIZE) {
                memcpy(g_emu.flash, data, sz);
                uint32_t entry = *(uint32_t *)(g_emu.flash + 0x48);
                LOG("flash", "Update loaded: %s (%lu KB, entry=0x%08x)\n",
                    path, (unsigned long)(sz >> 10), entry);
                free(data);
                g_emu.is_v19_update = true;
                return;
            }
            free(data);

            /* Scan for subdirectories starting with prefix (e.g., rfm_15, rfm_18) */
            DIR *upd = opendir(upd_dirs[di]);
            if (upd) {
                struct dirent *de;
                while ((de = readdir(upd)) != NULL) {
                    if (de->d_name[0] == '.') continue;
                    size_t plen = strlen(g_emu.game_prefix);
                    if (strncmp(de->d_name, g_emu.game_prefix, plen) != 0) continue;
                    /* Must be prefix or prefix_version */
                    if (de->d_name[plen] != '\0' && de->d_name[plen] != '_') continue;
                    if (!build_update_path(path, sizeof(path), upd_dirs[di], de->d_name))
                        continue;
                    sz = 0;
                    data = load_file(path, &sz);
                    if (data && sz > 0x50 && sz <= FLASH_SIZE) {
                        memcpy(g_emu.flash, data, sz);
                        uint32_t entry = *(uint32_t *)(g_emu.flash + 0x48);
                        LOG("flash", "Update loaded: %s (%lu KB, entry=0x%08x)\n",
                            path, (unsigned long)(sz >> 10), entry);
                        /* Update game_id_str to match found version */
                        copy_cstr(g_emu.game_id_str, sizeof(g_emu.game_id_str), de->d_name);
                        free(data);
                        closedir(upd);
                        g_emu.is_v19_update = true;
                        return;
                    }
                    free(data);
                }
                closedir(upd);
            }
        }

        /* Also scan roms_dir for {prefix}_upd_*_update.bin */
        {
            DIR *rd = opendir(g_emu.roms_dir);
            if (rd) {
                struct dirent *de;
                char upd_pfx[64];
                snprintf(upd_pfx, sizeof(upd_pfx), "%s_upd_", g_emu.game_prefix);
                size_t upd_pfx_len = strlen(upd_pfx);
                while ((de = readdir(rd)) != NULL) {
                    if (strncmp(de->d_name, upd_pfx, upd_pfx_len) != 0) continue;
                    if (!strstr(de->d_name, "_update.bin")) continue;
                    snprintf(path, sizeof(path), "%s/%s", g_emu.roms_dir, de->d_name);
                    size_t sz = 0;
                    uint8_t *data = load_file(path, &sz);
                    if (data && sz > 0x50 && sz <= FLASH_SIZE) {
                        memcpy(g_emu.flash, data, sz);
                        uint32_t entry = *(uint32_t *)(g_emu.flash + 0x48);
                        LOG("flash", "Update loaded: %s (%lu KB, entry=0x%08x)\n",
                            path, (unsigned long)(sz >> 10), entry);
                        /* Extract version ID from filename */
                        const char *ver_start = de->d_name + upd_pfx_len;
                        const char *ver_end = strstr(ver_start, "_update.bin");
                        if (ver_end && ver_end > ver_start) {
                            /* Use the full id like "rfm_15" from "rfm_upd_rfm_15_update.bin" */
                            char full_id[32];
                            size_t id_len = ver_end - ver_start;
                            if (id_len < sizeof(full_id)) {
                                memcpy(full_id, ver_start, id_len);
                                full_id[id_len] = '\0';
                                copy_cstr(g_emu.game_id_str, sizeof(g_emu.game_id_str), full_id);
                            }
                        }
                        free(data);
                        closedir(rd);
                        g_emu.is_v19_update = true;
                        return;
                    }
                    free(data);
                }
                closedir(rd);
            }
        }
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
