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

The default 32-bit surface converts guest RGB555 to ARGB8888. `--bpp 16` uses a
native x1r5g5b5 surface. SDL is the normal desktop backend; GTK is available
only when the QEMU build was configured with it. `--display none` disables the
window and desktop input.

The source framebuffer is bottom-up, so vertical flip starts enabled. Press
`F2` to toggle it. `F3` writes a screenshot under `--screenshot-dir`.

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
