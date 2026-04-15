/*
 * display.c — SDL2 window, framebuffer rendering, keyboard input.
 *
 * Guest framebuffer: 640×240 RGB555 at GX_BASE+0x800000
 * Row pitch: 2048 bytes (GP blit stride, y << 11)
 * Double each row → 640×480 output
 * Y-flip: GP blit Y=0 is bottom, SDL Y=0 is top
 */
#include "encore.h"

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

    /* Save as uncompressed TGA (type 2) — no libpng dependency, widely viewable */
    char path[256];
    snprintf(path, sizeof(path), SCREENSHOT_DIR "/%s_%03d.tga", prefix, id);
    FILE *f = fopen(path, "wb");
    if (!f) return;

    uint8_t hdr[18];
    memset(hdr, 0, sizeof(hdr));
    hdr[2]  = 2;                           /* uncompressed true-color */
    hdr[12] = SCREEN_W & 0xFF;
    hdr[13] = (SCREEN_W >> 8) & 0xFF;
    hdr[14] = SCREEN_H & 0xFF;
    hdr[15] = (SCREEN_H >> 8) & 0xFF;
    hdr[16] = 32;                          /* 32 bpp */
    hdr[17] = 0x20;                        /* top-left origin */
    fwrite(hdr, 1, 18, f);

    /* Write BGRA pixels (TGA native order) */
    for (int y = 0; y < SCREEN_H; y++) {
        for (int x = 0; x < SCREEN_W; x++) {
            uint32_t argb = g_emu.fb_pixels[y * SCREEN_W + x];
            uint8_t px[4] = {
                (uint8_t)(argb),               /* B */
                (uint8_t)(argb >> 8),          /* G */
                (uint8_t)(argb >> 16),         /* R */
                (uint8_t)(argb >> 24)          /* A */
            };
            fwrite(px, 1, 4, f);
        }
    }
    fclose(f);
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

    g_emu.renderer = SDL_CreateRenderer(g_emu.window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_emu.renderer) {
        /* Fallback to software renderer */
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
    if (!g_emu.display_ready || !g_emu.uc) return;

    /* Read framebuffer from guest physical RAM at 0x800000 + dc_fb_offset.
     * Game decompresses to phys RAM; GP BLTs also use 0x800000. */
    uint32_t fb_base = 0x00800000u + g_emu.dc_fb_offset;
    uint8_t fb_row[FB_STRIDE];
    bool any_nonzero = false;

    /* Render 240 source rows → 480 output rows (double each row, Y-flip) */
    for (int src_y = 0; src_y < FB_H; src_y++) {
        int draw_y = s_y_flip ? (FB_H - 1 - src_y) : src_y;
        int dst_y = draw_y * 2;

        /* Read one row from guest framebuffer */
        uc_err err = uc_mem_read(g_emu.uc, fb_base + src_y * FB_STRIDE, fb_row, FB_STRIDE);
        if (err != UC_ERR_OK) continue;

        uint16_t *pixels = (uint16_t *)fb_row;
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
        LOG("disp", "first non-zero framebuffer detected (fb_off=0x%x)\n", g_emu.dc_fb_offset);
        save_screenshot("encore_first", 0);
    }

    /* Upload to GPU and present */
    SDL_UpdateTexture(g_emu.texture, NULL, g_emu.fb_pixels, SCREEN_W * 4);
    SDL_RenderClear(g_emu.renderer);
    SDL_RenderCopy(g_emu.renderer, g_emu.texture, NULL, NULL);
    SDL_RenderPresent(g_emu.renderer);

    g_emu.frame_count++;
    if (s_seen_nonzero_fb &&
        (g_emu.frame_count == 1000 || g_emu.frame_count == 3000 ||
         g_emu.frame_count == 5000 || g_emu.frame_count == 10000)) {
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
