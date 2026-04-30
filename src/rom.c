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
    /* Called twice during init (early discovery + full load). Suppress the
     * banner messages on subsequent calls so the log isn't doubled. */
    static bool s_announced = false;
    #define ROM_LOG(fmt, ...) do { if (!s_announced) LOG("rom", fmt, ##__VA_ARGS__); } while(0)

    /* If user specified --game, use that. */
    if (g_emu.game_prefix[0] && strcmp(g_emu.game_prefix, "auto") != 0) {
        ROM_LOG("Game override: %s\n", g_emu.game_prefix);
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
    ROM_LOG("Auto-detected ROM prefix: %s\n", g_emu.game_prefix);

    } /* end of auto-detect block */

    g_emu.game_id = detect_game_id_from_prefix(g_emu.game_prefix);
    if (g_emu.game_id == GAME_ID_SWE1)
        ROM_LOG("Game ID: %u → Star Wars Episode I\n", g_emu.game_id);
    else if (g_emu.game_id == GAME_ID_RFM)
        ROM_LOG("Game ID: %u → Revenge From Mars\n", g_emu.game_id);
    else if (g_emu.game_id != 0)
        ROM_LOG("Game ID: %u (unknown game)\n", g_emu.game_id);

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
    ROM_LOG("Save data ID: %s\n", g_emu.game_id_str);

    s_announced = true;
    #undef ROM_LOG
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
                          strcmp(g_emu.game_prefix, "rfm") == 0);

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
            LOGV("rom", "Bank %d chip u%d: %s\n", b, chips[b][c], path);
        }

        if (loaded == 2) {
            g_emu.rom_banks[b] = bank_mem;
            g_emu.rom_sizes[b] = BANK_SIZE;
            /* DCS presence table at bank0+0x10000 left intact. The game
             * needs it to detect DCS2 hardware and create the DCS2 task. */
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
        LOGV("rom", "DCS chip u%d: %s\n", chip, path);
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
        "./roms/bios.bin",
        "./roms/bios.bin",
        NULL
    };
    /* Build relative path from roms_dir */
    snprintf(path, sizeof(path), "%s/./roms/bios.bin", g_emu.roms_dir);
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

    /* Load savedata if available (unless --no-savedata) */
    if (!g_emu.no_savedata)
        savedata_load();
    else
        LOG("save", "--no-savedata: skipping NVRAM/SEEPROM load\n");

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

/* All-terrain update assembly.
 *
 * Accepts any of the following sources:
 *   • A pre-concatenated update.bin (or any file ≤ FLASH_SIZE that
 *     starts with a date string + entry-point at offset 0x48 — exactly
 *     what the bootdata header looks like)
 *   • A directory containing the 4-6 individual *.rom parts
 *     (bootdata, im_flsh0, game, symbols [, pubboot, sf]) as shipped
 *     by Williams' update bundles
 *   • A ZIP archive (the official .exe installers ARE ZIPs — confirmed
 *     by file(1) magic; they contain {game_id}/ subfolder with the
 *     same .rom layout). Extracted to a unique tmp dir via the
 *     system unzip(1), then assembled like the directory case.
 *
 * The on-disk layout the binary's option-ROM expects is fixed:
 *     0x000000  bootdata.rom
 *     0x008000  im_flsh0.rom
 *     0x008000+|im_flsh0|  game.rom
 *     0x008000+|im_flsh0|+|game|  symbols.rom
 * matching tools/build_update_bin.py exactly.
 *
 * Returns assembled byte count on success, 0 on failure. Caller owns
 * *out_data via free(). */

static int assemble_update_from_dir(const char *dir,
                                    uint8_t **out_data, size_t *out_sz);

static int try_extract_zip_to_tmp(const char *zip_path, char *tmpdir, size_t tmpdir_sz)
{
    /* Probe with `unzip -t` — handles raw .zip *and* self-extracting
     * .exe wrappers (PE with appended ZIP, central directory at EOF).
     * Exit code 0 means the archive is readable. */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "unzip -tqq '%s' >/dev/null 2>&1", zip_path);
    if (system(cmd) != 0) {
        LOG("flash", "zip-detect: %s is not a readable zip/exe-installer\n",
            zip_path);
        return -1;
    }
    LOG("flash", "zip-detect: %s is a valid archive, extracting...\n", zip_path);

    /* Allocate a unique tmp dir */
    snprintf(tmpdir, tmpdir_sz, "/tmp/encore_upd_XXXXXX");
    if (!mkdtemp(tmpdir)) {
        LOG("flash", "mkdtemp failed: %s\n", strerror(errno));
        return -1;
    }

    /* Quote the zip path to survive spaces/special chars. unzip -q for
     * silence, -o for overwrite. */
    snprintf(cmd, sizeof(cmd),
             "unzip -q -o '%s' -d '%s' >/dev/null 2>&1", zip_path, tmpdir);
    int rc = system(cmd);
    if (rc != 0) {
        LOG("flash", "unzip(1) failed (rc=%d) on %s\n", rc, zip_path);
        return -1;
    }
    return 0;
}

/* Recursively search DIR for *.rom files, classify by suffix, and
 * concatenate per the bootdata/im_flsh0/game/symbols layout. */
static void scan_for_roms(const char *dir, char by_kind[6][512])
{
    static const char *kinds[6] = {
        "bootdata", "im_flsh0", "game", "symbols", "pubboot", "sf"
    };
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            scan_for_roms(path, by_kind);
            continue;
        }
        if (!S_ISREG(st.st_mode)) continue;
        const char *ext = strrchr(de->d_name, '.');
        if (!ext || strcasecmp(ext, ".rom") != 0) continue;
        for (int k = 0; k < 6; k++) {
            char needle[32];
            snprintf(needle, sizeof(needle), "_%s.", kinds[k]);
            if (strstr(de->d_name, needle) && by_kind[k][0] == '\0') {
                snprintf(by_kind[k], 512, "%s", path);
                break;
            }
        }
    }
    closedir(d);
}

static int assemble_update_from_dir(const char *dir,
                                    uint8_t **out_data, size_t *out_sz)
{
    char by_kind[6][512] = {{0}};
    scan_for_roms(dir, by_kind);

    /* Need at minimum bootdata + im_flsh0 + game + symbols. */
    static const char *kind_names[4] = {"bootdata", "im_flsh0", "game", "symbols"};
    for (int k = 0; k < 4; k++) {
        if (by_kind[k][0] == '\0') {
            LOG("flash", "directory %s missing %s.rom\n", dir, kind_names[k]);
            return -1;
        }
    }

    size_t sz_bd = 0, sz_im = 0, sz_g = 0, sz_sy = 0;
    uint8_t *bd = load_file(by_kind[0], &sz_bd);
    uint8_t *im = load_file(by_kind[1], &sz_im);
    uint8_t *g  = load_file(by_kind[2], &sz_g);
    uint8_t *sy = load_file(by_kind[3], &sz_sy);
    if (!bd || !im || !g || !sy) {
        free(bd); free(im); free(g); free(sy);
        LOG("flash", "directory %s: failed to load one of the .rom parts\n", dir);
        return -1;
    }

    size_t total = 0x8000 + sz_im + sz_g + sz_sy;
    if (total > FLASH_SIZE) {
        free(bd); free(im); free(g); free(sy);
        LOG("flash", "assembled size %zu > FLASH_SIZE\n", total);
        return -1;
    }
    uint8_t *buf = calloc(1, total);
    if (!buf) {
        free(bd); free(im); free(g); free(sy);
        return -1;
    }
    memcpy(buf, bd, sz_bd > 0x8000 ? 0x8000 : sz_bd);
    size_t off = 0x8000;
    memcpy(buf + off, im, sz_im); off += sz_im;
    memcpy(buf + off, g,  sz_g);  off += sz_g;
    memcpy(buf + off, sy, sz_sy); off += sz_sy;
    free(bd); free(im); free(g); free(sy);

    LOG("flash", "assembled update from %s: bootdata=%zuKB im=%zuKB game=%zuKB sym=%zuKB → %zuKB total\n",
        dir, sz_bd >> 10, sz_im >> 10, sz_g >> 10, sz_sy >> 10, total >> 10);

    *out_data = buf;
    *out_sz = total;
    return 0;
}

/* Returns 0 on success, -1 if PATH cannot be converted to an update
 * payload by any of the supported routes. */
static int load_update_anyform(const char *path,
                               uint8_t **out_data, size_t *out_sz)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        LOG("flash", "stat(%s): %s\n", path, strerror(errno));
        return -1;
    }

    /* Directory → assemble in place */
    if (S_ISDIR(st.st_mode))
        return assemble_update_from_dir(path, out_data, out_sz);

    /* Try ZIP/EXE-installer */
    char tmpdir[64];
    if (try_extract_zip_to_tmp(path, tmpdir, sizeof(tmpdir)) == 0) {
        int rc = assemble_update_from_dir(tmpdir, out_data, out_sz);
        /* Best-effort cleanup; non-fatal */
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", tmpdir);
        if (system(cmd) != 0) { /* nothing to do */ }
        return rc;
    }

    /* Plain .bin */
    size_t sz = 0;
    uint8_t *data = load_file(path, &sz);
    if (!data) return -1;
    if (sz <= 0x50 || sz > FLASH_SIZE) {
        LOG("flash", "%s: size %zu out of range\n", path, sz);
        free(data);
        return -1;
    }
    *out_data = data;
    *out_sz = sz;
    return 0;
}

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
 *   - ./updates/{game_id_str}/
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

    /* Strategy 0: explicit --update PATH override (file/dir/zip-exe). */
    if (g_emu.update_file[0]) {
        uint8_t *data = NULL; size_t sz = 0;
        if (load_update_anyform(g_emu.update_file, &data, &sz) == 0) {
            memcpy(g_emu.flash, data, sz);
            uint32_t entry = *(uint32_t *)(g_emu.flash + 0x48);
            LOG("flash", "Update loaded (--update): %s (%lu KB, entry=0x%08x)\n",
                g_emu.update_file, (unsigned long)(sz >> 10), entry);
            free(data);
            g_emu.is_v19_update = true;
            return;
        }
        LOG("flash", "WARN: --update %s could not be parsed as bin/dir/exe-zip"
                     " — falling back to default search\n",
            g_emu.update_file);
    }

    /* Strategy 1: Try pre-concatenated update.bin.
     * Scan update dirs for subdirectories starting with game prefix. */
    {
        const char *upd_dirs[] = {
            "./updates",
            "./updates",
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
            "./updates",
            "./updates",
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
    LOG("flash", "  Expected: ./updates/%s/{id}_update.bin\n", id);
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
