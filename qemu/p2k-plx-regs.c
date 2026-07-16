/*
 * pinball2000 PLX 9050 / 9054 BAR0 register file with 93C46 SEEPROM
 * emulation.
 *
 * Background
 * ----------
 * PRISM's pci_probe() calls plx_ee_verify(), which bit-bangs the PLX
 * SEEPROM through the PLX CNTRL register (offset 0x50) bits 24..27 and
 * compares the recovered words against an expected configuration
 * blob.  Without an EEPROM model the verify fails ("plx_ee_verify():
 * failed") and PRISM proceeds with an uninitialised resource table —
 * every later resource lookup then prints
 *
 *     *** NonFatal: Retrieve Resource (get &) Failed, ID=
 *
 * forever.  This file ports the proven 93C46 state machine and the
 * pre-populated PRISM EEPROM image from the legacy reference:
 *   - 93C46 protocol handling
 *   - 48-word PRISM image (DID/VID override, BAR prefetch flags)
 *   - BAR0 read special bits (0x6C data latch for EEPROM interface)
 *   - BAR0 write + 0x50 dispatch (start/stop/read EEPROM cycle)
 *
 * Scope
 * -----
 * - 64-dword PLX register file backed by simple memory.
 * - INTCSR (0x4C) read returns bit2 SET / bit5 set: bit2=1 satisfies
 *   pci_watchdog_bone() in SWE1 v2.10 (caller @ 0x1a6f48 fires Fatal
 *   when bone() returns 1; bone() returns 1 iff bit2==0). bit5=1
 *   reports the PLX as ready.
 * - CNTRL (0x50) read returns the latched value with EE_PRESENT (bit28)
 *   forced and EEDO (bit27) reflecting the SEEPROM shift register.
 * - CNTRL writes drive the 93C46 state machine.
 *
 * - DCS2 serial bit-bang on the same CNTRL bits 24..26 (clock/enable/
 *   SEEPROM both observe bits 24..26 simultaneously; bit 27 on read is
 *   OR'd from both (EEDO | DCS audio_ready/audio_ack). This is
 *   real-hardware coexistence, not a compatibility bridge.
 *
 *   Without this, the game's PCI-side DCS detect never sees the
 *   audio_ready=1 response and falls back to the I/O-port (UART byte-
 *   pair) DCS path. On SWE1 base 0.40 (--update none) that fallback
 *   does NOT emit cmd=0x003A → no boot dong, no S0001-LP1 attract
 *   loop, only sparse UI clicks. With the serial responding, the
 *   game stays on the BAR4 cmd path and emits the full DCS stream.
 *
 * Out of scope (do not snowball this file):
 * - Chip-select / mailbox writes (0x3C..0x48, 0x80+) — currently
 *   stored as plain dwords; add semantics on demand.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/notify.h"
#include "qapi/error.h"
#include "system/system.h"
#include "exec/address-spaces.h"
#include <errno.h>

#include "p2k-internal.h"

#define P2K_PLX_BAR0_BASE    0x10000000u
#define P2K_PLX_BAR0_SIZE    0x00000100u   /* 64 dwords, mirrored a few times */

/* ---------- PLX register backing store ----------------------------------- */

static uint32_t s_plx_regs[64];

/* ---------- 93C46 SEEPROM ------------------------------------------------ *
 * PLX 9050 CNTRL (0x50) bit assignments:
 *   bit 24 = EESK   (serial clock,  host -> EEPROM)
 *   bit 25 = EECS   (chip select,   host -> EEPROM)
 *   bit 26 = EEDI   (data in,       host -> EEPROM)
 *   bit 27 = EEDO   (data out,      EEPROM -> host, read-only)
 *   bit 28 = EEPRESENT (read-only, always 1 for our board)
 */
static uint16_t s_seeprom[64];

static struct {
    int      state;     /* 0=idle 1=opcode 2=address 3=data_out 4=data_in */
    int      bits;
    int      opcode;    /* 2-bit:  10=READ  01=WRITE  11=ERASE  00=MISC */
    int      addr;      /* 6-bit word address */
    uint16_t shift;
    int      do_bit;    /* current EEDO output level */
    int      last_sk;
    bool     write_enabled; /* EWEN latch; false after chip reset/boot */
    bool     write_all;     /* current data-in cycle is WRAL */
} s_ee;

static char s_seeprom_save_path[1024];
static bool s_seeprom_dirty;

static void seeprom_init_default(void)
{
    /* Pre-populate with the proven PRISM configuration image.  Values come
     * from the original x64 POC and successfully pass plx_ee_verify(). */
    for (int i = 0; i < 64; i++) {
        s_seeprom[i] = 0xFFFF;
    }
    s_seeprom[0]  = 0x9050;       /* PCI device id  */
    s_seeprom[1]  = 0x10B5;       /* PCI vendor id  */
    s_seeprom[2]  = 0x0680;       /* class code hi  */
    s_seeprom[3]  = 0x0001;       /* class/rev      */
    s_seeprom[4]  = 0x1078;       /* subsys vendor  */
    s_seeprom[5]  = 0x0001;       /* subsys device  */
    /* Range-register pairs (low half / high half).
     * LAS0RR=0x0FFE0000  LAS1RR=0x0F800000  LAS2RR=0x0FFF8000
     * LAS3RR=0x0C000008  EROMRR=0x0FFF8000                       */
    s_seeprom[6]  = 0x0000; s_seeprom[7]  = 0x0FFE;
    s_seeprom[8]  = 0x0000; s_seeprom[9]  = 0x0F80;
    s_seeprom[10] = 0x8000; s_seeprom[11] = 0x0FFF;
    s_seeprom[12] = 0x0008; s_seeprom[13] = 0x0C00;
    s_seeprom[14] = 0x8000; s_seeprom[15] = 0x0FFF;
    /* Base addresses + flags.
     * LAS0BA=0x00100001  LAS1BA=0x01000001  LAS2BA=0x00000001
     * LAS3BA=0x08000001  EROMBA=0x08000000                       */
    s_seeprom[16] = 0x0001; s_seeprom[17] = 0x0010;
    s_seeprom[18] = 0x0001; s_seeprom[19] = 0x0100;
    s_seeprom[20] = 0x0001; s_seeprom[21] = 0x0000;
    s_seeprom[22] = 0x0001; s_seeprom[23] = 0x0800;
    s_seeprom[24] = 0x0000; s_seeprom[25] = 0x0800;
    /* Misc PLX descriptors. */
    s_seeprom[26] = 0xA1E0; s_seeprom[27] = 0x5403;
    s_seeprom[28] = 0xB940; s_seeprom[29] = 0x5473;
    s_seeprom[30] = 0xA060; s_seeprom[31] = 0x4041;
    s_seeprom[32] = 0xB8C0; s_seeprom[33] = 0x54B2;
    s_seeprom[34] = 0xB8C0; s_seeprom[35] = 0x54B2;
    /* CS0..CS3 base + 1 (enable bit) and INTCSR. */
    s_seeprom[36] = 0x0001; s_seeprom[37] = 0x0880;
    s_seeprom[38] = 0x0001; s_seeprom[39] = 0x0980;
    s_seeprom[40] = 0x0001; s_seeprom[41] = 0x0A80;
    s_seeprom[42] = 0x0001; s_seeprom[43] = 0x0B80;
    s_seeprom[44] = 0x0000; s_seeprom[45] = 0x0000;
    /* CNTRL default = 0x40789242. */
    s_seeprom[46] = 0x9242; s_seeprom[47] = 0x4078;

    memset(&s_ee, 0, sizeof(s_ee));
    s_ee.do_bit = 1;
}

static bool seeprom_set_word(int addr, uint16_t val)
{
    addr &= 63;
    if (s_seeprom[addr] == val) {
        return false;
    }
    s_seeprom[addr] = val;
    s_seeprom_dirty = true;
    return true;
}

static void seeprom_erase_all(void)
{
    for (int i = 0; i < 64; i++) {
        seeprom_set_word(i, 0xFFFF);
    }
}

static void seeprom_write_all(uint16_t val)
{
    for (int i = 0; i < 64; i++) {
        seeprom_set_word(i, val);
    }
}

static void seeprom_process_misc(void)
{
    switch ((s_ee.addr >> 4) & 3) {
    case 0:                                     /* EWDS: 00 00xxxx */
        s_ee.write_enabled = false;
        break;
    case 1:                                     /* WRAL: 00 01xxxx */
        s_ee.state = 4;
        s_ee.bits = 0;
        s_ee.shift = 0;
        s_ee.write_all = true;
        return;
    case 2:                                     /* ERAL: 00 10xxxx */
        if (s_ee.write_enabled) {
            seeprom_erase_all();
        }
        break;
    case 3:                                     /* EWEN: 00 11xxxx */
        s_ee.write_enabled = true;
        break;
    }
    s_ee.state = 0;
    s_ee.do_bit = 1;
}

static void seeprom_process_write(uint32_t cntrl_val)
{
    int sk = (cntrl_val >> 24) & 1;
    int cs = (cntrl_val >> 25) & 1;
    int di = (cntrl_val >> 26) & 1;

    if (!cs) {
        s_ee.state     = 0;
        s_ee.bits      = 0;
        s_ee.write_all = false;
        s_ee.do_bit    = 1;
        s_ee.last_sk   = sk;
        return;
    }

    if (sk && !s_ee.last_sk) {
        switch (s_ee.state) {
        case 0:
            if (di) {
                s_ee.state  = 1;
                s_ee.bits   = 0;
                s_ee.opcode = 0;
            }
            break;
        case 1:
            s_ee.opcode = (s_ee.opcode << 1) | di;
            if (++s_ee.bits >= 2) {
                s_ee.state = 2;
                s_ee.bits  = 0;
                s_ee.addr  = 0;
            }
            break;
        case 2:
            s_ee.addr = (s_ee.addr << 1) | di;
            if (++s_ee.bits >= 6) {
                s_ee.addr &= 63;
                if (s_ee.opcode == 2) {            /* READ  (10) */
                    s_ee.state  = 3;
                    s_ee.bits   = 0;
                    s_ee.shift  = s_seeprom[s_ee.addr];
                    s_ee.do_bit = 0;               /* dummy 0 bit */
                } else if (s_ee.opcode == 1) {     /* WRITE (01) */
                    s_ee.state = 4;
                    s_ee.bits  = 0;
                    s_ee.shift = 0;
                    s_ee.write_all = false;
                } else if (s_ee.opcode == 3) {     /* ERASE (11) */
                    if (s_ee.write_enabled) {
                        seeprom_set_word(s_ee.addr, 0xFFFF);
                    }
                    s_ee.state = 0;
                    s_ee.do_bit = 1;
                } else {                           /* EWDS/WRAL/ERAL/EWEN */
                    seeprom_process_misc();
                }
            }
            break;
        case 3:
            s_ee.do_bit = (s_ee.shift >> (15 - s_ee.bits)) & 1;
            if (++s_ee.bits >= 16) {
                s_ee.addr  = (s_ee.addr + 1) & 63;
                s_ee.shift = s_seeprom[s_ee.addr];
                s_ee.bits  = 0;
                s_ee.do_bit = (s_ee.shift >> 15) & 1;
            }
            break;
        case 4:
            s_ee.shift = (s_ee.shift << 1) | di;
            if (++s_ee.bits >= 16) {
                if (s_ee.write_enabled) {
                    if (s_ee.write_all) {
                        seeprom_write_all(s_ee.shift);
                    } else {
                        seeprom_set_word(s_ee.addr, s_ee.shift);
                    }
                }
                s_ee.write_all = false;
                s_ee.state  = 0;
                s_ee.do_bit = 1;
            }
            break;
        }
    }
    s_ee.last_sk = sk;
}

/* ---------- DCS2 serial bit-bang ----------------------------------------- *
 *
 * on PCI/BAR4 is fronted by a 1-wire serial protocol carried on PLX CNTRL bits
 * 24..26 (same physical lines as the 93C46 EEPROM bit-bang). The
 * game's pci-side DCS detect bit-bangs an 8-bit command byte over
 * these lines and reads bit 27 (output) for the chip's response. The
 * specific command class=3 sub=0x03 (param=0xFF) sets `audio_ready`,
 * which is the signal the game waits for before switching to BAR4
 * cmd word writes.
 *
 * No host invention here — this is faithful to real silicon as
 * documented in the unicorn POC (i386/bar4.c → ported to bar.c). */
static struct {
    int     enable_prev, clock_prev;
    int     cmd_received, byte_complete;
    int     bit_cnt;
    uint8_t shift_reg;
    uint8_t data_addr;
    int     param;
    int     audio_active, audio_ack, audio_ready;
    int     cntrl_bit1, cntrl_bit2;
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
            if (!s_dcs_serial.audio_ready) {
                info_report("dcs-serial: cmd=0x%02x → audio_ready=1 "
                            "(DCS chip alive on PCI; game should now use BAR4)",
                            s_dcs_serial.param);
            }
            s_dcs_serial.audio_ready = 1;
            return;
        default:
            return;
        }
    }
    case 1:
        s_dcs_serial.audio_active = 0;
        if (s_dcs_serial.cntrl_bit1 && s_dcs_serial.cntrl_bit2) return;
        if (!continuing) {
            s_dcs_serial.data_addr = (uint8_t)(s_dcs_serial.param & 0x3F);
        }
        return;
    case 2:
        s_dcs_serial.audio_active = 1;
        if (continuing) return;
        s_dcs_serial.data_addr = (uint8_t)(s_dcs_serial.param & 0x3F);
        s_dcs_serial.audio_ack  = 0;
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
                s_dcs_serial.bit_cnt        = 0;
                s_dcs_serial.byte_complete  = 1;
                s_dcs_serial.param          = (int)s_dcs_serial.shift_reg;
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
        if (s_dcs_serial.audio_ack) {
            bits |= 0x08000000u;        /* bit 27 = data ready */
        } else {
            s_dcs_serial.audio_ack = 1;
        }
    }
    if (s_dcs_serial.audio_ready) {
        bits |= 0x08000000u;            /* bit 27 = DCS init response */
    }
    return bits;
}

/* ---------- BAR0 MMIO ops ------------------------------------------------- */

static uint64_t p2k_plx_read(void *opaque, hwaddr addr, unsigned size)
{
    uint32_t off = addr & 0xFCu;        /* dword aligned */
    uint32_t shift = (addr & 3u) * 8u;
    uint32_t mask = (size == 4) ? 0xFFFFFFFFu : ((1u << (size * 8)) - 1u);
    uint32_t val;

    if ((off >> 2) >= 64) {
        return 0xFFFFFFFFu & mask;
    }
    val = s_plx_regs[off >> 2];

    switch (off) {
    case 0x4C:                          /* INTCSR */
        /* Per disasm of pci_watchdog_bone() in SWE1 v2.10 (caller at
         * 0x1a6f48, body at 0x1a6fa8): the function returns 1 iff
         * (probe()==1 AND INTCSR bit2 == 0), and the *caller* fires
         * "pci_watchdog_bone(): the watchdog has expired" Fatal when
         * bone()==1.  So bit 2 = 1 means "no watchdog action needed"
         * and bit 2 = 0 means "trigger Fatal".  Force bit2=1 and bit5=1
         * (PLX ready). Replaces the unicorn RAM scribbler. */
        val = val | 0x04u | 0x20u;
        break;
    case 0x50:                          /* CNTRL */
        val &= ~0x1F000000u;
        val |= 0x10000000u;             /* bit28 = EEPRESENT */
        if (s_ee.do_bit) {
            val |= 0x08000000u;         /* bit27 = EEDO */
        }
        val |= dcs_serial_get_plx50_bits(); /* bit27 also = DCS audio ready */
        break;
    default:
        break;
    }

    return (val >> shift) & mask;
}

static void p2k_plx_write(void *opaque, hwaddr addr,
                          uint64_t val, unsigned size)
{
    uint32_t off = addr & 0xFCu;
    uint32_t shift = (addr & 3u) * 8u;
    uint32_t mask = (size == 4) ? 0xFFFFFFFFu : ((1u << (size * 8)) - 1u);
    uint32_t old, nv;

    if ((off >> 2) >= 64) {
        return;
    }
    old = s_plx_regs[off >> 2];
    nv  = (old & ~(mask << shift)) | (((uint32_t)val & mask) << shift);
    s_plx_regs[off >> 2] = nv;

    if (off == 0x50) {
        if (nv == 0x40789242u) {
            p2k_dcs_adsp_host_reset();
        }
        seeprom_process_write(nv);
        dcs_serial_process_plx50(nv);   /* DCS chip shares CNTRL bits 24..26 */
    }
}

static const MemoryRegionOps p2k_plx_ops = {
    .read       = p2k_plx_read,
    .write      = p2k_plx_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl       = { .min_access_size = 1, .max_access_size = 4 },
    .valid      = { .min_access_size = 1, .max_access_size = 4 },
};

/* Flush 93C46 SEEPROM contents back to savedata at exit when an enabled
 * WRITE/ERASE/ERAL/WRAL actually changes a word. Atomic write via tmp+rename.
 * Disable with P2K_NO_SAVEDATA=1. */
static void p2k_seeprom_save_cb(Notifier *notifier, void *data)
{
    if (!s_seeprom_save_path[0]) return;
    if (p2k_no_savedata_enabled()) {
        info_report("pinball2000: P2K_NO_SAVEDATA set — discarding SEEPROM changes");
        return;
    }
    if (!s_seeprom_dirty) {
        info_report("pinball2000: SEEPROM unchanged, skipping save");
        return;
    }
    uint8_t buf[128];
    for (int i = 0; i < 64; i++) {
        buf[i*2]   = (uint8_t)(s_seeprom[i] & 0xFF);
        buf[i*2+1] = (uint8_t)((s_seeprom[i] >> 8) & 0xFF);
    }
    char tmp[1100];
    snprintf(tmp, sizeof(tmp), "%s.tmp", s_seeprom_save_path);
    FILE *fp = fopen(tmp, "wb");
    if (!fp) {
        warn_report("pinball2000: SEEPROM save: fopen(%s) failed: %s",
                    tmp, strerror(errno));
        return;
    }
    size_t w = fwrite(buf, 1, sizeof(buf), fp);
    fclose(fp);
    if (w != sizeof(buf)) {
        warn_report("pinball2000: SEEPROM save: short write %zu/128", w);
        unlink(tmp);
        return;
    }
    if (rename(tmp, s_seeprom_save_path) != 0) {
        warn_report("pinball2000: SEEPROM save: rename failed: %s",
                    strerror(errno));
        unlink(tmp);
        return;
    }
    info_report("pinball2000: SEEPROM saved to %s (128 bytes)",
                s_seeprom_save_path);
}

static Notifier p2k_seeprom_exit_notifier = {
    .notify = p2k_seeprom_save_cb,
};

void p2k_install_plx_regs(Pinball2000MachineState *s)
{
    seeprom_init_default();

    /* Try to seed from savedata/<game>.see (128 bytes = 64 words LE).
     * If absent or wrong size, the defaults from seeprom_init_default()
     * are kept — that's enough for plx_ee_verify() to pass. */
    if (s && s->game) {
        snprintf(s_seeprom_save_path, sizeof(s_seeprom_save_path),
                 "savedata/%s.see", s->game);
        FILE *fp = p2k_no_savedata_enabled() ? NULL : fopen(s_seeprom_save_path, "rb");
        if (fp) {
            long sz = -1;
            if (fseek(fp, 0, SEEK_END) == 0) {
                sz = ftell(fp);
                rewind(fp);
            }
            if (sz == 128) {
                uint8_t buf[128];
                size_t n = fread(buf, 1, sizeof(buf), fp);
                if (n == sizeof(buf)) {
                    for (int i = 0; i < 64; i++) {
                        s_seeprom[i] = (uint16_t)buf[i*2] |
                                       ((uint16_t)buf[i*2+1] << 8);
                    }
                    info_report("pinball2000: SEEPROM seeded from %s (128 bytes)",
                                s_seeprom_save_path);
                } else {
                    warn_report("pinball2000: SEEPROM %s short read %zu/128; "
                                "using built-in defaults",
                                s_seeprom_save_path, n);
                }
            } else {
                warn_report("pinball2000: SEEPROM %s invalid size %ld "
                            "(expected exactly 128 bytes); using built-in "
                            "defaults", s_seeprom_save_path, sz);
            }
            fclose(fp);
        }
        qemu_add_exit_notifier(&p2k_seeprom_exit_notifier);
    }

    memset(s_plx_regs, 0, sizeof(s_plx_regs));

    MemoryRegion *mr = g_new(MemoryRegion, 1);
    memory_region_init_io(mr, NULL, &p2k_plx_ops, NULL,
                          "p2k.plx-regs", P2K_PLX_BAR0_SIZE);
    memory_region_add_subregion(get_system_memory(),
                                P2K_PLX_BAR0_BASE, mr);

    info_report("pinball2000: PLX BAR0 regs+SEEPROM @ 0x%08x (%u bytes)",
                P2K_PLX_BAR0_BASE, P2K_PLX_BAR0_SIZE);
}
