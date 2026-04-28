/*
 * pinball2000 minimal ISA-port stubs.
 *
 * QEMU's i386 PC machinery normally provides keyboard, RTC/CMOS, system
 * control port B, COM1, etc. — but we deliberately do NOT use the `pc`
 * machine.  We only attach what PRISM/XINU actually polls so the boot
 * loops terminate.  Each stub mimics unicorn.old/src/io.c:io_port_read.
 *
 * Currently provided:
 *   0x60 / 0x64       i8042 keyboard controller (always idle)
 *   0x61              system-control port B (bit 4 toggles each read)
 *   0x70 / 0x71       CMOS index/data (zeroed)
 *   0x80              POST code (write-only side-effect, read returns 0)
 *   0x3F8..0x3FF      COM1 16550 UART — printf path hot loop
 *                     LSR (offset 5) reports THRE|TEMT so puts() does
 *                     not spin; the actual character is dropped (or
 *                     forwarded to host stderr if P2K_UART_TO_STDERR=1).
 *
 * These are intentionally minimum-viable.  If a future PRISM path needs
 * more (e.g. PCI cf8/cfc, MediaGX 0x22/0x23, VGA 0x3DA toggle, LPT
 * 0x378), add another tiny block below — keep one concern per region.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "exec/ioport.h"

#include "p2k-internal.h"

/* ---------- i8042 keyboard ------------------------------------------------ */

static uint64_t p2k_kbd_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0x00;  /* IBF=0, OBF=0, no error */
}

static void p2k_kbd_write(void *opaque, hwaddr addr,
                          uint64_t val, unsigned size)
{
}

static const MemoryRegionOps p2k_kbd_ops = {
    .read       = p2k_kbd_read,
    .write      = p2k_kbd_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl       = { .min_access_size = 1, .max_access_size = 1 },
};

/* ---------- system-control port B (0x61) ---------------------------------- *
 * Bit 4 toggles every read on real hardware (refresh-clock derived).
 * Some BIOS code uses this as a microsecond delay reference; without the
 * toggle the wait-loop never terminates. */

static uint8_t s_port61;

static uint64_t p2k_port61_read(void *opaque, hwaddr addr, unsigned size)
{
    s_port61 ^= 0x10;
    return s_port61 & 0x1F;
}

static void p2k_port61_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned size)
{
    s_port61 = (s_port61 & ~0x0F) | (val & 0x0F);
}

static const MemoryRegionOps p2k_port61_ops = {
    .read       = p2k_port61_read,
    .write      = p2k_port61_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl       = { .min_access_size = 1, .max_access_size = 1 },
};

/* ---------- CMOS / RTC (0x70 index, 0x71 data) ---------------------------- */

static uint8_t s_cmos_addr;
static uint8_t s_cmos_data[128];

static uint64_t p2k_cmos_read(void *opaque, hwaddr addr, unsigned size)
{
    if (addr == 0) return s_cmos_addr;
    uint8_t reg = s_cmos_addr & 0x7F;
    /* Mirror unicorn.old/src/io.c:1692-1711: live time on RTC reg reads. */
    if (reg <= 0x09 || reg == 0x32) {
        time_t t = time(NULL);
        struct tm *tm = localtime(&t);
        #define BCD(v) (uint8_t)((((v)/10)<<4) | ((v)%10))
        s_cmos_data[0x00] = BCD(tm->tm_sec);
        s_cmos_data[0x02] = BCD(tm->tm_min);
        s_cmos_data[0x04] = BCD(tm->tm_hour);
        s_cmos_data[0x06] = BCD(tm->tm_wday ? tm->tm_wday : 7);
        s_cmos_data[0x07] = BCD(tm->tm_mday);
        s_cmos_data[0x08] = BCD(tm->tm_mon + 1);
        s_cmos_data[0x09] = BCD(tm->tm_year % 100);
        s_cmos_data[0x32] = BCD((tm->tm_year + 1900) / 100);
        /* Status register A: bit 7 = UIP (update-in-progress) — must
         * be 0 most of the time so reads aren't gated.  Status B: BCD
         * mode + 24h clock = 0x02. */
        s_cmos_data[0x0A] = 0x26;   /* 32 kHz / 1024 Hz, UIP=0 */
        s_cmos_data[0x0B] = 0x02;
        s_cmos_data[0x0D] = 0x80;   /* battery valid */
        #undef BCD
    }
    return s_cmos_data[reg];
}

static void p2k_cmos_write(void *opaque, hwaddr addr,
                           uint64_t val, unsigned size)
{
    if (addr == 0) {
        s_cmos_addr = val & 0xFF;
    } else {
        s_cmos_data[s_cmos_addr & 0x7F] = val & 0xFF;
    }
}

static const MemoryRegionOps p2k_cmos_ops = {
    .read       = p2k_cmos_read,
    .write      = p2k_cmos_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl       = { .min_access_size = 1, .max_access_size = 1 },
};

/* ---------- POST diagnostic port (0x80) ----------------------------------- */

static uint8_t s_post_code;

static uint64_t p2k_post_read(void *opaque, hwaddr addr, unsigned size)
{
    return s_post_code;
}

static void p2k_post_write(void *opaque, hwaddr addr,
                           uint64_t val, unsigned size)
{
    s_post_code = val & 0xFF;
}

static const MemoryRegionOps p2k_post_ops = {
    .read       = p2k_post_read,
    .write      = p2k_post_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl       = { .min_access_size = 1, .max_access_size = 1 },
};

/* ---------- COM1 16550 UART (0x3F8..0x3FF) -------------------------------- *
 *
 * PRISM's printf hot-loop polls the LSR (offset 5) waiting for THRE|TEMT
 * (bits 5 and 6 = 0x60).  We always report "transmitter empty, no RX
 * data" so each character write completes immediately.  Optionally the
 * data byte at offset 0 is forwarded to the host's stderr when
 * P2K_UART_TO_STDERR=1 — extremely useful for tracing PRISM diagnostics.
 *
 * Registers (DLAB-aware enough for printf to work):
 *   0  THR/RBR/DLL  -- write: emit char (or DLL latch when LCR.7=1)
 *   1  IER/DLM      -- ignored
 *   2  IIR/FCR      -- read: 0x01 (no interrupt pending)
 *   3  LCR          -- stored, used for DLAB only
 *   4  MCR          -- ignored
 *   5  LSR          -- 0x60 (THRE+TEMT, no RX data, no errors)
 *   6  MSR          -- 0x00
 *   7  SCR          -- scratch register, stored
 */

static uint8_t s_uart_lcr;       /* bit 7 = DLAB */
static uint8_t s_uart_scr;
static bool    s_uart_to_stderr;

/* Pre-canned RX buffer fed into RBR.  Source: P2K_UART_INPUT env var.
 * Lets us send "continue\r\n" to the XINA monitor without a full chardev. */
static const char *s_uart_input;   /* pointer into env-var string */

static uint64_t p2k_uart_read(void *opaque, hwaddr addr, unsigned size)
{
    bool has_rx = (s_uart_input && *s_uart_input);
    switch (addr) {
    case 0:                      /* RBR */
        if (has_rx) {
            uint8_t c = (uint8_t)*s_uart_input++;
            if (c == '\\' && *s_uart_input == 'n') { c = '\n'; s_uart_input++; }
            else if (c == '\\' && *s_uart_input == 'r') { c = '\r'; s_uart_input++; }
            return c;
        }
        return 0x00;
    case 1:  return 0x00;        /* IER */
    case 2:  return 0x01;        /* IIR: no interrupt pending */
    case 3:  return s_uart_lcr;
    case 4:  return 0x00;        /* MCR */
    case 5:  return has_rx ? 0x61 : 0x60;  /* LSR: DR (if RX) | THRE | TEMT */
    case 6:  return 0x00;        /* MSR */
    case 7:  return s_uart_scr;
    }
    return 0xFF;
}

static void p2k_uart_write(void *opaque, hwaddr addr,
                           uint64_t val, unsigned size)
{
    switch (addr) {
    case 0:
        if (!(s_uart_lcr & 0x80) && s_uart_to_stderr) {
            uint8_t c = val & 0xFF;
            fwrite(&c, 1, 1, stderr);
            fflush(stderr);
        }
        break;
    case 3:
        s_uart_lcr = val & 0xFF;
        break;
    case 7:
        s_uart_scr = val & 0xFF;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps p2k_uart_ops = {
    .read       = p2k_uart_read,
    .write      = p2k_uart_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl       = { .min_access_size = 1, .max_access_size = 1 },
};

/* ---------- helper -------------------------------------------------------- */

static void p2k_iostub(MemoryRegion *io, const char *name,
                       hwaddr base, uint64_t size,
                       const MemoryRegionOps *ops)
{
    MemoryRegion *mr = g_new(MemoryRegion, 1);
    memory_region_init_io(mr, NULL, ops, NULL, name, size);
    memory_region_add_subregion(io, base, mr);
}

void p2k_install_isa_stubs(void)
{
    MemoryRegion *io = get_system_io();

    s_uart_to_stderr = (getenv("P2K_UART_TO_STDERR") != NULL);
    s_uart_input     = getenv("P2K_UART_INPUT");  /* e.g. "continue\r\n" */

    p2k_iostub(io, "p2k.i8042-data",   0x60,  1, &p2k_kbd_ops);
    p2k_iostub(io, "p2k.port61",       0x61,  1, &p2k_port61_ops);
    p2k_iostub(io, "p2k.i8042-status", 0x64,  1, &p2k_kbd_ops);
    p2k_iostub(io, "p2k.cmos",         0x70,  2, &p2k_cmos_ops);
    p2k_iostub(io, "p2k.post",         0x80,  1, &p2k_post_ops);
    p2k_iostub(io, "p2k.com1",         0x3F8, 8, &p2k_uart_ops);

    info_report("pinball2000: installed ISA stubs (kbd/0x61/cmos/post/com1)%s",
                s_uart_to_stderr ? " [UART->stderr]" : "");
}
