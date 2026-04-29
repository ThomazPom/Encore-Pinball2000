/*
 * pinball2000 SMC8216T LAN-ROM shadow @ 0xD0008..0xD000F.
 *
 * On real hardware the BIOS POST shadows the NIC's 16-byte address
 * PROM into the D-segment (0xD0000-0xD007FF) so the driver can
 * read the MAC + board_id + checksum without touching the chip.
 * Williams XINU's network init walks 0xD0008..0xD000F and aborts
 * if the checksum doesn't validate; without the shadow it ends up
 * with board_id=0x00 and the downstream peripheral profile is wrong.
 *
 * Old implementation (deleted in this commit): a one-shot host-side
 * `cpu_physical_memory_write()` of 8 precomputed bytes into guest
 * RAM at p2k_post_reset(). Worked, but is exactly the dirty pattern
 * we are retiring across the codebase: a host-side write into guest
 * BSS at a hard-coded address.
 *
 * Current implementation: an 8-byte read-only MMIO overlay on top
 * of the system RAM region at 0xD0008. The read handler returns the
 * same MAC + board_id + checksum the original poke produced. Writes
 * are silently dropped (this *is* read-only shadow ROM on real
 * hardware: the BIOS POST is what wrote it, and after POST it never
 * changes). The rest of the D-segment (0xD0000..0xD0007 and
 * 0xD0010..0xD007FF) is untouched and remains plain RAM, so any
 * other guest use of the segment continues to work.
 *
 * Mirrors unicorn.old/src/io.c:680-700 (BT-131):
 *   0xD0008..0xD000D : MAC = 00:00:C0:01:02:03 (Williams OUI placeholder)
 *   0xD000E         : board_id = 0x2A
 *   0xD000F         : checksum = 0xFF - sum(0xD0008..0xD000E)
 *
 * One concern per file: this module owns the 8 LAN-ROM shadow bytes
 * and nothing else.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "exec/address-spaces.h"

#include "p2k-internal.h"

#define P2K_NIC_DSEG_BASE   0x000D0008u
#define P2K_NIC_DSEG_LEN    8u

static uint8_t p2k_nic_shadow[P2K_NIC_DSEG_LEN];

static uint64_t p2k_nic_dseg_read(void *opaque, hwaddr off, unsigned sz)
{
    uint64_t val = 0;
    if (off + sz > P2K_NIC_DSEG_LEN) {
        sz = (off >= P2K_NIC_DSEG_LEN) ? 0 : (P2K_NIC_DSEG_LEN - off);
    }
    for (unsigned i = 0; i < sz; i++) {
        val |= ((uint64_t)p2k_nic_shadow[off + i]) << (i * 8);
    }
    return val;
}

static void p2k_nic_dseg_write(void *opaque, hwaddr off,
                               uint64_t val, unsigned sz)
{
    /* Read-only shadow ROM. */
}

static const MemoryRegionOps p2k_nic_dseg_ops = {
    .read       = p2k_nic_dseg_read,
    .write      = p2k_nic_dseg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid      = { .min_access_size = 1, .max_access_size = 4 },
    .impl       = { .min_access_size = 1, .max_access_size = 4 },
};

void p2k_install_nic_dseg(void)
{
    static const uint8_t mac[6] = { 0x00, 0x00, 0xC0, 0x01, 0x02, 0x03 };
    const uint8_t board_id = 0x2A;
    memcpy(p2k_nic_shadow, mac, 6);
    p2k_nic_shadow[6] = board_id;
    uint8_t sum = 0;
    for (int i = 0; i < 7; i++) {
        sum += p2k_nic_shadow[i];
    }
    p2k_nic_shadow[7] = (uint8_t)(0xFFu - sum);

    MemoryRegion *sys = get_system_memory();
    MemoryRegion *mr  = g_new(MemoryRegion, 1);
    memory_region_init_io(mr, NULL, &p2k_nic_dseg_ops, NULL,
                          "p2k.nic-dseg-shadow", P2K_NIC_DSEG_LEN);
    memory_region_add_subregion_overlap(sys, P2K_NIC_DSEG_BASE, mr, 1);

    info_report("pinball2000: BT-131 NIC LAN ROM shadow installed at "
                "0x%05x (8B read-only MMIO; MAC %02x:%02x:%02x:%02x:%02x:%02x "
                "board_id=0x%02x checksum=0x%02x)",
                P2K_NIC_DSEG_BASE,
                p2k_nic_shadow[0], p2k_nic_shadow[1], p2k_nic_shadow[2],
                p2k_nic_shadow[3], p2k_nic_shadow[4], p2k_nic_shadow[5],
                p2k_nic_shadow[6], p2k_nic_shadow[7]);
}
