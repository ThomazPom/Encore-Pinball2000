# 23 — MediaGX display

Pinball 2000 renders into a MediaGX RGB555 framebuffer. Encore exposes that
framebuffer through a QEMU display surface and implements the MediaGX operations
the Williams display driver actually uses.

## Current path

| Stage | Source |
|---|---|
| MediaGX register windows and framebuffer alias | `qemu/p2k-gx.c` |
| Graphics blitter | `qemu/p2k-gp-blt.c` |
| Display-list/redraw observation | `qemu/p2k-gfxlist-watch.c` |
| QEMU console surface and pixel conversion | `qemu/p2k-display.c` |
| Frame/VSync state | `qemu/p2k-vsync.c` |
| MediaGX CPU instructions and CCR enable state | `qemu/p2k-mediagx-gate.c`, `qemu/p2k-cyrix-ccr.c` |

The framebuffer is guest RAM aliased at `0x40800000`. Display registers are in
the `0x40000000` MediaGX windows. The blitter's semantic MMIO overlay begins at
`0x40008100`.

## Host display

The default desktop path is the direct framebuffer renderer described below.
The QEMU console's default 32-bit surface converts guest RGB555 to ARGB8888;
`--bpp 16` uses a native x1r5g5b5 surface. Select QEMU's SDL path explicitly
with `--display sdl`; GTK is available only when the QEMU build was configured
with it. `--display none` disables the window and desktop input.

`--framebuffer` selects the default independent desktop display path. The
wrapper gives QEMU display `none`; a dedicated SDL thread reads the RAM-backed controller
offset and native 640×240 RGB555 framebuffer through direct pointers. SDL
uploads it with the guest pitch, flips and scales it to 640×480, presents the
window and forwards its keyboard events to the cabinet-input handler. It does
not create a QEMU graphics console or call QEMU display APIs. An explicit
`--display <backend>` uses QEMU's synchronous console path instead.

`--qemu-framebuffer` keeps QEMU's console, window and input handling, but reads
the framebuffer through its RAM pointer and uses a lookup table for RGB555 to
ARGB conversion. `--qemu-framebuffer-async` additionally submits that surface
from a worker. The async option requires QEMU's SDL backend; the launcher uses
accelerated X11 by default. Native Wayland transfers the OpenGL renderer context
from QEMU's refresh thread to the worker on the first frame; SDL software is the
compatibility fallback.

> [!NOTE]
> Host conversion and QEMU presentation are separate costs. Use `--bench` on
> the target computer to compare the two paths.

The source framebuffer is bottom-up, so vertical flip starts enabled. Press
`F2` to toggle it. `F3` writes a screenshot under `--screenshot-dir`; the
direct SDL path writes BMP while the QEMU console path prefers JPEG and falls
back to PPM.

## H.264 capture

`--record-video <file.mp4>` records the complete run from machine startup until
clean shutdown. Capture reads the same native RGB555 source through
`p2k-display.c`, independently of the selected display backend, then streams
packed frames through an anonymous pipe to FFmpeg. Only the final H.264 MP4 is
written; there is no raw intermediate video.

```sh
scripts/run-qemu.sh --record-video ./gameplay.mp4
```

The encoder uses 640×480 at 60 fps, `libx264`, CRF 20, the `veryfast` preset
and two encoder threads. The result is video-only. The wrapper requires an
installed FFmpeg with `libx264`, validates the `.mp4` destination and refuses
to overwrite an existing file. The capture follows the current vertical-flip
state but excludes host-only window scaling and overlays.

## VSync

`p2k-vsync.c` advances MediaGX timing state and sets the guest VSync latch in
BAR2. The guest observes this state while QEMU decides when the host surface is
updated. Display refresh is not the guest game clock; use `--bench` to measure
XINU time.

## MediaGX instructions

Encore's QEMU patch implements the gated MediaGX `BB1_RESET`, `CPU_WRITE` and
`CPU_READ` operations required by the display driver. The gate is enabled only
for the `pinball2000` machine so other QEMU i386 machines retain normal x86/SSE
decoding.

Details: [CPU and timing](12-cpu-and-timers.md) and
[memory map](13-memory-map.md).

---

← [Documentation index](README.md) · [Project README](../README.md)
