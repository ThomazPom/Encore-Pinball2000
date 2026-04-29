/*
 * pinball2000 DCS-2 sound board on I/O ports 0x138-0x13F.
 *
 * Now a thin frontend.  All DCS-2 protocol state (response queue, echo
 * byte, 0x0E suspend, ACE1 capture) lives in p2k-dcs-core.c and is shared
 * with the BAR4 MMIO view in p2k-dcs.c.  This file only owns the legacy
 * 16550-style register surface that the DCS-2 board overlays on top of
 * the same I/O window — which only exists here, not in BAR4.
 *
 * Narrow-route policy (matches unicorn.old/src/io.c:1444-1650):
 *   - Word writes to off=4 (port 0x13C) -> DCS command (core)
 *   - Word reads of off=4              -> pop DCS response (core)
 *   - Reads of off=6 (port 0x13E)      -> DCS flag byte (core)
 *   - All other byte accesses          -> minimal 16550 register sim
 *                                         (TX always ready, no RX) so
 *                                         puts/printf-style probes do
 *                                         not spin
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "exec/ioport.h"
#include "exec/address-spaces.h"

#include "p2k-internal.h"

#define P2K_DCS_UART_BASE   0x138u
#define P2K_DCS_UART_LEN    0x008u

/* 16550 register state (local to this view). */
static uint8_t p2k_dcs_dll = 0x01u;
static uint8_t p2k_dcs_ier = 0x00u;
static uint8_t p2k_dcs_iir = 0x01u;
static uint8_t p2k_dcs_lcr = 0x00u;
static uint8_t p2k_dcs_mcr = 0x00u;
static uint8_t p2k_dcs_lsr = 0x60u;   /* THRE | TEMT */
static uint8_t p2k_dcs_msr = 0xB0u;   /* CTS | DSR | DCD */
static uint8_t p2k_dcs_scr = 0x00u;

/* Diagnostic: capture the late-Unicorn `0001de2` clue that DCS commands
 * are sometimes issued as TWO byte writes to 0x13C — high then low —
 * instead of a single word write.  Enable with P2K_DCS_BYTE_TRACE=1 to
 * verify whether QEMU sees the same access shape from this guest.
 *
 * State: high-byte latch + a small ring of recent (high<<8 | low) pairs
 * to log at boot end.  No effect on protocol unless trace is enabled. */
static int      s_dcs_byte_trace = -1;     /* -1=unread, 0/1=resolved */
static uint8_t  s_dcs_high_latch;
static int      s_dcs_high_seen;
static uint16_t s_dcs_last_byte_pair;
static unsigned s_dcs_byte_pair_count;

static bool p2k_dcs_byte_trace_enabled(void)
{
    if (s_dcs_byte_trace < 0) {
        const char *e = getenv("P2K_DCS_BYTE_TRACE");
        s_dcs_byte_trace = (e && *e && e[0] != '0') ? 1 : 0;
    }
    return s_dcs_byte_trace == 1;
}

static uint64_t p2k_dcs_uart_read(void *opaque, hwaddr addr, unsigned size)
{
    int off = (int)addr;

    /* off=4 (0x13C) word -> DCS data pop. */
    if (off == 4 && size >= 2) {
        return p2k_dcs_core_read_resp();
    }

    /* off=6 (0x13E) -> DCS flag byte (both byte and word). */
    if (off == 6) {
        uint8_t f = p2k_dcs_core_flag_byte();
        if (size == 1 && !(p2k_dcs_mcr & 0x10u)) {
            return (p2k_dcs_msr & 0x30u) | (f & 0xC0u);
        }
        return f;
    }

    /* Byte access: 16550 register simulation. */
    switch (off) {
    case 0:
        return (p2k_dcs_lcr & 0x80u) ? p2k_dcs_dll
                                     : p2k_dcs_core_get_echo();
    case 1:
        return (p2k_dcs_lcr & 0x80u) ? 0x00u : p2k_dcs_ier;
    case 2:
        return p2k_dcs_iir;
    case 3:
        return p2k_dcs_lcr;
    case 4:
        return p2k_dcs_mcr;
    case 5:
        return p2k_dcs_lsr;
    case 6:                            /* MSR loopback synth */
        return (uint8_t)(((p2k_dcs_mcr & 0x0Cu) << 4) |
                         ((p2k_dcs_mcr & 0x02u) << 3) |
                         ((p2k_dcs_mcr & 0x01u) << 5));
    case 7:
        return p2k_dcs_scr;
    default:
        return 0xFFu;
    }
}

static void p2k_dcs_uart_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    int off = (int)addr;

    /* off=4 (0x13C) word -> DCS command. */
    if (off == 4 && size >= 2) {
        if (p2k_dcs_byte_trace_enabled() && s_dcs_high_seen) {
            warn_report("dcs-uart: word write 0x%04x at 0x13c voids "
                        "pending byte-pair high=0x%02x",
                        (unsigned)(val & 0xFFFF), s_dcs_high_latch);
            s_dcs_high_seen = 0;
        }
        p2k_dcs_core_note_source("UART:0x13c.w");
        p2k_dcs_core_write_cmd((uint16_t)(val & 0xFFFFu));
        return;
    }

    /* off=4 (0x13C) byte: HISTORICAL diagnostic only. Unicorn
     * (unicorn.old/src/io.c:1986-1991) explicitly DROPS byte writes
     * to off=4 — only word writes (size>=2) become DCS commands;
     * byte writes fall through to the 16550 MCR (case 4 below). The
     * "0001de2" clue + 86c2412 ("byte-pair as real protocol") was a
     * misread: SWE1 io-handled mode does NOT emit DCS commands as
     * byte pairs in Unicorn's port handler. Synthesizing commands
     * from byte writes here jams the real DCS pipe with garbage like
     * 0x55aa / 0x609f / 0x00ec (observed in --no-savedata --update
     * none, which made S03CE loop forever and no real audio init).
     * Keep the latch+log strictly behind P2K_DCS_BYTE_TRACE so it
     * remains a diagnostic aid, never a protocol path. */
    if (off == 4 && size == 1 && p2k_dcs_byte_trace_enabled()) {
        if (!s_dcs_high_seen) {
            s_dcs_high_latch = (uint8_t)(val & 0xFFu);
            s_dcs_high_seen  = 1;
        } else {
            uint16_t cmd = (uint16_t)((s_dcs_high_latch << 8) | (val & 0xFFu));
            s_dcs_high_seen = 0;
            s_dcs_last_byte_pair = cmd;
            s_dcs_byte_pair_count++;
            info_report("dcs-uart: [diag] byte-pair seen=0x%04x (#%u) "
                        "[NOT delivered to core; Unicorn drops these]",
                        cmd, s_dcs_byte_pair_count);
        }
        /* Fall through to MCR write below (Unicorn-equivalent). */
    }

    /* Byte: 16550 registers. */
    switch (off) {
    case 0:
        if (p2k_dcs_lcr & 0x80u) {
            p2k_dcs_dll = val & 0xFFu;
        } else {
            p2k_dcs_core_set_echo(val & 0xFFu);
        }
        break;
    case 1:
        if (!(p2k_dcs_lcr & 0x80u)) {
            p2k_dcs_ier = val & 0xFFu;
        }
        break;
    case 3:
        p2k_dcs_lcr = val & 0xFFu;
        break;
    case 4:
        p2k_dcs_mcr = val & 0xFFu;
        break;
    case 7:
        p2k_dcs_scr = val & 0xFFu;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps p2k_dcs_uart_ops = {
    .read  = p2k_dcs_uart_read,
    .write = p2k_dcs_uart_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = { .min_access_size = 1, .max_access_size = 4 },
    .valid = { .min_access_size = 1, .max_access_size = 4 },
};

void p2k_install_dcs_uart(void)
{
    MemoryRegion *io = get_system_io();
    MemoryRegion *mr = g_new0(MemoryRegion, 1);

    p2k_dcs_core_reset();

    memory_region_init_io(mr, NULL, &p2k_dcs_uart_ops, NULL,
                          "p2k-dcs-uart", P2K_DCS_UART_LEN);
    memory_region_add_subregion(io, P2K_DCS_UART_BASE, mr);

    info_report("pinball2000: DCS-2 UART installed at I/O 0x%03x-0x%03x [shared core]",
                P2K_DCS_UART_BASE,
                P2K_DCS_UART_BASE + P2K_DCS_UART_LEN - 1);
}
