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
#include "ui/surface.h"
#include "system/runstate.h"

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
    return fd;
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
    case 2:  req = PPWCONTROL; break;
    default: return;
    }
    (void)ioctl(fd, req, &v);
}
#endif

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
    uint64_t v;
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
    case 1:  v = 0x87;                          break;
    case 2:  v = s_rendering_flags;             break;
    default: v = 0xFF;                          break;
    }
    p2k_lpt_trace("R", addr, v);
    return v;
}

static void p2k_lpt_write(void *opaque, hwaddr addr,
                          uint64_t val, unsigned size)
{
    p2k_lpt_trace("W", addr, val);
#ifdef __linux__
    if (s_pp_fd >= 0) {
        p2k_lpt_pp_write(s_pp_fd, addr, val & 0xFF);
        return;
    }
#endif
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
    case Q_KEY_CODE_F3:                              /* screenshot to PPM */
        if (down) p2k_lpt_screenshot();
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
    const char *trace_fn = getenv("P2K_LPT_TRACE_FILE");
    unsigned    ioport   = 0x378;

    if (disable && *disable && strcmp(disable, "0") != 0) {
        info_report("pinball2000: LPT driver-board disabled "
                    "(P2K_LPT_DISABLE set) — game will not boot, "
                    "the switch matrix is unreachable. Diagnostic only.");
        return;
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

    if (parport && *parport) {
#ifdef __linux__
        int fd = p2k_lpt_pp_open(parport);
        if (fd < 0) {
            error_report("pinball2000: cannot open/claim host parport '%s' "
                         "(%s) — is the device present and are you in the "
                         "'lp' group with ppdev loaded?",
                         parport, strerror(errno));
        } else {
            s_pp_fd = fd;
            snprintf(s_pp_path, sizeof(s_pp_path), "%s", parport);
            info_report("pinball2000: LPT board PASSTHROUGH to host %s "
                        "(real-cabinet wiring, all reads/writes hit hardware)",
                        s_pp_path);
        }
#else
        error_report("pinball2000: --lpt-device <hostdev> only supported on "
                     "Linux (ppdev). Ignoring '%s'.", parport);
#endif
    }

    MemoryRegion *io = get_system_io();
    MemoryRegion *mr = g_new(MemoryRegion, 1);
    memory_region_init_io(mr, NULL, &p2k_lpt_ops, NULL,
                          "p2k.lpt-board", 3);
    memory_region_add_subregion(io, ioport, mr);

    qemu_input_handler_register(NULL, &p2k_lpt_input_handler);

    info_report("pinball2000: LPT driver-board installed at I/O 0x%x-0x%x "
                "(STATUS=0x87, edge-detect dispatch, "
                "keys: F1 quit | F4 door | F5/Enter pulse | F6/F9 actions | "
                "F7/F8 flippers | Space/S start | F10/C coin | F12 dump | "
                "Esc/Left service | Up/Down volume | Right enter)",
                ioport, ioport + 2);
}
