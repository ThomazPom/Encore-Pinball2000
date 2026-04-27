/*
 * splash.c — startup splash screen.
 *
 * Shows a full-window image as soon as the SDL window is created, then
 * gets dismissed automatically the first time the guest framebuffer
 * contains non-zero pixels (i.e. when the PRISM update validator's
 * white "PLEASE WAIT…" screen is about to be rendered).
 *
 * Image source priority:
 *   1. --splash-screen none       → splash disabled, window starts black.
 *   2. --splash-screen <path>     → load any JPEG/PNG/BMP/TGA from disk.
 *   3. (default)                  → use the JPEG embedded into the binary
 *                                   from assets/splash-screen.jpg.
 *
 * Builders who want a custom default splash just drop a JPEG over
 * assets/splash-screen.jpg before `make` — the whole image is linked
 * verbatim into the binary by `ld -r -b binary` (see Makefile).
 */
#include "encore.h"
#include "stb_image.h"

/* Symbols emitted by `ld -r -b binary assets/splash-screen.jpg`. */
extern const unsigned char _binary_assets_splash_screen_jpg_start[];
extern const unsigned char _binary_assets_splash_screen_jpg_end[];

static SDL_Texture *s_splash_tex = NULL;
static int          s_splash_w   = 0;
static int          s_splash_h   = 0;
static bool         s_dismissed  = false;

/* Decode `bytes`/`len` (any format stb_image supports) into an ARGB8888
 * SDL_Texture sized to the image's native resolution. Returns NULL on
 * any failure — caller treats that as "no splash". */
static SDL_Texture *splash_decode(SDL_Renderer *r,
                                  const unsigned char *bytes, int len,
                                  int *out_w, int *out_h)
{
    int w = 0, h = 0, n = 0;
    /* Force 4 channels (RGBA) so the upload format is fixed regardless
     * of the source image's channel count. */
    unsigned char *px = stbi_load_from_memory(bytes, len, &w, &h, &n, 4);
    if (!px) {
        LOG("splash", "stbi decode failed: %s\n", stbi_failure_reason());
        return NULL;
    }

    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888,
                                       SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) {
        LOG("splash", "SDL_CreateTexture failed: %s\n", SDL_GetError());
        stbi_image_free(px);
        return NULL;
    }
    SDL_UpdateTexture(t, NULL, px, w * 4);
    stbi_image_free(px);
    *out_w = w; *out_h = h;
    return t;
}

void splash_show(void)
{
    if (g_emu.headless || !g_emu.renderer)
        return;
    if (g_emu.splash_disabled) {
        LOG("splash", "disabled via --splash-screen none\n");
        return;
    }

    SDL_Texture *t = NULL;
    int w = 0, h = 0;

    /* External path takes priority over the embedded default. */
    if (g_emu.splash_path[0]) {
        FILE *f = fopen(g_emu.splash_path, "rb");
        if (!f) {
            LOG("splash", "could not open '%s' (%s) — falling back to embedded\n",
                g_emu.splash_path, strerror(errno));
        } else {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            unsigned char *buf = malloc((size_t)sz);
            if (buf && fread(buf, 1, (size_t)sz, f) == (size_t)sz) {
                t = splash_decode(g_emu.renderer, buf, (int)sz, &w, &h);
                if (t)
                    LOG("splash", "loaded '%s' (%dx%d)\n",
                        g_emu.splash_path, w, h);
            }
            free(buf);
            fclose(f);
        }
    }

    if (!t) {
        int len = (int)(_binary_assets_splash_screen_jpg_end -
                        _binary_assets_splash_screen_jpg_start);
        if (len > 0) {
            t = splash_decode(g_emu.renderer,
                              _binary_assets_splash_screen_jpg_start, len, &w, &h);
            if (t)
                LOG("splash", "embedded splash decoded (%dx%d, %d bytes)\n",
                    w, h, len);
        }
    }

    if (!t) return;

    s_splash_tex = t;
    s_splash_w   = w;
    s_splash_h   = h;
    s_dismissed  = false;

    splash_present();
}

/* Render the splash texture letterboxed into the current window so the
 * native aspect ratio is preserved no matter what size the user picked
 * (or how the window has been resized). Safe to call every frame. */
void splash_present(void)
{
    if (!s_splash_tex || !g_emu.renderer) return;

    int win_w = SCREEN_W, win_h = SCREEN_H;
    if (g_emu.window) SDL_GetWindowSize(g_emu.window, &win_w, &win_h);

    float src_aspect = (float)s_splash_w / (float)s_splash_h;
    float win_aspect = (float)win_w / (float)win_h;
    SDL_Rect dst;
    if (src_aspect > win_aspect) {
        dst.w = win_w;
        dst.h = (int)(win_w / src_aspect);
    } else {
        dst.h = win_h;
        dst.w = (int)(win_h * src_aspect);
    }
    dst.x = (win_w - dst.w) / 2;
    dst.y = (win_h - dst.h) / 2;

    SDL_SetRenderDrawColor(g_emu.renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_emu.renderer);
    SDL_RenderCopy(g_emu.renderer, s_splash_tex, NULL, &dst);
    SDL_RenderPresent(g_emu.renderer);
}

bool splash_active(void)
{
    return s_splash_tex && !s_dismissed;
}

void splash_dismiss(void)
{
    if (!s_splash_tex) return;
    LOG("splash", "dismissed (guest framebuffer is now driving the window)\n");
    SDL_DestroyTexture(s_splash_tex);
    s_splash_tex = NULL;
    s_dismissed  = true;
}
