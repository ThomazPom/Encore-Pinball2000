# 17 — VBLANK Delivery

The Cyrix MediaGX display controller signals VBLANK through two
independent channels:

1. A bit in the **display-status register** that the CPU can poll
   (`DC_TIMING2 = GX_BASE + 0x8354`).
2. A flag in **BAR2 SRAM** (offsets 4..7) that the game's VSYNC ISR
   polls when installed.

Encore drives both at ~57 Hz from the wall clock, independent of the
guest's programmed refresh rate.

## The wall-clock generator

In `src/cpu.c:523-547`:

```c
static uint64_t last_vsync_ns = 0;
if (last_vsync_ns == 0) last_vsync_ns = now_ns;
if (now_ns - last_vsync_ns >= 17500000ULL) { /* 17.5 ms = 57 Hz */
    last_vsync_ns += 17500000ULL;
    g_emu.vsync_count++;
    g_emu.bar2_sram[4] = 1;                    /* VSYNC flag */
    g_emu.bar2_sram[5] = 0;
    g_emu.bar2_sram[6] = 0;
    g_emu.bar2_sram[7] = 0;
    uint32_t one = 1;
    uc_mem_write(uc, WMS_BAR2 + 4, &one, 4);

    g_emu.dc_timing2 = 241;
    uint32_t vbl = 241;
    uc_mem_write(uc, GX_BASE + 0x8354, &vbl, 4);
} else {
    /* Active-scan period: cycle through line numbers 0..240 */
    static uint32_t dc_timing2_counter = 0;
    dc_timing2_counter += 8;
    if (dc_timing2_counter > 240) dc_timing2_counter = 0;
    g_emu.dc_timing2 = dc_timing2_counter;
    uc_mem_write(uc, GX_BASE + 0x8354, &dc_timing2_counter, 4);
}
```

This runs once every 64 execution batches (`exec_count & 0x3F == 0`),
which is often enough to keep the guest's VSYNC poll loop responsive.

## Why 57 Hz and not 60?

The original Pinball 2000 cabinets used a 19" Wells-Gardner CGA
monitor running at 15.75 kHz horizontal / ~60 Hz vertical. The game
code, however, was originally developed against arcade hardware that
ran at ~57 Hz, and the game's frame scheduler assumes periodicity
closer to 17.5 ms than 16.67 ms. Pushing a true-60-Hz VSYNC onto the
guest causes subtle timing glitches in the attract-mode animations.

57 Hz (period 17.5 ms) was chosen empirically after testing 50 / 55 /
57 / 60 / 62 / 67 Hz on SWE1 v1.5. 57 produced the cleanest attract
mode and the least audio stutter.

The emulator's SDL frame rate is a separate concern — that runs at
whatever the host monitor allows, up to 60 FPS, and is gated by the
wall-clock cadence inside `cpu.c:952-969`, not by VBLANK.

## `DC_TIMING2` layout

The real register encodes the current scanline counter in bits 0..10
and the VBLANK active flag in bits 11+. Encore simplifies: we write
0..240 during the active-scan period and a special value 241 to mean
"inside VBLANK". The game's polling loops accept any value ≥ 240 as
VBLANK and any value < 240 as active-scan.

## The BAR2 handshake

Some code paths expect a latched word at BAR2+4. The layout is
effectively a 32-bit "VBLANK latched" flag the game clears after
reading. On real hardware it is set by dedicated sync-detection logic
in the PRISM card. Encore sets the low byte to 1 each VBLANK; the
game reads, processes, and writes 0 back to acknowledge (which we
absorb through the normal BAR2 write path).

## VSYNC ISR installation

At some point during init, the game installs a VSYNC callback at
`0x19BF64` (SWE1 v1.5). Encore's `cpu.c` hooks this address and logs
when it is reached, as a boot-progress marker. The ISR itself runs
natively inside Unicorn.

Callback dispatch is handled by the game's own code reading
`DC_TIMING2`; Encore does not "call" the callback, we only keep the
registers fresh.

## Debug dump

Every few heartbeats in the boot window, Encore dumps the values of
three VSYNC-related state words:

```
[dbg] VSYNC enable=0x1 gx_ptr=0x00800000 dm_mode=1 (exec=…)
```

* `enable`  — `RAM_RD32(0x2E8AF4)` — is the VSYNC ISR armed?
* `gx_ptr`  — `RAM_RD32(0x2E8B74)` — current framebuffer start
  relative to `GX_FB`.
* `dm_mode` — `RAM_RD32(0x2E8E2C)` — display-mode state machine.

These are SWE1 v1.5 offsets; on RFM they point to different addresses.
The dump is for boot forensics only and has no behavioural effect.

## Interaction with the SDL frame loop

The SDL side is independent: it just re-renders whatever the guest
has written into its framebuffer at up to 60 FPS. There is no
VSYNC-to-SDL-present synchronisation. Tearing is not an issue because
the guest updates the framebuffer in page-flip style (two buffers, one
for scanout, one for drawing — the game rotates via
`DC_FB_ST_OFFSET`).

## What we do not emulate

* **Horizontal sync.** The game does not poll HSYNC.
* **Interlace.** No bundle uses interlaced mode.
* **Hardware cursor.** None used by the game.
* **Palette registers.** The game runs in 15-bit true colour
  (`RGB555`), no palette indirection.

## VBLANK count as health signal

`g_emu.vsync_count` is the simplest "is the emulator alive"
counter. At 57 Hz it should advance by ~285 every 5 seconds. If the
heartbeat log shows a stalled `vsync_count`, the emulator's main loop
itself is wedged — not just the guest. This happens only if SDL blocks
indefinitely, which is rare.
