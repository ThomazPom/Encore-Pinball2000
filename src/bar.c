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

/* ===== DCS2 serial bit-bang via PLX CNTRL register (0x50) =====
 * Reverse-engineered from P2K-driver objdump (UpdateSeepromState @ 0x805c159,
 * ProcessSeepromData @ 0x805cf0d) and i386 POC (bar4.c).
 *
 * PLX CNTRL register (0x50) bit assignments for DCS2 serial:
 *   bit 24 = clock (rising edge shifts one data bit)
 *   bit 25 = enable (low→high = new sequence; low = reset)
 *   bit 26 = data bit (shifted into command byte on clock)
 *   bit 27 = output data (set when DCS2 response ready)
 *
 * The SEEPROM protocol uses the SAME bits (24-26) for EEPROM access.
 * Both protocols coexist — the DCS serial state machine runs alongside. */
static struct {
    int enable_prev, clock_prev;
    int cmd_received, byte_complete;
    int bit_cnt;
    uint8_t shift_reg;
    uint8_t data_addr;
    int param;
    int audio_active, audio_ack, audio_ready;
    int cntrl_bit1, cntrl_bit2;
} s_dcs_serial;

static void dcs_serial_shift_bit(int data_bit)
{
    int pos = 7 - ((s_dcs_serial.bit_cnt - 1) & 7);
    s_dcs_serial.shift_reg = (s_dcs_serial.shift_reg & ~(1 << pos)) |
                             ((data_bit & 1) << pos);
}

static void dcs_serial_process_command(int continuing)
{
    int cls = (s_dcs_serial.param >> 6) & 3;

    switch (cls) {
    case 0: {
        int sub_type = (s_dcs_serial.param >> 4) & 3;
        switch (sub_type) {
        case 0:
            s_dcs_serial.audio_active = 0;
            if (s_dcs_serial.cntrl_bit1 && s_dcs_serial.cntrl_bit2) return;
            s_dcs_serial.audio_ready = 0;
            return;
        case 1:
            s_dcs_serial.audio_active = 0;
            return;
        case 3:
            s_dcs_serial.audio_active = 0;
            if (s_dcs_serial.cntrl_bit1 && s_dcs_serial.cntrl_bit2) return;
            s_dcs_serial.audio_ready = 1;
            LOG("dcs-serial", "cmd=0x%02x → READY flag set!\n", s_dcs_serial.param);
            return;
        default:
            return;
        }
    }
    case 1:
        s_dcs_serial.audio_active = 0;
        if (s_dcs_serial.cntrl_bit1 && s_dcs_serial.cntrl_bit2) return;
        if (!continuing) s_dcs_serial.data_addr = (uint8_t)(s_dcs_serial.param & 0x3F);
        return;
    case 2:
        s_dcs_serial.audio_active = 1;
        if (continuing) return;
        s_dcs_serial.data_addr = (uint8_t)(s_dcs_serial.param & 0x3F);
        s_dcs_serial.audio_ack = 0;
        return;
    case 3:
        s_dcs_serial.audio_active = 0;
        return;
    }
}

static void dcs_serial_process_plx50(uint32_t val)
{
    int enable = (val >> 25) & 1;
    int clock  = (val >> 24) & 1;
    int data   = (val >> 26) & 1;

    if (!enable) {
        s_dcs_serial.cmd_received  = 0;
        s_dcs_serial.byte_complete = 0;
        s_dcs_serial.shift_reg     = 0;
        s_dcs_serial.bit_cnt       = 0;
        s_dcs_serial.audio_active  = 0;
        s_dcs_serial.data_addr     = 0;
        s_dcs_serial.audio_ack     = 0;
    }

    if (!s_dcs_serial.enable_prev && enable) {
        s_dcs_serial.byte_complete = 0;
        s_dcs_serial.param         = 0;
        s_dcs_serial.shift_reg     = 0;
        s_dcs_serial.cmd_received  = 0;
        s_dcs_serial.audio_active  = 0;
        s_dcs_serial.data_addr     = 0;
        s_dcs_serial.audio_ack     = 0;
    }

    if (!s_dcs_serial.clock_prev && clock) {
        if (!s_dcs_serial.cmd_received) {
            if (data) s_dcs_serial.cmd_received = 1;
        } else if (!s_dcs_serial.byte_complete) {
            dcs_serial_shift_bit(data);
            s_dcs_serial.bit_cnt++;
            if (s_dcs_serial.bit_cnt == 8) {
                s_dcs_serial.bit_cnt = 0;
                s_dcs_serial.byte_complete = 1;
                s_dcs_serial.param = (int)s_dcs_serial.shift_reg;
                LOG("dcs-serial", "byte assembled: 0x%02x (cls=%d sub=0x%02x)\n",
                    s_dcs_serial.param,
                    (s_dcs_serial.param >> 6) & 3,
                    s_dcs_serial.param & 0x3f);
                dcs_serial_process_command(0);
            }
        } else {
            dcs_serial_process_command(1);
        }
    }

    s_dcs_serial.enable_prev = enable;
    s_dcs_serial.clock_prev  = clock;
    s_dcs_serial.cntrl_bit1  = (val >> 1) & 1;
    s_dcs_serial.cntrl_bit2  = (val >> 2) & 1;
}

static uint32_t dcs_serial_get_plx50_bits(void)
{
    uint32_t bits = 0;
    if (s_dcs_serial.audio_active) {
        if (s_dcs_serial.audio_ack)
            bits |= 0x08000000u;  /* bit 27 = data ready */
        else
            s_dcs_serial.audio_ack = 1;
    }
    if (s_dcs_serial.audio_ready)
        bits |= 0x08000000u;  /* bit 27 = DCS ready (init response) */
    return bits;
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

    /* Dedicated BAR2/BAR4 access counters - never throttled */
    static uint32_t bar2_rd_total = 0, bar4_rd_total = 0;
    if (a >= WMS_BAR2 && a < WMS_BAR2 + 0x01000000u) {
        if (++bar2_rd_total <= 10)
            LOG("BAR2", "READ #%u addr=0x%08x size=%d\n", bar2_rd_total, a, size);
    }
    if (a >= WMS_BAR4 && a < WMS_BAR4 + BAR4_SIZE) {
        if (++bar4_rd_total <= 10)
            LOG("BAR4", "READ #%u addr=0x%08x size=%d\n", bar4_rd_total, a, size);
    }

    if (a >= GX_BASE && a < GX_BASE + GX_BASE_SIZE) {
        /* GX_BASE MMIO */
        uint32_t off = a - GX_BASE;

        if (off >= 0x800000 && off < 0xC00000) {
            /* Framebuffer read — let Unicorn return what's in GX_BASE memory.
             * GP BLT and display_update both use GX_BASE+0x800000 directly. */
            return;
        }

        /* DC registers */
        switch (off) {
        case DC_FB_ST_OFFSET:
            val = g_emu.dc_fb_offset;
            break;
        case DC_TIMING2: {
            /* Return cached value from timer-driven counter.
             * VSYNC callback checks (val & 0x7FF) > 0xF0. */
            val = g_emu.dc_timing2;
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
                    /* CNTRL: clear bits 24-27, force EE_Present(28) */
                    val = (val & ~0x1F000000u) | 0x10000000u; /* bit28=present */
                    if (s_ee.do_bit)
                        val |= 0x08000000u; /* bit27=EEDO */
                    val |= dcs_serial_get_plx50_bits(); /* DCS2 serial status */
                }
            } else {
                val = 0;
            }
            if (off < 0x60 && plx_rd_cnt < 500) {
                plx_rd_cnt++;
                LOG("plx", "RD off=0x%02x size=%u val=0x%08x (#%d)\n", off, size, val, plx_rd_cnt);
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
            static int s_dcs_rd = 0;
            if (++s_dcs_rd <= 200)
                LOG("dcs", "RD off=0 val=0x%04x size=%d cnt=%d left=%d\n",
                    val, size, s_dcs_rd, g_emu.dcs_resp.count);
        } else if (off == 2) {
            /* Status/flags read: bit 7 = data available */
            uint16_t f = g_emu.dcs_flags;
            if (g_emu.dcs_resp.count > 0)
                f |= 0x80;
            else
                f &= ~0x80u;
            val = f;
            static int s_dcs_flag_rd = 0;
            if (++s_dcs_flag_rd <= 200 || (s_dcs_flag_rd % 50000 == 0))
                LOG("dcs", "RD off=2 flags=0x%04x size=%d cnt=%d q=%d\n",
                    val, size, s_dcs_flag_rd, g_emu.dcs_resp.count);
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

/* ===== GP (Graphics Pipeline) BLT Engine =====
 * Reverse-engineered from P2K-driver, ported from x64 POC qemu_glue.c.
 * GP registers at GX_BASE + 0x8100-0x820C control hardware blitter.
 * Game writes dst/src/width/mode, then writes GP_BLT_STATUS to trigger. */

static uint16_t s_gp_dst_x, s_gp_dst_y;
static uint16_t s_gp_src_x, s_gp_src_y;
static uint16_t s_gp_width;
static int      s_gp_transparent;   /* bit 12 of GP_RASTER_MODE */
static uint32_t s_gp_blt_count;

#define GP_ROW_STRIDE  2048   /* bytes per row in GP address space */
#define GP_TRANS_KEY   0x7C1F /* RGB555 magenta transparency key */

/* GP blit framebuffer base — physical RAM 0x800000.
 * Game decompresses images here; GP BLTs and display both use this address. */
#define GP_FB_BASE     0x00800000u

static void gp_execute_blt(uc_engine *uc)
{
    uint32_t src_off = (uint32_t)s_gp_src_x * 2 + (uint32_t)s_gp_src_y * GP_ROW_STRIDE;
    uint32_t dst_off = (uint32_t)s_gp_dst_x * 2 + (uint32_t)s_gp_dst_y * GP_ROW_STRIDE;
    uint32_t copy_bytes = (uint32_t)s_gp_width * 2;
    uint32_t fb_size = 0x400000; /* 4MB framebuffer */

    if (src_off + copy_bytes > fb_size || dst_off + copy_bytes > fb_size) {
        if (s_gp_blt_count < 50)
            LOG("gp", "BLT #%u OOB: src=0x%x dst=0x%x size=%u\n",
                s_gp_blt_count, src_off, dst_off, copy_bytes);
        s_gp_blt_count++;
        return;
    }

    if (s_gp_blt_count < 20)
        LOG("gp", "BLT #%u: src(%u,%u) dst(%u,%u) w=%u mode=%s\n",
            s_gp_blt_count, s_gp_src_x, s_gp_src_y,
            s_gp_dst_x, s_gp_dst_y, s_gp_width,
            s_gp_transparent ? "transparent" : "memcpy");

    uint8_t row_buf[GP_ROW_STRIDE];
    if (uc_mem_read(uc, GP_FB_BASE + src_off, row_buf, copy_bytes) != UC_ERR_OK) {
        s_gp_blt_count++;
        return;
    }

    if (!s_gp_transparent) {
        uc_mem_write(uc, GP_FB_BASE + dst_off, row_buf, copy_bytes);
    } else {
        uint8_t dst_buf[GP_ROW_STRIDE];
        if (uc_mem_read(uc, GP_FB_BASE + dst_off, dst_buf, copy_bytes) != UC_ERR_OK) {
            s_gp_blt_count++;
            return;
        }
        uint16_t *src_px = (uint16_t *)row_buf;
        uint16_t *dst_px = (uint16_t *)dst_buf;
        for (uint16_t i = 0; i < s_gp_width; i++) {
            if (src_px[i] != GP_TRANS_KEY)
                dst_px[i] = src_px[i];
        }
        uc_mem_write(uc, GP_FB_BASE + dst_off, dst_buf, copy_bytes);
    }

    s_gp_blt_count++;
}

/* ===== MMIO Write Hook ===== */
void bar_mmio_write(uc_engine *uc, uc_mem_type type, uint64_t addr, int size,
                    int64_t value, void *user_data)
{
    uint32_t a = (uint32_t)addr;
    uint32_t val = (uint32_t)value;
    static int mmio_wr_cnt = 0;
    if (mmio_wr_cnt < 50) {
        mmio_wr_cnt++;
        LOG("mmio", "WRITE addr=0x%08x size=%d val=0x%08x\n", a, size, val);
    }

    /* Dedicated BAR2/BAR4 write counters - never throttled */
    static uint32_t bar2_wr_total = 0, bar4_wr_total = 0;
    if (a >= WMS_BAR2 && a < WMS_BAR2 + 0x01000000u) {
        if (++bar2_wr_total <= 10)
            LOG("BAR2", "WRITE #%u addr=0x%08x size=%d val=0x%x\n", bar2_wr_total, a, size, val);
    }
    if (a >= WMS_BAR4 && a < WMS_BAR4 + BAR4_SIZE) {
        if (++bar4_wr_total <= 10)
            LOG("BAR4", "WRITE #%u addr=0x%08x size=%d val=0x%x\n", bar4_wr_total, a, size, val);
    }

    if (a >= GX_BASE && a < GX_BASE + GX_BASE_SIZE) {
        uint32_t off = a - GX_BASE;
        static int gx_wr_cnt = 0;
        if (gx_wr_cnt < 30 && off < 0x800000) {
            gx_wr_cnt++;
            LOG("gx", "WRITE off=0x%x val=0x%x (reg)\n", off, val);
        }

        if (off >= 0x800000 && off < 0xC00000) {
            /* Framebuffer write — mirror to physical RAM at 0x800000.
             * Direct memcpy to g_emu.ram (backing Unicorn via uc_mem_map_ptr). */
            uint32_t phys = (off - 0x800000);
            if (phys + size <= RAM_SIZE - 0x800000)
                memcpy(g_emu.ram + 0x800000u + phys, &val, size);
            return;
        }

        switch (off) {
        case DC_UNLOCK:
            break;
        case DC_FB_ST_OFFSET: {
            static uint32_t prev_fb_off = 0xFFFFFFFF;
            static int fb_off_log_count = 0;
            if (val != prev_fb_off) {
                if (fb_off_log_count < 20)
                    LOG("dc", "DC_FB_ST_OFFSET changed: 0x%x → 0x%x\n", prev_fb_off, val);
                prev_fb_off = val;
                fb_off_log_count++;
            }
            g_emu.dc_fb_offset = val;
            break;
        }
        case DC_TIMING2:
            g_emu.dc_timing2 = val;
            break;
        /* GP register writes — follow x64 POC proven layout:
         * 0x8100 = packed dst (x bits[15:0], y bits[31:16])
         * 0x8104 = width (bits[15:0])
         * 0x8108 = packed src (x bits[15:0], y bits[31:16])
         * 0x8200 = raster mode (bit 12 = transparent blt)
         * 0x8208 = BLT trigger (write starts blit)
         * 0x820C = BLT status (read returns 0x300 = idle) */
        case 0x8100: /* GP DST coords (packed) */
            s_gp_dst_x = (uint16_t)(val & 0xFFFF);
            s_gp_dst_y = (uint16_t)((val >> 16) & 0xFFFF);
            g_emu.gx_regs[off >> 2] = val;
            break;
        case 0x8104: /* GP WIDTH */
            s_gp_width = (uint16_t)(val & 0xFFFF);
            g_emu.gx_regs[off >> 2] = val;
            break;
        case 0x8108: /* GP SRC coords (packed) */
            s_gp_src_x = (uint16_t)(val & 0xFFFF);
            s_gp_src_y = (uint16_t)((val >> 16) & 0xFFFF);
            g_emu.gx_regs[off >> 2] = val;
            break;
        case 0x8200: /* GP_RASTER_MODE */
            s_gp_transparent = (val >> 12) & 1;
            g_emu.gx_regs[off >> 2] = val;
            break;
        case 0x8208: /* GP BLT trigger (x64 POC: write to 0x8208 executes blit) */
            gp_execute_blt(uc);
            g_emu.gx_regs[off >> 2] = val;
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

            /* SEEPROM + DCS2 serial bit-bang on CNTRL register (0x50) */
            if (off == 0x50) {
                seeprom_process_write(val);
                dcs_serial_process_plx50(val);
            }

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

            static int s_dcs_wr = 0;
            if (++s_dcs_wr <= 200)
                LOG("dcs", "WR off=0 cmd=0x%04x size=%d active=%d pending=%d cnt=%d\n",
                    cmd, size, g_emu.dcs_active, g_emu.dcs_pending, s_dcs_wr);

            if (g_emu.dcs_active && cmd == 0x0E) {
                g_emu.dcs_active = false;
                g_emu.dcs_pending = false;
                g_emu.dcs_remaining = 0;
                g_emu.dcs_layer = 0;
                memset(g_emu.dcs_mixer, 0, sizeof(g_emu.dcs_mixer));
                dcs_enqueue_response(10);
                goto dcs_write_done;
            }

            if (g_emu.dcs_active)
                goto dcs_write_done;

            if (g_emu.dcs_pending) {
                if (g_emu.dcs_remaining == 0) {
                    g_emu.dcs_remaining = ((cmd >> 8) == 0x55) ? 1 : 2;
                    g_emu.dcs_mixer[0] = cmd;
                    g_emu.dcs_layer = 1;
                    goto dcs_write_done;
                }

                if (g_emu.dcs_layer < 4)
                    g_emu.dcs_mixer[g_emu.dcs_layer++] = cmd;
                if (--g_emu.dcs_remaining != 0)
                    goto dcs_write_done;

                if (g_emu.dcs_mixer[0] == 999 || g_emu.dcs_mixer[0] == 1000) {
                    dcs_enqueue_response(0x100);
                    dcs_enqueue_response(0x10);
                } else {
                    sound_execute_mixer(g_emu.dcs_mixer[0],
                                        g_emu.dcs_layer > 1 ? g_emu.dcs_mixer[1] : 0,
                                        g_emu.dcs_layer > 2 ? g_emu.dcs_mixer[2] : 0);
                }

                g_emu.dcs_layer = 0;
                g_emu.dcs_remaining = 0;
                memset(g_emu.dcs_mixer, 0, sizeof(g_emu.dcs_mixer));
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
                {
                    static bool s_dong_played = false;
                    if (!s_dong_played) {
                        s_dong_played = true;
                        sound_play_boot_dong();
                    }
                }
                dcs_enqueue_response(0xCC01); /* board present */
                dcs_enqueue_response(10);
                break;
            case 0x1B:
                dcs_enqueue_response(0xCC09);
                dcs_enqueue_response(10);
                break;
            case 0xAA:
                sound_process_cmd(0x00AA);
                sound_start_audio_init_thread();
                dcs_enqueue_response(0xCC04);
                dcs_enqueue_response(10);
                break;
            case 0x0E:
                g_emu.dcs_active = true;
                g_emu.dcs_pending = false;
                g_emu.dcs_remaining = 0;
                g_emu.dcs_layer = 0;
                memset(g_emu.dcs_mixer, 0, sizeof(g_emu.dcs_mixer));
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
