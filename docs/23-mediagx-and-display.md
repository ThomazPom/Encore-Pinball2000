# 23 — MediaGX and display

Encore models the parts of the Cyrix MediaGX graphics path that the Pinball
2000 software actually exercises. The guest still discovers the GX base,
executes MediaGX-only CPU instructions, writes graphics registers and draws
into its own framebuffer. Host presentation begins only after those guest
operations have produced pixels.

```text
Cyrix CCR/GCR        MediaGX TCG opcodes       GX MMIO / GP engine
I/O 22h/23h    →     internal registers   →    semantic row blits
                                                    │
                                                    ▼
guest RAM 00800000h ← alias 40800000h      RGB555 framebuffer
                                                    │
                    ┌───────────────────────────────┼───────────────┐
                    ▼                               ▼               ▼
              direct SDL                    QEMU console       video worker
              desktop default               normal / fast      FFmpeg pipe
```

This page separates three concerns that are easy to conflate:

- **device emulation** makes the guest's MediaGX programming work;
- **frame extraction** selects and interprets the guest RGB555 buffer;
- **presentation or capture** sends that frame to a window, screenshot or
  video encoder.

The surrounding addresses are listed in the
[memory map](13-memory-map.md). Keyboard controls, including F2 and F3, are
covered by the [desktop controls guide](41-cli-keyboard-guide.md).

### Implementation owners

| Concern | Primary source |
|---|---|
| indexed Cyrix configuration | `qemu/p2k-cyrix-ccr.c` |
| MediaGX instruction state/gate | `qemu/p2k-mediagx-gate.c` and `qemu/upstream-patches/mediagx-instructions/` |
| GX regions and framebuffer alias | `qemu/p2k-gx.c` |
| semantic GP blitter | `qemu/p2k-gp-blt.c` |
| frame extraction and presentation | `qemu/p2k-display.c` |
| guest VSYNC state | `qemu/p2k-vsync.c` |
| FFmpeg producer/process lifecycle | `qemu/p2k-video-capture.c` |
| revision-specific graphics watch | `qemu/p2k-gfxlist-watch.c` |
| public selection and validation | `scripts/run-qemu.sh` |

## MediaGX configuration and CPU instructions

The indexed Cyrix configuration interface lives at I/O ports `0x22/0x23`.
Register `GCR` at index `0xb8` resets to `0x0d`:

- bits 1:0 select the 1 GiB-aligned GX base `0x40000000`;
- bits 3:2 select a non-zero 4 KiB scratchpad, enabling the MediaGX display
  instructions;
- the byte remains writable, so clearing the scratchpad-size field makes the
  implemented instructions raise `#UD`, as the processor contract requires.

`CCR3` and the other indexed bytes also preserve guest writes. This matters
because the ROM temporarily sets the extended-register mapping bit and later
restores it.

Encore's upstream QEMU patch adds narrow TCG decoder entries. They are gated
twice: the `pinball2000` machine must enable the MediaGX extensions, and the
current `GCR` scratchpad field must be non-zero. Other QEMU machines retain
their normal x86 decoding.

| Opcode | MediaGX name | Implemented behavior |
|---:|---|---|
| `0f 3b` | `BB1_RESET` | copy internal `L1_BB1_BASE` to `L1_BB1_POINTER` |
| `0f 3c` | `CPU_WRITE` | write `EAX` to the internal register selected by `EBX`; also store `(EAX, EBX)` at `[DS:EDX]` and advance `EDX` by 8 |
| `0f 3d` | `CPU_READ` | return the internal register selected by `EBX` in `EAX` |
| `0f 36/37/39/3f` | unimplemented/observational | rate-limited diagnostic log, then `#UD` |
| `0f 3a` | `BB0_RESET` on MediaGX | deliberately not claimed; this byte is the later x86 SSE4 escape and the guest has not been observed to require it |

The internal-register store has explicit slots for both blit-buffer bases and
pointers plus `PM_BASE` and `PM_MASK`. A bounded overflow table records newly
observed register addresses instead of silently discarding them.

> [!NOTE]
> The `CPU_WRITE` scratchpad stores and `EDX += 8` are an observed Pinball
> 2000 silicon/ROM contract in addition to the documented internal-register
> write. Omitting that side effect makes the ROM overwrite the packed buffer
> it is constructing.

## GX address space

The guest-visible GX block begins at `0x40000000`:

| Range | Backing and behavior |
|---:|---|
| `0x40000000–0x407fffff` | 8 MiB RAM-like register backing; the BIU block starts at `+0x8000` and `BC_DRAM_TOP` starts as `0x007fffff` |
| `0x40008100–0x4000820f` | priority-1 semantic Graphics Pipeline overlay |
| `0x40800000–0x40bfffff` | 4 MiB alias of system RAM `0x00800000–0x00bfffff` |
| `0x40c00000–0x40ffffff` | 4 MiB upper RAM-like register backing |

The alias is important: the guest, the blitter, the window renderer and the
video recorder all ultimately observe the same bytes in low RAM. There is no
second framebuffer that must be synchronized.

Most GX registers intentionally behave as writable storage. Two small pieces
have active semantics:

- the Graphics Pipeline overlay executes the blits described below;
- the VSYNC ticker updates `DC_TIMING2` in the first register backing.

## Semantic Graphics Pipeline blits

The current GP model implements the one-row screen-to-screen operation used
by the game. Register offsets below are relative to `0x40008100`:

| Offset | Access | Meaning |
|---:|---|---|
| `0x000` | write | packed destination: x in bits 15:0, y in bits 31:16 |
| `0x004` | write | width in RGB555 pixels |
| `0x008` | write | packed source coordinates |
| `0x100` | write | raster mode; bit 12 enables transparent copy |
| `0x108` | write | trigger one row copy |
| `0x10c` | read | returns idle status `0x300` |

Rows use a fixed 2,048-byte stride in the 4 MiB framebuffer. A normal trigger
copies `width × 2` bytes; transparent mode leaves destination pixels unchanged
where the source equals key `0x7c1f`. Zero, oversized and out-of-bounds copies
are rejected. Other GP slots retain their last written value so guest
read/modify/write sequences still work.

This is functional device emulation, not a display-side reconstruction. If
the GP trigger is ignored, the guest believes it drew successfully while the
framebuffer remains empty.

## VSYNC contract

A `QEMU_CLOCK_VIRTUAL` timer represents the two timing values the game polls:

- `DC_TIMING2` at `0x40008354` advances by eight scanline units;
- BAR2 SRAM dword `0x11000004` is set to 1 at end of frame.

The timer divides a 17.5 ms frame into 30 subticks, giving an approximate
57 Hz VSYNC. When the counter reaches 241, both end-of-frame values are
written and the internal scanline counter returns to zero. This timer is
separate from the i8254/IRQ0 path documented in
[CPU, PIT and IRQ0 timing](12-cpu-and-timers.md).

> [!IMPORTANT]
> VSYNC is driven by QEMU virtual time. The host window refresh loop and the
> video encoder's 60 fps sampling loop are presentation schedules; neither is
> the guest's display-timing source.

## Framebuffer interpretation

The native guest image is 640×240, 16-bit RGB555 and stored bottom-up. Encore
presents it as 640×480:

- QEMU-console paths duplicate every source row;
- direct SDL uploads 640×240 RGB555 and lets SDL scale it to the window;
- video capture uses nearest-neighbor scaling in FFmpeg.

The source begins at low RAM `0x00800000` plus `DC_FB_ST_OFFSET`
(`GX + 0x8310`). Offsets above `0x300000` are treated as zero to keep the
selected 640×240 image inside the 4 MiB alias.

The boot/PRISM layout uses a 1,280-byte pitch. The first non-zero framebuffer
offset that is an exact multiple of `0x78000` (`240 × 2048`) permanently
latches the runtime 2,048-byte pitch for that machine run. All presentation
and capture paths share this source-selection function.

Normal orientation reverses the bottom-up guest rows for display.
`--flipscreen` or F2 toggles a second vertical reversal relative to that
normal result; it does not mean that the unmodified guest buffer is top-down.

## Presentation paths

| Path | How to select it | Frame access | Window/input owner | Intended status |
|---|---|---|---|---|
| direct SDL | desktop default, or `--framebuffer` | direct pointers to GX registers and RAM; native RGB555 texture | dedicated SDL worker | normal desktop path |
| standard QEMU console | explicit `--display sdl` (or another compiled backend) | QEMU address-space reads plus RGB conversion | QEMU display backend | supported comparison/compatibility path |
| fast QEMU console | `--qemu-framebuffer` | direct RAM reads and RGB555 lookup into QEMU's ARGB surface | QEMU display backend | experimental |
| async fast QEMU console | `--qemu-framebuffer-async` | fast path plus worker submission of the QEMU surface | QEMU SDL backend | experimental A/B path |

The launcher chooses direct SDL only when a graphical desktop is available
and the caller did not explicitly choose a display backend, headless mode or a
QEMU-framebuffer experiment. Direct SDL makes QEMU itself use `-display none`;
that is an implementation detail and does not make the run headless.

`--headless` and `--display none` create no visible window. `--framebuffer`
cannot be combined with `--headless` or `--qemu-framebuffer`, and the async
variant requires QEMU's SDL backend. Its driver selector supports `auto`,
`wayland`, `x11` and `software` for controlled comparisons.

`--bpp 16` changes the standard QEMU console to native `x1r5g5b5`; its normal
32-bit surface converts RGB555 to ARGB8888. The direct renderer is already
native RGB555, so this option does not optimize it. The fast QEMU path normally
keeps ARGB; an internal `P2K_QEMU_FB_FORMAT=565` experiment enables its RGB565
surface and lookup.

Host pixel preparation and backend submission are distinct costs. When a
presentation change affects timing, compare both display FPS and the guest
timing verdict on the target host rather than attributing the total to RGB
conversion alone.

The direct renderer owns F11 and Alt+Enter fullscreen. QEMU's SDL backend owns
its usual Ctrl+Alt+F fullscreen control. Both paths display the same temporary
audio/input status banner on top of the guest frame.

## Screenshots and video

F3 requests a screenshot of the currently presented 640×480 frame:

- direct SDL writes `p2k_screen_<timestamp>.bmp`;
- QEMU-console paths prefer JPEG through `cjpeg`, `magick` or `convert`, and
  fall back to binary PPM when no JPEG helper is available;
- `--screenshot-dir DIR` selects the existing output directory.

The QEMU screenshot writer understands both the default 32-bit surface and
the public 16-bit `x1r5g5b5` surface. The direct path reads the fully composed
back buffer before presenting it, so the saved BMP includes the guest frame
and status overlay rather than an undefined post-present buffer.

`--record-video PATH` starts a separate producer and FFmpeg process. It:

- refuses an existing target or a missing parent directory;
- extracts native 640×240 RGB555 at a host-monotonic 60 fps;
- sends raw frames through a pipe, writes no intermediate raw file;
- scales to 640×480 with nearest-neighbor filtering and records no audio;
- follows the current F2/`--flipscreen` state, but excludes the host status
  overlay and any window-size/compositor scaling;
- lets FFmpeg choose codec/container from the filename, adding `faststart` to
  MP4/MOV-family containers;
- closes the pipe and waits up to five seconds for encoder finalization during
  normal shutdown, then terminates a stuck encoder.

Because recording samples independently of VSYNC, its 60 fps stream may
repeat a guest frame. Use a clean emulator exit when the final container
matters; forcibly terminating the process can leave FFmpeg unable to finalize
it cleanly.

## Maintainer diagnostics

The following environment variables are investigation tools, not alternate
user-facing renderers:

| Variable | Scope |
|---|---|
| `P2K_DISPLAY_PROFILE=1` | report frame preparation/submission averages and maxima for QEMU-console paths on exit |
| `P2K_GFXLIST_WATCH=1` | sample hard-coded SWE1 2.10 graphics globals every 100 ms and log transitions |
| `P2K_QEMU_FB_FORMAT=565` | force the fast QEMU console's internal RGB565 experiment |

`P2K_GFXLIST_WATCH` is revision-specific forensic instrumentation. Its guest
addresses must not be treated as a general Pinball 2000 ABI. The separate
`P2K_MEM_DETECT_PATCH` experiment changes XINU memory sizing and is not a
display feature even though graphics investigation originally exposed it.

## Verification checklist

For a display change, validate behavior rather than only successful startup:

1. run the desktop default and visually inspect orientation, colors and a
   moving attract/game frame;
2. trigger F3 and inspect the saved image, not merely its dimensions;
3. repeat with explicit QEMU SDL at 32 bpp and `--bpp 16`;
4. exercise `--qemu-framebuffer` separately from its async variant;
5. verify a clean `--record-video` shutdown with `ffprobe` (dimensions,
   frame rate, frame count and decodability);
6. confirm real GP trigger activity and continuing VSYNC/guest progress;
7. compare display FPS and timing diagnostics when changing a worker or
   submission boundary.

A minimal read-only desktop run is:

```bash
scripts/run-qemu.sh --game swe1 --update 2.10 --audio none --no-savedata
```

For launch failures and backend-specific recovery, see
[Troubleshooting](04-troubleshooting.md). For automated console screenshots
and input sequences, see [Console scripting](42-console-scripting.md).

---

← [Memory and I/O map](13-memory-map.md) ·
[Documentation index](README.md) · [DCS sound](25-dcs-sound.md)
