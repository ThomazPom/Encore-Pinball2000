/*
 * pinball2000 minimal PCI configuration space (cf8/cfc).
 *
 * We do NOT instantiate a QEMU PCI host bridge. PRISM uses PCI only as a
 * lookup mechanism — it scans config space for known vendor/device IDs to
 * discover the Cyrix MediaGX bridges and the WMS PRISM PLX9050 card, then
 * talks to the real hardware via the BARs we already mapped in
 * p2k-plx9054.c. So all we need is a tiny cf8/cfc handler that returns
 * the same dwords the real board's southbridge would report.
 *
 * Authoritative source: unicorn.old/src/pci.c (verified working on the
 * full Unicorn boot path that booted to attract mode + sound).
 *
 * Bus 0:
 *   Dev 0   fn 0  Cyrix MediaGX host bridge   (0x1078:0x0001, class 0x060000)
 *   Dev 8   fn 0  WMS PRISM (PLX9050 wrapper) (0x146E:0x0001, class 0x030000)
 *   Dev 18  fn 0  Cyrix Cx5520 ISA bridge     (0x1078:0x0002, class 0x060100)
 *
 * Without these, gx_fb_reg_setup() in PRISM bombs with
 *   "*** Fatal: gx_fb_reg_setup(): PCI_cx55xx_dev not set"
 * and warm-resets back to 0x801C9 forever.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "exec/address-spaces.h"
#include "exec/ioport.h"

#include "pinball2000.h"
#include "p2k-internal.h"

/* Mirrors of unicorn.old PLX BAR addresses, kept here so this file is
 * self-contained.  These are the GUEST-VISIBLE physical addresses that
 * PRISM later reads back from PCI config and uses for MMIO. */
#define P2K_PCI_GX_BASE      0x40000000u
#define P2K_PCI_WMS_BAR0     0x10000000u
#define P2K_PCI_WMS_BAR2     0x11000000u
#define P2K_PCI_WMS_BAR3     0x12000000u
#define P2K_PCI_WMS_BAR4     0x13000000u
#define P2K_PCI_BAR5_BANK0   0x14000000u
#define P2K_PCI_ROMBAR_DCS   0x18000000u

static uint32_t s_pci_addr;

static uint32_t p2k_pci_cfg_read(uint8_t bus, uint8_t dev,
                                 uint8_t fn, uint8_t reg)
{
    if (bus != 0 || fn != 0) {
        return 0xFFFFFFFFu;
    }

    switch (dev) {
    case 0: /* Cyrix MediaGX host bridge */
        switch (reg) {
        case 0x00: return 0x00011078u;   /* vendor:device */
        case 0x04: return 0x02800007u;   /* command/status */
        case 0x08: return 0x06000000u;   /* class: host bridge */
        case 0x0C: return 0x00000000u;
        case 0x10: return P2K_PCI_GX_BASE;
        case 0x14: return P2K_PCI_GX_BASE;
        case 0x3C: return 0x00000100u;
        default:   return 0x00000000u;
        }

    case 8: /* WMS PRISM (PLX 9050 wrapper) */
        switch (reg) {
        case 0x00: return 0x0001146Eu;   /* vendor=WMS, device=0x0001 */
        case 0x04: return 0x02800007u;
        case 0x08: return 0x03000002u;   /* class: display, rev 2 */
        case 0x0C: return 0x00000000u;
        case 0x10: return P2K_PCI_WMS_BAR0;
        case 0x14: return 0x00000000u;
        case 0x18: return P2K_PCI_WMS_BAR2;
        case 0x1C: return P2K_PCI_WMS_BAR3;
        case 0x20: return P2K_PCI_WMS_BAR4;
        case 0x24: return P2K_PCI_BAR5_BANK0;
        case 0x2C: return 0x00011078u;   /* subsystem */
        case 0x30: return P2K_PCI_ROMBAR_DCS;
        case 0x3C: return 0x01080109u;
        default:   return 0x00000000u;
        }

    case 18: /* Cyrix Cx5520 ISA bridge */
        switch (reg) {
        case 0x00: return 0x00021078u;
        case 0x04: return 0x02800007u;
        case 0x08: return 0x06010000u;
        default:   return 0x00000000u;
        }

    default:
        return 0xFFFFFFFFu;
    }
}

/* ---------- 0xCF8 address latch ------------------------------------------ */

static uint64_t p2k_pci_addr_read(void *opaque, hwaddr addr, unsigned size)
{
    if (size == 4) return s_pci_addr;
    /* Byte/word reads of CF8 are unusual but legal. */
    uint32_t v = s_pci_addr;
    int off = (int)addr & 3;
    if (size == 1) return (v >> (off * 8)) & 0xFF;
    if (size == 2) return (v >> ((off & 2) * 8)) & 0xFFFF;
    return v;
}

static void p2k_pci_addr_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    if (size == 4) {
        s_pci_addr = (uint32_t)val;
        return;
    }
    int off = (int)addr & 3;
    uint32_t shift = off * 8;
    uint32_t mask = (size == 1) ? (0xFFu << shift) : (0xFFFFu << shift);
    s_pci_addr = (s_pci_addr & ~mask) | (((uint32_t)val << shift) & mask);
}

static const MemoryRegionOps p2k_pci_addr_ops = {
    .read       = p2k_pci_addr_read,
    .write      = p2k_pci_addr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl       = { .min_access_size = 1, .max_access_size = 4 },
    .valid      = { .min_access_size = 1, .max_access_size = 4 },
};

/* ---------- 0xCFC data window -------------------------------------------- */

static uint64_t p2k_pci_data_read(void *opaque, hwaddr addr, unsigned size)
{
    if (!(s_pci_addr & 0x80000000u)) return 0xFFFFFFFFu;

    uint8_t bus = (s_pci_addr >> 16) & 0xFF;
    uint8_t dev = (s_pci_addr >> 11) & 0x1F;
    uint8_t fn  = (s_pci_addr >>  8) & 0x07;
    uint8_t reg = s_pci_addr & 0xFC;

    uint32_t val = p2k_pci_cfg_read(bus, dev, fn, reg);
    int byte_off = (int)addr + (s_pci_addr & 3);

    if (size == 1) return (val >> ((byte_off & 3) * 8)) & 0xFF;
    if (size == 2) return (val >> ((byte_off & 2) * 8)) & 0xFFFF;
    return val;
}

static void p2k_pci_data_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    /* PRISM does BAR-size probes (write 0xFFFFFFFF, read back).  We
     * report fixed addresses so we just swallow the writes. */
}

static const MemoryRegionOps p2k_pci_data_ops = {
    .read       = p2k_pci_data_read,
    .write      = p2k_pci_data_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl       = { .min_access_size = 1, .max_access_size = 4 },
    .valid      = { .min_access_size = 1, .max_access_size = 4 },
};

void p2k_install_pci_stub(void)
{
    MemoryRegion *io = get_system_io();

    MemoryRegion *cf8 = g_new(MemoryRegion, 1);
    memory_region_init_io(cf8, NULL, &p2k_pci_addr_ops, NULL,
                          "p2k.pci-addr", 4);
    memory_region_add_subregion(io, 0xCF8, cf8);

    MemoryRegion *cfc = g_new(MemoryRegion, 1);
    memory_region_init_io(cfc, NULL, &p2k_pci_data_ops, NULL,
                          "p2k.pci-data", 4);
    memory_region_add_subregion(io, 0xCFC, cfc);

    info_report("pinball2000: installed PCI cf8/cfc stub "
                "(MediaGX dev0, PRISM dev8, Cx5520 dev18)");
}
