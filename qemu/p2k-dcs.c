/*
 * ============================================================================
 * STATUS: PARTIALLY TEMPORARY — DCS state is duplicated between this
 * module (BAR4 MMIO) and qemu/p2k-dcs-uart.c (I/O 0x138-0x13F overlay).
 *
 * Why temporary: real DCS-2 hardware has ONE state machine (the ADSP-2105
 * sound CPU). The game can talk to it through either the BAR4 word
 * window or the legacy UART port — but both views must mutate the
 * same queue/flag state. Today we have two parallel state copies that
 * happen to agree only because both see the same guest writes.
 *
 * Removal condition: refactor to a single owning DCS device object
 *   (e.g. p2k-dcs-core.c) that owns the response queue, ECHO byte, and
 *   ADSP handshake, and have BOTH this module and p2k-dcs-uart.c be
 *   thin views over the shared state — or better, make this BAR4
 *   region the only path and route the I/O port window through it via
 *   a memory-region alias.
 * Until then: any state divergence between the two paths is a likely
 * source of bugs; treat both files together when changing semantics.
 * ============================================================================
 *
 * pinball2000 DCS audio MMIO (PLX9054 BAR4 @ 0x13000000, 16 MiB window).
 *
 * Mirrors the DCS-on-BAR4 semantics validated in unicorn.old:
 *   unicorn.old/src/bar.c:549-589 (reads)
 *   unicorn.old/src/bar.c:892-1010 (writes)
 *   unicorn.old/src/io.c:1427-1655 (DCS state machine, also exposed on
 *                                   I/O port 0x138-0x13F for legacy paths)
 *
 * Game accesses BAR4 at:
 *   off 0 byte read  : echo last byte written       (SRAM/ADSP handshake)
 *   off 0 byte write : store echo
 *   off 0 word read  : pop next DCS response word
 *   off 0 word write : DCS command (5800/5A00/3A/1B/AA/0E/ACE1/...)
 *   off 2 read       : flags — bit6=ready (always), bit7=output available
 *   off 2 write      : ignored (game pokes 0x80/0x40 there during init;
 *                      flags are computed, not stored)
 *
 * Wedge confirmed at EIP=0x0E14 traces back to 0x18371c which calls a
 * DCS probe at 0x182110 then writes [0x13000002]=0x80,0x40.  With BAR4
 * as plain RAM the probe gets 0 → game falls into bogus dispatch.
 *
 * No timer here — pure state machine; sound playback is a separate
 * concern (deferred; we just need the protocol to keep the game past
 * DCS init).
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"

#include "pinball2000.h"
#include "p2k-internal.h"

#define P2K_BAR4_BASE        0x13000000u
#define P2K_BAR4_SIZE        0x01000000u   /* 16 MiB */

#define DCS_RESP_RING        64

typedef struct {
    uint16_t buf[DCS_RESP_RING];
    int      head;            /* read index */
    int      count;
    uint8_t  echo;
    int      active;          /* 0x0E suspend mode */
    int      pending;         /* ACE1 multi-word accumulator */
    int      remaining;
    int      layer;
    int      mixer[8];
    uint32_t cnt_wr, cnt_rd, cnt_flag;
} DcsState;

static DcsState s_dcs;

static void dcs_push(uint16_t v)
{
    if (s_dcs.count >= DCS_RESP_RING) {
        return;
    }
    int tail = (s_dcs.head + s_dcs.count) % DCS_RESP_RING;
    s_dcs.buf[tail] = v;
    s_dcs.count++;
}

static uint16_t dcs_pop(void)
{
    if (s_dcs.count == 0) {
        return 0;
    }
    uint16_t v = s_dcs.buf[s_dcs.head];
    s_dcs.head = (s_dcs.head + 1) % DCS_RESP_RING;
    s_dcs.count--;
    return v;
}

static void dcs_execute(uint16_t cmd)
{
    /* Active (0x0E) mode: only another 0x0E exits, then push 10 ack */
    if (s_dcs.active) {
        if (cmd == 0x0E) {
            s_dcs.active = 0;
            s_dcs.pending = 0;
            s_dcs.remaining = 0;
            s_dcs.layer = 0;
            dcs_push(10);
        }
        return;
    }

    /* ACE1 multi-word accumulator */
    if (s_dcs.pending) {
        if (s_dcs.remaining == 0) {
            s_dcs.remaining = ((cmd >> 8) == 0x55) ? 1 : 2;
            s_dcs.mixer[0] = cmd;
            s_dcs.layer = 1;
            return;
        }
        if (s_dcs.layer < 4) {
            s_dcs.mixer[s_dcs.layer++] = cmd;
        }
        if (--s_dcs.remaining != 0) {
            return;
        }
        if (s_dcs.mixer[0] == 999 || s_dcs.mixer[0] == 1000) {
            dcs_push(0x100);
            dcs_push(0x10);
        }
        /* else: real sound dispatch deferred */
        s_dcs.layer = 0;
        s_dcs.remaining = 0;
        memset(s_dcs.mixer, 0, sizeof(s_dcs.mixer));
        return;
    }

    switch (cmd) {
    case 0x5800:
    case 0x5A00:
        dcs_push(0x1000);                  /* "I'm alive" */
        break;
    case 0x3A:                              /* dong / boot ping */
        dcs_push(0xCC01);
        dcs_push(10);
        break;
    case 0x1B:
        dcs_push(0xCC09);
        dcs_push(10);
        break;
    case 0xAA:
        dcs_push(0xCC04);
        dcs_push(10);
        break;
    case 0x0E:
        s_dcs.active = 1;
        s_dcs.pending = 0;
        break;
    case 0xACE1:
        s_dcs.pending = 1;
        dcs_push(0x0100);
        dcs_push(0x0C);
        break;
    default:
        /* Sound playback commands — no response expected */
        break;
    }
}

static uint64_t p2k_dcs_read(void *opaque, hwaddr off, unsigned size)
{
    if (off == 0) {
        if (size == 1) {
            return s_dcs.echo;
        }
        s_dcs.cnt_rd++;
        return dcs_pop();
    }
    if (off == 2) {
        s_dcs.cnt_flag++;
        uint16_t f = 0x40;                 /* always ready */
        if (s_dcs.count > 0) {
            f |= 0x80;
        }
        return f;
    }
    return 0;
}

static void p2k_dcs_write(void *opaque, hwaddr off, uint64_t val,
                          unsigned size)
{
    if (off == 0) {
        if (size == 1) {
            s_dcs.echo = val & 0xFF;
            return;
        }
        s_dcs.cnt_wr++;
        dcs_execute(val & 0xFFFF);
        return;
    }
    /* off == 2 (flags) and everything else: writes are no-ops */
}

static const MemoryRegionOps p2k_dcs_ops = {
    .read  = p2k_dcs_read,
    .write = p2k_dcs_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
};

void p2k_install_dcs(void)
{
    MemoryRegion *sm = get_system_memory();
    MemoryRegion *mr = g_new(MemoryRegion, 1);

    memset(&s_dcs, 0, sizeof(s_dcs));

    memory_region_init_io(mr, NULL, &p2k_dcs_ops, NULL,
                          "p2k.bar4-dcs", P2K_BAR4_SIZE);
    memory_region_add_subregion(sm, P2K_BAR4_BASE, mr);

    info_report("pinball2000: DCS BAR4 MMIO @ 0x%08x (16 MiB)",
                P2K_BAR4_BASE);
}
