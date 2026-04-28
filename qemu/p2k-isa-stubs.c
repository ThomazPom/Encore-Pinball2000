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

/* ---------- i8042 keyboard ------------------------------------------------
 *
 * Minimal AT-style controller, ported from unicorn.old/src/io.c:1670-1881.
 *
 *   port 0x60 (data):
 *     read   -> outbuf, clears OBF
 *     write  -> ignored (placeholder until we model PS/2 cmds)
 *
 *   port 0x64 (status/cmd):
 *     read   -> kbc_status (initial 0x14: self-test passed, IBF clear)
 *     write  -> latch a sensible response into outbuf and set OBF.
 *               0xAA  controller self-test       outbuf := 0x55
 *               0xAB  interface test             outbuf := 0x00
 *               otherwise outbuf stays as-is, but OBF is asserted so
 *               polling loops complete.
 */

static uint8_t s_kbc_status = 0x14;   /* self-test passed, IBF clear */
static uint8_t s_kbc_outbuf = 0x55;

static uint64_t p2k_kbd_read(void *opaque, hwaddr addr, unsigned size)
{
    uint8_t port = (uint8_t)(uintptr_t)opaque;
    if (port == 0x60) {
        s_kbc_status &= ~0x01u;   /* OBF cleared on data read */
        return s_kbc_outbuf;
    }
    return s_kbc_status;          /* port 0x64 status */
}

static void p2k_kbd_write(void *opaque, hwaddr addr,
                          uint64_t val, unsigned size)
{
    uint8_t port = (uint8_t)(uintptr_t)opaque;
    if (port == 0x64) {
        switch (val & 0xFF) {
        case 0xAA: s_kbc_outbuf = 0x55; break;  /* self-test OK */
        case 0xAB: s_kbc_outbuf = 0x00; break;  /* interface test OK */
        case 0x20: /* read CCB */ s_kbc_outbuf = 0x45; break;
        case 0xD1: case 0xFE: default: break;
        }
        s_kbc_status = 0x15;  /* OBF + self-test passed */
    }
    /* port 0x60 data writes — ignored (no real PS/2 device). */
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
static uint8_t s_uart_ier;       /* bit 0 RDA, bit 1 THRI */
static bool    s_uart_thri_pending;
static bool    s_uart_to_stderr;
static qemu_irq s_uart_irq;      /* ISA IRQ4 line, set by p2k_isa_set_uart_irq */

void p2k_isa_set_uart_irq(qemu_irq irq)
{
    s_uart_irq = irq;
}

static void p2k_uart_update_irq(void)
{
    /* THRI fires whenever transmit holding is empty AND IER bit 1 is set.
     * Our TX is "instant" (we already wrote to stderr) so THRE is always
     * true after a write — pulse the line. i8259 is edge-triggered for
     * ISA, so a pulse is sufficient. */
    if (s_uart_irq && s_uart_thri_pending && (s_uart_ier & 0x02)) {
        qemu_irq_pulse(s_uart_irq);
    }
}

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
    case 1:  return s_uart_ier;  /* IER */
    case 2: {                    /* IIR */
        /* Priority: RX-data > THR-empty > none. THRI clears on read. */
        if (has_rx && (s_uart_ier & 0x01)) {
            return 0x04;         /* RDA interrupt */
        }
        if (s_uart_thri_pending && (s_uart_ier & 0x02)) {
            s_uart_thri_pending = false;
            return 0x02;         /* THR-empty interrupt */
        }
        return 0x01;             /* no pending */
    }
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
        if (!(s_uart_lcr & 0x80)) {
            if (s_uart_to_stderr) {
                uint8_t c = val & 0xFF;
                fwrite(&c, 1, 1, stderr);
                fflush(stderr);
            }
            /* TX is instant — flag THR-empty so the next IIR read (or our
             * pulse below) signals the guest. */
            s_uart_thri_pending = true;
            p2k_uart_update_irq();
        }
        break;
    case 1:
        if (!(s_uart_lcr & 0x80)) {
            s_uart_ier = val & 0x0F;
            /* Newly enabled THRI on an already-empty transmitter must fire
             * an interrupt immediately (16550 semantics). */
            s_uart_thri_pending = true;
            p2k_uart_update_irq();
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
    void *opaque = (void *)(uintptr_t)base;
    memory_region_init_io(mr, NULL, ops, opaque, name, size);
    memory_region_add_subregion(io, base, mr);
}

void p2k_install_isa_stubs(void)
{
    MemoryRegion *io = get_system_io();

    /* UART/XINA mirror to host stderr is ON by default so Fatal/NonFatal/
     * monitor output is visible during bring-up without remembering an
     * env var. Set P2K_NO_UART_STDERR=1 to silence. P2K_UART_TO_STDERR
     * still works as an explicit override. */
    {
        const char *off = getenv("P2K_NO_UART_STDERR");
        const char *on  = getenv("P2K_UART_TO_STDERR");
        if (off && *off && off[0] != '0') {
            s_uart_to_stderr = false;
        } else if (on && *on && on[0] != '0') {
            s_uart_to_stderr = true;
        } else {
            s_uart_to_stderr = true;  /* default ON */
        }
    }
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
