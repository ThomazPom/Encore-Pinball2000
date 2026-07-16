/*
 * pinball2000 minimal ISA-port stubs.
 *
 * QEMU's i386 PC machinery normally provides keyboard, RTC/CMOS, system
 * control port B, COM1/COM2, etc. — but we deliberately do NOT use the `pc`
 * machine.  We only attach what PRISM/XINU actually polls so the boot
 * loops terminate.  Each stub mimics the legacy I/O port handler.
 *
 * Currently provided:
 *   0x60 / 0x64       i8042 keyboard controller (always idle)
 *   0x61              system-control port B (bit 4 toggles each read)
 *   0x70 / 0x71       CMOS index/data (zeroed)
 *   0x80              POST code (write-only side-effect, read returns 0)
 *   0x2F8..0x2FF      COM2 16550 UART — probe-compatible register model
 *   0x3F8..0x3FF      COM1 16550 UART — register model + console bridge
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
#include "system/system.h"
#include "chardev/char-fe.h"
#include "chardev/char.h"

#include "p2k-internal.h"

/* ---------- i8042 keyboard ------------------------------------------------
 *
 * Minimal AT-style controller, ported from the legacy I/O port handlers.
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
/* Legacy hand-rolled CMOS emulator. As of 2026-07-10 the machine
 * uses upstream QEMU mc146818_rtc by default (pinball2000.c). This
 * code remains as an opt-in fallback (P2K_USE_MC146818=0) and for
 * archaeological reference: the hand-rolled BCD encoding caused
 * XINA v2.10 & v2.0 to display years as shown = 1999 + 2 * y (the
 * XINA date-conversion routine near guest EIP 0x0025ceb4 doubles
 * the year), while upstream mc146818 hits a XINA code path that
 * displays the correct year. Root cause of the behavioural
 * difference between the two CMOS implementations was not fully
 * traced. */

static uint8_t s_cmos_addr;
static uint8_t s_cmos_data[128];

static uint64_t p2k_cmos_read(void *opaque, hwaddr addr, unsigned size)
{
    if (addr == 0) return s_cmos_addr;
    uint8_t reg = s_cmos_addr & 0x7F;
    /* Return live time on RTC reg reads. */
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
        s_cmos_data[0x0A] = 0x26;   /* 32 kHz / 1024 Hz, UIP=0 */
        s_cmos_data[0x0B] = 0x02;   /* BCD + 24h */
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

typedef struct P2KUartState {
    uint8_t lcr;                 /* bit 7 = DLAB */
    uint8_t scr;
    uint8_t ier;                 /* bit 0 RDA, bit 1 THRI */
    uint8_t mcr;
    uint8_t fcr;
    uint8_t dll;
    uint8_t dlm;
    uint8_t loopback_rx;
    bool loopback_rx_valid;
    bool thri_pending;
} P2KUartState;

static P2KUartState s_uart[2];   /* COM1 (0x3f8), COM2 (0x2f8) */
static bool    s_uart_to_stderr;
static qemu_irq s_uart_irq;      /* ISA IRQ4 line, set by p2k_isa_set_uart_irq */

static P2KUartState *p2k_uart_state(void *opaque)
{
    return &s_uart[(uintptr_t)opaque == 0x2f8 ? 1 : 0];
}

/* TX line filter — drops whole lines from the UART output stream that
 * match any space-separated substring in P2K_UART_DROP (default
 * "swd Debug:"). Bytes are buffered until '\n' (or buffer full); on
 * flush, the line is forwarded to chardev/stderr ONLY if no filter
 * substring matches. Set P2K_UART_DROP="" to disable. */
#define P2K_TX_LINE_MAX 1024
static char     s_tx_line[P2K_TX_LINE_MAX];
static size_t   s_tx_line_len;
static char    *s_tx_drop_pats;        /* malloc'd copy of env */
static char   **s_tx_drop_vec;         /* NULL-terminated */
static unsigned s_tx_drop_n;
static bool     s_tx_filter_inited;

/* Bidirectional QEMU chardev frontend (e.g. -serial tcp:host:port,server,nowait).
 * TX  : every guest byte written to THR is mirrored to the chardev so
 *       --uart-tcp / nc see the XINU/NonFatal stream.
 * RX  : bytes received on the chardev (e.g. typed via `nc host port`)
 *       are pushed into s_rx_ring and surfaced to the guest via RBR;
 *       IRQ4 fires when IER bit 0 (RDA) is enabled. This is what makes
 *       --serial-tcp interactive — type XINA monitor commands like
 *       `continue\r\n` or `?` straight from your terminal. */
static CharBackend s_uart_be;
static bool        s_uart_be_inited;

#define P2K_UART_RX_RING 256
static uint8_t  s_rx_ring[P2K_UART_RX_RING];
static unsigned s_rx_head;       /* next byte to consume */
static unsigned s_rx_tail;       /* next free slot      */
static QemuMutex s_rx_lock;

static inline unsigned p2k_rx_count(void)
{
    return (s_rx_tail - s_rx_head) & (P2K_UART_RX_RING - 1);
}

static int p2k_uart_can_read(void *opaque)
{
    qemu_mutex_lock(&s_rx_lock);
    int free = (P2K_UART_RX_RING - 1) - (int)p2k_rx_count();
    qemu_mutex_unlock(&s_rx_lock);
    return free > 0 ? free : 0;
}

static void p2k_uart_receive(void *opaque, const uint8_t *buf, int size)
{
    qemu_mutex_lock(&s_rx_lock);
    for (int i = 0; i < size; i++) {
        unsigned next = (s_rx_tail + 1) & (P2K_UART_RX_RING - 1);
        if (next == s_rx_head) break;     /* ring full, drop */
        s_rx_ring[s_rx_tail] = buf[i];
        s_rx_tail = next;
    }
    bool has = (s_rx_head != s_rx_tail);
    qemu_mutex_unlock(&s_rx_lock);
    /* Edge-triggered ISA IRQ4: pulse if the guest enabled RDA. */
    if (has && s_uart_irq && (s_uart[0].ier & 0x01)) {
        qemu_irq_pulse(s_uart_irq);
    }
}

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
    if (s_uart_irq && s_uart[0].thri_pending && (s_uart[0].ier & 0x02)) {
        qemu_irq_pulse(s_uart_irq);
    }
}

/* Pre-canned RX buffer fed into RBR.  Source: P2K_UART_INPUT env var.
 * Lets us send "continue\r\n" to the XINA monitor without a chardev.
 * Real chardev RX (s_rx_ring, fed by qemu_chr_fe handlers) takes
 * priority — env input is only a fallback for headless one-shot runs.
 *
 * The string is consumed ONCE, left-to-right. SWE1's XINU control()
 * op=22 callback (killsafe_waits_msg → ttysputc) performs synchronous
 * polled reads with interrupts disabled during boot; pre-stuffing a
 * handful of CR/LF bytes lets that polling loop complete so boot can
 * proceed. We deliberately do NOT cycle the buffer — feeding an
 * infinite stream of CRs keeps the diag prompt re-entering itself
 * (every CR submits an empty command, eliciting more output and more
 * reads) and starves the rest of the system worse than the original
 * wedge. A bounded one-shot pattern lets the boot fall out of the
 * diag handler once XINU's other init paths complete. */
static const char *s_uart_input;

static bool p2k_uart_pop_rx(uint8_t *out)
{
    qemu_mutex_lock(&s_rx_lock);
    if (s_rx_head == s_rx_tail) {
        qemu_mutex_unlock(&s_rx_lock);
        return false;
    }
    *out = s_rx_ring[s_rx_head];
    s_rx_head = (s_rx_head + 1) & (P2K_UART_RX_RING - 1);
    bool more = (s_rx_head != s_rx_tail);
    qemu_mutex_unlock(&s_rx_lock);
    /* If more bytes remain, re-pulse so the guest comes back for them. */
    if (more && s_uart_irq && (s_uart[0].ier & 0x01)) {
        qemu_irq_pulse(s_uart_irq);
    }
    return true;
}

static bool p2k_uart_has_rx(void)
{
    qemu_mutex_lock(&s_rx_lock);
    bool has_ring = (s_rx_head != s_rx_tail);
    qemu_mutex_unlock(&s_rx_lock);
    return has_ring || (s_uart_input && *s_uart_input);
}

static uint64_t p2k_uart_read(void *opaque, hwaddr addr, unsigned size)
{
    P2KUartState *uart = p2k_uart_state(opaque);
    bool com1 = uart == &s_uart[0];
    bool has_rx = uart->loopback_rx_valid || (com1 && p2k_uart_has_rx());
    switch (addr) {
    case 0:                      /* RBR */
        {
            if (uart->lcr & 0x80) {
                return uart->dll;
            }
            if (uart->loopback_rx_valid) {
                uart->loopback_rx_valid = false;
                return uart->loopback_rx;
            }
            uint8_t c;
            if (com1 && p2k_uart_pop_rx(&c)) {
                return c;
            }
            if (com1 && s_uart_input && *s_uart_input) {
                c = (uint8_t)*s_uart_input++;
                if (c == '\\' && *s_uart_input == 'n') { c = '\n'; s_uart_input++; }
                else if (c == '\\' && *s_uart_input == 'r') { c = '\r'; s_uart_input++; }
                return c;
            }
            return 0x00;
        }
    case 1:  return (uart->lcr & 0x80) ? uart->dlm : uart->ier;
    case 2: {                    /* IIR */
        uint8_t fifo = (uart->fcr & 0x01) ? 0xc0 : 0x00;
        /* Priority: RX-data > THR-empty > none. THRI clears on read. */
        if (has_rx && (uart->ier & 0x01)) {
            return fifo | 0x04;  /* RDA interrupt */
        }
        if (uart->thri_pending && (uart->ier & 0x02)) {
            uart->thri_pending = false;
            return fifo | 0x02;  /* THR-empty interrupt */
        }
        return fifo | 0x01;      /* no pending */
    }
    case 3:  return uart->lcr;
    case 4:  return uart->mcr;
    case 5:  return has_rx ? 0x61 : 0x60;  /* LSR: DR (if RX) | THRE | TEMT */
    case 6:
        /* 16550 internal loopback reflects MCR outputs onto modem inputs. */
        if (uart->mcr & 0x10) {
            return ((uart->mcr & 0x02) ? 0x10 : 0) | /* RTS -> CTS */
                   ((uart->mcr & 0x01) ? 0x20 : 0) | /* DTR -> DSR */
                   ((uart->mcr & 0x04) ? 0x40 : 0) | /* OUT1 -> RI */
                   ((uart->mcr & 0x08) ? 0x80 : 0);  /* OUT2 -> DCD */
        }
        return 0x00;
    case 7:  return uart->scr;
    }
    return 0xFF;
}

static void p2k_tx_filter_init(void)
{
    if (s_tx_filter_inited) return;
    s_tx_filter_inited = true;
    const char *env = getenv("P2K_UART_DROP");
    if (!env) env = "swd Debug:";  /* sensible default — phantom switch debug */
    if (!*env) return;             /* explicit disable */
    s_tx_drop_pats = g_strdup(env);
    /* Tab-separated to allow patterns containing spaces (default uses one). */
    const char *sep = strchr(s_tx_drop_pats, '\t') ? "\t" : "\n";
    /* Single pattern by default; multi-pattern via env using tab/newline. */
    unsigned cap = 4;
    s_tx_drop_vec = g_new0(char *, cap + 1);
    char *save = NULL;
    for (char *tok = strtok_r(s_tx_drop_pats, sep, &save);
         tok; tok = strtok_r(NULL, sep, &save)) {
        if (s_tx_drop_n + 1 >= cap) {
            cap *= 2;
            s_tx_drop_vec = g_realloc(s_tx_drop_vec, sizeof(char *) * (cap + 1));
        }
        s_tx_drop_vec[s_tx_drop_n++] = tok;
    }
    s_tx_drop_vec[s_tx_drop_n] = NULL;
}

static bool p2k_tx_line_matches_drop(const char *line, size_t len)
{
    if (!s_tx_drop_n) return false;
    /* line is NOT NUL-terminated; we use memmem-equivalent. */
    for (unsigned i = 0; i < s_tx_drop_n; i++) {
        const char *pat = s_tx_drop_vec[i];
        size_t plen = strlen(pat);
        if (plen == 0 || plen > len) continue;
        if (memmem(line, len, pat, plen)) return true;
    }
    return false;
}

static void p2k_tx_emit(const char *buf, size_t len)
{
    if (!len) return;
    if (s_uart_to_stderr) {
        fwrite(buf, 1, len, stderr);
        fflush(stderr);
    }
    if (s_uart_be_inited) {
        qemu_chr_fe_write_all(&s_uart_be, (const uint8_t *)buf, len);
    }
}

static void p2k_tx_push_byte(uint8_t c)
{
    p2k_tx_filter_init();
    /* Buffer up to a newline; on '\n' decide drop-or-emit the whole line.
     * XINU's interactive prompt is the newline-less token "% ". Flush that
     * exact standalone token immediately, otherwise it remains hidden until
     * the user's next command contributes a newline and makes the console
     * look as though the previous command never completed. */
    if (s_tx_line_len < sizeof(s_tx_line)) {
        s_tx_line[s_tx_line_len++] = (char)c;
    }
    bool xinu_prompt = s_tx_line_len == 2 &&
                       s_tx_line[0] == '%' && s_tx_line[1] == ' ';
    bool flush = (c == '\n') || xinu_prompt ||
                 (s_tx_line_len == sizeof(s_tx_line));
    if (!flush) return;
    if (!p2k_tx_line_matches_drop(s_tx_line, s_tx_line_len)) {
        p2k_tx_emit(s_tx_line, s_tx_line_len);
    }
    s_tx_line_len = 0;
}

static void p2k_uart_write(void *opaque, hwaddr addr,
                           uint64_t val, unsigned size)
{
    P2KUartState *uart = p2k_uart_state(opaque);
    bool com1 = uart == &s_uart[0];
    switch (addr) {
    case 0:
        if (uart->lcr & 0x80) {
            uart->dll = val & 0xff;
        } else {
            uint8_t c = val & 0xFF;
            if (uart->mcr & 0x10) {
                uart->loopback_rx = c;
                uart->loopback_rx_valid = true;
            } else if (com1) {
                p2k_tx_push_byte(c);
            }
            /* TX is instant — flag THR-empty so the next IIR read (or our
             * pulse below) signals the guest. */
            uart->thri_pending = true;
            if (com1) {
                p2k_uart_update_irq();
            }
        }
        break;
    case 1:
        if (uart->lcr & 0x80) {
            uart->dlm = val & 0xff;
        } else {
            uart->ier = val & 0x0F;
            /* Newly enabled THRI on an already-empty transmitter must fire
             * an interrupt immediately (16550 semantics). */
            uart->thri_pending = true;
            if (com1) {
                p2k_uart_update_irq();
            }
        }
        break;
    case 2:
        uart->fcr = val & 0xc9;
        if (val & 0x02) {
            uart->loopback_rx_valid = false;
        }
        break;
    case 3:
        uart->lcr = val & 0xFF;
        break;
    case 4:
        uart->mcr = val & 0x1f;
        break;
    case 7:
        uart->scr = val & 0xFF;
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

    /* RX ring lock + bidirectional chardev frontend. Bind to the first
     * -serial slot (tcp:host:port, stdio, unix socket, file, …) and
     * register can_read/receive handlers so typing into `nc host port`
     * surfaces in the guest UART RBR — the XINA monitor becomes
     * interactive over --serial-tcp. */
    qemu_mutex_init(&s_rx_lock);
    Chardev *chr = serial_hd(0);
    if (chr) {
        qemu_chr_fe_init(&s_uart_be, chr, &error_abort);
        qemu_chr_fe_set_handlers(&s_uart_be,
                                 p2k_uart_can_read,
                                 p2k_uart_receive,
                                 NULL,   /* event */
                                 NULL,   /* be_change */
                                 NULL,   /* opaque */
                                 NULL,   /* context */
                                 true);  /* set_open */
        s_uart_be_inited = true;
    }

    p2k_iostub(io, "p2k.i8042-data",   0x60,  1, &p2k_kbd_ops);
    p2k_iostub(io, "p2k.port61",       0x61,  1, &p2k_port61_ops);
    p2k_iostub(io, "p2k.i8042-status", 0x64,  1, &p2k_kbd_ops);
    /* CMOS: default OFF (upstream mc146818 owns 0x70/0x71 -- see
     * pinball2000.c). Opt-in to legacy hand-rolled CMOS with
     * P2K_USE_MC146818=0. The hand-rolled implementation had XINA
     * date-display doubling bug, kept only for A/B. */
    {
        const char *mc = getenv("P2K_USE_MC146818");
        if (mc && mc[0] == '0') {
            p2k_iostub(io, "p2k.cmos",         0x70,  2, &p2k_cmos_ops);
        }
    }
    p2k_iostub(io, "p2k.post",         0x80,  1, &p2k_post_ops);
    p2k_iostub(io, "p2k.com2",         0x2F8, 8, &p2k_uart_ops);
    p2k_iostub(io, "p2k.com1",         0x3F8, 8, &p2k_uart_ops);

    info_report("pinball2000: installed ISA stubs (kbd/0x61/cmos/post/com1/com2)%s%s",
                s_uart_to_stderr ? " [UART->stderr]" : "",
                s_uart_be_inited ? " [UART<->chardev bidir]" : "");
}
