# 36 — CLI Keyboard Guide

Quick reference for all key bindings while Encore is running. All
bindings are defined in `src/display.c`.

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## Normal mode (default)

In normal mode the host intercepts F-keys, SPACE and navigation keys.
The game's own keyboard handling (XINA shell) does not receive these.

| Key(s)              | Action |
|---------------------|--------|
| `F1`                | Clean exit (flushes savedata, terminates) |
| `F2`                | Toggle Y-flip (invert framebuffer vertically) |
| `F3`                | Save timestamped PNG screenshot (see `src/display.c` `SCREENSHOT_DIR`) |
| `F4`                | Toggle coin-door interlock (closed ↔ open) |
| `F6`                | Left action button (`Phys[10].b7`) |
| `F7`                | Left flipper (`Phys[10].b5`) |
| `F8`                | Right flipper (`Phys[10].b4`) |
| `F9`                | Right action button (`Phys[10].b6`) |
| `F10` or `C`        | Insert credit (queueable; press repeatedly for multi-credit) |
| `F11` or `Alt+Enter`| Toggle fullscreen |
| `F12`               | Dump guest switch state to stderr |
| `SPACE` or `S`      | Start button (`sw=2`, `Phys[0].b2`) |

### Coin-door service panel

These keys emulate the four buttons on the coin-door service panel.
Their function changes depending on whether the game is in normal play
or test/service mode:

| Key(s)              | Normal mode            | Service / menu mode |
|---------------------|------------------------|---------------------|
| `ESC` / `Left arrow`| Service Credits        | Escape / Back       |
| `Down arrow` / `-`  | Volume −               | Menu Down           |
| `Up arrow` / `=`    | Volume +               | Menu Up             |
| `Right arrow` / `Enter` / `KP Enter` | Begin Test | Enter / Select |

### Credit pulse behaviour

`F10` / `C` does not inject a raw switch closure. Instead it enqueues
a discrete coin pulse: the coin line is held HIGH for `COIN_HIGH_FRAMES`
frames (~100 ms), then LOW for `COIN_LOW_FRAMES` frames (~50 ms). Up to
`COIN_QUEUE_MAX` pulses can be queued by rapid pressing; each queued
pulse produces one credit.

## Alt+K — raw keyboard capture mode

`Alt+K` toggles **raw keyboard capture**. In capture mode:

* Every key press and release is injected as a PS/2 Set 1 scancode
  directly into the guest's keyboard controller (KBC).
* F-key gameplay bindings, SPACE, and coin/flipper polling are all
  **suspended** — the guest's own keyboard handler receives the input.
* **F1 still quits** (the only exception).
* `Alt+K` again returns to normal mode.

Capture mode is the correct way to interact with the XINA shell prompt
or with any game menu that expects real keyboard input.

The toggle is logged:

```
[disp] kbd_capture ON (Alt+K)
[disp] kbd_capture OFF (Alt+K)
```

### Extended scancodes in capture mode

Arrow keys, Page Up/Down, and similar navigation keys require E0-
prefixed extended scancodes. The scancode table in `src/display.c`
(lines 92–105) maps these correctly; they are available in capture mode.

### Limitation

Right-side modifier keys (`RCtrl`, `RAlt`) and some multimedia keys are
mapped as extended (`E0` prefix) in the table but E0-prefixed injection
is not yet implemented. These keys are noted as "not implemented" in the
`--help` text. All standard alphanumeric keys, F1–F12, and navigation
keys work correctly.

## Diagnostic keys

| Key            | Action |
|----------------|--------|
| `[`            | Decrease probe column (debug LPT switch-bit probing) |
| `]`            | Increase probe column |
| `0`–`7`        | Force `Phys[c<col>].bN` — LPT bit probe during attract mode |
| `F12`          | Print current guest switch state bitmask to stderr |

## Debug probe context

`0`–`7` digit keys temporarily force a specific bit in the current LPT
column register high. This was used during initial LPT development to
identify which physical switch bit corresponds to the Start button. In
normal use these keys can be safely ignored; they have no effect on
game state unless the LPT polling loop is active.

## Startup flags affecting keys

| CLI flag        | Effect on keys |
|-----------------|----------------|
| `--fullscreen`  | Equivalent to pressing `F11` before the window appears |
| `--flipscreen`  | Equivalent to pressing `F2` before the first frame |
| `--headless`    | No SDL window; no key input available |

## Cross-references

* Display pipeline: [11-display-pipeline.md](11-display-pipeline.md)
* LPT emulation: [18-lpt-emulation.md](18-lpt-emulation.md)
* Real cabinet passthrough: [19-real-lpt-passthrough.md](19-real-lpt-passthrough.md)
* CLI reference: [03-cli-reference.md](03-cli-reference.md)

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
