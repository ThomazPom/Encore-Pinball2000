# 49 — Splash screen

Encore shows a startup splash image in the SDL window from the moment
the window is created, then hands the window over to the guest the
first time the PRISM update validator (or any later code) draws a
non-zero pixel into the framebuffer.

This page documents what the splash is, how it is shipped, and how to
swap it for your own.

## Why a splash at all?

Without one, the SDL window appears black for the full second or so
between `SDL_CreateWindow` and the first frame the guest renders.
Users who enabled `--no-savedata` or hit a slow PCI enumeration could
easily mistake the black window for a hang.

The splash also gives a clean visual identity to the build — and lets
forks ship their own branding without touching any code.

## Source priority

In order, Encore picks the first source that succeeds:

1. **`--splash-screen none`** → splash disabled; window starts black.
2. **`--splash-screen PATH`** → load `PATH` from disk. Supports any
   format `stb_image` understands: JPEG, PNG, BMP, TGA, PSD, PIC, PNM.
3. **Default** → use the JPEG embedded in the binary from
   `assets/splash-screen.jpg`.

If a `PATH` exists but can't be opened or decoded, Encore logs a
warning and falls back to step 3 — a typo never leaves the user
staring at a black window.

## Embedding mechanism

`assets/splash-screen.jpg` is linked verbatim into the binary by
`ld -r -b binary` (see the `$(SPLASH_OBJ)` rule in the Makefile),
which exports three symbols:

```
_binary_assets_splash_screen_jpg_start
_binary_assets_splash_screen_jpg_end
_binary_assets_splash_screen_jpg_size
```

`src/splash.c` declares the `_start` / `_end` symbols as `extern` and
hands the byte range to `stbi_load_from_memory`. No external file is
required at runtime in the default case — the binary is fully
self-contained, which matters for the `make install` / autologin /
kiosk paths documented in
[37-distribution.md](37-distribution.md).

## Customising the default

Builders only need to overwrite the file and rebuild:

```sh
cp my-cool-splash.jpg assets/splash-screen.jpg
make
```

The Makefile has `$(SPLASH_OBJ): $(SPLASH_SRC)` so a single touched
JPEG triggers a re-link, no `make clean` necessary.

Notes:
* Any JPEG works — Encore decodes at runtime, so resolution and quality
  are entirely up to you. The image is **letterboxed** to the SDL
  window's aspect (640×480 by default; whatever the user resized it to
  otherwise), so non-4:3 sources just gain black bars.
* Keep the file under a few hundred KB if you care about distribution
  size — the bytes live inside the executable.
* If you want PNG instead of JPEG, change the filename in `Makefile`
  (`SPLASH_SRC`) and the `extern` symbol names in `src/splash.c` to
  match (e.g. `_binary_assets_splash_screen_png_start`).

## Runtime override

End users who don't rebuild can still personalise:

```sh
./build/encore --splash-screen ~/Pictures/cabinet-logo.png
./build/encore --splash-screen none          # window starts black
```

Both forms are also accepted in the YAML config:

```yaml
splash-screen: ~/Pictures/cabinet-logo.png
# or
splash-screen: none
```

## Lifecycle

| Phase | What you see |
|---|---|
| `display_init` returns | splash texture is created and `SDL_RenderPresent` once |
| CPU thread loop, FB still all zero | every frame re-`splash_present`s (cheap; one `SDL_RenderCopy`) |
| First frame with any non-zero pixel | `splash_dismiss()` destroys the texture; control passes to `display_update`'s normal RGB555 → ARGB8888 blit path |

The dismissal hook lives in `display_update()` and triggers from the
same `s_seen_nonzero_fb` latch that emits the
`first non-zero framebuffer detected` log line — see
[11-display-pipeline.md](11-display-pipeline.md) for the broader
framebuffer state machine (1280-stride boot mode → 2048-stride game
mode latch).

## Files

* `assets/splash-screen.jpg` — the embedded default. Replace before
  `make` to ship a custom one.
* `src/splash.c` — decoding + present + dismiss.
* `src/stb_impl.c` — single-translation-unit `stb_image` implementation.
* `include/stb_image.h` — vendored stb_image (public domain).
* `Makefile` — `$(SPLASH_OBJ)` rule and link line.
