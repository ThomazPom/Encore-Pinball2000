# 11 — Display Pipeline

Encore's video path is a straight line: guest MediaGX framebuffer →
`display_update()` → host ARGB buffer → SDL texture → window.

Source: `src/display.c` (595 lines).

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## The guest side

The game draws into the MediaGX framebuffer region at guest physical
`0x40800000`. The scanout origin is determined by the display
controller register `DC_FB_ST_OFFSET` (offset `0x8310` of
`GX_BASE`), which the game programs during init.

Encore watches `DC_FB_ST_OFFSET` at every write (hooked in `bar.c`)
and caches the latest value in `g_emu.dc_fb_offset`. On every redraw
the host reads 240 rows of 640 pixels starting at
`g_emu.ram + 0x800000 + dc_fb_offset`.

Each guest pixel is 15-bit RGB555 stored in 16 bits, with bit 15 as a
priority/alpha bit. Row stride is a fixed 2048 bytes (`FB_STRIDE`),
wider than the visible 1280 bytes to give the GP blit engine alignment
slack.

## The host side

`display_update()` runs roughly every 16 ms (64 FPS cap via a
wall-clock gate inside `cpu.c`). Its body is:

```c
for (int src_y = 0; src_y < 240; src_y++) {
    int draw_y = s_y_flip ? (239 - src_y) : src_y;
    int dst_y = draw_y * 2;
    uint16_t *pixels = (uint16_t *)(guest_fb + src_y * FB_STRIDE);
    for (int x = 0; x < 640; x++) {
        uint16_t px = pixels[x] & 0x7FFF;
        uint32_t argb = g_emu.rgb555_lut[px];
        dst1[x] = argb;   /* line N   */
        dst2[x] = argb;   /* line N+1 */
    }
}
SDL_UpdateTexture(...); SDL_RenderClear(...); SDL_RenderCopy(...);
SDL_RenderPresent(...);
g_emu.frame_count++;
```

Two things to note:

1. **Row doubling.** The guest runs 640×240 but the displayed output
   is 640×480. Every source row is written to two destination rows.
   This matches the 19" CGA arcade monitor's native resolution (480
   visible lines) without a scaling filter.
2. **LUT-driven colour conversion.** `rgb555_lut[]` is a 32768-entry
   precomputed table filled once at `display_init()`. One array lookup
   per pixel replaces a bit-shift / mask / scale chain.

A parallel `rgb555_lut16[]` exists for `--bpp 16`, emitting RGB565
directly into a 16-bit output buffer (same LUT lookup, different
target format).

## Y-flip

The `--flipscreen` flag initialises `s_y_flip = true`, which the
per-row loop honours. Flip can be toggled live by pressing `F2`. The
actual flip is a constant-cost one-shot — the LUT doesn't change, the
output texture doesn't change, only the `dst_y` computation flips.

## Aspect and windowing

Default window size is 640×480, the native 4:3 aspect. SDL handles
stretching when the window is resized — `SDL_RENDERER_ACCELERATED`
gives us GPU-side bilinear filtering for free.

Fullscreen uses `SDL_WINDOW_FULLSCREEN_DESKTOP` (borderless,
desktop-resolution) rather than `SDL_WINDOW_FULLSCREEN` (real
resolution switch). This is the modern best practice: no resolution
change, no flicker, no lost windows on the other monitor.

## First-frame detection

A latch `s_seen_nonzero_fb` triggers the first time any non-zero
pixel appears in the scan buffer. On the transition we:

* print `[disp] first non-zero framebuffer detected (fb_off=0x…)`
* save a timestamped PNG screenshot prefixed `encore_first` to the
  hardcoded `SCREENSHOT_DIR` path defined in `src/display.c`

This gives a zero-effort visual confirmation of "the guest has entered
attract mode" across any bundle.

## Periodic screenshots

At frame counts 1000, 3000 and 8000, `save_screenshot()` writes a PNG
with a timestamped filename to the hardcoded `SCREENSHOT_DIR` path
defined in `src/display.c`. These three checkpoints
were chosen empirically: 1000 ≈ WMS logo, 3000 ≈ XINU boot, 8000 ≈
attract-mode loop. Useful as a regression artefact for CI.

Screenshots are saved in ARGB8888 regardless of `--bpp`; the 32-bit
shadow buffer `fb_pixels[]` is maintained even in 16-bit mode for
exactly this purpose.

## SDL renderer setup

```c
SDL_CreateWindow("Encore — Pinball 2000", …, 640, 480, flags);
SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
SDL_CreateTexture(rnd,
    bpp == 16 ? SDL_PIXELFORMAT_RGB565 : SDL_PIXELFORMAT_ARGB8888,
    SDL_TEXTUREACCESS_STREAMING, 640, 480);
```

Streaming textures keep the upload cost minimal — `SDL_UpdateTexture`
does a single DMA transfer and the renderer handles presentation.

## Framebuffer byte-layout quirks

* The game uses bit 15 of each pixel as a priority mask (hardware
  sprite overlay). We strip it with `px & 0x7FFF`.
* Some bundles clear only the visible area (1280 bytes/row) and leave
  the stride padding dirty. Since we read strictly `FB_W` pixels per
  row and ignore the rest, this is invisible to Encore.
* On a cold start, before the guest has written anything, the
  framebuffer contains whatever `mmap`'d RAM looked like (zeroed by
  Linux). The first render paints black, which is what a real monitor
  would show during the boot gap.

## Performance

A typical 5-second interval reports:

```
[disp] FPS: 60.0 (300 frames / 5000 ms)
```

Stable 60 FPS on modest hardware (Intel i5 laptop). The per-frame
blit is 150 K pixels × 2 (row double) × one LUT lookup = ~800 k
operations, well under a millisecond. The bulk of display-path cost is
the SDL present itself, which is GPU-gated.

## Key bindings while the window is up

Handled by `display_handle_events()`. See
[36-cli-keyboard-guide.md](36-cli-keyboard-guide.md) for the full
key map.

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
