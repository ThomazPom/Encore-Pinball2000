# 24 — FPS and Pacing

Encore drives two distinct timing domains: the **guest PIT** (which
controls XINU's process scheduler) and the **host VBLANK** (which
controls how often the framebuffer is scanned out to the SDL window).
Understanding both is necessary when tuning performance or debugging
timing-sensitive boot behaviour.

## Execution batches

Unicorn does not expose hardware timers; each call to `uc_emu_start`
runs a fixed slice of guest instructions and then returns.

```c
/* src/cpu.c:750 */
size_t batch = 200000;
uc_err err = uc_emu_start(uc, eip, 0, 0, batch);
```

One batch = 200 000 retired instructions (Unicorn counts translated
basic blocks, so the actual x86 instruction count is slightly higher).
After each batch the host loop:

1. Checks the wall clock and fires any pending PIT ticks.
2. Checks the wall clock and fires any pending VBLANK pulses.
3. Drains the DCS command queue.
4. Flushes the TLB (`uc_ctl_flush_tlb`) every 64 batches.
5. Refreshes the SDL texture if a new frame is ready.

`exec_count` is incremented once per batch. Many periodic maintenance
tasks are gated on `exec_count & 0x3F == 0` (every 64 batches ≈ every
~12.8 M instructions) or `exec_count & 0x7F == 0` (every 128 batches).

## Guest PIT and IRQ0

The game programs PIT channel 0 with a divisor:

```
IRQ0 rate = 1 193 182 Hz / divisor
```

The observed divisor at runtime is approximately 298, giving:

```
1 193 182 / 298 ≈ 4003 Hz
```

Encore converts this to nanoseconds per tick (`src/cpu.c:511`):

```c
uint64_t pit_period_ns = (uint64_t)div * 838;
/* 1e9 / 1193182 ≈ 838 ns per PIT tick */
```

The host clock (`clock_gettime(CLOCK_MONOTONIC)`) is polled every 64
batches. When `now_ns - last_tick_ns >= pit_period_ns`, one or more
IRQ0 ticks are injected. This is a *wall-clock* delivery model, not a
cycle-accurate one — the emulator delivers the right number of ticks per
real second regardless of how fast the guest executes the 200 000-
instruction batch.

## VBLANK and output framerate

VBLANK is delivered independently of the PIT at a fixed wall-clock rate
of **~57 Hz** (17.5 ms per frame), matching the original Pinball 2000
hardware (see [17-vblank.md](17-vblank.md)):

```c
/* src/cpu.c:526 */
if (now_ns - last_vsync_ns >= 17500000ULL) { /* ~57 Hz = 17.5ms */
    last_vsync_ns += 17500000ULL;
    g_emu.vsync_count++;
    /* ... inject DC_TIMING2 pulse ... */
}
```

The SDL window is updated inside `display_render()` (`src/display.c`)
which is called from the display subsystem whenever the guest writes a
new frame. In practice the SDL window updates at whatever rate the guest
produces rendered frames, which tracks VBLANK. The actual measured
output is logged every 5 seconds:

```
[disp] FPS: 57.0 (285 frames / 5000 ms)
```

`COIN_HIGH_FRAMES 6` in `src/display.c:124` — 6 frames at 60 fps ≈ 100 ms
high-level on the coin line — is based on the 60 fps approximation but
the actual VBLANK rate means the coin pulse lasts ~105 ms, which is
within the acceptable range for the game's debounce logic.

## Why not target exactly 60 Hz?

The original Pinball 2000 MediaGX PLL produced a display clock of
`~57.27 Hz`. Matching this avoids a subtle timing mismatch: if the
emulator VBLANK races ahead of the PIT clock the game accumulates
spurious "missed frame" counters and eventually stalls the DCS command
queue. 57 Hz is therefore not a typo.

## exec_count budget

`exec_count` is also used as a coarse "time since start" proxy for boot
sequencing:

* `exec_count < 20` — very early; error messages are suppressed to keep
  the log clean during ROM load.
* `exec_count >= clkint_ready_exec + 50` — enough time for XINU
  `ctxsw` to settle after the clock interrupt is first detected; this is
  when `xinu_ready` is set and the DCS-mode patch fires.
* Various `exec_count & 0x3F == 0` / `0x7F == 0` guards — periodic
  maintenance throttling.

These are batch counts, not instruction counts; multiply by 200 000 for
the instruction equivalent.

## Cross-references

* VBLANK mechanics: [17-vblank.md](17-vblank.md)
* IRQ0 injection: [16-irq-pic.md](16-irq-pic.md)
* XINU ready detection: [22-xinu-boot-sequence.md](22-xinu-boot-sequence.md)
