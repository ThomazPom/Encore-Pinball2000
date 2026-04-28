/*
 * pinball2000 LPT driver-board protocol on ports 0x378-0x37A.
 *
 * P2K talks to its driver board (sound, lamps, switch matrix scan) over
 * the parallel port using a tiny edge-detect state machine — see
 * unicorn.old/src/io.c:705-1245 ("BT-120: Faithful P2K-driver
 * processParallelPortAccess protocol").
 *
 * Minimum-viable port (no host LPT, no input injection, no audio):
 *
 *   0x378 (DATA)   WRITE: latch
 *                  READ:  if rendering gated → switch-matrix status (idle 0)
 *                          else → echo last data byte
 *   0x379 (STATUS) READ:  always 0x87 (driver-board signature)
 *   0x37A (CTRL)   WRITE: edge-detect protocol:
 *                          bit2 rising  → capture data → opcode latch
 *                          bit0 falling → dispatch process_data_command
 *                  READ:  echo the last value written
 *
 * No timing, no IRQ, no LPT pacing.  The game sees an idle driver
 * board with all switches open / coin door closed / no flippers, which
 * is the boot-quiescent state.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "exec/address-spaces.h"
#include "exec/ioport.h"

#include "p2k-internal.h"

/* P2K rendering/switch state machine (mirrors io.c:720-742). */
static uint8_t s_lpt_data;
static uint8_t s_rendering_flags;
static uint8_t s_data_for_rendering;
static uint8_t s_rendering_data_val;
static uint8_t s_rendering_status[8];
static uint8_t s_data_val2;
static int     s_access_mode4_prev;
static int     s_access_mode1_prev;

/* Cabinet interlock — door starts CLOSED so the "OPEN COIN DOOR"
 * overlay disappears and play is enabled (mirrors io.c:756). */
static const uint8_t s_coin_door_closed = 1;

static int calc_bitwise_sum(uint8_t val)
{
    int has_bit = 0, sum = 0, pos = 0;
    for (unsigned v = val; v != 0; v >>= 1, pos++) {
        has_bit = 1;
        if (v & 1) sum += pos;
    }
    return has_bit + sum;
}

static uint8_t retrieve_rendering_status(uint8_t opcode)
{
    switch (opcode) {
    case 0x00: return 0x00;                        /* coin slots idle */
    case 0x01: return s_coin_door_closed ? 0x02 : 0x00;  /* door interlock */
    case 0x02: return 0xF0;                        /* status hi nibble */
    case 0x03: return 0x00;                        /* coin-door buttons idle */
    case 0x04: {
        int sel  = calc_bitwise_sum(s_rendering_data_val);
        int slot = (sel >= 1 && sel <= 8) ? sel : 1;
        return s_rendering_status[slot & 7];
    }
    case 0x0F: return 0x00;
    case 0x10: case 0x11: return 0xFF;             /* "no strobe" idle */
    default:   return 0x00;
    }
}

static void process_data_command(uint8_t opcode, uint8_t data)
{
    switch (opcode) {
    case 0x05: s_rendering_data_val = data; break;
    case 0x06: s_data_val2 = data; break;
    case 0x08: {
        if (data != 0) {
            int idx = calc_bitwise_sum(data);
            if (idx > 0 && idx < 8) s_rendering_status[idx] = s_data_val2;
        }
        break;
    }
    default: break;
    }
}

static uint64_t p2k_lpt_read(void *opaque, hwaddr addr, unsigned size)
{
    switch (addr) {
    case 0: { /* DATA */
        int gated = (s_rendering_flags & 0x01) && (s_rendering_flags & 0x08);
        return gated ? retrieve_rendering_status(s_data_for_rendering)
                     : s_lpt_data;
    }
    case 1:  return 0x87;                          /* STATUS = signature */
    case 2:  return s_rendering_flags;             /* CTRL echo */
    default: return 0xFF;
    }
}

static void p2k_lpt_write(void *opaque, hwaddr addr,
                          uint64_t val, unsigned size)
{
    switch (addr) {
    case 0:
        s_lpt_data = val & 0xFF;
        break;
    case 2: {
        uint8_t newctrl = val & 0xFF;
        /* Bit 2 rising edge → opcode latch. */
        if (!s_access_mode4_prev && (newctrl & 0x04))
            s_data_for_rendering = s_lpt_data;
        s_access_mode4_prev = newctrl & 0x04;
        /* Bit 0 falling edge → dispatch. */
        if (s_access_mode1_prev && !(newctrl & 0x01))
            process_data_command(s_data_for_rendering, s_lpt_data);
        s_access_mode1_prev = newctrl & 0x01;
        s_rendering_flags = newctrl;
        break;
    }
    default: break;
    }
}

static const MemoryRegionOps p2k_lpt_ops = {
    .read       = p2k_lpt_read,
    .write      = p2k_lpt_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl       = { .min_access_size = 1, .max_access_size = 1 },
};

void p2k_install_lpt_board(void)
{
    MemoryRegion *io = get_system_io();
    MemoryRegion *mr = g_new(MemoryRegion, 1);
    memory_region_init_io(mr, NULL, &p2k_lpt_ops, NULL,
                          "p2k.lpt-board", 3);
    memory_region_add_subregion(io, 0x378, mr);
    info_report("pinball2000: LPT driver-board installed at I/O 0x378-0x37a "
                "(STATUS=0x87, edge-detect dispatch, switches idle)");
}
