/*
 * io.c — I/O port handlers.
 *
 * PIC (i8259), PIT (i8254), CMOS/RTC, KBC, UART, LPT,
 * VGA, PRISM config (0x22/0x23), PCI (0xCF8/0xCFC), DCS2 status (0x6F96).
 */
#include "encore.h"

void io_init(void)
{
    /* PIC1 defaults */
    g_emu.pic[0].imr = 0xFF;  /* all masked initially */
    g_emu.pic[0].icw2 = 0x08; /* default vector base */

    /* PIC2 defaults */
    g_emu.pic[1].imr = 0xFF;
    g_emu.pic[1].icw2 = 0x70; /* default vector base */

    /* PIT defaults — channel 0 count */
    g_emu.pit.count[0] = 0xFFFF;
    g_emu.pit.count[1] = 0xFFFF;
    g_emu.pit.count[2] = 0xFFFF;

    /* CMOS — minimal RTC data */
    g_emu.cmos_data[0x00] = 0x00; /* seconds */
    g_emu.cmos_data[0x02] = 0x30; /* minutes */
    g_emu.cmos_data[0x04] = 0x12; /* hours */
    g_emu.cmos_data[0x06] = 0x04; /* day of week */
    g_emu.cmos_data[0x07] = 0x15; /* day */
    g_emu.cmos_data[0x08] = 0x04; /* month */
    g_emu.cmos_data[0x09] = 0x25; /* year (2025) */
    g_emu.cmos_data[0x0A] = 0x26; /* status A */
    g_emu.cmos_data[0x0B] = 0x02; /* status B (24h mode) */
    g_emu.cmos_data[0x0C] = 0x00; /* status C */
    g_emu.cmos_data[0x0D] = 0x80; /* status D (battery OK) */
    g_emu.cmos_data[0x0E] = 0x00; /* diagnostic status */
    g_emu.cmos_data[0x0F] = 0x00; /* shutdown code */
    g_emu.cmos_data[0x10] = 0x00; /* floppy */
    g_emu.cmos_data[0x14] = 0x06; /* equipment byte */
    g_emu.cmos_data[0x15] = 0x80; /* base memory low (640KB = 0x0280) */
    g_emu.cmos_data[0x16] = 0x02; /* base memory high */
    g_emu.cmos_data[0x17] = 0x00; /* ext memory low (15MB = 0x3C00) */
    g_emu.cmos_data[0x18] = 0x3C; /* ext memory high */
    g_emu.cmos_data[0x30] = 0x00; /* ext memory low (copy) */
    g_emu.cmos_data[0x31] = 0x3C; /* ext memory high (copy) */
    g_emu.cmos_data[0x32] = 0x20; /* century BCD */

    /* KBC */
    g_emu.kbc_status = 0x14; /* self-test passed, input buffer empty */

    /* UART — default idle state */
    g_emu.uart_regs[5] = 0x60; /* LSR: THRE + TEMT (transmitter empty+ready) */

    /* PRISM config — GX_BASE at 0x40000000 */
    g_emu.gx_base_addr = GX_BASE;

    /* LPT — activate for PinIO (BT-93 from i386 POC) */
    g_emu.lpt_status = 0xDF; /* bit5=0: paper not out, bits 7,6,4,3 = 1 */
    g_emu.lpt_ctrl = 0x00;

    LOG("io", "I/O ports initialized\n");
}

/* ===== PIC handlers ===== */
static uint32_t pic_read(int idx, uint16_t port)
{
    PICState *pic = &g_emu.pic[idx];
    if (port & 1) {
        return pic->imr;
    } else {
        return pic->read_isr ? pic->isr : pic->irr;
    }
}

static void pic_write(int idx, uint16_t port, uint8_t val)
{
    PICState *pic = &g_emu.pic[idx];

    if (port & 1) {
        /* Data port */
        if (pic->init_mode) {
            switch (pic->icw_step) {
            case 1: pic->icw2 = val & 0xF8; pic->icw_step = 2; break;
            case 2: pic->icw3 = val;         pic->icw_step = 3; break;
            case 3: pic->icw4 = val;         pic->icw_step = 0;
                    pic->init_mode = false;  break;
            }
        } else {
            pic->imr = val;
        }
    } else {
        /* Command port */
        if (val & 0x10) {
            /* ICW1 */
            pic->icw1 = val;
            pic->icw_step = 1;
            pic->init_mode = true;
            pic->imr = 0;
            pic->isr = 0;
            pic->irr = 0;
        } else if (val == 0x20) {
            /* Non-specific EOI */
            for (int i = 0; i < 8; i++) {
                if (pic->isr & (1 << i)) {
                    pic->isr &= ~(1 << i);
                    break;
                }
            }
        } else if (val == 0x60) {
            /* Specific EOI for IRQ0 */
            pic->isr &= ~0x01;
        } else if ((val & 0x60) == 0x60) {
            /* Specific EOI */
            pic->isr &= ~(1 << (val & 7));
        } else if (val == 0x0B) {
            /* OCW3: read ISR */
            pic->read_isr = 1;
        } else if (val == 0x0A) {
            /* OCW3: read IRR */
            pic->read_isr = 0;
        }
    }
}

/* ===== PIT handlers ===== */
static uint32_t pit_read(uint16_t port)
{
    int ch = port - PORT_PIT_CH0;
    if (ch < 0 || ch > 2) return 0;

    PITState *pit = &g_emu.pit;
    uint16_t val;

    if (pit->latched[ch]) {
        val = pit->latch[ch];
    } else {
        /* Simulate decrementing counter based on host time */
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint32_t ticks = (uint32_t)(ts.tv_nsec / 838); /* ~1.193MHz */
        val = pit->count[ch] - (ticks & 0xFFFF);
    }

    if (pit->access_lo[ch]) {
        pit->access_lo[ch] = 0;
        pit->latched[ch] = false;
        return (val >> 8) & 0xFF;
    } else {
        pit->access_lo[ch] = 1;
        return val & 0xFF;
    }
}

static void pit_write(uint16_t port, uint8_t val)
{
    if (port == PORT_PIT_CMD) {
        int ch = (val >> 6) & 3;
        if (ch == 3) return; /* readback, ignore */
        g_emu.pit.rw_mode[ch] = (val >> 4) & 3;
        g_emu.pit.mode[ch] = (val >> 1) & 7;
        g_emu.pit.access_lo[ch] = 0;
        if (g_emu.pit.rw_mode[ch] == 0) {
            /* Latch command */
            g_emu.pit.latch[ch] = g_emu.pit.count[ch];
            g_emu.pit.latched[ch] = true;
        }
    } else {
        int ch = port - PORT_PIT_CH0;
        if (ch < 0 || ch > 2) return;
        if (g_emu.pit.access_lo[ch] == 0) {
            g_emu.pit.count[ch] = (g_emu.pit.count[ch] & 0xFF00) | val;
            g_emu.pit.access_lo[ch] = 1;
        } else {
            g_emu.pit.count[ch] = (g_emu.pit.count[ch] & 0x00FF) | (val << 8);
            g_emu.pit.access_lo[ch] = 0;
        }
    }
}

/* ===== UART handler (COM1) ===== */
static void uart_write(uint16_t port, uint8_t val)
{
    uint16_t off = port - PORT_COM1_BASE;
    if (off == 0) {
        /* THR — transmit character */
        if (val >= 0x20 || val == '\n' || val == '\r') {
            if (g_emu.uart_pos < (int)sizeof(g_emu.uart_buf) - 1)
                g_emu.uart_buf[g_emu.uart_pos++] = (char)val;
        }
        /* Detect milestones in UART output */
        if (val == '\n' && g_emu.uart_pos > 1) {
            g_emu.uart_buf[g_emu.uart_pos] = '\0';
            /* Print UART lines */
            LOG("uart", "%s", g_emu.uart_buf);
            /* Detect key strings */
            if (strstr(g_emu.uart_buf, "STARTING GAME CODE"))
                g_emu.game_started = true;
            if (strstr(g_emu.uart_buf, "XINU"))
                g_emu.xinu_booted = true;
            g_emu.uart_pos = 0;
        }
    } else if (off < 8) {
        g_emu.uart_regs[off] = val;
    }
}

static uint32_t uart_read(uint16_t port)
{
    uint16_t off = port - PORT_COM1_BASE;
    switch (off) {
    case 0: return 0;                  /* RBR — no data */
    case 2: return 0x01;               /* IIR — no interrupt pending */
    case 5: return 0x60;               /* LSR — THRE + TEMT */
    case 6: return 0x30;               /* MSR — CTS + DSR */
    default: return g_emu.uart_regs[off];
    }
}

/* ===== Main I/O dispatch ===== */

uint32_t io_port_read(uint16_t port, int size)
{
    switch (port) {
    /* PIC */
    case PORT_PIC1_CMD:
    case PORT_PIC1_DATA:
        return pic_read(0, port);
    case PORT_PIC2_CMD:
    case PORT_PIC2_DATA:
        return pic_read(1, port);

    /* PIT */
    case PORT_PIT_CH0:
    case PORT_PIT_CH1:
    case PORT_PIT_CH2:
        return pit_read(port);
    case PORT_PIT_CMD:
        return 0;

    /* KBC */
    case PORT_KBC_DATA:
        return g_emu.kbc_outbuf;
    case PORT_KBC_CMD:
        return g_emu.kbc_status;

    /* CMOS/RTC */
    case PORT_CMOS_DATA:
        return g_emu.cmos_data[g_emu.cmos_addr & 0x7F];

    /* A20 gate */
    case PORT_A20:
        return g_emu.a20_enabled ? 0x02 : 0x00;

    /* PCI */
    case PORT_PCI_ADDR:
        return g_emu.pci_addr;
    case PORT_PCI_DATA:
    case PORT_PCI_DATA + 1:
    case PORT_PCI_DATA + 2:
    case PORT_PCI_DATA + 3: {
        if (!(g_emu.pci_addr & 0x80000000u)) return 0xFFFFFFFFu;
        uint8_t bus = (g_emu.pci_addr >> 16) & 0xFF;
        uint8_t dev = (g_emu.pci_addr >> 11) & 0x1F;
        uint8_t fn  = (g_emu.pci_addr >> 8) & 0x07;
        uint8_t reg = g_emu.pci_addr & 0xFC;
        uint32_t val = pci_read(bus, dev, fn, reg);
        int byte_off = (port - PORT_PCI_DATA) + (g_emu.pci_addr & 3);
        if (size == 1) return (val >> ((byte_off & 3) * 8)) & 0xFF;
        if (size == 2) return (val >> ((byte_off & 2) * 8)) & 0xFFFF;
        return val;
    }

    /* PRISM/MediaGX config */
    case PORT_PRISM_IDX:
        return g_emu.prism_idx;
    case PORT_PRISM_DATA:
        switch (g_emu.prism_idx) {
        case 0xB8: return (g_emu.gx_base_addr >> 24) & 0xFF; /* GX_BASE high byte */
        case 0xC3: return 0x10; /* bit 4 set = feature flag */
        default:   return 0x00;
        }

    /* VGA */
    case PORT_VGA_STATUS:
        g_emu.vga_flipflop = !g_emu.vga_flipflop;
        /* Bit 0 = display active, Bit 3 = VBLANK */
        return g_emu.vga_flipflop ? 0x09 : 0x00;
    case PORT_VGA_SEQ_DATA:
        return g_emu.vga_seq[g_emu.vga_seq_idx & 7];
    case PORT_VGA_CRTC_DATA:
        return g_emu.vga_crtc[g_emu.vga_crtc_idx % 25];
    case 0x03CC: /* VGA misc read */
        return g_emu.vga_misc;

    /* UART */
    case PORT_COM1_BASE ... PORT_COM1_BASE + 7:
        return uart_read(port);

    /* LPT */
    case PORT_LPT_DATA:
        return g_emu.lpt_data;
    case PORT_LPT_STATUS:
        return g_emu.lpt_status;
    case PORT_LPT_CTRL:
        return g_emu.lpt_ctrl;

    /* DCS2 status — must return 0x00 (ready) */
    case PORT_DCS2_STATUS:
        return 0x00;

    /* POST code */
    case PORT_POST:
        return g_emu.post_code;

    /* DMA page register */
    case PORT_DMA_PAGE:
    case 0x0089:
    case 0x008A:
    case 0x008B:
        return 0x00;

    default:
        break;
    }

    return (size == 1) ? 0xFF : (size == 2) ? 0xFFFF : 0xFFFFFFFF;
}

void io_port_write(uint16_t port, uint32_t val, int size)
{
    switch (port) {
    /* PIC */
    case PORT_PIC1_CMD:
    case PORT_PIC1_DATA:
        pic_write(0, port, val & 0xFF);
        break;
    case PORT_PIC2_CMD:
    case PORT_PIC2_DATA:
        pic_write(1, port, val & 0xFF);
        break;

    /* PIT */
    case PORT_PIT_CH0:
    case PORT_PIT_CH1:
    case PORT_PIT_CH2:
    case PORT_PIT_CMD:
        pit_write(port, val & 0xFF);
        break;

    /* KBC */
    case PORT_KBC_DATA:
        /* Keyboard data write — mostly ignored */
        break;
    case PORT_KBC_CMD:
        if (val == 0xAA) g_emu.kbc_outbuf = 0x55; /* self-test OK */
        else if (val == 0xAB) g_emu.kbc_outbuf = 0x00; /* interface test OK */
        else if (val == 0xD1) {} /* write output port */
        else if (val == 0xFE) {} /* system reset — ignore */
        g_emu.kbc_status = 0x15; /* output buffer full */
        break;

    /* CMOS/RTC */
    case PORT_CMOS_ADDR:
        g_emu.cmos_addr = val & 0x7F;
        break;
    case PORT_CMOS_DATA:
        g_emu.cmos_data[g_emu.cmos_addr & 0x7F] = val;
        break;

    /* A20 gate */
    case PORT_A20:
        g_emu.a20_enabled = (val & 0x02) != 0;
        break;

    /* PCI */
    case PORT_PCI_ADDR:
        if (size == 4) g_emu.pci_addr = val;
        else if (size == 1) {
            int shift = (port & 3) * 8;
            g_emu.pci_addr = (g_emu.pci_addr & ~(0xFF << shift)) | ((val & 0xFF) << shift);
        }
        break;
    case PORT_PCI_DATA:
    case PORT_PCI_DATA + 1:
    case PORT_PCI_DATA + 2:
    case PORT_PCI_DATA + 3:
        if (g_emu.pci_addr & 0x80000000u) {
            uint8_t bus = (g_emu.pci_addr >> 16) & 0xFF;
            uint8_t dev = (g_emu.pci_addr >> 11) & 0x1F;
            uint8_t fn  = (g_emu.pci_addr >> 8) & 0x07;
            uint8_t reg = g_emu.pci_addr & 0xFC;
            pci_write(bus, dev, fn, reg, val);
        }
        break;

    /* PRISM/MediaGX config */
    case PORT_PRISM_IDX:
        g_emu.prism_idx = val & 0xFF;
        break;
    case PORT_PRISM_DATA:
        if (g_emu.prism_idx == 0xB8) {
            g_emu.gx_base_addr = (val & 0xFF) << 24;
            LOG_ONCE("prism", "GX_BASE set to 0x%08x\n", g_emu.gx_base_addr);
        }
        break;

    /* VGA */
    case PORT_VGA_MISC_W:
        g_emu.vga_misc = val;
        break;
    case PORT_VGA_SEQ_IDX:
        g_emu.vga_seq_idx = val & 7;
        break;
    case PORT_VGA_SEQ_DATA:
        g_emu.vga_seq[g_emu.vga_seq_idx & 7] = val;
        break;
    case PORT_VGA_CRTC_IDX:
        g_emu.vga_crtc_idx = val % 25;
        break;
    case PORT_VGA_CRTC_DATA:
        g_emu.vga_crtc[g_emu.vga_crtc_idx % 25] = val;
        break;
    case PORT_VGA_STATUS:
        g_emu.vga_flipflop = false; /* reset flipflop on write */
        break;

    /* UART */
    case PORT_COM1_BASE ... PORT_COM1_BASE + 7:
        uart_write(port, val & 0xFF);
        break;

    /* LPT */
    case PORT_LPT_DATA:
        g_emu.lpt_data = val;
        break;
    case PORT_LPT_CTRL:
        g_emu.lpt_ctrl = val;
        break;

    /* POST code */
    case PORT_POST:
        g_emu.post_code = val;
        break;

    /* DCS2 status — write ignored */
    case PORT_DCS2_STATUS:
        break;

    default:
        break;
    }
}
