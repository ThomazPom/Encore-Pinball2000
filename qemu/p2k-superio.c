/*
 * pinball2000 — SuperIO and CC5530 EEPROM I/O stubs.
 *
 * Direct port of the legacy I/O port handlers plus port 0x61 toggle.
 *
 *   - 0x2E/0x2F: Winbond W83977EF SuperIO config index/data.
 *                io_setup_global() reads register 0x20 expecting chip ID
 *                0x97. If absent, every io_setup_<dev>() panic-prints
 *                "SuperIOType unknown (0)".
 *   - 0x370/0x371: National PC97338 alternate config index/data. Older
 *                  firmware identifies this first as ID/revision 0x02/0x01.
 *   - 0xEA/0xEB: Cyrix CC5530 SoC config index/data — chip ID 0x02,
 *                rev 0x01.
 *   - 0x61: KBD/PIT control port — bit 4 (refresh) must toggle on every
 *           read (BT-107 wait-loop) or the timer-calibration spin wedges.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "p2k-qemu-compat.h"
#include "p2k-qemu-compat.h"

#include "p2k-internal.h"

static uint8_t p2k_superio_idx;
static uint8_t p2k_cc5530_idx;
static uint8_t p2k_port61_state;
static uint8_t p2k_pc97338_idx;
static uint8_t p2k_pc97338_regs[256];
static unsigned p2k_pc97338_unlock;
static bool p2k_pc97338_config;

static uint64_t p2k_superio_read(void *opaque, hwaddr addr, unsigned size)
{
    if (addr == 0) {
        return p2k_superio_idx;
    }
    if (p2k_superio_idx == 0x20) {
        return 0x97;          /* W83977EF chip ID */
    }
    return 0;
}

static void p2k_superio_write(void *opaque, hwaddr addr,
                              uint64_t val, unsigned size)
{
    if (addr == 0) {
        p2k_superio_idx = val & 0xff;
    }
    /* writes to data port (offset 1) are ignored */
}

static const MemoryRegionOps p2k_superio_ops = {
    .read  = p2k_superio_read,
    .write = p2k_superio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = { .min_access_size = 1, .max_access_size = 1 },
};

/* Older PRISM/XINU builds identify the National PC97338 through its
 * alternate 0x370/0x371 configuration pair before trying 0x2e/0x2f.
 * Entering configuration mode requires two consecutive 0x55 writes;
 * 0xaa exits.  SWE1 1.30 expects ID/revision 0x02/0x01 here. */
static uint64_t p2k_pc97338_read(void *opaque, hwaddr addr, unsigned size)
{
    if (addr == 0) {
        return p2k_pc97338_idx;
    }
    if (!p2k_pc97338_config) {
        return 0xff;
    }
    switch (p2k_pc97338_idx) {
    case 0x20: return 0x02;
    case 0x21: return 0x01;
    default:   return p2k_pc97338_regs[p2k_pc97338_idx];
    }
}

static void p2k_pc97338_write(void *opaque, hwaddr addr,
                              uint64_t val, unsigned size)
{
    uint8_t byte = val & 0xff;

    if (addr == 0) {
        if (!p2k_pc97338_config) {
            if (byte == 0x55 && ++p2k_pc97338_unlock >= 2) {
                p2k_pc97338_config = true;
            } else if (byte != 0x55) {
                p2k_pc97338_unlock = 0;
            }
            return;
        }
        if (byte == 0xaa) {
            p2k_pc97338_config = false;
            p2k_pc97338_unlock = 0;
            return;
        }
        p2k_pc97338_idx = byte;
        return;
    }
    if (p2k_pc97338_config) {
        p2k_pc97338_regs[p2k_pc97338_idx] = byte;
    }
}

static const MemoryRegionOps p2k_pc97338_ops = {
    .read  = p2k_pc97338_read,
    .write = p2k_pc97338_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = { .min_access_size = 1, .max_access_size = 1 },
};

static uint64_t p2k_cc5530_read(void *opaque, hwaddr addr, unsigned size)
{
    if (addr == 0) {
        return p2k_cc5530_idx;
    }
    switch (p2k_cc5530_idx) {
    case 0x20: return 0x02;   /* chip ID */
    case 0x21: return 0x01;   /* revision */
    default:   return 0;
    }
}

static void p2k_cc5530_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned size)
{
    if (addr == 0) {
        p2k_cc5530_idx = val & 0xff;
    }
}

static const MemoryRegionOps p2k_cc5530_ops = {
    .read  = p2k_cc5530_read,
    .write = p2k_cc5530_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = { .min_access_size = 1, .max_access_size = 1 },
};

static uint64_t p2k_port61_read(void *opaque, hwaddr addr, unsigned size)
{
    p2k_port61_state ^= 0x10;
    return p2k_port61_state & 0x1f;
}

static void p2k_port61_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned size)
{
    p2k_port61_state = val & 0xff;
}

static const MemoryRegionOps p2k_port61_ops = {
    .read  = p2k_port61_read,
    .write = p2k_port61_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = { .min_access_size = 1, .max_access_size = 1 },
};

void p2k_install_superio(void)
{
    MemoryRegion *io = get_system_io();

    MemoryRegion *mr_sio = g_new0(MemoryRegion, 1);
    memory_region_init_io(mr_sio, NULL, &p2k_superio_ops, NULL,
                          "p2k.superio", 2);
    memory_region_add_subregion(io, 0x2E, mr_sio);

    MemoryRegion *mr_pc97338 = g_new0(MemoryRegion, 1);
    memory_region_init_io(mr_pc97338, NULL, &p2k_pc97338_ops, NULL,
                          "p2k.pc97338", 2);
    memory_region_add_subregion(io, 0x370, mr_pc97338);

    MemoryRegion *mr_cc = g_new0(MemoryRegion, 1);
    memory_region_init_io(mr_cc, NULL, &p2k_cc5530_ops, NULL,
                          "p2k.cc5530", 2);
    memory_region_add_subregion(io, 0xEA, mr_cc);

    /* NOTE: do NOT register an overlap on port 0x61. QEMU's PC-HW i8254
     * already exposes the System Control port B at 0x61; layering our
     * own handler causes timer-calibration code to read garbage.
     * Encore needed the toggle because it had no real i8254 backing. */

    info_report("pinball2000: SuperIO W83977EF (0x2E/0x2F), PC97338 "
                "(0x370/0x371), CC5530 (0xEA/0xEB) installed");
}
