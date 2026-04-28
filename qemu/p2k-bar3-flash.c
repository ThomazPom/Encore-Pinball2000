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

    MemoryRegion *mr = g_new(MemoryRegion, 1);
    memory_region_init_io(mr, NULL, &p2k_bar3_ops, NULL,
                          "p2k.bar3-flash", P2K_BAR3_SIZE);
    memory_region_add_subregion(get_system_memory(), P2K_BAR3_BASE, mr);
}
