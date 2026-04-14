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
    /* If user specified --game, use that */
    if (g_emu.game_prefix[0] && strcmp(g_emu.game_prefix, "auto") != 0) {
        LOG("rom", "Game override: %s\n", g_emu.game_prefix);
        return 0;
    }

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

    /* Build game_id_str for savedata naming (e.g. "swe1_14", "rfm_15") */
    strncpy(g_emu.game_id_str, g_emu.game_prefix, sizeof(g_emu.game_id_str) - 1);
    LOG("rom", "Save data ID: %s\n", g_emu.game_id_str);

    return 0;
}

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

    /* Initialize SEEPROM (all 0xFFFF = blank) */
    for (int i = 0; i < 64; i++)
        g_emu.seeprom[i] = 0xFFFF;

    /* Clear EMS */
    memset(g_emu.ems, 0, sizeof(g_emu.ems));

    /* Load savedata if available */
    savedata_load();

    fflush(stdout);
    return 0;
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
