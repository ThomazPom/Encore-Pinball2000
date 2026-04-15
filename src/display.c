/*
 * display.c — SDL2 window, framebuffer rendering, keyboard input.
 *
 * Guest framebuffer: 640×240 RGB555 at GX_BASE+0x800000
 * Row pitch: 2048 bytes (GP blit stride, y << 11)
 * Double each row → 640×480 output
 * Y-flip: GP blit Y=0 is bottom, SDL Y=0 is top
 */
#include "encore.h"

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

    /* Read framebuffer from guest memory at GX_BASE + 0x800000 + dc_fb_offset */
    uint32_t fb_base = GX_FB + g_emu.dc_fb_offset;
    uint8_t fb_row[FB_STRIDE];

    /* Render 240 source rows → 480 output rows (double each row, Y-flip) */
    for (int src_y = 0; src_y < FB_H; src_y++) {
        /* Y-flip: source row 0 (bottom) → output rows 478-479 (near bottom in SDL) */
        int dst_y = (FB_H - 1 - src_y) * 2;

        /* Read one row from guest framebuffer */
        uc_err err = uc_mem_read(g_emu.uc, fb_base + src_y * FB_STRIDE, fb_row, FB_STRIDE);
        if (err != UC_ERR_OK) continue;

        uint16_t *pixels = (uint16_t *)fb_row;
        uint32_t *dst1 = &g_emu.fb_pixels[dst_y * SCREEN_W];
        uint32_t *dst2 = &g_emu.fb_pixels[(dst_y + 1) * SCREEN_W];

        for (int x = 0; x < FB_W; x++) {
            uint32_t argb = g_emu.rgb555_lut[pixels[x] & 0x7FFF];
            dst1[x] = argb;
            dst2[x] = argb;
        }
    }

    /* Upload to GPU and present */
    SDL_UpdateTexture(g_emu.texture, NULL, g_emu.fb_pixels, SCREEN_W * 4);
    SDL_RenderClear(g_emu.renderer);
    SDL_RenderCopy(g_emu.renderer, g_emu.texture, NULL, NULL);
    SDL_RenderPresent(g_emu.renderer);

    g_emu.frame_count++;
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
            case SDLK_F9:
                /* Coin insert — set LPT bit */
                g_emu.lpt_data |= 0x01;
                LOG("input", "Coin inserted\n");
                break;
            case SDLK_F10:
                /* Start button */
                g_emu.lpt_data |= 0x02;
                LOG("input", "Start pressed\n");
                break;
            case SDLK_1:
                g_emu.lpt_data |= 0x04;
                break;
            case SDLK_2:
                g_emu.lpt_data |= 0x08;
                break;
            case SDLK_SPACE:
                /* Flipper */
                g_emu.lpt_data |= 0x10;
                break;
            }
            break;

        case SDL_KEYUP:
            switch (ev.key.keysym.sym) {
            case SDLK_F9:  g_emu.lpt_data &= ~0x01; break;
            case SDLK_F10: g_emu.lpt_data &= ~0x02; break;
            case SDLK_1:   g_emu.lpt_data &= ~0x04; break;
            case SDLK_2:   g_emu.lpt_data &= ~0x08; break;
            case SDLK_SPACE: g_emu.lpt_data &= ~0x10; break;
            }
            break;
        }
    }
}

void display_cleanup(void)
{
    if (g_emu.texture)  { SDL_DestroyTexture(g_emu.texture);   g_emu.texture = NULL; }
    if (g_emu.renderer) { SDL_DestroyRenderer(g_emu.renderer); g_emu.renderer = NULL; }
    if (g_emu.window)   { SDL_DestroyWindow(g_emu.window);     g_emu.window = NULL; }
    SDL_Quit();
    g_emu.display_ready = false;
}
