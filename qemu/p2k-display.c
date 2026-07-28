/*
 * p2k-display.c — Cyrix MediaGX display output (640×240 RGB555 → window).
 *
 * Reads the guest framebuffer directly from system RAM at physical 0x800000,
 * the same backing store that the GX FB window (0x40800000) aliases (see
 * p2k-gx.c). Rendering uses the game's bottom-up RGB framebuffer:
 *
 *   - Source is 640×240 RGB555 (16 bpp), pitch 1280 during PRISM/boot and
 *     2048 during game runtime.  We latch into 2048-pitch the first time
 *     DC_FB_ST_OFFSET lands on a non-zero multiple of 0x78000 (the size of
 *     a single 240×2048 buffer).
 *   - DC_FB_ST_OFFSET (GX_BASE + 0x8310) is the start offset within the FB.
 *   - The QEMU-console paths produce 640×480, line-doubled output.
 *   - The direct SDL path uploads native 640×240 RGB555 with its real pitch;
 *     SDL performs the Y flip and scales it into a 640×480 window.
 *
 * The default path uses QEMU's graphics console and address-space reads.
 * P2K_QEMU_FRAMEBUFFER=1 keeps that console/backend but reads native RGB555
 * through direct RAM pointers and uses a lookup table for ARGB expansion,
 * avoiding address-space reads and per-pixel color arithmetic.
 * P2K_FRAMEBUFFER_THREAD=1 instead gives an SDL-owned host thread direct
 * pointers to the RAM-backed DC registers and framebuffer; QEMU then runs
 * with -display none.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "p2k-qemu-compat.h"
#include "hw/boards.h"
#include "qemu/notify.h"
#include "qemu/main-loop.h"
#include "qemu/thread.h"
#include "system/runstate.h"
#include "system/system.h"
#include "ui/console.h"
#include "ui/input.h"
#include "ui/surface.h"
#include <SDL2/SDL.h>

#include "p2k-internal.h"

#define FB_W           640
#define FB_H           240
#define SCREEN_W       640
#define SCREEN_H       480

#define GX_BASE                0x40000000u
#define GX_DC_FB_ST_OFFSET     0x00008310u  /* relative to GX_BASE */
#define GX_FB_RAM_MIRROR       0x00800000u  /* alias target in system RAM */
#define GAME_BUF_SIZE          0x00078000u  /* 240 * 2048 */

typedef struct P2KDisplayState {
    QemuConsole   *con;
    uint8_t       *ram;
    uint8_t       *gx_regs;
    QemuMutex      frame_lock;
    QemuThread     worker;
    QemuThread     submit_worker;
    QemuMutex      submit_lock;
    QemuCond       submit_cond;
    Notifier       shutdown_notifier;
    Notifier       exit_notifier;
    SDL_Window    *window;
    SDL_Renderer  *renderer;
    SDL_Texture   *texture;
    bool           worker_run;
    bool           worker_started;
    bool           threaded;
    bool           qemu_framebuffer;
    bool           qemu_rgb565;
    bool           bpp16;
    bool           profile;
    bool           submit_run;
    bool           submit_started;
    bool           submit_pending;
    bool           submit_context_checked;
    bool           submit_context_handoff;
    int            screenshot_pending;
    uint8_t       *submit_staging;
    size_t         submit_bytes;
    SDL_Window    *submit_window;
    SDL_GLContext  submit_gl_context;
    bool           game_pitch;       /* false: stride 1280 (boot), true: 2048 */
    uint32_t       last_fb_off;
    uint64_t       profile_frames;
    uint64_t       profile_prepare_ns;
    uint64_t       profile_submit_ns;
    uint64_t       profile_submit_frames;
    uint64_t       profile_prepare_max_ns;
    uint64_t       profile_submit_max_ns;
} P2KDisplayState;

static P2KDisplayState s_disp;
static QemuMutex s_status_lock;
static char s_status[96];
static void p2k_display_update(void *opaque);

void p2k_display_set_status(const char *status)
{
    qemu_mutex_lock(&s_status_lock);
    snprintf(s_status, sizeof(s_status), "%s", status ? status : "");
    qemu_mutex_unlock(&s_status_lock);
}

void p2k_display_refresh_status(void)
{
    /* Status is painted by the next QEMU or direct-SDL refresh. */
}

/* Compact 5x7 font for the generation banner. */
static const uint8_t *status_glyph(char c)
{
    static const uint8_t blank[7] = {0};
#define GLYPH(ch,a,b,c,d,e,f,g) case ch: { static const uint8_t r[7] = {a,b,c,d,e,f,g}; return r; }
    switch (c) {
    GLYPH('A',14,17,17,31,17,17,17) GLYPH('B',30,17,17,30,17,17,30)
    GLYPH('E',31,16,16,30,16,16,31) GLYPH('G',14,17,16,23,17,17,15)
    GLYPH('I',31,4,4,4,4,4,31)      GLYPH('K',17,18,20,24,20,18,17)
    GLYPH('L',16,16,16,16,16,16,31) GLYPH('N',17,25,21,19,17,17,17)
    GLYPH('P',30,17,17,30,16,16,16) GLYPH('R',30,17,17,30,20,18,17)
    GLYPH('S',15,16,16,14,1,1,30)   GLYPH('T',31,4,4,4,4,4,4)
    GLYPH('0',14,17,19,21,25,17,14) GLYPH('1',4,12,4,4,4,4,14)
    GLYPH('2',14,17,1,2,4,8,31)     GLYPH('3',30,1,1,14,1,1,30)
    GLYPH('4',2,6,10,18,31,2,2)     GLYPH('5',31,16,16,30,1,1,30)
    GLYPH('6',14,16,16,30,17,17,14) GLYPH('7',31,1,2,4,8,8,8)
    GLYPH('8',14,17,17,14,17,17,14) GLYPH('9',14,17,17,15,1,1,14)
    GLYPH('/',1,2,2,4,8,8,16)       GLYPH('.',0,0,0,0,0,12,12)
    default: return blank;
    }
#undef GLYPH
}

static void draw_status_pixels(void *dst_raw, bool bpp16, uint16_t white16)
{
    char text[sizeof(s_status)];
    qemu_mutex_lock(&s_status_lock);
    memcpy(text, s_status, sizeof(text));
    qemu_mutex_unlock(&s_status_lock);
    if (!text[0]) return;

    int width = MIN(SCREEN_W, 16 + (int)strlen(text) * 12);
    for (int y = 8; y < 34; y++) {
        for (int x = 8; x < width; x++) {
            if (bpp16) ((uint16_t *)dst_raw)[y * SCREEN_W + x] = 0;
            else ((uint32_t *)dst_raw)[y * SCREEN_W + x] = 0xff000000;
        }
    }
    for (int n = 0; text[n] && 12 + n * 12 + 10 < SCREEN_W; n++) {
        const uint8_t *rows = status_glyph(g_ascii_toupper(text[n]));
        for (int gy = 0; gy < 7; gy++) for (int gx = 0; gx < 5; gx++) {
            if (!(rows[gy] & (1 << (4 - gx)))) continue;
            for (int sy = 0; sy < 2; sy++) for (int sx = 0; sx < 2; sx++) {
                int x = 12 + n * 12 + gx * 2 + sx, y = 12 + gy * 2 + sy;
                if (bpp16) ((uint16_t *)dst_raw)[y * SCREEN_W + x] = white16;
                else ((uint32_t *)dst_raw)[y * SCREEN_W + x] = 0xffffffff;
            }
        }
    }
}

static void draw_status_sdl(P2KDisplayState *s)
{
    char text[sizeof(s_status)];

    qemu_mutex_lock(&s_status_lock);
    memcpy(text, s_status, sizeof(text));
    qemu_mutex_unlock(&s_status_lock);
    if (!text[0]) {
        return;
    }

    SDL_SetRenderDrawColor(s->renderer, 0, 0, 0, 255);
    SDL_Rect background = {
        .x = 8, .y = 8,
        .w = MIN(SCREEN_W, 16 + (int)strlen(text) * 12) - 8,
        .h = 26,
    };
    SDL_RenderFillRect(s->renderer, &background);
    SDL_SetRenderDrawColor(s->renderer, 255, 255, 255, 255);
    for (int n = 0; text[n] && 12 + n * 12 + 10 < SCREEN_W; n++) {
        const uint8_t *rows = status_glyph(g_ascii_toupper(text[n]));
        for (int gy = 0; gy < 7; gy++) {
            for (int gx = 0; gx < 5; gx++) {
                if (rows[gy] & (1 << (4 - gx))) {
                    SDL_Rect pixel = {
                        .x = 12 + n * 12 + gx * 2,
                        .y = 12 + gy * 2,
                        .w = 2, .h = 2,
                    };
                    SDL_RenderFillRect(s->renderer, &pixel);
                }
            }
        }
    }
}

/* RGB555 pixel -> ARGB8888.  Replicate top bits to lower bits for proper
 * scaling so the brightest 5-bit value maps to 0xFF rather than 0xF8. */
static inline uint32_t rgb555_to_argb(uint16_t px)
{
    uint32_t r5 = (px >> 10) & 0x1F;
    uint32_t g5 = (px >>  5) & 0x1F;
    uint32_t b5 =  px        & 0x1F;
    uint32_t r8 = (r5 << 3) | (r5 >> 2);
    uint32_t g8 = (g5 << 3) | (g5 >> 2);
    uint32_t b8 = (b5 << 3) | (b5 >> 2);
    return 0xFF000000u | (r8 << 16) | (g8 << 8) | b8;
}

static uint32_t s_rgb555_lut[1 << 15];
static uint16_t s_rgb565_lut[1 << 15];
/* The guest framebuffer is bottom-up, so display/capture flip by default. */
static bool s_flip_y = true;

static inline uint16_t rgb555_to_rgb565(uint16_t px)
{
    uint16_t r5 = (px >> 10) & 0x1f;
    uint16_t g5 = (px >> 5) & 0x1f;
    uint16_t b5 = px & 0x1f;
    uint16_t g6 = (g5 << 1) | (g5 >> 4);

    return (r5 << 11) | (g6 << 5) | b5;
}

static uint32_t p2k_phys_ldl(hwaddr pa)
{
    return address_space_ldl_le(&address_space_memory, pa,
                                MEMTXATTRS_UNSPECIFIED, NULL);
}

static void p2k_phys_read(hwaddr pa, void *buf, uint32_t len)
{
    address_space_read(&address_space_memory, pa, MEMTXATTRS_UNSPECIFIED,
                       buf, len);
}

static uint32_t p2k_display_latch_source(P2KDisplayState *s,
                                         uint32_t fb_off,
                                         int *src_pitch,
                                         bool *flip_y)
{
    qemu_mutex_lock(&s->frame_lock);
    if (!s->game_pitch && fb_off != 0 && (fb_off % GAME_BUF_SIZE) == 0) {
        s->game_pitch = true;
    }
    *src_pitch = s->game_pitch ? 2048 : 1280;
    *flip_y = s_flip_y;
    s->last_fb_off = fb_off;
    qemu_mutex_unlock(&s->frame_lock);

    return fb_off <= 0x300000u ? fb_off : 0;
}

bool p2k_display_copy_rgb555_frame(uint16_t *pixels, size_t pixel_count)
{
    uint32_t fb_off;
    int src_pitch;
    bool flip_y;
    const uint8_t *guest_fb;

    if (!pixels || pixel_count < FB_W * FB_H ||
        !s_disp.ram || !s_disp.gx_regs) {
        return false;
    }

    fb_off = ldl_le_p(s_disp.gx_regs + GX_DC_FB_ST_OFFSET);
    fb_off = p2k_display_latch_source(&s_disp, fb_off, &src_pitch, &flip_y);
    guest_fb = s_disp.ram + GX_FB_RAM_MIRROR + fb_off;

    for (int src_y = 0; src_y < FB_H; src_y++) {
        int dst_y = flip_y ? FB_H - 1 - src_y : src_y;
        memcpy(pixels + dst_y * FB_W, guest_fb + src_y * src_pitch,
               FB_W * sizeof(*pixels));
    }
    return true;
}

static void p2k_display_invalidate(void *opaque)
{
    /* Force-refresh on next update — nothing to clear here, our pixel
     * buffer is regenerated from guest RAM every gfx_update call. */
}

void p2k_display_toggle_flip_y(void)
{
    qemu_mutex_lock(&s_disp.frame_lock);
    s_flip_y = !s_flip_y;
    qemu_mutex_unlock(&s_disp.frame_lock);
    fprintf(stderr, "[display] F2 → flip-Y %s\n",
            s_flip_y ? "ON (default)" : "OFF (raw orientation)");
}

/* Per-frame submit counter. Snapshotted by the audit panel to emit
 * FPS (see p2k-timing-audit.c). Was missing from the QEMU port though
 * Encore/Encore had a `[disp] FPS:` line. */
static uint64_t s_disp_frames;

uint64_t p2k_display_get_frames(void) { return s_disp_frames; }

typedef struct P2KHostKey {
    int qcode;
    bool down;
} P2KHostKey;

static void p2k_host_key_bh(void *opaque)
{
    P2KHostKey *key = opaque;
    p2k_lpt_host_key(key->qcode, key->down);
    g_free(key);
}

static void p2k_queue_host_key(int qcode, bool down)
{
    P2KHostKey *key;
    if (qcode == Q_KEY_CODE_UNMAPPED) {
        return;
    }
    key = g_new(P2KHostKey, 1);
    key->qcode = qcode;
    key->down = down;
    aio_bh_schedule_oneshot(qemu_get_aio_context(), p2k_host_key_bh, key);
}

static int p2k_sdl_qcode(SDL_Keycode sym)
{
    if (sym >= SDLK_a && sym <= SDLK_z) {
        return p2k_switch_keymap_letter_qcode('a' + sym - SDLK_a);
    }
    switch (sym) {
    case SDLK_F1: return Q_KEY_CODE_F1;
    case SDLK_F2: return Q_KEY_CODE_F2;
    case SDLK_F3: return Q_KEY_CODE_F3;
    case SDLK_F4: return Q_KEY_CODE_F4;
    case SDLK_F5: return Q_KEY_CODE_F5;
    case SDLK_F6: return Q_KEY_CODE_F6;
    case SDLK_F7: return Q_KEY_CODE_F7;
    case SDLK_F8: return Q_KEY_CODE_F8;
    case SDLK_F9: return Q_KEY_CODE_F9;
    case SDLK_F10: return Q_KEY_CODE_F10;
    case SDLK_F12: return Q_KEY_CODE_F12;
    case SDLK_RETURN: return Q_KEY_CODE_RET;
    case SDLK_KP_ENTER: return Q_KEY_CODE_KP_ENTER;
    case SDLK_ESCAPE: return Q_KEY_CODE_ESC;
    case SDLK_LEFT: return Q_KEY_CODE_LEFT;
    case SDLK_RIGHT: return Q_KEY_CODE_RIGHT;
    case SDLK_UP: return Q_KEY_CODE_UP;
    case SDLK_DOWN: return Q_KEY_CODE_DOWN;
    case SDLK_KP_MINUS: return Q_KEY_CODE_KP_SUBTRACT;
    case SDLK_KP_PLUS: return Q_KEY_CODE_KP_ADD;
    case SDLK_KP_0: return Q_KEY_CODE_KP_0;
    case SDLK_KP_1: return Q_KEY_CODE_KP_1;
    case SDLK_KP_2: return Q_KEY_CODE_KP_2;
    case SDLK_KP_3: return Q_KEY_CODE_KP_3;
    case SDLK_KP_4: return Q_KEY_CODE_KP_4;
    case SDLK_KP_5: return Q_KEY_CODE_KP_5;
    case SDLK_KP_6: return Q_KEY_CODE_KP_6;
    case SDLK_KP_7: return Q_KEY_CODE_KP_7;
    case SDLK_KP_8: return Q_KEY_CODE_KP_8;
    case SDLK_KP_9: return Q_KEY_CODE_KP_9;
    case SDLK_0: return Q_KEY_CODE_0;
    case SDLK_1: return Q_KEY_CODE_1;
    case SDLK_2: return Q_KEY_CODE_2;
    case SDLK_3: return Q_KEY_CODE_3;
    case SDLK_4: return Q_KEY_CODE_4;
    case SDLK_5: return Q_KEY_CODE_5;
    case SDLK_6: return Q_KEY_CODE_6;
    case SDLK_7: return Q_KEY_CODE_7;
    case SDLK_8: return Q_KEY_CODE_8;
    case SDLK_9: return Q_KEY_CODE_9;
    case SDLK_LCTRL: return Q_KEY_CODE_CTRL;
    case SDLK_RCTRL: return Q_KEY_CODE_CTRL_R;
    case SDLK_LALT: return Q_KEY_CODE_ALT;
    case SDLK_RALT: return Q_KEY_CODE_ALT_R;
    case SDLK_EQUALS: return Q_KEY_CODE_EQUAL;
    case SDLK_SPACE: return Q_KEY_CODE_SPC;
    default: return Q_KEY_CODE_UNMAPPED;
    }
}

static void p2k_sdl_screenshot(P2KDisplayState *s)
{
    const char *dir = getenv("P2K_SCREENSHOT_DIR");
    char path[PATH_MAX];
    struct tm tm;
    time_t now = time(NULL);
    SDL_Surface *shot = NULL;
    SDL_Surface *readback = NULL;
    int output_w;
    int output_h;
    bool ok = false;

    if (!dir || !dir[0]) {
        dir = "/tmp";
    }
    if (SDL_GetRendererOutputSize(s->renderer, &output_w, &output_h) < 0 ||
        output_w <= 0 || output_h <= 0) {
        warn_report("pinball2000: cannot determine screenshot size: %s",
                    SDL_GetError());
        return;
    }
    localtime_r(&now, &tm);
    snprintf(path, sizeof(path),
             "%s/p2k_screen_%04d%02d%02d_%02d%02d%02d.bmp", dir,
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    shot = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_W, SCREEN_H, 32,
                                          SDL_PIXELFORMAT_ARGB8888);
    if (!shot) {
        goto done;
    }

    /* SDL_RenderReadPixels() does not scale: a NULL rectangle writes the
     * complete renderer output.  Read fullscreen/resized/HiDPI windows into
     * a correctly sized surface first, then scale the complete image back to
     * the normal 640x480 presentation size. */
    if (output_w == SCREEN_W && output_h == SCREEN_H) {
        readback = shot;
    } else {
        readback = SDL_CreateRGBSurfaceWithFormat(0, output_w, output_h, 32,
                                                  SDL_PIXELFORMAT_ARGB8888);
        if (!readback) {
            goto done;
        }
    }
    if (SDL_RenderReadPixels(s->renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                             readback->pixels, readback->pitch) < 0) {
        goto done;
    }
    if (readback != shot && SDL_BlitScaled(readback, NULL, shot, NULL) < 0) {
        goto done;
    }
    if (SDL_SaveBMP(shot, path) < 0) {
        goto done;
    }
    ok = true;

done:
    if (!ok) {
        warn_report("pinball2000: --framebuffer screenshot failed: %s",
                    SDL_GetError());
    } else {
        info_report("pinball2000: screenshot written to %s (%dx%d)", path,
                    SCREEN_W, SCREEN_H);
    }
    if (readback && readback != shot) {
        SDL_FreeSurface(readback);
    }
    if (shot) {
        SDL_FreeSurface(shot);
    }
}

bool p2k_display_request_screenshot(void)
{
    if (!s_disp.threaded || !qatomic_read(&s_disp.worker_run)) {
        return false;
    }
    qatomic_set(&s_disp.screenshot_pending, 1);
    return true;
}

static void p2k_sdl_events(P2KDisplayState *s)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            p2k_queue_host_key(Q_KEY_CODE_F1, true);
        } else if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
            bool down = ev.type == SDL_KEYDOWN;
            SDL_Keycode sym = ev.key.keysym.sym;
            if (down && ev.key.repeat) {
                continue;
            }
            if (down && (sym == SDLK_F11 ||
                         (sym == SDLK_RETURN &&
                          (ev.key.keysym.mod & KMOD_ALT)))) {
                Uint32 flags = SDL_GetWindowFlags(s->window);
                SDL_SetWindowFullscreen(s->window,
                    (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 :
                    SDL_WINDOW_FULLSCREEN_DESKTOP);
                continue;
            }
            if (down && sym == SDLK_F3) {
                p2k_sdl_screenshot(s);
                continue;
            }
            p2k_queue_host_key(p2k_sdl_qcode(sym), down);
        }
    }
}

/* Build frames away from QEMU's vCPU/UI path.  Both source regions are
 * RAM-backed host pointers: no address-space transaction, BQL acquisition,
 * TLB flush, or display-backend call occurs on this worker. */
static void *p2k_display_worker(void *opaque)
{
    P2KDisplayState *s = opaque;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        error_report("pinball2000: --framebuffer SDL init failed: %s",
                     SDL_GetError());
        return NULL;
    }
    Uint32 window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    if (getenv("P2K_FRAMEBUFFER_FULLSCREEN")) {
        window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    s->window = SDL_CreateWindow("Encore Pinball 2000 emulator",
                                 SDL_WINDOWPOS_CENTERED,
                                 SDL_WINDOWPOS_CENTERED,
                                 SCREEN_W, SCREEN_H,
                                 window_flags);
    if (!s->window) {
        error_report("pinball2000: --framebuffer window failed: %s",
                     SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return NULL;
    }
    s->renderer = SDL_CreateRenderer(s->window, -1, SDL_RENDERER_ACCELERATED);
    if (!s->renderer) {
        s->renderer = SDL_CreateRenderer(s->window, -1, SDL_RENDERER_SOFTWARE);
    }
    s->texture = s->renderer ? SDL_CreateTexture(s->renderer,
                                                  SDL_PIXELFORMAT_RGB555,
                                                  SDL_TEXTUREACCESS_STREAMING,
                                                  FB_W, FB_H) : NULL;
    if (!s->renderer || !s->texture) {
        error_report("pinball2000: --framebuffer renderer failed: %s",
                     SDL_GetError());
        goto out;
    }

    while (qatomic_read(&s->worker_run)) {
        uint32_t fb_off = ldl_le_p(s->gx_regs + GX_DC_FB_ST_OFFSET);
        int src_pitch;
        bool flip_y;

        fb_off = p2k_display_latch_source(s, fb_off, &src_pitch, &flip_y);

        uint8_t *guest_fb = s->ram + GX_FB_RAM_MIRROR + fb_off;
        p2k_sdl_events(s);
        SDL_UpdateTexture(s->texture, NULL, guest_fb, src_pitch);
        SDL_SetRenderDrawColor(s->renderer, 0, 0, 0, 255);
        SDL_RenderClear(s->renderer);
        SDL_RenderCopyEx(s->renderer, s->texture, NULL, NULL, 0.0, NULL,
                         flip_y ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE);
        draw_status_sdl(s);
        SDL_RenderPresent(s->renderer);
        if (qatomic_xchg(&s->screenshot_pending, 0)) {
            p2k_sdl_screenshot(s);
        }
        qatomic_inc(&s_disp_frames);
        g_usleep(16000);
    }

out:
    if (s->texture) SDL_DestroyTexture(s->texture);
    if (s->renderer) SDL_DestroyRenderer(s->renderer);
    if (s->window) SDL_DestroyWindow(s->window);
    s->texture = NULL;
    s->renderer = NULL;
    s->window = NULL;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return NULL;
}

/* Experimental QEMU-backend async submit. QEMU still owns the console,
 * DisplaySurface, DisplayChangeListener, window and input. An OpenGL-backed
 * SDL renderer transfers its context from QEMU's refresh thread to this worker
 * on the first real refresh; software renderers need no context handoff. */
static void *p2k_qemu_submit_worker(void *opaque)
{
    P2KDisplayState *s = opaque;
    bool context_acquired = false;

    while (qatomic_read(&s->submit_run)) {
        qemu_mutex_lock(&s->submit_lock);
        while (!s->submit_pending && qatomic_read(&s->submit_run)) {
            qemu_cond_wait(&s->submit_cond, &s->submit_lock);
        }
        if (!qatomic_read(&s->submit_run)) {
            qemu_mutex_unlock(&s->submit_lock);
            break;
        }
        if (s->submit_context_handoff && !context_acquired) {
            if (SDL_GL_MakeCurrent(s->submit_window,
                                   s->submit_gl_context) < 0) {
                error_report("pinball2000: async display context acquire "
                             "failed: %s", SDL_GetError());
                qatomic_set(&s->submit_run, false);
                qemu_mutex_unlock(&s->submit_lock);
                break;
            }
            context_acquired = true;
            info_report("pinball2000: async submit worker acquired QEMU "
                        "SDL OpenGL context");
        }

        DisplaySurface *surf = qemu_console_surface(s->con);
        memcpy(surface_data(surf), s->submit_staging, s->submit_bytes);
        s->submit_pending = false;
        qemu_mutex_unlock(&s->submit_lock);

        int64_t submit_start = s->profile ?
            qemu_clock_get_ns(QEMU_CLOCK_REALTIME) : 0;
        dpy_gfx_update_full(s->con);
        if (s->profile) {
            uint64_t submit_ns = qemu_clock_get_ns(QEMU_CLOCK_REALTIME) -
                                 submit_start;
            qemu_mutex_lock(&s->submit_lock);
            s->profile_submit_frames++;
            s->profile_submit_ns += submit_ns;
            s->profile_submit_max_ns = MAX(s->profile_submit_max_ns,
                                           submit_ns);
            qemu_mutex_unlock(&s->submit_lock);
        }
        qatomic_inc(&s_disp_frames);
    }
    if (context_acquired && (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) &&
        SDL_GL_MakeCurrent(s->submit_window, NULL) < 0) {
        warn_report("pinball2000: async display context release failed: %s",
                    SDL_GetError());
    }
    return NULL;
}

static void p2k_display_update(void *opaque)
{
    P2KDisplayState *s = opaque;
    DisplaySurface *surf = qemu_console_surface(s->con);
    int64_t profile_start = s->profile ?
        qemu_clock_get_ns(QEMU_CLOCK_REALTIME) : 0;

    if (!surf) {
        return;
    }
    if (!s->threaded) {
        if (s->submit_started) {
            qemu_mutex_lock(&s->submit_lock);
            if (!s->submit_context_checked) {
                s->submit_context_checked = true;
                s->submit_window = SDL_GL_GetCurrentWindow();
                s->submit_gl_context = SDL_GL_GetCurrentContext();
                if (s->submit_window && s->submit_gl_context) {
                    if (SDL_GL_MakeCurrent(s->submit_window, NULL) < 0) {
                        warn_report("pinball2000: async display context "
                                    "release failed; using backend-native "
                                    "ownership: %s", SDL_GetError());
                    } else {
                        s->submit_context_handoff = true;
                        info_report("pinball2000: QEMU SDL OpenGL context "
                                    "released for async submit worker");
                    }
                }
            }
        }
        void *dst_raw = s->submit_started ? s->submit_staging :
                                            surface_data(surf);
        bool bpp16 = surface_bytes_per_pixel(surf) == 2;
        uint32_t fb_off = s->qemu_framebuffer ?
            ldl_le_p(s->gx_regs + GX_DC_FB_ST_OFFSET) :
            p2k_phys_ldl(GX_BASE + GX_DC_FB_ST_OFFSET);
        uint16_t row_buf[FB_W];
        int src_pitch;
        bool flip_y;

        fb_off = p2k_display_latch_source(s, fb_off, &src_pitch, &flip_y);
        for (int src_y = 0; src_y < FB_H; src_y++) {
            int dst_y = (flip_y ? (FB_H - 1 - src_y) : src_y) * 2;
            const uint16_t *src;

            if (s->qemu_framebuffer) {
                src = (const uint16_t *)(s->ram + GX_FB_RAM_MIRROR + fb_off +
                                         src_y * src_pitch);
            } else {
                hwaddr row_pa = GX_FB_RAM_MIRROR + fb_off + src_y * src_pitch;
                p2k_phys_read(row_pa, row_buf, sizeof(row_buf));
                src = row_buf;
            }
            if (bpp16) {
                uint16_t *r1 = &((uint16_t *)dst_raw)[dst_y * SCREEN_W];
                uint16_t *r2 = &((uint16_t *)dst_raw)[(dst_y + 1) * SCREEN_W];
                if (s->qemu_framebuffer) {
                    if (s->qemu_rgb565) {
                        for (int x = 0; x < FB_W; x++) {
                            r1[x] = s_rgb565_lut[src[x] & 0x7fffu];
                        }
                        memcpy(r2, r1, FB_W * sizeof(*r1));
                    } else {
                        memcpy(r1, src, FB_W * sizeof(*src));
                        memcpy(r2, src, FB_W * sizeof(*src));
                    }
                } else {
                    for (int x = 0; x < FB_W; x++) {
                        uint16_t px = src[x] & 0x7fffu;
                        r1[x] = px;
                        r2[x] = px;
                    }
                }
            } else {
                uint32_t *dst = dst_raw;
                uint32_t *r1 = &dst[dst_y * SCREEN_W];
                uint32_t *r2 = &dst[(dst_y + 1) * SCREEN_W];
                if (s->qemu_framebuffer) {
                    for (int x = 0; x < FB_W; x++) {
                        r1[x] = s_rgb555_lut[src[x] & 0x7fffu];
                    }
                    memcpy(r2, r1, FB_W * sizeof(*r1));
                } else {
                    for (int x = 0; x < FB_W; x++) {
                        uint32_t argb = rgb555_to_argb(src[x] & 0x7fffu);
                        r1[x] = argb;
                        r2[x] = argb;
                    }
                }
            }
        }
        draw_status_pixels(dst_raw, bpp16,
                           surface_format(surf) == PIXMAN_r5g6b5 ?
                           0xffff : 0x7fff);
        int64_t profile_submit = s->profile ?
            qemu_clock_get_ns(QEMU_CLOCK_REALTIME) : 0;
        if (s->profile) {
            uint64_t prepare_ns = profile_submit - profile_start;
            s->profile_frames++;
            s->profile_prepare_ns += prepare_ns;
            s->profile_prepare_max_ns = MAX(s->profile_prepare_max_ns,
                                            prepare_ns);
        }
        if (s->submit_started) {
            s->submit_pending = true;
            qemu_cond_signal(&s->submit_cond);
            qemu_mutex_unlock(&s->submit_lock);
            return;
        }
        dpy_gfx_update_full(s->con);
        if (s->profile) {
            uint64_t submit_ns = qemu_clock_get_ns(QEMU_CLOCK_REALTIME) -
                                 profile_submit;
            s->profile_submit_frames++;
            s->profile_submit_ns += submit_ns;
            s->profile_submit_max_ns = MAX(s->profile_submit_max_ns,
                                           submit_ns);
        }
        s_disp_frames++;
        return;
    }
    /* The host SDL path owns presentation and never installs a QEMU console,
     * so a threaded instance cannot reach this callback. */
}

void p2k_display_stop_presentation(void)
{
    P2KDisplayState *s = &s_disp;

    qatomic_set(&s->worker_run, false);
    if (s->worker_started) {
        qemu_thread_join(&s->worker);
        s->worker_started = false;
    }
    if (s->submit_started) {
        qatomic_set(&s->submit_run, false);
        qemu_mutex_lock(&s->submit_lock);
        qemu_cond_signal(&s->submit_cond);
        qemu_mutex_unlock(&s->submit_lock);
        qemu_thread_join(&s->submit_worker);
        s->submit_started = false;
        if (s->submit_context_handoff &&
            (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) &&
            SDL_GL_MakeCurrent(s->submit_window,
                               s->submit_gl_context) < 0) {
            warn_report("pinball2000: display context restore failed: %s",
                        SDL_GetError());
        }
        g_clear_pointer(&s->submit_staging, g_free);
    }
}

static void p2k_display_shutdown(Notifier *notifier, void *data)
{
    P2KDisplayState *s = container_of(notifier, P2KDisplayState,
                                      exit_notifier);

    p2k_display_stop_presentation();
    if (s->profile && s->profile_frames) {
        info_report("pinball2000: QEMU display profile frames=%llu "
                    "prepare_avg=%.1fus prepare_max=%.1fus "
                    "submit_avg=%.1fus submit_max=%.1fus",
                    (unsigned long long)s->profile_frames,
                    (double)s->profile_prepare_ns / s->profile_frames / 1000.0,
                    (double)s->profile_prepare_max_ns / 1000.0,
                    s->profile_submit_frames ?
                    (double)s->profile_submit_ns /
                    s->profile_submit_frames / 1000.0 : 0.0,
                    (double)s->profile_submit_max_ns / 1000.0);
    }
}

static void p2k_display_quiesce(Notifier *notifier, void *data)
{
    (void)notifier;
    (void)data;

    p2k_display_stop_presentation();
}

static const GraphicHwOps p2k_display_ops = {
    .invalidate = p2k_display_invalidate,
    .gfx_update = p2k_display_update,
};

void p2k_install_display(void)
{
    DisplaySurface *surf;
    const char     *bpp_env = getenv("P2K_DISPLAY_BPP");
    const char     *thread_env = getenv("P2K_FRAMEBUFFER_THREAD");
    const char     *qemu_fb_env = getenv("P2K_QEMU_FRAMEBUFFER");
    const char     *qemu_fb_format = getenv("P2K_QEMU_FB_FORMAT");
    const char     *qemu_fb_async = getenv("P2K_QEMU_FB_ASYNC");
    const char     *profile_env = getenv("P2K_DISPLAY_PROFILE");
    bool            bpp16   = bpp_env && !strcmp(bpp_env, "16");

    qemu_mutex_init(&s_status_lock);
    qemu_mutex_init(&s_disp.frame_lock);
    qemu_mutex_init(&s_disp.submit_lock);
    qemu_cond_init(&s_disp.submit_cond);
    s_disp.ram = memory_region_get_ram_ptr(MACHINE(qdev_get_machine())->ram);
    s_disp.gx_regs = p2k_gx_regs_host();
    s_disp.threaded = thread_env && thread_env[0] == '1';
    s_disp.qemu_framebuffer = qemu_fb_env && qemu_fb_env[0] == '1';
    s_disp.qemu_rgb565 = s_disp.qemu_framebuffer && qemu_fb_format &&
        !strcmp(qemu_fb_format, "565");
    s_disp.profile = profile_env && profile_env[0] == '1';
    if (s_disp.qemu_framebuffer && bpp16 && !s_disp.qemu_rgb565) {
        warn_report("pinball2000: fast QEMU framebuffer ignores 16 bpp; "
                    "QEMU SDL presents its ARGB surface more efficiently");
        bpp16 = false;
    }
    if (s_disp.qemu_rgb565) {
        bpp16 = true;
    }
    s_disp.bpp16 = bpp16;
    s_disp.shutdown_notifier.notify = p2k_display_quiesce;
    qemu_register_shutdown_notifier(&s_disp.shutdown_notifier);
    s_disp.exit_notifier.notify = p2k_display_shutdown;
    qemu_add_exit_notifier(&s_disp.exit_notifier);
    if (s_disp.qemu_framebuffer) {
        for (unsigned px = 0; px < G_N_ELEMENTS(s_rgb555_lut); px++) {
            s_rgb555_lut[px] = rgb555_to_argb(px);
            s_rgb565_lut[px] = rgb555_to_rgb565(px);
        }
    }
    if (s_disp.threaded) {
        s_disp.worker_run = true;
        s_disp.worker_started = true;
        qemu_thread_create(&s_disp.worker, "p2k-sdl-render",
                           p2k_display_worker, &s_disp, QEMU_THREAD_JOINABLE);
        info_report("pinball2000: --framebuffer direct SDL renderer started");
        return;
    }

    s_disp.con = graphic_console_init(NULL, 0, &p2k_display_ops, &s_disp);
    qemu_console_resize(s_disp.con, SCREEN_W, SCREEN_H);

    /* Replace the placeholder surface with one we own so the gfx_update
     * pixel writes have a stable backing buffer. */
    if (bpp16) {
        size_t   stride = SCREEN_W * sizeof(uint16_t);
        uint8_t *buf    = g_malloc0(stride * SCREEN_H);
        surf = qemu_create_displaysurface_from(SCREEN_W, SCREEN_H,
                                               s_disp.qemu_rgb565 ?
                                               PIXMAN_r5g6b5 : PIXMAN_x1r5g5b5,
                                               stride, buf);
        info_report("pinball2000: display surface = 16 bpp (%s%s)",
                    s_disp.qemu_rgb565 ? "r5g6b5, RGB555 lookup" :
                    "x1r5g5b5, native source format",
                    s_disp.qemu_framebuffer ? ", direct RAM copy" : "");
    } else {
        surf = qemu_create_displaysurface(SCREEN_W, SCREEN_H);
        if (s_disp.qemu_framebuffer) {
            info_report("pinball2000: fast QEMU framebuffer = direct RAM "
                        "RGB555 lookup into native ARGB display surface");
        }
    }
    dpy_gfx_replace_surface(s_disp.con, surf);

    if (s_disp.qemu_framebuffer && qemu_fb_async &&
        qemu_fb_async[0] == '1') {
        s_disp.submit_bytes = surface_stride(surf) * surface_height(surf);
        s_disp.submit_staging = g_malloc0(s_disp.submit_bytes);
        s_disp.submit_run = true;
        s_disp.submit_started = true;
        qemu_thread_create(&s_disp.submit_worker, "p2k-qemu-display",
                           p2k_qemu_submit_worker, &s_disp,
                           QEMU_THREAD_JOINABLE);
        info_report("pinball2000: experimental QEMU async submit started");
    }

}
