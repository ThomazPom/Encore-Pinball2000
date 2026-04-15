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

/* ===== 93C46 SEEPROM bit-bang state machine =====
 * Copycat of x64 POC (cpu_backend_qemu.c) + i386 POC (bar4.c).
 * PLX 9050 CNTRL register (0x50) bit assignments:
 *   Bit 24 = EESK  (serial clock, driven by host)
 *   Bit 25 = EECS  (chip select, driven by host)
 *   Bit 26 = EEDI  (data in, host→EEPROM)
 *   Bit 27 = EEDO  (data out, EEPROM→host, read-only)
 *   Bit 28 = EEPRESENT (read-only, always 1)
 */
static struct {
    int      state;      /* 0=idle, 1=opcode, 2=address, 3=data_out, 4=data_in */
    int      bits;       /* bit counter within current state */
    int      opcode;     /* 2-bit opcode (10=read, 01=write) */
    int      addr;       /* 6-bit address (0-63) */
    uint16_t shift;      /* shift register for data I/O */
    int      do_bit;     /* EEDO output bit (data out to host) */
    int      last_sk;    /* previous clock state for edge detection */
} s_ee;

/* Pre-populate SEEPROM with PLX 9050 PRISM configuration.
 * PLX 9050 serial EEPROM layout (93C46 format, 16-bit words):
 *   Words 0-5: PCI identification
 *   Words 6+: Local config registers (pairs: low half, high half)
 * Values from x64 POC (proven working for plx_ee_verify). */
static void seeprom_init_default(void)
{
    for (int i = 0; i < 64; i++)
        g_emu.seeprom[i] = 0xFFFF;

    g_emu.seeprom[0]  = 0x9050;       /* Device ID (PLX) */
    g_emu.seeprom[1]  = 0x10B5;       /* Vendor ID (PLX) */
    g_emu.seeprom[2]  = 0x0680;       /* Class code upper */
    g_emu.seeprom[3]  = 0x0001;       /* Class/Rev */
    g_emu.seeprom[4]  = 0x1078;       /* Subsystem vendor (Cyrix/WMS board) */
    g_emu.seeprom[5]  = 0x0001;       /* Subsystem device (PRISM) */
    /* Reg 0x00 (LAS0RR) = 0x0FFE0000 */
    g_emu.seeprom[6]  = 0x0000; g_emu.seeprom[7]  = 0x0FFE;
    /* Reg 0x04 (LAS1RR) = 0x0F800000 */
    g_emu.seeprom[8]  = 0x0000; g_emu.seeprom[9]  = 0x0F80;
    /* Reg 0x08 (LAS2RR) = 0x0FFF8000 */
    g_emu.seeprom[10] = 0x8000; g_emu.seeprom[11] = 0x0FFF;
    /* Reg 0x0C (LAS3RR) = 0x0C000008 */
    g_emu.seeprom[12] = 0x0008; g_emu.seeprom[13] = 0x0C00;
    /* Reg 0x10 (EROMRR) = 0x0FFF8000 */
    g_emu.seeprom[14] = 0x8000; g_emu.seeprom[15] = 0x0FFF;
    /* Reg 0x14 (LAS0BA) = 0x00100001 */
    g_emu.seeprom[16] = 0x0001; g_emu.seeprom[17] = 0x0010;
    /* Reg 0x18 (LAS1BA) = 0x01000001 */
    g_emu.seeprom[18] = 0x0001; g_emu.seeprom[19] = 0x0100;
    /* Reg 0x1C (LAS2BA) = 0x00000001 */
    g_emu.seeprom[20] = 0x0001; g_emu.seeprom[21] = 0x0000;
    /* Reg 0x20 (LAS3BA) = 0x08000001 */
    g_emu.seeprom[22] = 0x0001; g_emu.seeprom[23] = 0x0800;
    /* Reg 0x24 (EROMBA) = 0x08000000 */
    g_emu.seeprom[24] = 0x0000; g_emu.seeprom[25] = 0x0800;
    /* Reg 0x28 (CS0/MARBR) = 0x5403A1E0 */
    g_emu.seeprom[26] = 0xA1E0; g_emu.seeprom[27] = 0x5403;
    /* Reg 0x2C (LMISC/BTERM) = 0x5473B940 */
    g_emu.seeprom[28] = 0xB940; g_emu.seeprom[29] = 0x5473;
    /* Reg 0x30 = 0x4041A060 */
    g_emu.seeprom[30] = 0xA060; g_emu.seeprom[31] = 0x4041;
    /* Reg 0x34 = 0x54B2B8C0 */
    g_emu.seeprom[32] = 0xB8C0; g_emu.seeprom[33] = 0x54B2;
    /* Reg 0x38 = 0x54B2B8C0 (same as 0x34) */
    g_emu.seeprom[34] = 0xB8C0; g_emu.seeprom[35] = 0x54B2;
    /* Reg 0x3C (CS0 base) = 0x08800001 */
    g_emu.seeprom[36] = 0x0001; g_emu.seeprom[37] = 0x0880;
    /* Reg 0x40 (CS1 base) = 0x09800001 */
    g_emu.seeprom[38] = 0x0001; g_emu.seeprom[39] = 0x0980;
    /* Reg 0x44 (CS2 base) = 0x0A800001 */
    g_emu.seeprom[40] = 0x0001; g_emu.seeprom[41] = 0x0A80;
    /* Reg 0x48 (CS3 base) = 0x0B800001 */
    g_emu.seeprom[42] = 0x0001; g_emu.seeprom[43] = 0x0B80;
    /* Reg 0x4C (INTCSR) = 0x00000000 */
    g_emu.seeprom[44] = 0x0000; g_emu.seeprom[45] = 0x0000;
    /* CNTRL (reg 0x50) = 0x40789242 */
    g_emu.seeprom[46] = 0x9242; g_emu.seeprom[47] = 0x4078;

    s_ee.do_bit = 1;  /* pull-up default */
    LOG("plx", "EEPROM pre-populated with PRISM config (48 words)\n");
}

/* Process CNTRL register write for SEEPROM bit-bang */
static void seeprom_process_write(uint32_t cntrl_val)
{
    int sk = (cntrl_val >> 24) & 1;  /* EESK */
    int cs = (cntrl_val >> 25) & 1;  /* EECS */
    int di = (cntrl_val >> 26) & 1;  /* EEDI */

    if (!cs) {
        /* CS deasserted → reset state machine */
        s_ee.state = 0;
        s_ee.bits = 0;
        s_ee.do_bit = 1;  /* float high when deselected */
        s_ee.last_sk = sk;
        return;
    }

    /* Process on rising edge of SK only */
    if (sk && !s_ee.last_sk) {
        switch (s_ee.state) {
        case 0: /* Waiting for start bit (DI=1) */
            if (di) {
                s_ee.state = 1;
                s_ee.bits = 0;
                s_ee.opcode = 0;
            }
            break;
        case 1: /* Reading 2-bit opcode */
            s_ee.opcode = (s_ee.opcode << 1) | di;
            s_ee.bits++;
            if (s_ee.bits >= 2) {
                s_ee.state = 2;
                s_ee.bits = 0;
                s_ee.addr = 0;
            }
            break;
        case 2: /* Reading 6-bit address */
            s_ee.addr = (s_ee.addr << 1) | di;
            s_ee.bits++;
            if (s_ee.bits >= 6) {
                s_ee.addr &= 63;
                if (s_ee.opcode == 2) { /* READ (opcode 10) */
                    s_ee.state = 3;
                    s_ee.bits = 0;
                    s_ee.shift = g_emu.seeprom[s_ee.addr];
                    s_ee.do_bit = 0;  /* dummy "0" bit before data */
                    LOG("ee", "READ addr=%d → 0x%04x\n", s_ee.addr, s_ee.shift);
                } else if (s_ee.opcode == 1) { /* WRITE (opcode 01) */
                    s_ee.state = 4;
                    s_ee.bits = 0;
                    s_ee.shift = 0;
                    LOG("ee", "WRITE addr=%d start\n", s_ee.addr);
                } else {
                    LOG("ee", "MISC op=%d addr=%d\n", s_ee.opcode, s_ee.addr);
                    /* EWEN/EWDS/ERAL/WRAL — simplified: acknowledge */
                    s_ee.state = 0;
                    s_ee.do_bit = 1;
                }
            }
            break;
        case 3: /* Data out (READ) — 16 bits MSB first */
            s_ee.do_bit = (s_ee.shift >> (15 - s_ee.bits)) & 1;
            s_ee.bits++;
            if (s_ee.bits >= 16) {
                /* Auto-increment address for sequential reads */
                s_ee.addr = (s_ee.addr + 1) & 63;
                s_ee.shift = g_emu.seeprom[s_ee.addr];
                s_ee.bits = 0;
                s_ee.do_bit = (s_ee.shift >> 15) & 1;
            }
            break;
        case 4: /* Data in (WRITE) — 16 bits MSB first */
            s_ee.shift = (s_ee.shift << 1) | di;
            s_ee.bits++;
            if (s_ee.bits >= 16) {
                g_emu.seeprom[s_ee.addr] = s_ee.shift;
                LOG("ee", "WRITE addr=%d ← 0x%04x\n", s_ee.addr, s_ee.shift);
                s_ee.state = 0;
                s_ee.do_bit = 1;  /* ready */
            }
            break;
        }
    }
    s_ee.last_sk = sk;
}

/* PLX 9050 local config registers — pre-init matching real PRISM hardware */
static void plx_init(void)
{
    uint32_t *r = g_emu.plx_regs;

    /* Range registers (address decode masks, 16MB windows) */
    r[0x00 >> 2] = 0xFF000000u;  /* LAS0RR */
    r[0x04 >> 2] = 0xFF000000u;  /* LAS1RR */
    r[0x08 >> 2] = 0xFF000000u;  /* LAS2RR */
    r[0x0C >> 2] = 0xFF000000u;  /* LAS3RR */
    r[0x10 >> 2] = 0x00000000u;  /* EROMRR */

    /* Local address space base addresses (bit 0 = decode enable) */
    r[0x14 >> 2] = 0x00100001u;  /* LAS0BA → 0x00100000 (DCS image window) */
    r[0x18 >> 2] = 0x01000001u;  /* LAS1BA → 0x01000000 */
    r[0x1C >> 2] = 0x00000001u;  /* LAS2BA → 0x00000000 */
    r[0x20 >> 2] = 0x08000001u;  /* LAS3BA → bank0 @ 0x08000000 */
    r[0x24 >> 2] = 0x00000000u;  /* EROMBA */

    /* Bus region descriptors */
    r[0x28 >> 2] = 0x00000000u;  /* LAS0BRD */
    r[0x2C >> 2] = 0x00000000u;  /* LAS1BRD */
    r[0x30 >> 2] = 0x00000000u;  /* LAS2BRD */
    r[0x34 >> 2] = 0x00000000u;  /* LAS3BRD */
    r[0x38 >> 2] = 0x00000000u;  /* EROMBRD */

    /* Chip-select base addresses (bit 0 = enable) */
    r[0x3C >> 2] = PLX_BANK1 | 1;   /* CS0 → bank1 @ 0x08800000 */
    r[0x40 >> 2] = PLX_BANK2 | 1;   /* CS1 → bank2 @ 0x09800000 */
    r[0x44 >> 2] = PLX_BANK3 | 1;   /* CS2 → bank3 @ 0x0A800000 */
    r[0x48 >> 2] = PLX_CS3_DCS | 1; /* CS3 → DCS  @ 0x0B800000 */

    /* Interrupt and control */
    r[0x4C >> 2] = 0x00000000u;  /* INTCSR — interrupts disabled */
    r[0x50 >> 2] = 0x00000000u;  /* CNTRL */
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
        case 0x10: return 0x51;  /* 'Q' */
        case 0x11: return 0x52;  /* 'R' */
        case 0x12: return 0x59;  /* 'Y' */
        case 0x13: return 0x01;  /* primary vendor: Intel */
        case 0x14: return 0x00;
        case 0x15: return 0x31;  /* primary table address */
        case 0x16: return 0x00;
        case 0x1B: return 0x45;  /* Vcc min */
        case 0x1C: return 0x55;  /* Vcc max */
        case 0x1F: return 0x07;  /* typical word program timeout: 2^7=128µs */
        case 0x21: return 0x0A;  /* typical block erase timeout: 2^10=1024ms */
        case 0x27: return 0x16;  /* device size = 2^22 = 4MB */
        case 0x28: return 0x02;  /* interface: x16 */
        case 0x29: return 0x00;
        case 0x2A: return 0x05;  /* max write buffer = 2^5 = 32 bytes */
        case 0x2B: return 0x00;
        case 0x2C: return 0x01;  /* 1 erase block region */
        case 0x2D: return 0x1F;  /* 32-1=31 blocks, low byte */
        case 0x2E: return 0x00;  /* blocks-1, high byte */
        case 0x2F: return 0x00;  /* block size/256 = 0x200, low byte */
        case 0x30: return 0x02;  /* block size/256, high byte */
        default: return 0;
        }
    default: return 0;
    }
}

/* Enqueue a response into the DCS2 output buffer */
static void dcs_enqueue_response(uint16_t val)
{
    DCSRespBuf *rb = &g_emu.dcs_resp;
    if (rb->count < DCS_RESP_BUF_SIZE) {
        rb->buf[rb->tail] = val;
        rb->tail = (rb->tail + 1) % DCS_RESP_BUF_SIZE;
        rb->count++;
    }
}

void bar_init(void)
{
    seeprom_init_default();  /* pre-populate before .see file may override */
    plx_init();
    memset(g_emu.bar2_sram, 0, sizeof(g_emu.bar2_sram));

    /* Seed BC_DRAM_TOP at GX_BASE+0x20000 = 0x007FFFFF (8MB-1)
     * Matches P2K-driver at 0x8058257 — tells BIOS how much RAM is installed */
    g_emu.gx_regs[0x20000 >> 2] = 0x007FFFFF;

    LOG("bar", "BAR handlers initialized\n");
}

/* Re-apply SEEPROM PLX config after savedata may have loaded stale .see file */
void bar_seeprom_reinit(void)
{
    seeprom_init_default();
}

/* ===== MMIO Read Hook ===== */
void bar_mmio_read(uc_engine *uc, uc_mem_type type, uint64_t addr, int size, int64_t value, void *user_data)
{
    uint32_t val = 0;
    uint32_t a = (uint32_t)addr;
    static int mmio_log_cnt = 0;
    if (mmio_log_cnt < 20 && a >= WMS_BAR0 && a < WMS_BAR0 + 0x01000000u) {
        mmio_log_cnt++;
        LOG("mmio", "READ addr=0x%08x size=%d\n", a, size);
    }

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
            /* VBLANK simulation: counter 0-243, guest checks (val&0x7FF)>0xF0
             * to detect VBLANK. No extra flag bits — copycat of i386 POC (BT-109). */
            static uint32_t s_row = 0;
            val = s_row++;
            if (s_row > 0xF3) s_row = 0;
            break;
        }
        case GP_BLT_STATUS:
            val = 0x300; /* pipeline idle + ready (BT-106) */
            break;
        default:
            if ((off >> 2) < 0x9000)
                val = g_emu.gx_regs[off >> 2];
            break;
        }
    }
    else if (a >= WMS_BAR0 && a < WMS_BAR0 + 0x01000000u) {
        uint32_t off = a - WMS_BAR0;
        static int plx_rd_cnt = 0;
        if (off < 0x60 && plx_rd_cnt < 100) {
            plx_rd_cnt++;
            LOG("plx", "RD off=0x%02x size=%u\n", off, size);
        }

        if (off < WMS_BAR2 - WMS_BAR0) {
            /* BAR0: PLX 9050 registers */
            if ((off >> 2) < 64) {
                val = g_emu.plx_regs[off >> 2];

                /* Special read behaviors (from i386/x64 POC) */
                if (off == 0x4C) {
                    /* INTCSR: force bit 2 clear, bit 5 set (PLX ready) */
                    val = (val & ~0x04u) | 0x20u;
                }
                else if (off == 0x50) {
                    /* CNTRL: replace DO bit(27) with emulated, force EE_Present(28) */
                    val = (val & ~0x18000000u) | 0x10000000u; /* bit28=present */
                    if (s_ee.do_bit)
                        val |= 0x08000000u; /* bit27=EEDO */
                }
            } else {
                val = 0;
            }
        }
        else {
            val = 0xFF;
        }
    }
    else if (a >= WMS_BAR2 && a < WMS_BAR2 + 0x01000000u) {
        /* BAR2: DCS2 SRAM echo */
        uint32_t off = a - WMS_BAR2;

        /* Phase 3 start trigger: offset 0x21C0 DWORD read (V1.12 only) */
        if (off == 0x21C0 && size == 4 && !g_emu.is_v19_update) {
            static bool s_phase3_done = false;
            if (!s_phase3_done) {
                s_phase3_done = true;
                LOG("bar2", "Phase 3 start (V1.12) — applying RAM patches\n");
                /* V1.12-specific patches would go here */
            }
        }

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
        /* BAR4: DCS audio — word-only reads */
        uint32_t off = a - WMS_BAR4;
        if (off == 0) {
            /* Data/command read: retrieve response from output buffer */
            DCSRespBuf *rb = &g_emu.dcs_resp;
            if (rb->count > 0) {
                val = rb->buf[rb->head];
                rb->head = (rb->head + 1) % DCS_RESP_BUF_SIZE;
                rb->count--;
            } else {
                val = 0;
            }
        } else if (off == 2) {
            /* Status/flags read: bit 7 = data available */
            uint16_t f = g_emu.dcs_flags;
            if (g_emu.dcs_resp.count > 0)
                f |= 0x80;
            else
                f &= ~0x80u;
            val = f;
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
void bar_mmio_write(uc_engine *uc, uc_mem_type type, uint64_t addr, int size,
                    int64_t value, void *user_data)
{
    uint32_t a = (uint32_t)addr;
    uint32_t val = (uint32_t)value;
    static int mmio_wr_cnt = 0;
    if (mmio_wr_cnt < 20 && a >= WMS_BAR0 && a < WMS_BAR0 + 0x01000000u) {
        mmio_wr_cnt++;
        LOG("mmio", "WRITE addr=0x%08x size=%d val=0x%08x\n", a, size, val);
    }

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
            if ((off >> 2) < 0x9000)
                g_emu.gx_regs[off >> 2] = val;
            break;
        }
    }
    else if (a >= WMS_BAR0 && a < WMS_BAR2) {
        /* BAR0: PLX 9050 registers */
        uint32_t off = a - WMS_BAR0;
        if ((off >> 2) < 64) {
            g_emu.plx_regs[off >> 2] = val;

            /* SEEPROM bit-bang on CNTRL register (0x50) */
            if (off == 0x50)
                seeprom_process_write(val);

            /* Track chip-select writes */
            if (off >= 0x3C && off <= 0x48) {
                int cs_num = (off - 0x3C) / 4;
                uint32_t base = val & ~1u;
                LOG("plx", "CS%d = 0x%08x (enable=%d)\n", cs_num, base, val & 1);
            }
            else if (off == 0x20) {
                LOG("plx", "LAS3BA = 0x%08x (bank0)\n", val & ~1u);
            }

            static int s_plx_log = 0;
            if (s_plx_log < 40) {
                s_plx_log++;
                LOG("plx", "WR off=0x%02x val=0x%08x\n", off, val);
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

        /* XINU boot detection: first byte write at offset ≥ 0x1b14 = framebuffer
         * write = XINU has booted and is rendering text. Activate LPT. */
        if (size == 1 && off >= 0x1b14 && !g_emu.lpt_active) {
            LOG("bar2", "XINU boot detected (first BAR2 write at off=0x%x) — activating LPT\n", off);
            lpt_activate();
        }

        /* DCS2 Phase completion intercepts (V1.12 only) */
        if (!g_emu.is_v19_update) {
            /* Offset 0x50 byte write = 0xFF: auto-ack DCS2 completion */
            if (off == 0x50 && size == 1 && (val & 0xFF) == 0xFF) {
                g_emu.bar2_sram[0x50] = 0x00; /* auto-clear ack */
                LOG("bar2", "DCS2 completion ack (V1.12) — patching RAM\n");
                /* Write 0 at 0x002797C4, 1 at 0x002C927C */
                uint32_t zero = 0, one = 1;
                uc_mem_write(g_emu.uc, 0x002797C4, &zero, 4);
                uc_mem_write(g_emu.uc, 0x002C927C, &one, 4);
            }
            /* Offset 0x1C DWORD write ≥ 8: DCS2 channel count ready */
            if (off == 0x1C && size == 4 && val >= 8) {
                uint32_t zero = 0;
                uc_mem_write(g_emu.uc, 0x002797C4, &zero, 4);
                LOG("bar2", "DCS2 channels=%u (V1.12) — patching ready\n", val);
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
        /* BAR3: Flash write — Intel 28F320 command protocol.
         *
         * Key fix: ALL command writes must set flash_cmd_active=true so that
         * subsequent reads return status (0x80 = ready) rather than raw array
         * data (0xFF = all error bits set). Missing flash_cmd_active updates
         * on 0x60, 0xD0, and 0x50 caused "flash_wait: device blocked" Fatals. */
        uint8_t cmd = val & 0xFF;
        uint32_t foff = a - WMS_BAR3;
        switch (cmd) {
        case 0xFF: /* Read array */
            flash_cmd = 0xFF; flash_cmd_active = false; break;
        case 0x70: flash_cmd = 0x70; flash_cmd_active = true; break;
        case 0x90: flash_cmd = 0x90; flash_cmd_active = true; break;
        case 0x98: flash_cmd = 0x98; flash_cmd_active = true; break;
        case 0x20: /* Block erase setup */
            flash_cmd = 0x20; flash_cmd_active = true; break;
        case 0x40: case 0x10: /* Byte/halfword program setup */
            flash_cmd = cmd; flash_cmd_active = true; break;
        case 0x60: /* Block lock/unlock setup */
            flash_cmd = 0x60; flash_cmd_active = true; break;
        case 0x01: /* Lock-block confirm (after 0x60) */
        case 0xD0: /* Unlock-block confirm OR erase/program confirm */
            flash_status = 0x80;
            flash_cmd = 0x70;   /* switch to read-status mode */
            flash_cmd_active = true;
            break;
        case 0x50: /* Clear status register */
            flash_status = 0x80;
            flash_cmd = 0x70;   /* stay in status mode after clear */
            flash_cmd_active = true;
            break;
        default:
            /* Data write during byte/halfword program sequence */
            if (flash_cmd_active &&
                (flash_cmd == 0x40 || flash_cmd == 0x10)) {
                /* Store data into flash array */
                if (g_emu.flash && foff < FLASH_SIZE) {
                    if (size == 1)
                        g_emu.flash[foff] = (uint8_t)val;
                    else if (size == 2 && foff + 1 < FLASH_SIZE)
                        g_emu.flash[foff] = val & 0xFF,
                        g_emu.flash[foff+1] = (val >> 8) & 0xFF;
                    else if (foff + 3 < FLASH_SIZE)
                        g_emu.flash[foff]   = val & 0xFF,
                        g_emu.flash[foff+1] = (val >> 8) & 0xFF,
                        g_emu.flash[foff+2] = (val >> 16) & 0xFF,
                        g_emu.flash[foff+3] = (val >> 24) & 0xFF;
                }
                flash_status = 0x80;   /* program success */
                /* stay in flash_cmd=0x40 status mode */
            }
            break;
        }
    }
    else if (a >= WMS_BAR4 && a < WMS_BAR4 + BAR4_SIZE) {
        /* BAR4: DCS audio — word-only writes */
        uint32_t off = a - WMS_BAR4;
        if (off == 0) {
            /* Command/data write */
            uint16_t cmd = val & 0xFFFF;

            /* If in multi-word mode, accumulate */
            if (g_emu.dcs_pending && g_emu.dcs_remaining > 0) {
                if (g_emu.dcs_layer < 4)
                    g_emu.dcs_mixer[g_emu.dcs_layer++] = cmd;
                g_emu.dcs_remaining--;
                if (g_emu.dcs_remaining == 0) {
                    /* Multi-word command complete — dispatch to sound */
                    if (g_emu.dcs_mixer[0] == 999 || g_emu.dcs_mixer[0] == 1000) {
                        dcs_enqueue_response(0x100);
                        dcs_enqueue_response(0x10);
                    } else {
                        sound_process_cmd(g_emu.dcs_mixer[0]);
                    }
                    g_emu.dcs_layer = 0;
                    g_emu.dcs_remaining = 0;
                    memset(g_emu.dcs_mixer, 0, sizeof(g_emu.dcs_mixer));
                }
                goto dcs_write_done;
            }

            static int s_dcs_log = 0;
            if (s_dcs_log < 80) {
                s_dcs_log++;
                LOG("dcs", "cmd=0x%04x\n", cmd);
            }

            /* Command dispatch */
            switch (cmd) {
            case 0x3A:
                dcs_enqueue_response(0xCC01); /* board present */
                break;
            case 0x1B:
                dcs_enqueue_response(0xCC09);
                break;
            case 0xAA:
                dcs_enqueue_response(0xCC04);
                break;
            case 0x0E:
                g_emu.dcs_pending = false;
                break;
            case 0xACE1:
                g_emu.dcs_pending = true;
                dcs_enqueue_response(0x0100);
                dcs_enqueue_response(0x0C);
                break;
            default:
                /* For unknown commands, try sound processing */
                sound_process_cmd(cmd);
                break;
            }
        } else if (off == 2) {
            /* Flags write — store flags value */
            g_emu.dcs_flags = val & 0xFFFF;
        }
dcs_write_done: ;
    }
}
