/*
 * pinball2000 BAR3 — Intel 28F320 update flash @ 0x12000000 (4 MiB).
 *
 * Mirrors unicorn.old/src/bar.c:355-403 (read path) and 837-905 (write
 * path).  Reads in command modes return status / device-ID / CFI;
 * reads in array mode return the backing buffer.  The buffer is seeded
 * from `savedata/<game>.flash` at boot.
 *
 * Without command-protocol semantics, status reads (0x70) come back as
 * 0xFF which the game interprets as "all error bits" and panics with
 * either "device blocked" Fatal or, more subtly, with the
 * "Retrieve Resource (get &) Failed" loop seen during XINU init when
 * the resource walker reads flash to find resource entries.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include <dirent.h>
#include <sys/stat.h>

#include "pinball2000.h"
#include "p2k-internal.h"

#define P2K_BAR3_BASE        0x12000000u
#define P2K_BAR3_SIZE        0x00400000u   /* 4 MiB */
#define P2K_BAR3_MASK        (P2K_BAR3_SIZE - 1u)

static uint8_t *s_flash;            /* 4 MiB array */
static int      s_cmd      = 0xFF;  /* current command mode */
static bool     s_cmd_act  = false; /* false = read array */
static uint8_t  s_status   = 0x80;  /* bit7 = ready */

static uint8_t flash_read_byte(uint32_t off)
{
    off &= P2K_BAR3_MASK;

    if (!s_cmd_act) {
        return s_flash[off];
    }
    switch (s_cmd) {
    case 0x70: return s_status;
    case 0x20: case 0x40: case 0x10: case 0xE8: return s_status;
    case 0x90: /* Read ID */
        switch ((off >> 1) & 0xFF) {
        case 0: return 0x89;     /* Intel */
        case 1: return 0x16;     /* 28F320J3 */
        default: return 0;
        }
    case 0x98: /* CFI */
        switch ((off >> 1) & 0xFF) {
        case 0x10: return 0x51;
        case 0x11: return 0x52;
        case 0x12: return 0x59;
        case 0x13: return 0x01;
        case 0x14: return 0x00;
        case 0x15: return 0x31;
        case 0x16: return 0x00;
        case 0x1B: return 0x45;
        case 0x1C: return 0x55;
        case 0x1F: return 0x07;
        case 0x21: return 0x0A;
        case 0x27: return 0x16;
        case 0x28: return 0x02;
        case 0x29: return 0x00;
        case 0x2A: return 0x05;
        case 0x2B: return 0x00;
        case 0x2C: return 0x01;
        case 0x2D: return 0x1F;
        case 0x2E: return 0x00;
        case 0x2F: return 0x00;
        case 0x30: return 0x02;
        default: return 0;
        }
    default: return 0;
    }
}

static uint64_t p2k_bar3_read(void *opaque, hwaddr off, unsigned size)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < size; i++) {
        v |= ((uint64_t)flash_read_byte(off + i)) << (i * 8);
    }
    return v;
}

static void p2k_bar3_write(void *opaque, hwaddr off, uint64_t val,
                           unsigned size)
{
    uint8_t cmd = val & 0xFF;
    uint32_t foff = off & P2K_BAR3_MASK;

    switch (cmd) {
    case 0xFF: s_cmd = 0xFF; s_cmd_act = false; return;
    case 0x70: s_cmd = 0x70; s_cmd_act = true;  return;
    case 0x90: s_cmd = 0x90; s_cmd_act = true;  return;
    case 0x98: s_cmd = 0x98; s_cmd_act = true;  return;
    case 0x20: s_cmd = 0x20; s_cmd_act = true;  return;
    case 0x40: case 0x10:
        s_cmd = cmd; s_cmd_act = true; return;
    case 0x60: s_cmd = 0x60; s_cmd_act = true;  return;
    case 0x01: case 0xD0:
        s_status  = 0x80;
        s_cmd     = 0x70;
        s_cmd_act = true;
        return;
    case 0x50:
        s_status  = 0x80;
        s_cmd     = 0x70;
        s_cmd_act = true;
        return;
    default:
        if (s_cmd_act && (s_cmd == 0x40 || s_cmd == 0x10)) {
            for (unsigned i = 0; i < size && (foff + i) < P2K_BAR3_SIZE; i++) {
                s_flash[foff + i] = (val >> (i * 8)) & 0xFF;
            }
            s_status  = 0x80;
            s_cmd     = 0x70;   /* arms read-status mode after program */
            s_cmd_act = true;
        }
        return;
    }
}

static const MemoryRegionOps p2k_bar3_ops = {
    .read  = p2k_bar3_read,
    .write = p2k_bar3_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl  = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

/* Scan dir (one level) for a file whose basename ends with `suffix`.
 * Returns malloced full path, or NULL. */
static char *find_with_suffix(const char *dir, const char *suffix)
{
    DIR *d = opendir(dir);
    if (!d) return NULL;
    struct dirent *e;
    char *result = NULL;
    while ((e = readdir(d))) {
        size_t nl = strlen(e->d_name), sl = strlen(suffix);
        if (nl >= sl && strcmp(e->d_name + nl - sl, suffix) == 0) {
            result = g_strdup_printf("%s/%s", dir, e->d_name);
            break;
        }
    }
    closedir(d);
    return result;
}

/* Read whole file into a freshly allocated buffer. */
static uint8_t *slurp(const char *path, size_t *out_sz)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0) { fclose(fp); return NULL; }
    uint8_t *buf = g_malloc(sz);
    size_t n = fread(buf, 1, sz, fp);
    fclose(fp);
    if (n != (size_t)sz) { g_free(buf); return NULL; }
    *out_sz = n;
    return buf;
}

/* Assemble the update bundle into the flash buffer, mirroring
 * unicorn.old/src/rom.c:526-576 layout:
 *   [0      .. 0x8000)  bootdata (truncated if larger)
 *   [0x8000 .. +A    )  im_flsh0
 *   [+A     .. +A+B  )  game
 *   [+A+B   .. +A+B+C)  symbols
 * Returns true if applied. */
static bool assemble_update(const char *dir)
{
    char *p_boot = find_with_suffix(dir, "_bootdata.rom");
    char *p_im   = find_with_suffix(dir, "_im_flsh0.rom");
    char *p_game = find_with_suffix(dir, "_game.rom");
    char *p_syms = find_with_suffix(dir, "_symbols.rom");

    if (!p_boot || !p_im || !p_game || !p_syms) {
        warn_report("pinball2000: update dir %s missing one of "
                    "*_bootdata/im_flsh0/game/symbols.rom — ignored", dir);
        g_free(p_boot); g_free(p_im); g_free(p_game); g_free(p_syms);
        return false;
    }

    size_t s_boot=0, s_im=0, s_game=0, s_syms=0;
    uint8_t *b_boot = slurp(p_boot, &s_boot);
    uint8_t *b_im   = slurp(p_im,   &s_im);
    uint8_t *b_game = slurp(p_game, &s_game);
    uint8_t *b_syms = slurp(p_syms, &s_syms);

    bool ok = (b_boot && b_im && b_game && b_syms);
    if (ok) {
        size_t boot_copy = s_boot > 0x8000 ? 0x8000 : s_boot;
        size_t off = 0;
        memcpy(s_flash + off, b_boot, boot_copy); off = 0x8000;
        if (off + s_im   <= P2K_BAR3_SIZE) { memcpy(s_flash + off, b_im,   s_im);   off += s_im; }   else ok = false;
        if (ok && off + s_game <= P2K_BAR3_SIZE) { memcpy(s_flash + off, b_game, s_game); off += s_game; } else ok = false;
        if (ok && off + s_syms <= P2K_BAR3_SIZE) { memcpy(s_flash + off, b_syms, s_syms); off += s_syms; } else ok = false;
        if (ok) {
            info_report("pinball2000: update bundle assembled into BAR3 "
                        "(boot=%zu im=%zu game=%zu syms=%zu, total=0x%zx)",
                        s_boot, s_im, s_game, s_syms, off);
        } else {
            warn_report("pinball2000: update bundle exceeds BAR3 size; "
                        "skipped");
        }
    }

    g_free(b_boot); g_free(b_im); g_free(b_game); g_free(b_syms);
    g_free(p_boot); g_free(p_im); g_free(p_game); g_free(p_syms);
    return ok;
}

void p2k_install_bar3_flash(Pinball2000MachineState *s)
{
    s_flash = g_malloc(P2K_BAR3_SIZE);
    memset(s_flash, 0xFF, P2K_BAR3_SIZE);

    const char *game = s->game ? s->game : "swe1";
    char path[1024];
    snprintf(path, sizeof(path), "savedata/%s.flash", game);
    FILE *fp = fopen(path, "rb");
    if (fp) {
        size_t n = fread(s_flash, 1, P2K_BAR3_SIZE, fp);
        fclose(fp);
        info_report("pinball2000: BAR3 seeded from %s (%zu bytes)", path, n);
    } else {
        warn_report("pinball2000: BAR3 flash %s not loaded", path);
    }

    if (s->update_path && *s->update_path) {
        info_report("pinball2000: applying update from %s", s->update_path);
        assemble_update(s->update_path);
    }

    MemoryRegion *mr = g_new(MemoryRegion, 1);
    memory_region_init_io(mr, NULL, &p2k_bar3_ops, NULL,
                          "p2k.bar3-flash", P2K_BAR3_SIZE);
    memory_region_add_subregion(get_system_memory(), P2K_BAR3_BASE, mr);
}
