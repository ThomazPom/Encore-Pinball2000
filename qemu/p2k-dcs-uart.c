/*
 * ============================================================================
 * STATUS: PARTIALLY TEMPORARY — see p2k-dcs.c header. This module owns
 * a parallel copy of the DCS response queue + flag byte that should be
 * the SAME state as the BAR4 view.
 *
 * Removal condition: collapse with p2k-dcs.c into a single DCS device
 * object whose state is shared. This file then becomes either an alias
 * MemoryRegion over the BAR4 window or a thin IO port view that calls
 * into the shared state.
 * ============================================================================
 *
 * pinball2000 DCS-2 sound board on I/O ports 0x138-0x13F.
 *
 * Direct port of unicorn.old/src/io.c:1444-1650 (DCS UART overlay).
 * The DCS-2 board exposes a 16550-style UART register window AND
 * overlays a word-mode command/data channel at off=4 (port 0x13C) plus
 * a flag/status read at off=6 (port 0x13E).  XINU's resource manager
 * uses this window during early init; without it, Resource<T>::get_value
 * recursively panics with "Retrieve Resource (get &) Failed".
 *
 * Narrow-route policy (matches unicorn.old):
 *   - Word writes to off=4 (0x13C) → DCS command, executed synchronously
 *   - Word reads of off=4 → pop DCS response queue
 *   - Reads of off=6 (0x13E) → DCS flag byte (0x40 ready, 0x80 if data)
 *   - All other byte accesses → minimal 16550 register simulation that
 *     reports "TX always ready, no RX" so puts/printf-style probes do
 *     not spin.  We do NOT pretend a full UART is present, because that
 *     misdetection sends some games down a Resource-retrieval-NonFatal
 *     path.
 *
 * No timing, no IRQs, no Unicorn batches.  Pure I/O.  Sound playback is
 * deliberately not wired here — we only need the protocol replies that
 * unblock resource init.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "exec/ioport.h"
#include "exec/address-spaces.h"

#include "p2k-internal.h"

#define P2K_DCS_UART_BASE   0x138u
#define P2K_DCS_UART_LEN    0x008u

/* 16550 register state (matches unicorn.old defaults). */
static uint8_t p2k_dcs_dll = 0x01u;
static uint8_t p2k_dcs_ier = 0x00u;
static uint8_t p2k_dcs_iir = 0x01u;
static uint8_t p2k_dcs_lcr = 0x00u;
static uint8_t p2k_dcs_mcr = 0x00u;
static uint8_t p2k_dcs_lsr = 0x60u;   /* THRE | TEMT */
static uint8_t p2k_dcs_msr = 0xB0u;   /* CTS | DSR | DCD */
static uint8_t p2k_dcs_scr = 0x00u;

/* DCS-2 protocol queue + control flags. */
static struct {
    uint16_t buf[32];
    int      wr;
    int      rd;
    uint8_t  echo;
    int      pending;        /* ACE1 multi-word capture */
    int      active;         /* 0x0E "active" hold */
    int      mixer[8];
    int      layer;
    int      remaining;
    int      cmd;
} p2k_dcs;

static void p2k_dcs_push(uint16_t v)
{
    p2k_dcs.buf[p2k_dcs.wr] = v;
    p2k_dcs.wr = (p2k_dcs.wr + 1) & 31;
}

static uint16_t p2k_dcs_pop(void)
{
    if (p2k_dcs.wr == p2k_dcs.rd) {
        return 0;
    }
    uint16_t v = p2k_dcs.buf[p2k_dcs.rd];
    p2k_dcs.rd = (p2k_dcs.rd + 1) & 31;
    return v;
}

/* Mirrors unicorn.old/src/io.c:dcs_io_execute. */
static void p2k_dcs_execute(void)
{
    int cmd = p2k_dcs.cmd;

    if (p2k_dcs.active && cmd == 0x0E) {
        p2k_dcs.active  = 0;
        p2k_dcs.pending = 0;
        p2k_dcs_push(10);
        return;
    }
    if (p2k_dcs.active) {
        return;
    }

    if (p2k_dcs.pending) {
        if (p2k_dcs.remaining == 0) {
            p2k_dcs.remaining = ((cmd >> 8) == 0x55) ? 1 : 2;
            p2k_dcs.mixer[0]  = cmd;
            p2k_dcs.layer     = 1;
            return;
        }
        p2k_dcs.mixer[p2k_dcs.layer++] = cmd;
        if (--p2k_dcs.remaining != 0) {
            return;
        }
        if (p2k_dcs.mixer[0] == 999 || p2k_dcs.mixer[0] == 1000) {
            p2k_dcs_push(0x100);
            p2k_dcs_push(0x10);
        }
        /* mixer execution intentionally elided — no audio backend yet. */
        p2k_dcs.layer     = 0;
        p2k_dcs.remaining = 0;
        memset(p2k_dcs.mixer, 0, sizeof(p2k_dcs.mixer));
        return;
    }

    switch (cmd) {
    case 0x5800:                       /* RESET */
    case 0x5A00:
        p2k_dcs_push(0x1000);
        break;
    case 0x3A:                         /* boot dong */
        p2k_dcs_push(0xCC01);
        p2k_dcs_push(10);
        break;
    case 0x1B:
        p2k_dcs_push(0xCC09);
        p2k_dcs_push(10);
        break;
    case 0xAA:                         /* audio init handshake */
        p2k_dcs_push(0xCC04);
        p2k_dcs_push(10);
        break;
    case 0x0E:
        p2k_dcs.active  = 1;
        p2k_dcs.pending = 0;
        break;
    case 0xACE1:
        p2k_dcs.pending = 1;
        p2k_dcs_push(0x0100);
        p2k_dcs_push(0x0C);
        break;
    default:
        /* Unknown — drop, no reply (matches unicorn.old fallback to
         * sound_process_cmd which we don't ship). */
        break;
    }
}

static uint64_t p2k_dcs_read(void *opaque, hwaddr addr, unsigned size)
{
    int off = (int)addr;

    /* off=4 (0x13C) word → DCS data pop. */
    if (off == 4 && size >= 2) {
        return p2k_dcs_pop();
    }

    /* off=6 (0x13E) → DCS flags (both byte and word).
     * 0x40 = always ready to accept; 0x80 = data available. */
    if (off == 6) {
        uint16_t f = 0x40u;
        if (p2k_dcs.wr != p2k_dcs.rd) {
            f |= 0x80u;
        }
        if (size == 1 && !(p2k_dcs_mcr & 0x10u)) {
            return (p2k_dcs_msr & 0x30u) | (f & 0xC0u);
        }
        return f;
    }

    /* Byte access: 16550 register simulation. */
    switch (off) {
    case 0:
        return (p2k_dcs_lcr & 0x80u) ? p2k_dcs_dll : p2k_dcs.echo;
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

static void p2k_dcs_write(void *opaque, hwaddr addr,
                          uint64_t val, unsigned size)
{
    int off = (int)addr;

    /* off=4 (0x13C) word → DCS command. */
    if (off == 4 && size >= 2) {
        p2k_dcs.cmd = (int)(val & 0xFFFFu);
        p2k_dcs_execute();
        return;
    }

    /* Byte: 16550 registers. */
    switch (off) {
    case 0:
        if (p2k_dcs_lcr & 0x80u) {
            p2k_dcs_dll = val & 0xFFu;
        } else {
            p2k_dcs.echo = val & 0xFFu;
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
    .read  = p2k_dcs_read,
    .write = p2k_dcs_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = { .min_access_size = 1, .max_access_size = 4 },
    .valid = { .min_access_size = 1, .max_access_size = 4 },
};

void p2k_install_dcs_uart(void)
{
    MemoryRegion *io = get_system_io();
    MemoryRegion *mr = g_new0(MemoryRegion, 1);

    memory_region_init_io(mr, NULL, &p2k_dcs_uart_ops, NULL,
                          "p2k-dcs-uart", P2K_DCS_UART_LEN);
    memory_region_add_subregion(io, P2K_DCS_UART_BASE, mr);

    info_report("pinball2000: DCS-2 UART installed at I/O 0x%03x-0x%03x",
                P2K_DCS_UART_BASE,
                P2K_DCS_UART_BASE + P2K_DCS_UART_LEN - 1);
}
