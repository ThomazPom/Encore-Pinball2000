/*
 * bar.c — BAR MMIO handlers for WMS PRISM board + GX_BASE.
 *
 * BAR0 (0x10000000): PLX 9050 register file — SEEPROM, chip selects
 * BAR2 (0x11000000): DCS2 interface — SRAM echo, VSYNC, text display
 * BAR3 (0x12000000): Update flash — Intel 28F320 protocol
 * BAR4 (0x13000000): DCS audio board — word-only command interface
 * GX_BASE (0x40000000): MediaGX DC/GP registers + framebuffer
 */
#include "encore.h"

/* PLX 9050 local config registers — pre-init with sane values */
static void plx_init(void)
{
    uint32_t *r = g_emu.plx_regs;
    r[0x00 >> 2] = 0x905010B5u;  /* PLX device ID */
    r[0x04 >> 2] = 0x02800000u;
    /* LAS0RR/LAS1RR/LAS2RR/LAS3RR — local address space range */
    r[0x18 >> 2] = PLX_BANK0;    /* LAS0 base */
    r[0x2C >> 2] = 0x00000000u;  /* EEPROM control */
    /* Chip select base addresses */
    r[0x28 >> 2] = PLX_BANK0 | 1; /* CS0 → bank0 */
    r[0x30 >> 2] = 0x00000041u;   /* INTCSR */
    r[0x34 >> 2] = 0x00000043u;   /* CNTRL */
    r[0x38 >> 2] = 0x00000000u;   /* SEEPROM data */
}

/* Flash command state (Intel 28F320) */
static int     flash_cmd = 0xFF;
static bool    flash_cmd_active = false;
static uint8_t flash_status = 0x80; /* bit7=ready */

static uint8_t flash_read_byte(uint32_t off)
{
    if (!flash_cmd_active)
        return g_emu.flash ? g_emu.flash[off & (FLASH_SIZE - 1)] : 0xFF;

    switch (flash_cmd) {
    case 0x70: return flash_status;
    case 0x20: case 0x40: case 0x10: case 0xE8:
        return flash_status; /* ready after erase/program */
    case 0x90: /* Read ID */
        switch ((off >> 1) & 0xFF) {
        case 0: return 0x89;  /* Intel */
        case 1: return 0x16;  /* 28F320J3 */
        default: return 0;
        }
    case 0x98: /* CFI */
        switch ((off >> 1) & 0xFF) {
        case 0x10: return 'Q';
        case 0x11: return 'R';
        case 0x12: return 'Y';
        case 0x27: return 0x16; /* 4MB */
        default: return 0;
        }
    default: return 0;
    }
}

void bar_init(void)
{
    plx_init();
    memset(g_emu.bar2_sram, 0, sizeof(g_emu.bar2_sram));
    LOG("bar", "BAR handlers initialized\n");
}

/* ===== MMIO Read Hook ===== */
void bar_mmio_read(uc_engine *uc, uint64_t addr, uint32_t size, void *user_data)
{
    uint32_t val = 0;
    uint32_t a = (uint32_t)addr;

    if (a >= GX_BASE && a < GX_BASE + GX_BASE_SIZE) {
        /* GX_BASE MMIO */
        uint32_t off = a - GX_BASE;

        if (off >= 0x800000 && off < 0xC00000) {
            /* Framebuffer read — pass through (already in mapped memory) */
            return;
        }

        /* DC registers */
        switch (off) {
        case DC_FB_ST_OFFSET:
            val = g_emu.dc_fb_offset;
            break;
        case DC_TIMING2: {
            /* VBLANK simulation: scanline counter wraps at 0xF3 (243).
             * Lines > 0xF0 (240) indicate VBLANK period. */
            static uint16_t s_row = 0;
            s_row = (s_row + 1) % 244;
            val = (uint32_t)s_row | 0x00100000u;
            break;
        }
        case GP_BLT_STATUS:
            val = 0; /* not busy */
            break;
        default:
            if ((off >> 2) < 256)
                val = g_emu.dc_regs[off >> 2];
            break;
        }
    }
    else if (a >= WMS_BAR0 && a < WMS_BAR0 + 0x01000000u) {
        uint32_t off = a - WMS_BAR0;

        if (off < WMS_BAR2 - WMS_BAR0) {
            /* BAR0: PLX 9050 registers */
            if ((off >> 2) < 64)
                val = g_emu.plx_regs[off >> 2];
            else
                val = 0;
        }
        else {
            /* Shouldn't reach here — BAR2-4 have their own hooks */
            val = 0xFF;
        }
    }
    else if (a >= WMS_BAR2 && a < WMS_BAR2 + 0x01000000u) {
        /* BAR2: DCS2 SRAM echo */
        uint32_t off = a - WMS_BAR2;
        if (off < BAR2_SIZE) {
            if (size == 1)
                val = g_emu.bar2_sram[off];
            else if (size == 2)
                val = g_emu.bar2_sram[off] | (g_emu.bar2_sram[off + 1] << 8);
            else
                val = *(uint32_t *)&g_emu.bar2_sram[off];
        } else {
            /* Phase 3 channel scan: return 0xFFFFFFFF (empty sentinel) */
            val = 0xFFFFFFFF;
        }
    }
    else if (a >= WMS_BAR3 && a < WMS_BAR3 + 0x01000000u) {
        /* BAR3: Update flash */
        uint32_t off = a - WMS_BAR3;
        if (size == 1)
            val = flash_read_byte(off);
        else if (size == 2)
            val = flash_read_byte(off) | (flash_read_byte(off + 1) << 8);
        else
            val = flash_read_byte(off) | (flash_read_byte(off + 1) << 8) |
                  (flash_read_byte(off + 2) << 16) | (flash_read_byte(off + 3) << 24);
    }
    else if (a >= WMS_BAR4 && a < WMS_BAR4 + BAR4_SIZE) {
        /* BAR4: DCS audio — word-only reads at offsets 0 and 2 */
        uint32_t off = a - WMS_BAR4;
        if (off == 0 || off == 2) {
            /* Return command acknowledgment or status */
            val = 0x0000; /* idle/ready */
        } else {
            val = 0;
        }
    }

    /* Write the value back to memory so the CPU reads it */
    if (size == 1) {
        uint8_t v8 = val & 0xFF;
        uc_mem_write(uc, addr, &v8, 1);
    } else if (size == 2) {
        uint16_t v16 = val & 0xFFFF;
        uc_mem_write(uc, addr, &v16, 2);
    } else {
        uc_mem_write(uc, addr, &val, 4);
    }
}

/* ===== MMIO Write Hook ===== */
void bar_mmio_write(uc_engine *uc, uint64_t addr, uint32_t size,
                    uint64_t value, void *user_data)
{
    uint32_t a = (uint32_t)addr;
    uint32_t val = (uint32_t)value;

    if (a >= GX_BASE && a < GX_BASE + GX_BASE_SIZE) {
        uint32_t off = a - GX_BASE;

        if (off >= 0x800000 && off < 0xC00000) {
            /* Framebuffer write — pass through */
            return;
        }

        switch (off) {
        case DC_UNLOCK:
            /* DC unlock register — game writes 0x4758 to unlock DC regs */
            break;
        case DC_FB_ST_OFFSET:
            g_emu.dc_fb_offset = val;
            break;
        case DC_TIMING2:
            g_emu.dc_timing2 = val;
            break;
        case GP_BLT_STATUS:
            /* BLT command trigger — immediate completion */
            break;
        default:
            if ((off >> 2) < 256)
                g_emu.dc_regs[off >> 2] = val;
            break;
        }
    }
    else if (a >= WMS_BAR0 && a < WMS_BAR2) {
        /* BAR0: PLX 9050 registers */
        uint32_t off = a - WMS_BAR0;
        if ((off >> 2) < 64) {
            g_emu.plx_regs[off >> 2] = val;

            static int s_plx_log = 0;
            if (s_plx_log < 30) {
                s_plx_log++;
                LOG("plx", "WR off=0x%03x val=0x%08x\n", off, val);
            }
        }
    }
    else if (a >= WMS_BAR2 && a < WMS_BAR3) {
        /* BAR2: DCS2 SRAM echo write */
        uint32_t off = a - WMS_BAR2;
        g_emu.bar2_wr_count++;

        if (off < BAR2_SIZE) {
            if (size == 1)
                g_emu.bar2_sram[off] = val & 0xFF;
            else if (size == 2) {
                g_emu.bar2_sram[off] = val & 0xFF;
                g_emu.bar2_sram[off + 1] = (val >> 8) & 0xFF;
            } else {
                *(uint32_t *)&g_emu.bar2_sram[off] = val;
            }
        }

        /* Text display: printable chars at SRAM offsets are char display data */
        if (size == 1 && val >= 0x20 && val < 0x7F && off >= 0x200) {
            static char s_textbuf[128];
            static int s_textpos = 0;
            s_textbuf[s_textpos++] = (char)val;
            if (s_textpos >= 127 || val == '\n') {
                s_textbuf[s_textpos] = '\0';
                if (s_textpos > 0) LOG("vid", "%s\n", s_textbuf);
                s_textpos = 0;
            }
        }

        /* Log early BAR2 writes for debugging */
        if (g_emu.bar2_wr_count <= 30) {
            LOG("bar2", "WR #%u off=0x%x sz=%d val=0x%x\n",
                g_emu.bar2_wr_count, off, size, val);
        }
    }
    else if (a >= WMS_BAR3 && a < WMS_BAR4) {
        /* BAR3: Flash write — Intel command protocol */
        uint8_t cmd = val & 0xFF;
        switch (cmd) {
        case 0xFF: flash_cmd = 0xFF; flash_cmd_active = false; break;
        case 0x70: flash_cmd = 0x70; flash_cmd_active = true; break;
        case 0x90: flash_cmd = 0x90; flash_cmd_active = true; break;
        case 0x98: flash_cmd = 0x98; flash_cmd_active = true; break;
        case 0x20: flash_cmd = 0x20; flash_cmd_active = true; break;
        case 0x40: case 0x10:
            flash_cmd = cmd; flash_cmd_active = true; break;
        case 0xD0:
            /* Confirm erase/program → ready */
            flash_status = 0x80;
            flash_cmd = 0x70;
            break;
        case 0x50: /* Clear status */
            flash_status = 0x80;
            break;
        default:
            break;
        }
    }
    else if (a >= WMS_BAR4 && a < WMS_BAR4 + BAR4_SIZE) {
        /* BAR4: DCS audio — word-only writes at offsets 0 and 2 */
        uint32_t off = a - WMS_BAR4;
        if (size == 2 || size == 1) {
            if (off == 0) {
                g_emu.dcs_latch = val & 0xFFFF;
            } else if (off == 2) {
                /* Full 16-bit command = (latch << 16) | val, but DCS uses
                 * simple 16-bit commands written to offset 2 */
                uint16_t cmd = val & 0xFFFF;

                /* Enqueue in circular buffer */
                DCSCmdBuf *cb = &g_emu.dcs_cmds;
                if (cb->count < DCS_CMD_BUF_SIZE) {
                    cb->buf[cb->tail] = cmd;
                    cb->tail = (cb->tail + 1) % DCS_CMD_BUF_SIZE;
                    cb->count++;
                }

                /* Process immediately */
                sound_process_cmd(cmd);

                static int s_dcs_log = 0;
                if (s_dcs_log < 50) {
                    s_dcs_log++;
                    LOG("dcs", "cmd=0x%04x (latch=0x%04x)\n", cmd, g_emu.dcs_latch);
                }
            }
        }
    }
}
