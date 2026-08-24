/*
 * pinball2000 LPT driver-board protocol on ports 0x378-0x37A.
 *
 * P2K talks to its driver board (sound, lamps, switch matrix scan) over
 * the parallel port using a tiny edge-detect state machine — see the
 * protocol" handler.
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
 * Cabinet input injection (column-gated, mirrors Encore behaviour):
 *
 *   F4              coin door interlock toggle (Physical[10] bit 1)
 *   F7              LEFT  flipper             (Physical[10] bit 5)
 *   F8              RIGHT flipper             (Physical[10] bit 4)
 *   Space / S       Start button              (col 0 bit 2 of opcode 0x04)
 *   F10 / C         coin slot 1               (Physical[8] bit 0)
 *   F12             dump LPT state to stderr
 *   NN, hold Ctrl   select matrix switch NN, hold it while Ctrl is held
 *
 * Hooked through QEMU's input subsystem (`qemu_input_handler_register`)
 * so it works with -display sdl / gtk and the QEMU monitor `sendkey`
 * command alike. No host LPT, no per-game RAM scribbling.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "p2k-qemu-compat.h"
#include "p2k-qemu-compat.h"
#include "ui/input.h"
#include "ui/console.h"
#include "ui/surface.h"
#include "system/runstate.h"
#include "qemu/timer.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#ifdef __linux__
#include <linux/ppdev.h>
#include <linux/parport.h>
#endif

#include "p2k-internal.h"

/* Optional host parport passthrough (--lpt-device /dev/parportN). */
static int     s_pp_fd = -1;
static char    s_pp_path[256];
static bool    s_disconnected;
static bool    s_cabinet_purist;

/* Optional per-event trace file (--lpt-trace <file>). */
static FILE   *s_trace_fp;

static void p2k_lpt_trace(const char *kind, hwaddr addr, uint64_t val)
{
    if (!s_trace_fp) {
        return;
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    fprintf(s_trace_fp, "%lld.%06ld %s %02x=%02x\n",
            (long long)tv.tv_sec, (long)tv.tv_usec,
            kind, (unsigned)(addr & 0xff), (unsigned)(val & 0xff));
    fflush(s_trace_fp);
}

#ifdef __linux__
static int p2k_lpt_pp_open(const char *dev)
{
    int fd = open(dev, O_RDWR);
    if (fd < 0) {
        return -1;
    }
    if (ioctl(fd, PPCLAIM) < 0) {
        close(fd);
        return -1;
    }
    /* Negotiate IEEE-1284 compatibility (plain SPP) — the Williams
     * driver-board is a vanilla SPP peripheral, no EPP/ECP. Best-effort:
     * if the underlying chipset rejects it we keep going (the kernel
     * default is also compat). */
    int mode = IEEE1284_MODE_COMPAT;
    (void)ioctl(fd, PPNEGOT, &mode);
    /* Default data-direction to output; the guest will flip to input
     * via CONTROL bit5 (0x20) when it wants to read switch state, and
     * we mirror that flip in p2k_lpt_pp_write below. */
    int dir = 0;  /* 0 = output (forward), 1 = input (reverse) */
    (void)ioctl(fd, PPDATADIR, &dir);
    return fd;
}

static void p2k_lpt_pp_close(int fd)
{
    if (fd < 0) return;
    (void)ioctl(fd, PPRELEASE);
    close(fd);
}

static uint8_t p2k_lpt_pp_read(int fd, hwaddr addr)
{
    unsigned char v = 0;
    int req;
    switch (addr) {
    case 0:  req = PPRDATA;    break;
    case 1:  req = PPRSTATUS;  break;
    case 2:  req = PPRCONTROL; break;
    default: return 0xFF;
    }
    if (ioctl(fd, req, &v) < 0) {
        return 0xFF;
    }
    return v;
}

static void p2k_lpt_pp_write(int fd, hwaddr addr, uint8_t v)
{
    int req;
    switch (addr) {
    case 0:  req = PPWDATA;    break;
    case 2:  req = PPWCONTROL;
        /* CONTROL bit5 (0x20) selects data direction on a real SPP
         * port: 0 = output (drive PA0..PA7), 1 = input (read external
         * lines). PPWCONTROL doesn't always honor that bit by itself;
         * we mirror it explicitly via PPDATADIR so subsequent
         * PPRDATA reads sample the driver-board outputs. */
        {
            int dir = (v & 0x20) ? 1 : 0;
            (void)ioctl(fd, PPDATADIR, &dir);
        }
        break;
    default: return;
    }
    (void)ioctl(fd, req, &v);
}

static void p2k_lpt_pp_atexit(void)
{
    if (s_pp_fd >= 0) {
        p2k_lpt_pp_close(s_pp_fd);
        s_pp_fd = -1;
    }
}
#endif

/* P2K rendering/switch state machine (mirrors io.c:720-742). */
static uint8_t s_lpt_data;
static uint8_t s_lpt_status = 0x87;
static uint8_t s_rendering_flags;
static uint8_t s_data_for_rendering;
static uint8_t s_rendering_data_val;
/* Driver-board output and input are separate electrical paths.  Opcode 0x08
 * latches lamp rows; opcode 0x04 scans playfield switches.  Sharing one array
 * here fed illuminated lamps back into XINA as phantom closed switches. */
static uint8_t s_lamp_rows[8];
static uint8_t s_switch_matrix[8];
static uint8_t s_keymap_switch_matrix[8];
static uint8_t s_data_val2;
static int     s_access_mode4_prev;
static int     s_access_mode1_prev;

/* AUX strobe / data-flag state.
 * The driver board toggles bit6 every read of opcode 0x10/0x11 so the
 * P2K scan loop can distinguish "data ready" from "data busy". Without
 * the toggle, scan loops misinterpret stale matrix slots as live
 * switch transitions (e.g. phantom "Left Drop Target Hit" events while
 * idle). */
static uint8_t s_data_val3;
static uint8_t s_data_val4;
static uint8_t s_data_flag1;
static uint8_t s_data_flag2;
static uint8_t s_data_flag3;
static uint8_t s_data_flag4;
static uint8_t s_data_flag5;
static uint8_t s_data_flag6;
static uint8_t s_data_flag7;
static uint8_t s_data_bit4;
static uint8_t s_data_bit6;

/* Cabinet interlock — door starts CLOSED so the "OPEN COIN DOOR"
 * overlay disappears and play is enabled (mirrors io.c:756). */
static uint8_t s_coin_door_closed = 1;

/* Live cabinet input state (driven by p2k_lpt_key_event below). */
static uint8_t s_phys10_buttons;     /* Physical[10] bits 4-7 (flippers/actions) */
static uint8_t s_phys9_service;      /* Physical[9]  bits 0-3 (service menu) */
static uint8_t s_phys8_coin_slots;   /* Physical[8]  bits 0-3 (coin slots) */
static int     s_enter_pulse;        /* F5 short-press: ~60 LPT frames high */
static int     s_coin1_pulse;        /* C/F10: guaranteed scan-visible pulse */

/* Digits select a standard matrix switch (column, row).  The last complete
 * number stays selected; every Ctrl hold closes it for that exact duration. */
static unsigned s_numeric_switch_code;
static unsigned s_numeric_switch_digits;
static bool s_numeric_switch_active;
static int64_t s_numeric_switch_pressed_at;

static int calc_bitwise_sum(uint8_t val)
{
    int has_bit = 0, sum = 0, pos = 0;
    for (unsigned v = val; v != 0; v >>= 1, pos++) {
        has_bit = 1;
        if (v & 1) sum += pos;
    }
    return has_bit + sum;
}

static uint8_t p2k_matrix_slot(unsigned slot)
{
    return s_switch_matrix[slot & 7] | s_keymap_switch_matrix[slot & 7];
}

static bool p2k_set_switch_layer(uint8_t matrix[8], unsigned number, bool down)
{
    unsigned column = number / 10;
    unsigned row = number % 10;

    if (column < 1 || column > 8 || row < 1 || row > 8) {
        return false;
    }
    unsigned slot = column & 7;
    uint8_t mask = 1u << (row - 1);
    bool previous = (p2k_matrix_slot(slot) & mask) != 0;
    if (down) {
        matrix[slot] |= mask;
    } else {
        matrix[slot] &= ~mask;
    }
    bool current = (p2k_matrix_slot(slot) & mask) != 0;
    if (previous != current) {
        fprintf(stderr, "[lpt] matrix switch %02u %s (column=%u row=%u%s)\n",
                number, current ? "PRESSED" : "released", column, row,
                number == 13 ? ", Start" : "");
    }
    return true;
}

static bool p2k_set_matrix_switch(unsigned number, bool down)
{
    return p2k_set_switch_layer(s_switch_matrix, number, down);
}

bool p2k_lpt_set_keymap_switch(unsigned number, bool down)
{
    return p2k_set_switch_layer(s_keymap_switch_matrix, number, down);
}

static bool p2k_is_ctrl_key(int qcode)
{
    return qcode == Q_KEY_CODE_CTRL || qcode == Q_KEY_CODE_CTRL_R;
}

static int p2k_numeric_key_digit(int qcode)
{
    if (qcode >= Q_KEY_CODE_1 && qcode <= Q_KEY_CODE_9) {
        return qcode - Q_KEY_CODE_1 + 1;
    }
    if (qcode == Q_KEY_CODE_0) {
        return 0;
    }
    if (qcode >= Q_KEY_CODE_KP_0 && qcode <= Q_KEY_CODE_KP_9) {
        return qcode - Q_KEY_CODE_KP_0;
    }
    return -1;
}

/* Returns true when the event is a digit or Ctrl used by this selector. */
static bool p2k_handle_numeric_switch_key(int qcode, bool down)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    bool ctrl = p2k_is_ctrl_key(qcode);
    int digit = p2k_numeric_key_digit(qcode);

    if (ctrl) {
        if (down && !s_numeric_switch_active && s_numeric_switch_digits == 2) {
            p2k_set_matrix_switch(s_numeric_switch_code, true);
            s_numeric_switch_active = true;
            s_numeric_switch_pressed_at = now;
        } else if (!down && s_numeric_switch_active) {
            p2k_set_matrix_switch(s_numeric_switch_code, false);
            fprintf(stderr, "[lpt] numeric switch %02u held %.3f s\n",
                    s_numeric_switch_code,
                    (now - s_numeric_switch_pressed_at) / 1000000000.0);
            s_numeric_switch_active = false;
        }
        return true;
    }

    if (digit >= 0) {
        if (!down || s_numeric_switch_active) {
            return true;
        }
        if (digit < 1 || digit > 8) {
            s_numeric_switch_code = 0;
            s_numeric_switch_digits = 0;
            fprintf(stderr, "[lpt] matrix switch selection cleared; "
                    "digits must be 1 through 8\n");
            return true;
        }
        if (s_numeric_switch_digits == 2) {
            s_numeric_switch_code = 0;
            s_numeric_switch_digits = 0;
        }
        s_numeric_switch_code = s_numeric_switch_code * 10 + digit;
        s_numeric_switch_digits++;
        if (s_numeric_switch_digits == 2) {
            fprintf(stderr, "[lpt] matrix switch %02u selected; hold Ctrl "
                    "to press, Ctrl again repeats\n", s_numeric_switch_code);
        } else {
            fprintf(stderr, "[lpt] matrix switch selection: %u_\n", digit);
        }
        return true;
    }
    return false;
}

static uint8_t retrieve_rendering_status(uint8_t opcode)
{
    switch (opcode) {
    case 0x00: {                                      /* Physical[8] coin slots */
        uint8_t v = s_phys8_coin_slots & 0x0f;
        if (s_coin1_pulse > 0) {
            v |= 0x01;
            s_coin1_pulse--;
        }
        return v;
    }
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
        int slot = (sel >= 1 && sel <= 8) ? sel : 1;
        return p2k_matrix_slot(slot);
    }
    case 0x0F:
        return (uint8_t)((s_data_flag1 << 6) | (s_data_bit6 << 7));
    case 0x10:
    case 0x11: {
        uint8_t v = s_data_bit6 ? 0x00 : 0xFF;
        s_data_bit6 = !s_data_bit6;
        return v;
    }
    case 0x12:
    case 0x13:
        return 0x00;
    default:   return 0x00;
    }
}

static void process_data_command(uint8_t opcode, uint8_t data)
{
    switch (opcode) {
    case 0x05:
        s_rendering_data_val = data;
        s_data_flag1 = 1;
        p2k_timing_audit_note_pdb05();
        break;
    case 0x06: s_data_val2 = data; break;
    case 0x07: s_data_val3 = data; break;
    case 0x08: {
        s_data_val4 = data;
        if (data != 0) {
            int idx = calc_bitwise_sum(data);
            if (idx > 0 && idx < 8) s_lamp_rows[idx] = s_data_val2;
        }
        break;
    }
    case 0x09: s_data_flag3 = s_data_flag2 ? (s_data_flag3 | data) : 0; break;
    case 0x0A: s_data_flag4 = s_data_flag2 ? (s_data_flag4 | data) : 0; break;
    case 0x0B: s_data_flag5 = s_data_flag2 ? (s_data_flag5 | data) : 0; break;
    case 0x0C: s_data_flag6 = s_data_flag2 ? (s_data_flag6 | data) : 0; break;
    case 0x0D: {
        int new_bit4 = (data & 0x10) >> 4;
        if (new_bit4 != s_data_bit4) s_data_bit4 = new_bit4;
        s_data_flag2 = (data & 0x20) >> 5;
        s_data_bit6  = (data & 0x80) >> 7;
        s_data_flag7 = s_data_flag2 ? (s_data_flag7 | (data & 0x0F)) : 0;
        break;
    }
    default: break;
    }
}

static uint64_t p2k_lpt_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t v;
    if (s_disconnected) {
        /* Diagnostic open bus: a controller exists at the expected I/O
         * address, but no Pinball 2000 driver-board is attached.  Do not
         * execute any part of the software board model. */
        v = 0xFF;
        p2k_lpt_trace("R", addr, v);
        return v;
    }
#ifdef __linux__
    if (s_pp_fd >= 0) {
        v = p2k_lpt_pp_read(s_pp_fd, addr);
        p2k_lpt_trace("R", addr, v);
        return v;
    }
#endif
    switch (addr) {
    case 0: { /* DATA */
        int gated = (s_rendering_flags & 0x01) && (s_rendering_flags & 0x08);
        v = gated ? retrieve_rendering_status(s_data_for_rendering)
                  : s_lpt_data;
        break;
    }
    case 1:  v = s_lpt_status;                  break;
    case 2:  v = s_rendering_flags;             break;
    default: v = 0xFF;                          break;
    }
    p2k_lpt_trace("R", addr, v);
    return v;
}

/* Driver-board activity counters. Per Erikie (pinside msg #36): the
 * only thing that ultimately matters for cabinet behaviour is the LPT
 * rate seen by the driverboard (target ~16 kHz). Bump once per write/
 * dispatch; the audit panel snapshots the deltas to derive Hz. */
static uint64_t s_lpt_data_writes;
static uint64_t s_lpt_ctrl_writes;
static uint64_t s_lpt_dispatches;

uint64_t p2k_lpt_get_data_writes(void) { return s_lpt_data_writes; }
uint64_t p2k_lpt_get_ctrl_writes(void) { return s_lpt_ctrl_writes; }
uint64_t p2k_lpt_get_dispatches(void)  { return s_lpt_dispatches;  }

static void p2k_lpt_write(void *opaque, hwaddr addr,
                          uint64_t val, unsigned size)
{
    p2k_lpt_trace("W", addr, val);
    if (s_disconnected) {
        return;
    }
#ifdef __linux__
    if (s_pp_fd >= 0) {
        p2k_lpt_pp_write(s_pp_fd, addr, val & 0xFF);
        return;
    }
#endif
    switch (addr) {
    case 0:
        s_lpt_data_writes++;
        s_lpt_data = val & 0xFF;
        break;
    case 2: {
        s_lpt_ctrl_writes++;
        uint8_t newctrl = val & 0xFF;
        /* Bit 2 rising edge → opcode latch. */
        if (!s_access_mode4_prev && (newctrl & 0x04))
            s_data_for_rendering = s_lpt_data;
        s_access_mode4_prev = newctrl & 0x04;
        /* Bit 0 falling edge → dispatch. */
        if (s_access_mode1_prev && !(newctrl & 0x01)) {
            s_lpt_dispatches++;
            process_data_command(s_data_for_rendering, s_lpt_data);
        }
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
        "ctrl=0x%02x data=0x%02x op=0x%02x lamp1=0x%02x switch1=0x%02x\n",
        s_coin_door_closed ? "CLOSED" : "OPEN",
        s_phys10_buttons, s_phys8_coin_slots,
        !!(p2k_matrix_slot(1) & (1u << 2)),
        s_rendering_flags, s_lpt_data, s_data_for_rendering,
        s_lamp_rows[1], p2k_matrix_slot(1));
}

/* Pipe RGB to a JPEG-producing helper (cjpeg / magick / convert).
 * Returns true on success. PPM data is fed on stdin via "ppm:-".  */
static bool p2k_lpt_try_jpeg_pipe(const char *jpg_path, int w, int h,
                                  const uint8_t *data, int stride, int bpp)
{
    static const char *const candidates[] = {
        "cjpeg -quality 90 -outfile",   /* libjpeg-turbo-progs */
        "magick ppm:- -quality 90",     /* ImageMagick 7 */
        "convert ppm:- -quality 90",    /* ImageMagick 6 / GraphicsMagick */
        NULL,
    };
    for (int i = 0; candidates[i]; i++) {
        char tool[64];
        sscanf(candidates[i], "%63s", tool);
        char which[128];
        snprintf(which, sizeof(which), "command -v %s >/dev/null 2>&1", tool);
        if (system(which) != 0) continue;

        char cmd[512];
        if (i == 0) {
            snprintf(cmd, sizeof(cmd), "%s '%s'", candidates[i], jpg_path);
        } else {
            snprintf(cmd, sizeof(cmd), "%s 'jpg:%s'", candidates[i], jpg_path);
        }
        FILE *p = popen(cmd, "w");
        if (!p) continue;
        fprintf(p, "P6\n%d %d\n255\n", w, h);
        for (int y = 0; y < h; y++) {
            const uint8_t *row = data + y * stride;
            for (int x = 0; x < w; x++) {
                const uint8_t *px = row + x * bpp;
                uint8_t rgb[3] = { px[2], px[1], px[0] };
                fwrite(rgb, 1, 3, p);
            }
        }
        int rc = pclose(p);
        if (rc == 0) return true;
    }
    return false;
}

static void p2k_lpt_screenshot(void)
{
    if (p2k_display_request_screenshot()) {
        return;
    }
    QemuConsole *con = qemu_console_lookup_by_index(0);
    DisplaySurface *s = con ? qemu_console_surface(con) : NULL;
    if (!s) {
        fprintf(stderr, "[lpt] F3 screenshot: no console/surface\n");
        return;
    }
    int w = surface_width(s), h = surface_height(s);
    int stride = surface_stride(s);
    int bpp = surface_bytes_per_pixel(s);
    const uint8_t *data = surface_data(s);
    if (!data || w <= 0 || h <= 0 || bpp < 3) {
        fprintf(stderr, "[lpt] F3 screenshot: bad surface (w=%d h=%d bpp=%d)\n",
                w, h, bpp);
        return;
    }
    char stem[256];
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    const char *dir = getenv("P2K_SCREENSHOT_DIR");
    if (!dir || !*dir) dir = "/tmp";
    snprintf(stem, sizeof(stem), "%s/p2k_screen_%04d%02d%02d_%02d%02d%02d",
             dir,
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);

    /* Prefer JPEG via host helper. Fall back to PPM if no jpeg tool found. */
    char jpg_path[300];
    snprintf(jpg_path, sizeof(jpg_path), "%s.jpg", stem);
    if (p2k_lpt_try_jpeg_pipe(jpg_path, w, h, data, stride, bpp)) {
        fprintf(stderr, "[lpt] F3 screenshot: wrote %s (%dx%d)\n",
                jpg_path, w, h);
        return;
    }

    char ppm_path[300];
    snprintf(ppm_path, sizeof(ppm_path), "%s.ppm", stem);
    FILE *f = fopen(ppm_path, "wb");
    if (!f) {
        fprintf(stderr, "[lpt] F3 screenshot: open(%s): %s\n",
                ppm_path, strerror(errno));
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    /* Surface is little-endian ARGB8888: byte order B,G,R,A. PPM wants R,G,B. */
    for (int y = 0; y < h; y++) {
        const uint8_t *row = data + y * stride;
        for (int x = 0; x < w; x++) {
            const uint8_t *p = row + x * bpp;
            uint8_t rgb[3] = { p[2], p[1], p[0] };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    fprintf(stderr, "[lpt] F3 screenshot: wrote %s (%dx%d) "
            "(install libjpeg-turbo-progs or imagemagick for .jpg)\n",
            ppm_path, w, h);
}

void p2k_lpt_host_key(int qcode, bool down)
{
    /* The threaded SDL renderer forwards keys directly here instead of
     * passing through QEMU's registered input handler.  Enforce cabinet
     * isolation at this common endpoint as well.  F1 remains a host
     * lifecycle control so an operator can leave the cabinet session. */
    if (s_cabinet_purist && qcode != Q_KEY_CODE_F1) {
        return;
    }
    if (p2k_switch_keymap_handle_key(qcode, down)) {
        return;
    }
    if (p2k_handle_numeric_switch_key(qcode, down)) {
        return;
    }
    switch (qcode) {
    case Q_KEY_CODE_F1:                              /* quit */
        if (down) {
            fprintf(stderr, "[lpt] F1 → shutdown request\n");
            p2k_dcs_adsp_health_report();
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
        p2k_set_matrix_switch(13, down);
        break;
    }
    case Q_KEY_CODE_F10:
    case Q_KEY_CODE_C:                               /* coin slot 1 */
        if (down) {
            s_coin1_pulse = 60;
            fprintf(stderr, "[lpt] coin slot 1 pulse fired (~60 frames, "
                    "door=%s)\n", s_coin_door_closed ? "CLOSED" : "OPEN");
        }
        break;
    case Q_KEY_CODE_F11:                             /* scripted PCM capture */
        p2k_dcs_audio_capture_set(down);
        break;
    case Q_KEY_CODE_F3:                              /* screenshot to PPM */
        if (down) p2k_lpt_screenshot();
        break;
    case Q_KEY_CODE_F2:                              /* toggle flipscreen */
        if (down) p2k_display_toggle_flipscreen();
        break;
    case Q_KEY_CODE_F12:
        if (down) p2k_lpt_dump_state();
        break;
    default:
        if (down) {
            fprintf(stderr, "[lpt] unhandled key qcode=%d\n", qcode);
        }
        break;
    }
}

static void p2k_lpt_key_event(DeviceState *dev, QemuConsole *src,
                              InputEvent *evt)
{
    InputKeyEvent *key = evt->u.key.data;
    p2k_lpt_host_key(qemu_input_key_value_to_qcode(key->key), key->down);
}

static const QemuInputHandler p2k_lpt_input_handler = {
    .name  = "pinball2000 cabinet",
    .mask  = INPUT_EVENT_MASK_KEY,
    .event = p2k_lpt_key_event,
};

void p2k_install_lpt_board(void)
{
    const char *disable  = getenv("P2K_LPT_DISABLE");
    const char *ioport_s = getenv("P2K_LPT_IOPORT");
    const char *parport  = getenv("P2K_LPT_PARPORT");
    const char *disconnected = getenv("P2K_LPT_DISCONNECTED");
    const char *trace_fn = getenv("P2K_LPT_TRACE_FILE");
    const char *status_s = getenv("P2K_LPT_STATUS");
    unsigned    ioport   = 0x378;

    if (disable && *disable && strcmp(disable, "0") != 0) {
        info_report("pinball2000: LPT driver-board disabled "
                    "(P2K_LPT_DISABLE set) — game will not boot, "
                    "the switch matrix is unreachable. Diagnostic only.");
        return;
    }

    if (status_s && *status_s) {
        char *end = NULL;
        unsigned long v = strtoul(status_s, &end, 0);
        if (end != status_s && *end == '\0' && v <= 0xff) {
            s_lpt_status = (uint8_t)v;
        } else {
            error_report("pinball2000: invalid P2K_LPT_STATUS='%s' "
                         "(expected byte 0..255); using 0x87", status_s);
            s_lpt_status = 0x87;
        }
    }

    if (ioport_s && *ioport_s) {
        char *end = NULL;
        unsigned v = (unsigned)strtoul(ioport_s, &end, 0);
        if (end && *end == '\0' && v > 0 && v < 0xfffd) {
            ioport = v;
        } else {
            warn_report("pinball2000: P2K_LPT_IOPORT='%s' invalid, "
                        "keeping 0x378", ioport_s);
        }
    }

    if (trace_fn && *trace_fn) {
        s_trace_fp = fopen(trace_fn, "ae");
        if (!s_trace_fp) {
            warn_report("pinball2000: cannot open LPT trace '%s' (%s)",
                        trace_fn, strerror(errno));
        } else {
            info_report("pinball2000: LPT event trace → %s", trace_fn);
        }
    }

    s_disconnected = disconnected && *disconnected &&
                     strcmp(disconnected, "0") != 0;
    if (s_disconnected) {
        info_report("pinball2000: LPT DISCONNECTED diagnostic target "
                    "(open-bus reads=0xff, writes discarded, no emulated "
                    "driver-board)");
    }

    if (!s_disconnected && parport && *parport) {
#ifdef __linux__
        int fd = p2k_lpt_pp_open(parport);
        if (fd < 0) {
            error_report("pinball2000: cannot open/claim host parport '%s' "
                         "(%s) — is the device present and are you in the "
                         "'lp' group with ppdev loaded?",
                         parport, strerror(errno));
            error_report("pinball2000: refusing to fall back to the emulated "
                         "driver-board after an explicit real-cabinet request");
            exit(1);
        } else {
            s_pp_fd = fd;
            snprintf(s_pp_path, sizeof(s_pp_path), "%s", parport);
            atexit(p2k_lpt_pp_atexit);
            info_report("pinball2000: LPT board PASSTHROUGH to host %s "
                        "(real-cabinet wiring, all reads/writes hit hardware; "
                        "IEEE-1284 compat mode, PPDATADIR mirrors CTRL bit5)",
                        s_pp_path);
        }
#else
        error_report("pinball2000: --lpt-device <hostdev> only supported on "
                     "Linux (ppdev). Cannot use '%s'.", parport);
        exit(1);
#endif
    }

    MemoryRegion *io = get_system_io();
    MemoryRegion *mr = g_new(MemoryRegion, 1);
    memory_region_init_io(mr, NULL, &p2k_lpt_ops, NULL,
                          "p2k.lpt-board", 3);
    memory_region_add_subregion(io, ioport, mr);

    /* --cabinet-purist: trust the real driver-board protocol, no host
     * key injection. Only meaningful when also paired with a real host
     * parport; otherwise the switch matrix is unreachable. */
    const char *purist = getenv("P2K_CABINET_PURIST");
    bool purist_on = (purist && *purist && strcmp(purist, "0") != 0);
    s_cabinet_purist = purist_on;
    if (purist_on && s_pp_fd < 0 && !s_disconnected) {
        error_report("pinball2000: P2K_CABINET_PURIST=1 requires "
                     "--lpt-device <hostdev> or the explicit disconnected "
                     "diagnostic target. Without either, the switch matrix "
                     "is unreachable and the game cannot be played.");
    }
    if (!purist_on) {
        qemu_input_handler_register(NULL, &p2k_lpt_input_handler);
    } else {
        info_report("pinball2000: cabinet-purist mode — emulated board "
                    "controls disabled on every keyboard path (F1 remains "
                    "available for shutdown)");
    }

    info_report("pinball2000: LPT driver-board installed at I/O 0x%x-0x%x "
                "(STATUS=0x%02x, edge-detect dispatch%s)",
                ioport, ioport + 2, s_lpt_status,
                purist_on ? "; cabinet-purist (no desktop keys)" :
                "; keys: F1 quit | F2 vertical flip | F3 screenshot | "
                "F4 door | F5/Enter pulse | F6/F9 actions | "
                "F7/F8 flippers | Space/S start | F10/C coin | "
                "F12 dump | Esc/Left service | Up/Down volume | "
                "Right enter | NN then Ctrl matrix switch");
}
