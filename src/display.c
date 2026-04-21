/*
 * display.c — SDL2 window, framebuffer rendering, keyboard input.
 *
 * Guest framebuffer: 640×240 RGB555 at GX_BASE+0x800000
 * Row pitch: 2048 bytes (GP blit stride, y << 11)
 * Double each row → 640×480 output
 * Y-flip: GP blit Y=0 is bottom, SDL Y=0 is top
 */
#include "encore.h"
#include "stb_image_write.h"

static int  s_y_flip = 1;
static bool s_seen_nonzero_fb = false;
static int  s_snap_id = 0;

/* Alt+K raw keyboard capture state. When ON every SDL_KEYDOWN/UP gets
 * translated to PS/2 Set-1 scancode bytes and queued via netcon's
 * KBC injection helper. ALL gameplay polling (F-keys, flippers, coin,
 * START button) is suppressed so the keystrokes can't double-fire as
 * cabinet inputs. ALT+K toggles back; F1 still quits. */
static bool s_alt_k_held_last = false;

/* Map an SDL_Scancode to a PS/2 Set 1 make code. Returns 0 if not mapped.
 * Out-param `is_extended` is set true for keys that need an 0xE0 prefix
 * byte (right-side modifiers, arrows, navigation, KP_Enter, KP /). */
static uint8_t sdl_to_set1(SDL_Scancode sc, bool *is_extended)
{
    *is_extended = false;
    switch (sc) {
    /* Letters (Set 1 make codes) */
    case SDL_SCANCODE_A: return 0x1E;  case SDL_SCANCODE_B: return 0x30;
    case SDL_SCANCODE_C: return 0x2E;  case SDL_SCANCODE_D: return 0x20;
    case SDL_SCANCODE_E: return 0x12;  case SDL_SCANCODE_F: return 0x21;
    case SDL_SCANCODE_G: return 0x22;  case SDL_SCANCODE_H: return 0x23;
    case SDL_SCANCODE_I: return 0x17;  case SDL_SCANCODE_J: return 0x24;
    case SDL_SCANCODE_K: return 0x25;  case SDL_SCANCODE_L: return 0x26;
    case SDL_SCANCODE_M: return 0x32;  case SDL_SCANCODE_N: return 0x31;
    case SDL_SCANCODE_O: return 0x18;  case SDL_SCANCODE_P: return 0x19;
    case SDL_SCANCODE_Q: return 0x10;  case SDL_SCANCODE_R: return 0x13;
    case SDL_SCANCODE_S: return 0x1F;  case SDL_SCANCODE_T: return 0x14;
    case SDL_SCANCODE_U: return 0x16;  case SDL_SCANCODE_V: return 0x2F;
    case SDL_SCANCODE_W: return 0x11;  case SDL_SCANCODE_X: return 0x2D;
    case SDL_SCANCODE_Y: return 0x15;  case SDL_SCANCODE_Z: return 0x2C;
    /* Top-row digits */
    case SDL_SCANCODE_1: return 0x02;  case SDL_SCANCODE_2: return 0x03;
    case SDL_SCANCODE_3: return 0x04;  case SDL_SCANCODE_4: return 0x05;
    case SDL_SCANCODE_5: return 0x06;  case SDL_SCANCODE_6: return 0x07;
    case SDL_SCANCODE_7: return 0x08;  case SDL_SCANCODE_8: return 0x09;
    case SDL_SCANCODE_9: return 0x0A;  case SDL_SCANCODE_0: return 0x0B;
    /* Punctuation main row */
    case SDL_SCANCODE_MINUS:        return 0x0C;
    case SDL_SCANCODE_EQUALS:       return 0x0D;
    case SDL_SCANCODE_BACKSPACE:    return 0x0E;
    case SDL_SCANCODE_TAB:          return 0x0F;
    case SDL_SCANCODE_LEFTBRACKET:  return 0x1A;
    case SDL_SCANCODE_RIGHTBRACKET: return 0x1B;
    case SDL_SCANCODE_RETURN:       return 0x1C;
    case SDL_SCANCODE_LCTRL:        return 0x1D;
    case SDL_SCANCODE_SEMICOLON:    return 0x27;
    case SDL_SCANCODE_APOSTROPHE:   return 0x28;
    case SDL_SCANCODE_GRAVE:        return 0x29;
    case SDL_SCANCODE_LSHIFT:       return 0x2A;
    case SDL_SCANCODE_BACKSLASH:    return 0x2B;
    case SDL_SCANCODE_COMMA:        return 0x33;
    case SDL_SCANCODE_PERIOD:       return 0x34;
    case SDL_SCANCODE_SLASH:        return 0x35;
    case SDL_SCANCODE_RSHIFT:       return 0x36;
    case SDL_SCANCODE_KP_MULTIPLY:  return 0x37;
    case SDL_SCANCODE_LALT:         return 0x38;
    case SDL_SCANCODE_SPACE:        return 0x39;
    case SDL_SCANCODE_CAPSLOCK:     return 0x3A;
    /* F1..F10 */
    case SDL_SCANCODE_F1:  return 0x3B;  case SDL_SCANCODE_F2:  return 0x3C;
    case SDL_SCANCODE_F3:  return 0x3D;  case SDL_SCANCODE_F4:  return 0x3E;
    case SDL_SCANCODE_F5:  return 0x3F;  case SDL_SCANCODE_F6:  return 0x40;
    case SDL_SCANCODE_F7:  return 0x41;  case SDL_SCANCODE_F8:  return 0x42;
    case SDL_SCANCODE_F9:  return 0x43;  case SDL_SCANCODE_F10: return 0x44;
    case SDL_SCANCODE_NUMLOCKCLEAR: return 0x45;
    case SDL_SCANCODE_SCROLLLOCK:   return 0x46;
    /* Numeric keypad (no extension) */
    case SDL_SCANCODE_KP_7: return 0x47;  case SDL_SCANCODE_KP_8: return 0x48;
    case SDL_SCANCODE_KP_9: return 0x49;  case SDL_SCANCODE_KP_MINUS: return 0x4A;
    case SDL_SCANCODE_KP_4: return 0x4B;  case SDL_SCANCODE_KP_5: return 0x4C;
    case SDL_SCANCODE_KP_6: return 0x4D;  case SDL_SCANCODE_KP_PLUS:  return 0x4E;
    case SDL_SCANCODE_KP_1: return 0x4F;  case SDL_SCANCODE_KP_2: return 0x50;
    case SDL_SCANCODE_KP_3: return 0x51;  case SDL_SCANCODE_KP_0: return 0x52;
    case SDL_SCANCODE_KP_PERIOD: return 0x53;
    case SDL_SCANCODE_F11: return 0x57;
    case SDL_SCANCODE_F12: return 0x58;
    /* Extended (0xE0-prefixed) */
    case SDL_SCANCODE_RCTRL:    *is_extended = true; return 0x1D;
    case SDL_SCANCODE_RALT:     *is_extended = true; return 0x38;
    case SDL_SCANCODE_KP_ENTER: *is_extended = true; return 0x1C;
    case SDL_SCANCODE_KP_DIVIDE:*is_extended = true; return 0x35;
    case SDL_SCANCODE_INSERT:   *is_extended = true; return 0x52;
    case SDL_SCANCODE_HOME:     *is_extended = true; return 0x47;
    case SDL_SCANCODE_PAGEUP:   *is_extended = true; return 0x49;
    case SDL_SCANCODE_DELETE:   *is_extended = true; return 0x53;
    case SDL_SCANCODE_END:      *is_extended = true; return 0x4F;
    case SDL_SCANCODE_PAGEDOWN: *is_extended = true; return 0x51;
    case SDL_SCANCODE_UP:       *is_extended = true; return 0x48;
    case SDL_SCANCODE_LEFT:     *is_extended = true; return 0x4B;
    case SDL_SCANCODE_DOWN:     *is_extended = true; return 0x50;
    case SDL_SCANCODE_RIGHT:    *is_extended = true; return 0x4D;
    default: return 0;
    }
}

/* Update the SDL window title to reflect capture state. */
static void update_window_title(void)
{
    if (!g_emu.window) return;
    SDL_SetWindowTitle(g_emu.window,
        g_emu.kbd_capture
            ? "Encore — Pinball 2000  [KBD CAPTURE — ALT+K to release]"
            : "Encore — Pinball 2000");
}

/* Credit (F10/C) pulse state machine — supports rapid mashing.
 * Each KEYDOWN enqueues a discrete pulse: HIGH for COIN_HIGH_FRAMES,
 * then a LOW gap of COIN_LOW_FRAMES so the game's edge detector sees
 * each press as a distinct switch closure. */
#define COIN_HIGH_FRAMES 6   /* ~100ms HIGH @ 60fps */
#define COIN_LOW_FRAMES  3   /* ~50ms  LOW gap */
#define COIN_QUEUE_MAX   8
static int s_coin_high    = 0;
static int s_coin_low     = 0;
static int s_coin_pending = 0;
int g_probe_col = 0;  /* DEBUG: current col for digit-key probe; [/] adjust */

/* Screenshot output directory: env override, else ./screenshots/ in CWD. */
static const char *screenshot_dir(void)
{
    const char *env = getenv("ENCORE_SCREENSHOT_DIR");
    return (env && *env) ? env : "./screenshots";
}

static void ensure_screenshot_dir(void)
{
    mkdir(screenshot_dir(), 0777);
}

static void save_screenshot(const char *prefix, int id)
{
    if (!g_emu.display_ready) return;

    ensure_screenshot_dir();

    /* Timestamped PNG filename for unique, viewable captures */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);

    char path[320];
    snprintf(path, sizeof(path),
             "%s/%s_%04d%02d%02d_%02d%02d%02d_%03ld_%03d.png",
             screenshot_dir(), prefix,
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec,
             ts.tv_nsec / 1000000, id);

    /* Convert ARGB → RGBA for stb_image_write */
    static uint8_t rgba_buf[SCREEN_W * SCREEN_H * 4];
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        uint32_t argb = g_emu.fb_pixels[i];
        rgba_buf[i * 4 + 0] = (argb >> 16) & 0xFF; /* R */
        rgba_buf[i * 4 + 1] = (argb >>  8) & 0xFF; /* G */
        rgba_buf[i * 4 + 2] = (argb      ) & 0xFF; /* B */
        rgba_buf[i * 4 + 3] = 0xFF;                 /* A */
    }

    stbi_write_png(path, SCREEN_W, SCREEN_H, 4, rgba_buf, SCREEN_W * 4);
    LOG("disp", "screenshot → %s\n", path);
}

int display_init(void)
{
    /* Honour --flipscreen seed. Default keeps existing GP-blit Y-flip. */
    if (g_emu.start_flipscreen)
        s_y_flip = !s_y_flip;

    /* Resolve bpp early; values other than 16/32 fall back to 32. */
    if (g_emu.bpp == 0) g_emu.bpp = 32;
    if (g_emu.bpp != 16 && g_emu.bpp != 32) {
        LOG("disp", "--bpp %d unsupported, falling back to 32\n", g_emu.bpp);
        g_emu.bpp = 32;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
        LOG("disp", "SDL_Init failed: %s\n", SDL_GetError());
        /* Try without video for headless operation */
        if (!getenv("DISPLAY")) {
            LOG("disp", "No DISPLAY — running headless\n");
            return -1;
        }
        return -1;
    }

    Uint32 win_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    if (g_emu.start_fullscreen)
        win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    g_emu.window = SDL_CreateWindow(
        "Encore — Pinball 2000",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H,
        win_flags
    );
    if (!g_emu.window) {
        LOG("disp", "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return -1;
    }

    /* No PRESENTVSYNC — i386 POC uses uncapped SDL_Flip().
     * Render loop controls frame timing via timer. */
    g_emu.renderer = SDL_CreateRenderer(g_emu.window, -1,
        SDL_RENDERER_ACCELERATED);
    if (!g_emu.renderer) {
        g_emu.renderer = SDL_CreateRenderer(g_emu.window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!g_emu.renderer) {
        LOG("disp", "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return -1;
    }

    Uint32 tex_fmt = (g_emu.bpp == 16)
                    ? SDL_PIXELFORMAT_RGB565
                    : SDL_PIXELFORMAT_ARGB8888;
    g_emu.texture = SDL_CreateTexture(g_emu.renderer,
        tex_fmt, SDL_TEXTUREACCESS_STREAMING,
        SCREEN_W, SCREEN_H);
    if (!g_emu.texture) {
        LOG("disp", "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return -1;
    }

    /* Build RGB555 → ARGB8888 lookup table (32K entries).
     * Also build RGB555 → RGB565 LUT for --bpp 16 output. */
    for (int i = 0; i < 32768; i++) {
        int r5 = (i >> 10) & 0x1F;
        int g5 = (i >> 5)  & 0x1F;
        int b5 =  i        & 0x1F;
        uint8_t r8 = (r5 << 3) | (r5 >> 2);
        uint8_t g8 = (g5 << 3) | (g5 >> 2);
        uint8_t b8 = (b5 << 3) | (b5 >> 2);
        g_emu.rgb555_lut[i] = 0xFF000000u | (r8 << 16) | (g8 << 8) | b8;
        /* RGB565: 5R | 6G | 5B. Replicate g5's MSB into the new low bit
         * to get the 6th green bit (matches the standard 5-to-6 dither). */
        uint16_t g6 = (g5 << 1) | (g5 >> 4);
        g_emu.rgb555_lut16[i] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
    }

    g_emu.display_ready = true;
    LOG("disp", "SDL2 display: %dx%d %s%s%s\n", SCREEN_W, SCREEN_H,
        g_emu.bpp == 16 ? "RGB565" : "ARGB8888",
        g_emu.start_fullscreen ? " [fullscreen]" : "",
        g_emu.start_flipscreen ? " [flipscreen]" : "");

    /* Print key bindings to stderr so the user always has the up-to-date
     * help text without digging in source. Mirrors the actual SDL key
     * routing in display_handle_events. */
    fprintf(stderr,
        "\n=== Encore — SWE1 key bindings ===\n"
        "  F1            Quit\n"
        "  F2            Flip display Y axis\n"
        "  F3            Screenshot\n"
        "  F4            Toggle COIN DOOR (closed/open interlock)\n"
        "  F6            LEFT  action button   (Phys[10].b7)\n"
        "  F7            LEFT  flipper         (Phys[10].b5)\n"
        "  F8            RIGHT flipper         (Phys[10].b4)\n"
        "  F9            RIGHT action button   (Phys[10].b6)\n"
        "  F10 / C       Insert credit (queueable; mash for multi)\n"
        "  F11 / ALT+ENTER  Toggle FULLSCREEN\n"
        "  F12           Dump guest switch state to stderr\n"
        "  SPACE / S     START button (sw=2 / Phys[0].b2)\n"
        "  0..7          DEBUG: force Phys[c<col>].bN — find which bit is real Start\n"
        "  [ ]           DEBUG: decrement / increment probe col (0..11)\n"
        "  --- coin-door panel (4 buttons; dual-function by mode) ---\n"
        "  ESC / LEFT    btn1: Service Credits / Escape    (Phys[9].b0)\n"
        "  DOWN  - KP_-  btn2: Volume −        / Menu Down (Phys[9].b1)\n"
        "  UP    = KP_+  btn3: Volume +        / Menu Up   (Phys[9].b2)\n"
        "  RIGHT ENTER\n"
        "        KP_ENT  btn4: Begin Test      / Enter     (Phys[9].b3)\n"
        "==================================\n\n");

    return 0;
}

void display_update(void)
{
    if (!g_emu.display_ready || !g_emu.ram) return;

    /* Direct pointer to guest physical RAM framebuffer.
     * g_emu.ram is the Unicorn backing store (uc_mem_map_ptr),
     * so reads are zero-overhead pointer dereferences. */
    uint32_t fb_off = g_emu.dc_fb_offset;
    if (fb_off + FB_H * FB_STRIDE > RAM_SIZE) fb_off = 0;
    uint8_t *guest_fb = g_emu.ram + 0x800000u + fb_off;
    bool any_nonzero = false;

    /* Render 240 source rows → 480 output rows (double each row, Y-flip). */
    bool bpp16 = (g_emu.bpp == 16);
    for (int src_y = 0; src_y < FB_H; src_y++) {
        int draw_y = s_y_flip ? (FB_H - 1 - src_y) : src_y;
        int dst_y = draw_y * 2;

        uint16_t *pixels = (uint16_t *)(guest_fb + src_y * FB_STRIDE);
        if (bpp16) {
            uint16_t *dst1 = &g_emu.fb_pixels16[dst_y * SCREEN_W];
            uint16_t *dst2 = &g_emu.fb_pixels16[(dst_y + 1) * SCREEN_W];
            /* Maintain ARGB shadow only for screenshot/snapshot path. */
            uint32_t *sh1 = &g_emu.fb_pixels[dst_y * SCREEN_W];
            uint32_t *sh2 = &g_emu.fb_pixels[(dst_y + 1) * SCREEN_W];
            for (int x = 0; x < FB_W; x++) {
                uint16_t px = pixels[x] & 0x7FFF;
                if (px) any_nonzero = true;
                uint16_t out = g_emu.rgb555_lut16[px];
                dst1[x] = out;
                dst2[x] = out;
                uint32_t argb = g_emu.rgb555_lut[px];
                sh1[x] = argb;
                sh2[x] = argb;
            }
        } else {
            uint32_t *dst1 = &g_emu.fb_pixels[dst_y * SCREEN_W];
            uint32_t *dst2 = &g_emu.fb_pixels[(dst_y + 1) * SCREEN_W];
            for (int x = 0; x < FB_W; x++) {
                uint16_t px = pixels[x] & 0x7FFF;
                if (px) any_nonzero = true;
                uint32_t argb = g_emu.rgb555_lut[px];
                dst1[x] = argb;
                dst2[x] = argb;
            }
        }
    }

    if (any_nonzero && !s_seen_nonzero_fb) {
        s_seen_nonzero_fb = true;
        LOG("disp", "first non-zero framebuffer detected (fb_off=0x%x)\n", fb_off);
    }

    /* Upload to GPU and present */
    if (bpp16) {
        SDL_UpdateTexture(g_emu.texture, NULL, g_emu.fb_pixels16, SCREEN_W * 2);
    } else {
        SDL_UpdateTexture(g_emu.texture, NULL, g_emu.fb_pixels, SCREEN_W * 4);
    }
    SDL_RenderClear(g_emu.renderer);
    SDL_RenderCopy(g_emu.renderer, g_emu.texture, NULL, NULL);
    SDL_RenderPresent(g_emu.renderer);

    g_emu.frame_count++;

    /* FPS measurement — log every 5 seconds */
    {
        static struct timespec s_fps_ts = {0, 0};
        static uint32_t s_fps_frames = 0;
        s_fps_frames++;
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (s_fps_ts.tv_sec == 0) s_fps_ts = now;
        long elapsed_ms = (now.tv_sec - s_fps_ts.tv_sec) * 1000
                        + (now.tv_nsec - s_fps_ts.tv_nsec) / 1000000;
        if (elapsed_ms >= 5000) {
            float fps = (float)s_fps_frames * 1000.0f / (float)elapsed_ms;
            LOG("disp", "FPS: %.1f (%u frames / %ld ms)\n",
                fps, s_fps_frames, elapsed_ms);
            s_fps_frames = 0;
            s_fps_ts = now;
        }
    }

    /* Manual screenshots only — press F3. (Auto-snapshots removed
     * to avoid filling the screenshots directory unattended.) */
}

void display_handle_events(void)
{
    if (!g_emu.display_ready) return;

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            /* SDL_QUIT from window manager — log but keep running for debugging.
             * Real exit is via F1/Escape only. */
            LOG("disp", "SDL_QUIT received (ignored for debugging)\n");
            break;

        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            bool is_down = (ev.type == SDL_KEYDOWN);

            /* Alt+K toggles capture. We detect it on KEYDOWN, ignore SDL
             * key-repeat (key.repeat != 0). The toggling key itself must
             * NOT be forwarded to the guest, otherwise capture-mode would
             * inject a stray K + ALT make/break pair on every toggle. */
            if (is_down && !ev.key.repeat
                && ev.key.keysym.scancode == SDL_SCANCODE_K
                && (ev.key.keysym.mod & (KMOD_LALT | KMOD_RALT | KMOD_ALT))) {
                g_emu.kbd_capture = !g_emu.kbd_capture;
                update_window_title();
                LOG("disp", "kbd_capture %s (Alt+K)\n",
                    g_emu.kbd_capture ? "ON" : "OFF");
                /* When LEAVING capture, send guest break codes for the
                 * Alt+K combo so the guest's KBD driver doesn't think
                 * those keys are still held down. Order: K break, then
                 * ALT break. */
                if (!g_emu.kbd_capture) {
                    netcon_kbd_inject_scancode(0x80 | 0x25);  /* K break */
                    netcon_kbd_inject_scancode(0x80 | 0x38);  /* L-Alt break */
                }
                s_alt_k_held_last = true;
                break;
            }
            if (!is_down && ev.key.keysym.scancode == SDL_SCANCODE_K) {
                s_alt_k_held_last = false;
            }

            /* In CAPTURE mode every key (except F1 quit + the Alt+K
             * toggle handled above) is translated to a Set 1 scancode
             * and queued straight to the emulated KBC. */
            if (g_emu.kbd_capture) {
                if (is_down && ev.key.keysym.sym == SDLK_F1) {
                    LOG("disp", "F1 — exiting (during capture)\n");
                    g_emu.running = false;
                    break;
                }
                if (is_down && ev.key.repeat) break;  /* drop typematic */
                bool ext = false;
                uint8_t make = sdl_to_set1(ev.key.keysym.scancode, &ext);
                if (make == 0) break;
                if (ext) netcon_kbd_inject_scancode(0xE0);
                netcon_kbd_inject_scancode(is_down ? make : (uint8_t)(0x80 | make));
                break;
            }

            /* Non-capture KEYUP not handled by gameplay logic. */
            if (!is_down) break;

            /* Diagnostic: log every F-key + special so we can see which
             * scancodes actually reach SDL (some WMs eat F11). */
            if ((ev.key.keysym.sym >= SDLK_F1 && ev.key.keysym.sym <= SDLK_F12)
                || ev.key.keysym.sym == SDLK_RETURN
                || ev.key.keysym.sym == SDLK_SPACE
                || ev.key.keysym.sym == SDLK_s)
                fprintf(stderr, "[disp] keydown sym=0x%x mod=0x%x\n",
                        ev.key.keysym.sym, ev.key.keysym.mod);
            switch (ev.key.keysym.sym) {
            case SDLK_F1:
                LOG("disp", "F1 — exiting\n");
                g_emu.running = false;
                break;
            case SDLK_F2:
                s_y_flip = !s_y_flip;
                LOG("disp", "y_flip=%d\n", s_y_flip);
                break;
            case SDLK_F3:
                save_screenshot("encore_snap", s_snap_id++);
                break;
            case SDLK_F4:
                lpt_toggle_coin_door();
                break;
            case SDLK_F10:
            case SDLK_c:
                /* Enqueue a discrete credit pulse. State machine in the
                 * polling section below releases between pulses so the
                 * game's edge detector counts each press separately. */
                if (s_coin_pending < COIN_QUEUE_MAX) s_coin_pending++;
                LOG("input", "Coin pulse queued (pending=%d)\n", s_coin_pending);
                break;
            case SDLK_F11: {
                /* SDL_WINDOW_FULLSCREEN_DESKTOP and SDL_WINDOW_FULLSCREEN
                 * both set the FULLSCREEN bit; check just that one. */
                Uint32 fs = SDL_GetWindowFlags(g_emu.window) & SDL_WINDOW_FULLSCREEN;
                int rc = SDL_SetWindowFullscreen(g_emu.window,
                            fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                fprintf(stderr, "[disp] fullscreen %s (rc=%d %s)\n",
                        fs ? "OFF" : "ON", rc, rc ? SDL_GetError() : "");
                break;
            }
            case SDLK_RETURN:
                /* ALT+ENTER → fullscreen toggle (canonical emu hotkey).
                 * Bare ENTER falls through into the coin-door 'btn4'
                 * polling below, so this only fires with Alt held. */
                if (ev.key.keysym.mod & (KMOD_ALT | KMOD_LALT | KMOD_RALT)) {
                    Uint32 fs = SDL_GetWindowFlags(g_emu.window) & SDL_WINDOW_FULLSCREEN;
                    int rc = SDL_SetWindowFullscreen(g_emu.window,
                                fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                    fprintf(stderr, "[disp] fullscreen %s via ALT+ENTER (rc=%d %s)\n",
                            fs ? "OFF" : "ON", rc, rc ? SDL_GetError() : "");
                }
                break;
            case SDLK_LEFTBRACKET:
                if (g_probe_col > 0) g_probe_col--;
                fprintf(stderr, "[disp] probe col = %d\n", g_probe_col);
                break;
            case SDLK_RIGHTBRACKET:
                if (g_probe_col < 11) g_probe_col++;
                fprintf(stderr, "[disp] probe col = %d\n", g_probe_col);
                break;
            case SDLK_F12:
                lpt_dump_guest_switch_state();
                break;
            }
            break;
        }
        }
    }

    /* === Gameplay polling (F-keys, flippers, coin, START) ===
     * SUPPRESSED while raw keyboard capture is on, otherwise the same
     * physical keypress would fire as both a guest scancode AND a
     * cabinet input — wrong on both counts. Real cabinet path
     * (lpt_passthrough) is unaffected; it doesn't go through here. */
    if (g_emu.kbd_capture) {
        lpt_set_host_input(0, 0);
        lpt_set_start_button(0);
        lpt_set_probe_bit(0, 0);
        return;
    }

    const uint8_t *keys = SDL_GetKeyboardState(NULL);
    uint8_t buttons = 0;   /* opcode 0x01 → Physical[10] (flippers + actions) */
    uint8_t switches = 0;  /* opcode 0x03 → Physical[9]  (4 coin-door buttons) */

    /* Physical[10] — playfield buttons (sw_num 84-87, bits 4-7).
     * F-row keys are positionally identical on every keyboard layout.
     *   bit 4 = right flipper button → F8
     *   bit 5 = left  flipper button → F7
     *   bit 6 = right action button  → F9
     *   bit 7 = left  action button  → F6
     * Bits 0-2 (slam tilt / door / plumb tilt) are managed by io.c.
     */
    if (keys[SDL_SCANCODE_F8]) buttons |= 0x10;
    if (keys[SDL_SCANCODE_F7]) buttons |= 0x20;
    if (keys[SDL_SCANCODE_F9]) buttons |= 0x40;
    if (keys[SDL_SCANCODE_F6]) buttons |= 0x80;

    /* Physical[9] — 4 coin-door buttons (sw_num 72/73/74/75).
     *   bit 0 (sw=72 'Escape') → ESC, LEFT          (Service Credits / Escape)
     *   bit 1 (sw=73 'Down')   → DOWN, -, KP_-      (Volume − / Menu Down)
     *   bit 2 (sw=74 'Up')     → UP,   =, KP_+      (Volume + / Menu Up)
     *   bit 3 (sw=75 'Enter')  → RETURN, KP_ENTER, RIGHT  (Begin Test / Enter)
     */
    if (keys[SDL_SCANCODE_ESCAPE] || keys[SDL_SCANCODE_LEFT])     switches |= 0x01;
    if (keys[SDL_SCANCODE_DOWN]   || keys[SDL_SCANCODE_MINUS]
                                  || keys[SDL_SCANCODE_KP_MINUS]) switches |= 0x02;
    if (keys[SDL_SCANCODE_UP]     || keys[SDL_SCANCODE_EQUALS]
                                  || keys[SDL_SCANCODE_KP_PLUS])  switches |= 0x04;
    if (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER]
                                  || keys[SDL_SCANCODE_RIGHT])    switches |= 0x08;

    /* Credit pulse state machine: HIGH → LOW gap → next pulse. */
    if (s_coin_high > 0) {
        switches |= 0x01;
        s_coin_high--;
    } else if (s_coin_low > 0) {
        s_coin_low--;
    } else if (s_coin_pending > 0) {
        s_coin_pending--;
        s_coin_high = COIN_HIGH_FRAMES;
        s_coin_low  = COIN_LOW_FRAMES;
        switches |= 0x01;
        s_coin_high--;
    }

    /* SPACE or S → Start Button (sw=2, Phys[0].b2). Held-state polling. */
    lpt_set_start_button(keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_S]);

    /* DEBUG probe: digit keys 0..7 each set one bit of Physical[c0] +
     * Logical[c0]. Use this to discover which sw_num actually triggers
     * "Start Game". When a digit key works, that bit is the real Start. */
    int probe_bit = -1;
    if      (keys[SDL_SCANCODE_0]) probe_bit = 0;
    else if (keys[SDL_SCANCODE_1]) probe_bit = 1;
    else if (keys[SDL_SCANCODE_2]) probe_bit = 2;
    else if (keys[SDL_SCANCODE_3]) probe_bit = 3;
    else if (keys[SDL_SCANCODE_4]) probe_bit = 4;
    else if (keys[SDL_SCANCODE_5]) probe_bit = 5;
    else if (keys[SDL_SCANCODE_6]) probe_bit = 6;
    else if (keys[SDL_SCANCODE_7]) probe_bit = 7;
    if (probe_bit >= 0) lpt_set_probe_bit(probe_bit, 1);
    else                lpt_set_probe_bit(0, 0);

    lpt_set_host_input(buttons, switches);
}

void display_cleanup(void)
{
    if (g_emu.texture)  { SDL_DestroyTexture(g_emu.texture);   g_emu.texture = NULL; }
    if (g_emu.renderer) { SDL_DestroyRenderer(g_emu.renderer); g_emu.renderer = NULL; }
    if (g_emu.window)   { SDL_DestroyWindow(g_emu.window);     g_emu.window = NULL; }
    SDL_Quit();
    g_emu.display_ready = false;
}
