/*
 * pci.c — PCI configuration space for 3 devices.
 *
 * Bus 0, Function 0:
 *   Dev 0:  Cyrix MediaGX  (0x1078:0x0001, class 0x060000)
 *   Dev 8:  PLX 9050 / WMS PRISM  (0x10b5:0x9050, class 0x068000)
 *   Dev 18: Cyrix Cx5520   (0x1078:0x0002, class 0x060100)
 *
 * The PRISM PCI card uses a PLX 9050 as PCI interface. On real hardware
 * the PLX chip presents itself with vendor=0x10B5, device=0x9050.
 * The game's pci_probe() scans for this vendor ID.
 */
#include "encore.h"

static uint32_t pci_read_dword(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg)
{
    if (bus != 0 || fn != 0) return 0xFFFFFFFFu;

    switch (dev) {
    case 0: /* Cyrix MediaGX host bridge */
        switch (reg) {
        case 0x00: return 0x00011078u;   /* vendor:device */
        case 0x04: return 0x02800007u;   /* command/status */
        case 0x08: return 0x06000000u;   /* class: host bridge */
        case 0x0C: return 0x00000000u;   /* cache line, latency, header type */
        case 0x10: return GX_BASE;       /* BAR0 — GX_BASE */
        case 0x14: return GX_BASE;       /* BAR1 — GX_BASE alias */
        case 0x3C: return 0x00000100u;   /* interrupt */
        default:   return 0x00000000u;
        }

    case 8: /* WMS PRISM (PLX 9050 bridge on PRISM board) */
        switch (reg) {
        case 0x00: return 0x0001146eu;   /* vendor=WMS(0x146E), device=0x0001 */
        case 0x04: return 0x02800007u;   /* command/status — memory + I/O enabled */
        case 0x08: return 0x03000002u;   /* class: display, rev 2 */
        case 0x0C: return 0x00000000u;
        case 0x10: return WMS_BAR0;      /* BAR0 — PLX 9050 local regs */
        case 0x14: return 0x00000000u;   /* BAR1 — I/O (unused) */
        case 0x18: return WMS_BAR2;      /* BAR2 — DCS2 interface */
        case 0x1C: return WMS_BAR3;      /* BAR3 — update flash */
        case 0x20: return WMS_BAR4;      /* BAR4 — DCS audio */
        case 0x24: return BAR5_BANK0;    /* BAR5 — ROM flash window */
        case 0x2C: return 0x00011078u;   /* subsystem vendor=0x1078, device=0x0001 */
        case 0x30: return ROMBAR_DCS;    /* expansion ROM (DCS sound ROM) */
        case 0x3C: return 0x01080109u;   /* interrupt line/pin */
        default:   return 0x00000000u;
        }

    case 18: /* Cyrix Cx5520 ISA bridge */
        switch (reg) {
        case 0x00: return 0x00021078u;   /* vendor:device */
        case 0x04: return 0x02800007u;
        case 0x08: return 0x06010000u;   /* class: ISA bridge */
        default:   return 0x00000000u;
        }

    default:
        return 0xFFFFFFFFu;
    }
}

uint32_t pci_read(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg)
{
    return pci_read_dword(bus, dev, fn, reg);
}

void pci_write(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg, uint32_t val)
{
    /* PCI writes: log BAR probe/assign, mostly ignore */
    static int s_log = 0;
    if (s_log < 50) {
        s_log++;
        LOGV("pci", "write dev=%d reg=0x%02x val=0x%08x%s\n",
            dev, reg, val, (val == 0xFFFFFFFFu) ? " (size probe)" : "");
    }
}
