/*
 * pinball2000 LPT driver-board protocol on ports 0x378-0x37A.
 *
 * P2K talks to its driver board (sound, lamps, switch matrix scan) over
 * the parallel port using a tiny edge-detect state machine — see
 * unicorn.old/src/io.c:705-1245 ("BT-120: Faithful P2K-driver
 * processParallelPortAccess protocol").
 *
 *   0x378 (DATA)   WRITE: latch
 *                  READ:  if rendering gated → switch-matrix status
 *                          else → echo last data byte
 *   0x379 (STATUS) READ:  always 0x87 (driver-board signature)
 *   0x37A (CTRL)   WRITE: edge-detect protocol:
 *                          bit2 rising  → capture data → opcode latch
 *                          bit0 falling → dispatch process_data_command
 *                  READ:  echo the last value written
 *
 * Cabinet input injection (column-gated, mirrors Unicorn behaviour):
 *
 *   F4              coin door interlock toggle (Physical[10] bit 1)
 *   F7              LEFT  flipper             (Physical[10] bit 5)
 *   F8              RIGHT flipper             (Physical[10] bit 4)
 *   Space / S       Start button              (col 0 bit 2 of opcode 0x04)
 *   F10 / C         coin slot 1               (Physical[8] bit 0)
 *   F12             dump LPT state to stderr
 *
 * Hooked through QEMU's input subsystem (`qemu_input_handler_register`)
 * so it works with -display sdl / gtk and the QEMU monitor `sendkey`
 * command alike. No host LPT, no per-game RAM scribbling.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "exec/address-spaces.h"
#include "exec/ioport.h"
#include "ui/input.h"
#include "ui/console.h"
#include "system/runstate.h"

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
static uint8_t s_coin_door_closed = 1;

/* Live cabinet input state (driven by p2k_lpt_key_event below). */
static uint8_t s_phys10_buttons;     /* Physical[10] bits 4-7 (flippers/actions) */
static uint8_t s_phys9_service;      /* Physical[9]  bits 0-3 (service menu) */
static uint8_t s_phys8_coin_slots;   /* Physical[8]  bits 0-3 (coin slots) */
static int     s_start_button_held;  /* sw=2 → col 0 bit 2 */
static int     s_enter_pulse;        /* F5 short-press: ~60 LPT frames high */

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
    case 0x00:                                        /* Physical[8] coin slots */
        return s_phys8_coin_slots & 0x0F;
    case 0x01: {                                      /* Physical[10] flippers + door */
        uint8_t v = s_phys10_buttons & 0xF0;
        if (s_coin_door_closed) v |= 0x02;
        return v;
    }
    case 0x02: return 0xF0;                           /* status hi nibble */
    case 0x03: {                                      /* Physical[9] service menu */
        uint8_t v = s_phys9_service & 0x0F;
        if (s_enter_pulse > 0) { v |= 0x08; s_enter_pulse--; }
        return v;
    }
    case 0x04: {
        int sel  = calc_bitwise_sum(s_rendering_data_val);   /* 1..8 if one-hot */
        int col  = (sel >= 1 && sel <= 8) ? (sel - 1) : 0;
        int slot = (sel >= 1 && sel <= 8) ? sel : 1;
        uint8_t v = s_rendering_status[slot & 7];
        if (s_start_button_held && col == 0) v |= (uint8_t)(1u << 2);   /* sw=2 */
        return v;
    }
    case 0x0F: return 0x00;
    case 0x10: case 0x11: return 0xFF;                /* "no strobe" idle */
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

/* ---------- desktop input → switch matrix --------------------------------- */

static void p2k_lpt_dump_state(void)
{
    fprintf(stderr,
        "[lpt] coin_door=%s phys10=0x%02x phys8=0x%02x start=%d "
        "ctrl=0x%02x data=0x%02x op=0x%02x slot1=0x%02x\n",
        s_coin_door_closed ? "CLOSED" : "OPEN",
        s_phys10_buttons, s_phys8_coin_slots, s_start_button_held,
        s_rendering_flags, s_lpt_data, s_data_for_rendering,
        s_rendering_status[1]);
}

static void p2k_lpt_key_event(DeviceState *dev, QemuConsole *src,
                              InputEvent *evt)
{
    InputKeyEvent *key = evt->u.key.data;
    int qcode = qemu_input_key_value_to_qcode(key->key);
    bool down = key->down;

    switch (qcode) {
    case Q_KEY_CODE_F1:                              /* Unicorn parity: quit */
        if (down) {
            fprintf(stderr, "[lpt] F1 → shutdown request\n");
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_HOST_UI);
        }
        break;
    case Q_KEY_CODE_F4:
        if (down) {
            s_coin_door_closed = !s_coin_door_closed;
            fprintf(stderr, "[lpt] coin door %s (interlock bit=%d)\n",
                    s_coin_door_closed ? "CLOSED" : "OPEN",
                    s_coin_door_closed);
        }
        break;
    case Q_KEY_CODE_F5:                              /* short Enter pulse */
    case Q_KEY_CODE_KP_ENTER:
    case Q_KEY_CODE_RET:
        if (down) {
            s_enter_pulse = 60;                      /* ~60 LPT frames */
            fprintf(stderr, "[lpt] Enter pulse fired (~60 frames)\n");
        }
        break;
    case Q_KEY_CODE_F6:                              /* LEFT action button */
        if (down) s_phys10_buttons |=  (1u << 7);
        else      s_phys10_buttons &= ~(1u << 7);
        break;
    case Q_KEY_CODE_F7:                              /* LEFT flipper */
        if (down) s_phys10_buttons |=  (1u << 5);
        else      s_phys10_buttons &= ~(1u << 5);
        break;
    case Q_KEY_CODE_F8:                              /* RIGHT flipper */
        if (down) s_phys10_buttons |=  (1u << 4);
        else      s_phys10_buttons &= ~(1u << 4);
        break;
    case Q_KEY_CODE_F9:                              /* RIGHT action button */
        if (down) s_phys10_buttons |=  (1u << 6);
        else      s_phys10_buttons &= ~(1u << 6);
        break;
    case Q_KEY_CODE_ESC:                             /* Service / Escape */
    case Q_KEY_CODE_LEFT:
        if (down) s_phys9_service |=  (1u << 0);
        else      s_phys9_service &= ~(1u << 0);
        break;
    case Q_KEY_CODE_DOWN:                            /* Volume− / Menu Down */
    case Q_KEY_CODE_KP_SUBTRACT:
        if (down) s_phys9_service |=  (1u << 1);
        else      s_phys9_service &= ~(1u << 1);
        break;
    case Q_KEY_CODE_UP:                              /* Volume+ / Menu Up */
    case Q_KEY_CODE_KP_ADD:
    case Q_KEY_CODE_EQUAL:
        if (down) s_phys9_service |=  (1u << 2);
        else      s_phys9_service &= ~(1u << 2);
        break;
    case Q_KEY_CODE_RIGHT:                           /* Begin Test / Enter */
        if (down) s_phys9_service |=  (1u << 3);
        else      s_phys9_service &= ~(1u << 3);
        break;
    case Q_KEY_CODE_SPC:
    case Q_KEY_CODE_S: {                             /* Start button (sw=2) */
        int prev = s_start_button_held;
        s_start_button_held = down ? 1 : 0;
        if (prev != s_start_button_held) {
            fprintf(stderr, "[lpt] Start Button %s (sw=2, c0 b2)\n",
                    s_start_button_held ? "PRESSED" : "released");
        }
        break;
    }
    case Q_KEY_CODE_F10:
    case Q_KEY_CODE_C:                               /* coin slot 1 */
        if (down) s_phys8_coin_slots |=  (1u << 0);
        else      s_phys8_coin_slots &= ~(1u << 0);
        fprintf(stderr, "[lpt] coin slot 1 %s (phys8=0x%02x door=%s)\n",
                down ? "PRESSED" : "released",
                s_phys8_coin_slots,
                s_coin_door_closed ? "CLOSED" : "OPEN");
        break;
    case Q_KEY_CODE_F12:
        if (down) p2k_lpt_dump_state();
        break;
    default: break;
    }
}

static const QemuInputHandler p2k_lpt_input_handler = {
    .name  = "pinball2000 cabinet",
    .mask  = INPUT_EVENT_MASK_KEY,
    .event = p2k_lpt_key_event,
};

void p2k_install_lpt_board(void)
{
    MemoryRegion *io = get_system_io();
    MemoryRegion *mr = g_new(MemoryRegion, 1);
    memory_region_init_io(mr, NULL, &p2k_lpt_ops, NULL,
                          "p2k.lpt-board", 3);
    memory_region_add_subregion(io, 0x378, mr);

    qemu_input_handler_register(NULL, &p2k_lpt_input_handler);

    info_report("pinball2000: LPT driver-board installed at I/O 0x378-0x37a "
                "(STATUS=0x87, edge-detect dispatch, "
                "keys: F1 quit | F4 door | F5/Enter pulse | F6/F9 actions | "
                "F7/F8 flippers | Space/S start | F10/C coin | F12 dump | "
                "Esc/Left service | Up/Down volume | Right enter)");
}
