/*
 * p2k-display.c — Cyrix MediaGX display output (640×240 RGB555 → window).
 *
 * Reads the guest framebuffer directly from system RAM at physical 0x800000,
 * the same backing store that the GX FB window (0x40800000) aliases (see
 * p2k-gx.c).  Per unicorn.old/src/display.c:296-360 the rendering rules are:
 *
 *   - Source is 640×240 RGB555 (16 bpp), pitch 1280 during PRISM/boot and
 *     2048 during game runtime.  We latch into 2048-pitch the first time
 *     DC_FB_ST_OFFSET lands on a non-zero multiple of 0x78000 (the size of
 *     a single 240×2048 buffer), matching unicorn.old/src/display.c:316-318.
 *   - DC_FB_ST_OFFSET (GX_BASE + 0x8310) is the start offset within the FB.
 *   - Output is 640×480 ARGB8888, line-doubled and Y-flipped (boot validator
 *     and game both render upside-down relative to QEMU's top-left origin).
 *
 * No GP BLT / DC vsync semantics here — those still belong in their own
 * future files.  This is the smallest thing that turns "QEMU runs the game"
 * into "you can see the game".
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "hw/boards.h"
#include "ui/console.h"
#include "ui/surface.h"

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
    bool           game_pitch;       /* false: stride 1280 (boot), true: 2048 */
    uint32_t       last_fb_off;
} P2KDisplayState;

static P2KDisplayState s_disp;

/* Read a dword from system memory by physical address — used for both
 * the DC register at GX_BASE+0x8310 and the framebuffer pixels.  The
 * GX register space and FB are both ordinary RAM-backed regions, so
 * address_space_ldl_le is direct (no MMIO callbacks). */
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

static void p2k_display_invalidate(void *opaque)
{
    /* Force-refresh on next update — nothing to clear here, our pixel
     * buffer is regenerated from guest RAM every gfx_update call. */
}

static void p2k_display_update(void *opaque)
{
    P2KDisplayState *s = opaque;
    DisplaySurface  *surf = qemu_console_surface(s->con);
    void            *dst_raw;
    uint32_t         fb_off;
    int              src_pitch;
    uint16_t         row_buf[FB_W];
    bool             bpp16;

    if (!surf) {
        return;
    }
    dst_raw = surface_data(surf);
    bpp16   = (surface_format(surf) == PIXMAN_x1r5g5b5);

    /* Latest DC_FB_ST_OFFSET — the game writes it to switch buffers. */
    fb_off = p2k_phys_ldl(GX_BASE + GX_DC_FB_ST_OFFSET);

    /* Latch into 2048-byte stride the first time a non-zero offset lands
     * on a 0x78000 boundary.  Matches unicorn.old/src/display.c:316-318. */
    if (!s->game_pitch && fb_off != 0 && (fb_off % GAME_BUF_SIZE) == 0) {
        s->game_pitch = true;
    }
    src_pitch = s->game_pitch ? 2048 : 1280;
    s->last_fb_off = fb_off;

    /* Bounds-check: if fb_off looks bogus, fall back to 0. */
    if (fb_off > 0x300000u) {
        fb_off = 0;
    }

    for (int src_y = 0; src_y < FB_H; src_y++) {
        int dst_y = (FB_H - 1 - src_y) * 2;   /* Y-flip + line-double */
        hwaddr row_pa = GX_FB_RAM_MIRROR + fb_off + src_y * src_pitch;

        p2k_phys_read(row_pa, row_buf, FB_W * sizeof(uint16_t));

        if (bpp16) {
            uint16_t *r1 = &((uint16_t *)dst_raw)[dst_y       * SCREEN_W];
            uint16_t *r2 = &((uint16_t *)dst_raw)[(dst_y + 1) * SCREEN_W];
            for (int x = 0; x < FB_W; x++) {
                uint16_t px = row_buf[x] & 0x7FFFu;
                r1[x] = px;
                r2[x] = px;
            }
        } else {
            uint32_t *dst = (uint32_t *)dst_raw;
            uint32_t *r1 = &dst[dst_y       * SCREEN_W];
            uint32_t *r2 = &dst[(dst_y + 1) * SCREEN_W];
            for (int x = 0; x < FB_W; x++) {
                uint32_t argb = rgb555_to_argb(row_buf[x] & 0x7FFFu);
                r1[x] = argb;
                r2[x] = argb;
            }
        }
    }

    dpy_gfx_update_full(s->con);
}

static const GraphicHwOps p2k_display_ops = {
    .invalidate = p2k_display_invalidate,
    .gfx_update = p2k_display_update,
};

void p2k_install_display(void)
{
    DisplaySurface *surf;
    const char     *bpp_env = getenv("P2K_DISPLAY_BPP");
    bool            bpp16   = bpp_env && !strcmp(bpp_env, "16");

    s_disp.con = graphic_console_init(NULL, 0, &p2k_display_ops, &s_disp);
    qemu_console_resize(s_disp.con, SCREEN_W, SCREEN_H);

    /* Replace the placeholder surface with one we own so the gfx_update
     * pixel writes have a stable backing buffer. */
    if (bpp16) {
        size_t   stride = SCREEN_W * sizeof(uint16_t);
        uint8_t *buf    = g_malloc0(stride * SCREEN_H);
        surf = qemu_create_displaysurface_from(SCREEN_W, SCREEN_H,
                                               PIXMAN_x1r5g5b5,
                                               stride, buf);
        info_report("pinball2000: display surface = 16 bpp (x1r5g5b5, "
                    "native source format, no ARGB conversion)");
    } else {
        surf = qemu_create_displaysurface(SCREEN_W, SCREEN_H);
    }
    dpy_gfx_replace_surface(s_disp.con, surf);
}
