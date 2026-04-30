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

/* Diagnostic / A-B knob: P2K_DCS_NO_BYTE_PAIR=1 disables the byte-pair
 * assembly entirely (byte writes to 0x13C are still counted but become
 * no-ops as far as DCS commands go — they still fall through to the
 * 16550 MCR register at off=4 below). The point is to be able to A/B
 * the late-Unicorn `0001de2` byte-pair claim against current ROMs:
 *
 *   1. P2K_DCS_NO_BYTE_PAIR unset (default)  : byte-pair active
 *   2. P2K_DCS_NO_BYTE_PAIR=1                : byte-pair disabled
 *
 * Compare cmd_by_uart_w / cmd_by_uart_bp / boot health between the two
 * to see whether any guest actually emits byte-pair traffic and whether
 * removing it regresses anything. */
static int s_dcs_no_byte_pair = -1;
static bool p2k_dcs_no_byte_pair(void)
{
    if (s_dcs_no_byte_pair < 0) {
        const char *e = getenv("P2K_DCS_NO_BYTE_PAIR");
        s_dcs_no_byte_pair = (e && *e && e[0] != '0') ? 1 : 0;
    }
    return s_dcs_no_byte_pair == 1;
}
static unsigned s_dcs_byte_pair_skipped;
static unsigned s_dcs_byte_solo_skipped;

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

    /* off=4 (0x13C) byte: high/low command pair.
     * Per unicorn.old/src/io.c dcs2_port_write: SWE1's io-handled pump
     * at 0x194efc emits 16-bit DCS commands as TWO outb to 0x13c (HIGH
     * then LOW). Unicorn assembles the pair unconditionally; we do the
     * same. The pair handler does NOT conflict with legacy 16550 use
     * because byte writes to the data register are otherwise ignored.
     *
     * A-B verified 2026-04-30 (commit-time) on the SWE1 ROM:
     *   - default boot (auto-update path) : 0 byte-pair commands ever
     *     observed; all DCS traffic goes via word writes / BAR4. The
     *     byte-pair branch is dead code on this path, so leaving it
     *     unconditional is harmless for normal update boots.
     *   - --update none / --update none --no-savedata: 9 byte-pair
     *     commands fire (0x0000, 0x55aa, 0x609f, 0x00ec, then 0x03ce
     *     repeatedly). The 0x03ce one decodes to a real pb2kslib
     *     entry (S03CE) and produces audible audio output.
     *   - With P2K_DCS_NO_BYTE_PAIR=1 the same --update none boot
     *     emits 16+ byte writes to 0x13c that go nowhere, no DCS
     *     commands are produced, and S03CE never plays.
     * Conclusion: the byte-pair path is real silicon ROM behavior used
     * by the base/no-update path of SWE1, not late-Unicorn pollution.
     * Keep it on by default; the env knob exists for future A-B work
     * if a different ROM ever needs disagrees. */
    if (off == 4 && size == 1) {
        if (p2k_dcs_no_byte_pair()) {
            s_dcs_byte_pair_skipped++;
            if (s_dcs_byte_pair_skipped <= 16
                || (s_dcs_byte_pair_skipped & 0xFFu) == 0) {
                info_report("dcs-uart: byte write 0x%02x at 0x13c "
                            "skipped (P2K_DCS_NO_BYTE_PAIR=1) #%u",
                            (unsigned)(val & 0xFFu),
                            s_dcs_byte_pair_skipped);
            }
            /* Fall through to the 16550 register switch below: case 4
             * is MCR, harmless for DCS protocol. */
        } else if (!s_dcs_high_seen) {
            s_dcs_high_latch = (uint8_t)(val & 0xFFu);
            s_dcs_high_seen  = 1;
            return;
        } else {
            uint16_t cmd = (uint16_t)((s_dcs_high_latch << 8) | (val & 0xFFu));
            s_dcs_high_seen = 0;
            s_dcs_last_byte_pair = cmd;
            s_dcs_byte_pair_count++;
            if (p2k_dcs_byte_trace_enabled() || s_dcs_byte_pair_count <= 16) {
                info_report("dcs-uart: byte-pair cmd=0x%04x (#%u) "
                            "[0x13c byte HIGH/LOW]",
                            cmd, s_dcs_byte_pair_count);
            }
            p2k_dcs_core_note_source("UART:0x13c.bp");
            p2k_dcs_core_write_cmd(cmd);
            return;
        }
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
