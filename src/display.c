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

static int  s_coin_pulse = 0;
static int  s_start_pulse = 0;
static int  s_y_flip = 1;
static bool s_seen_nonzero_fb = false;
static int  s_snap_id = 0;
static int  s_auto_snap_id = 0;

#define PULSE_FRAMES 30
#define SCREENSHOT_DIR "/mnt/hgfs/encore_forensic/screen_captures"

static void ensure_screenshot_dir(void)
{
    mkdir(SCREENSHOT_DIR, 0777);
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
             SCREENSHOT_DIR "/%s_%04d%02d%02d_%02d%02d%02d_%03ld_%03d.png",
             prefix,
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
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
        LOG("disp", "SDL_Init failed: %s\n", SDL_GetError());
        /* Try without video for headless operation */
        if (!getenv("DISPLAY")) {
            LOG("disp", "No DISPLAY — running headless\n");
            return -1;
        }
        return -1;
    }

    g_emu.window = SDL_CreateWindow(
        "Encore — Pinball 2000",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H,
        SDL_WINDOW_SHOWN
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

    g_emu.texture = SDL_CreateTexture(g_emu.renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        SCREEN_W, SCREEN_H);
    if (!g_emu.texture) {
        LOG("disp", "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return -1;
    }

    /* Build RGB555 → ARGB8888 lookup table (32K entries) */
    for (int i = 0; i < 32768; i++) {
        int r5 = (i >> 10) & 0x1F;
        int g5 = (i >> 5)  & 0x1F;
        int b5 =  i        & 0x1F;
        uint8_t r8 = (r5 << 3) | (r5 >> 2);
        uint8_t g8 = (g5 << 3) | (g5 >> 2);
        uint8_t b8 = (b5 << 3) | (b5 >> 2);
        g_emu.rgb555_lut[i] = 0xFF000000u | (r8 << 16) | (g8 << 8) | b8;
    }

    g_emu.display_ready = true;
    LOG("disp", "SDL2 display: %dx%d ARGB8888\n", SCREEN_W, SCREEN_H);
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

    /* Render 240 source rows → 480 output rows (double each row, Y-flip) */
    for (int src_y = 0; src_y < FB_H; src_y++) {
        int draw_y = s_y_flip ? (FB_H - 1 - src_y) : src_y;
        int dst_y = draw_y * 2;

        uint16_t *pixels = (uint16_t *)(guest_fb + src_y * FB_STRIDE);
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

    if (any_nonzero && !s_seen_nonzero_fb) {
        s_seen_nonzero_fb = true;
        LOG("disp", "first non-zero framebuffer detected (fb_off=0x%x)\n", fb_off);
        save_screenshot("encore_first", 0);
    }

    /* Upload to GPU and present */
    SDL_UpdateTexture(g_emu.texture, NULL, g_emu.fb_pixels, SCREEN_W * 4);
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

    /* Periodic screenshot */
    if (g_emu.frame_count == 1000 || g_emu.frame_count == 3000 || g_emu.frame_count == 8000) {
        save_screenshot("encore_auto", s_auto_snap_id++);
    }
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
            switch (ev.key.keysym.sym) {
            case SDLK_ESCAPE:
            case SDLK_F1:
                LOG("disp", "Key F1/Esc pressed — exiting\n");
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
            case SDLK_F5: {
                static int s_service = 0;
                s_service = !s_service;
                lpt_inject_switch(1, s_service ? 0xFE : 0xFF);
                LOG("input", "F5 — service mode %s\n", s_service ? "ON" : "OFF");
                break;
            }
            case SDLK_F6:
                lpt_toggle_slam_tilt();
                break;
            case SDLK_F7: {
                int v = sound_get_global_volume();
                v = (v + 16 > 255) ? 255 : v + 16;
                sound_set_global_volume(v);
                break;
            }
            case SDLK_F8: {
                int v = sound_get_global_volume();
                v = (v - 16 < 0) ? 0 : v - 16;
                sound_set_global_volume(v);
                break;
            }
            case SDLK_F9:
                s_coin_pulse = PULSE_FRAMES;
                LOG("input", "Coin pulse\n");
                break;
            case SDLK_F10:
                s_start_pulse = PULSE_FRAMES;
                LOG("input", "Start pulse\n");
                break;
            case SDLK_F11:
                lpt_toggle_trace();
                break;
            case SDLK_F12:
                lpt_dump_guest_switch_state();
                break;
            }
            break;
        }
    }

    const uint8_t *keys = SDL_GetKeyboardState(NULL);
    uint8_t buttons = 0;
    uint8_t switches = 0;

    if (keys[SDL_SCANCODE_Z] || keys[SDL_SCANCODE_UP])    buttons |= 0x10;
    if (keys[SDL_SCANCODE_X] || keys[SDL_SCANCODE_DOWN])  buttons |= 0x20;
    if (keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_A]) buttons |= 0x40;
    if (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_A]) buttons |= 0x80;

    if (keys[SDL_SCANCODE_5] || keys[SDL_SCANCODE_KP_5])  switches |= 0x01;
    if (keys[SDL_SCANCODE_1] || keys[SDL_SCANCODE_KP_1])  switches |= 0x02;
    if (keys[SDL_SCANCODE_LEFT])  switches |= 0x04;
    if (keys[SDL_SCANCODE_RIGHT]) switches |= 0x08;

    if (s_coin_pulse > 0) {
        switches |= 0x01;
        s_coin_pulse--;
    }
    if (s_start_pulse > 0) {
        switches |= 0x02;
        s_start_pulse--;
    }

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
