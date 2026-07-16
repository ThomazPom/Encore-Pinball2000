# 41 — CLI Keyboard Guide

Quick reference for all key bindings while Encore is running. These are
implemented by the QEMU machine's key event handler (`qemu/p2k-lpt-board.c:p2k_lpt_key_event`)
and delivered through QEMU's SDL2/GTK display backends.

> [!WARNING]
> Real-cabinet validation is pending — see
> [docs/29-cabinet-testing-call.md](29-cabinet-testing-call.md) for
> how to help verify.

## Normal mode (default)

In normal mode the host intercepts certain keys for emulator control. The
game's own keyboard handling (XINA shell) does not receive these unless
raw capture mode is enabled (see below).

| Key(s)              | Action |
|---------------------|--------|
| `F1`                | Quit / shutdown request (clean exit with savedata flush) |
| `F2`                | Toggle vertical framebuffer flip (default ON: bottom-up → top-down) |
| `F3`                | Screenshot to `<screenshot-dir>/p2k_screen_<ts>.jpg` (default `/tmp`) |
| `F4`                | Toggle coin door open/closed |
| `F5` / `Enter` / `KP-Enter` | ~60-frame Enter pulse (service menu navigation) |
| `F6`                | Left action button (switch matrix) |
| `F7`                | Left flipper (switch matrix) |
| `F8`                | Right flipper (switch matrix) |
| `F9`                | Right action button (switch matrix) |
| `F10` or `C`        | Coin slot 1 (insert credit) |
| `F12`               | State dump (LPT registers, coin door, switch state to stderr) |
| `SPACE` or `S`      | Start button (switch matrix row 0, bit 2) |
| `Ctrl+C`            | Shell interrupt (terminates QEMU, triggers savedata flush via exit notifiers) |
| `Ctrl+Alt+F`        | SDL fullscreen toggle (QEMU built-in, not Encore-specific) |

> [!NOTE]
> Encore relies on QEMU's standard input subsystems. Most gameplay keys are injected
> via the LPT switch-matrix device (`qemu/p2k-lpt-board.c`), not via PS/2 keyboard scancodes.

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
a discrete coin pulse via the LPT device: the coin line is held HIGH for
a calibrated duration (~100 ms), then LOW (~50 ms). Multiple rapid presses
queue up to a configurable limit; each queued pulse produces one credit.

> [!TIP]
> If you need to add many credits quickly (e.g., for testing a multiball mode), hold `F10`
> for 2–3 seconds. The pulse queue will fill and drain, adding credits at the game's natural
> acceptance rate.

## QEMU monitor commands

Encore does not have a custom debugger UI, but QEMU's built-in monitor
provides low-level introspection. Access it via `Ctrl+A c` (toggle between
monitor and guest console) or by connecting to the monitor socket if `run-qemu.sh`
was invoked with `--monitor`.

Useful commands:

| Command | Effect |
|---------|--------|
| `info registers` | Dump CPU state (EIP, EAX, ESP, etc.) |
| `x/20i $eip` | Disassemble 20 instructions at current EIP |
| `x/8xw 0x11000000` | Hex dump 8 words from BAR2 SRAM base |
| `info mtree` | Show full memory map (all BARs, ROM, RAM) |
| `info qtree` | Show device tree (PCI devices, ISA devices) |
| `quit` | Exit QEMU immediately (no savedata flush) |

See the [QEMU monitor documentation](https://www.qemu.org/docs/master/system/monitor.html) for
the complete command set.

## Raw keyboard capture mode (PS/2 passthrough)

Encore can forward host keyboard events as PS/2 scancodes to the guest's
keyboard controller (i8042). This is useful for interacting with the XINA shell
prompt or game menus that expect real keyboard input.

**How to enable:** pass `-display sdl,grab-mod=lctrl-lalt` to QEMU (or configure
it in `run-qemu.sh`). Then press `Left Ctrl + Left Alt` to toggle grab mode.

When grab is active:

* Every key press and release is injected as a PS/2 Set 1 scancode
  directly into the guest's keyboard controller.
* Gameplay key bindings (F10, SPACE, flippers) are **suspended** — the guest's
  own keyboard driver receives the input.
* `Ctrl+A x` still quits (QEMU's monitor escape overrides everything).
* Press `Left Ctrl + Left Alt` again to release grab and return to normal mode.

The grab state is visible in the SDL window title bar (icon changes to a
padlock when active).

### Extended scancodes in grab mode

Arrow keys, Page Up/Down, and similar navigation keys require E0-
prefixed extended scancodes. QEMU's SDL backend handles these correctly;
they work in grab mode without additional configuration.

### Limitation

Right-side modifier keys (`RCtrl`, `RAlt`) work correctly in Encore —
device implements the full PS/2 extended scancode set.

## Diagnostic / debug keys

`[` / `]` for LPT column probing) via keyboard shortcuts. If you need to inspect
switch state or LPT registers, use the QEMU monitor (`x/<addr>` to read memory)
or add temporary `info_report()` calls to `qemu/p2k-lpt-board.c`.

For automated testing, use `--headless` with a wrapper script that kills QEMU after
a timeout, or use QEMU's monitor to send a quit command via `--monitor stdio`.

## Startup flags affecting keys

| CLI flag        | Effect on keys |
|-----------------|----------------|
| `--display none` (headless) | No SDL window; no key input available. Use for CI/regression tests. |
| `-display sdl,grab-mod=...` | Changes the key combo for toggling PS/2 grab mode. |
| `--monitor stdio` | QEMU monitor on stdin; guest console on separate PTY. Useful for scripted control. |


The legacy Encore (Unicorn-based) had an SDL event loop in `src/display.c` that mapped every
key explicitly:

* `F1` = clean exit with savedata flush
* `F2` = Y-flip toggle
* `F3` = screenshot
* `F4` = coin-door interlock toggle
* `F11` = fullscreen toggle
* `F12` = switch state dump
* `Alt+K` = raw keyboard capture toggle

Encore **does not reimplement this custom key-binding layer.** Instead it relies on:

* QEMU's standard input subsystems (SDL, GTK, VNC) for keyboard/mouse.
* The LPT switch-matrix device (`qemu/p2k-lpt-board.c`) for gameplay inputs (coins, start,
  flippers). These are injected programmatically by `run-qemu.sh` or by an external control
  script, not by intercepting host keypresses in the QEMU C code.
* QEMU's monitor for debugging (instead of custom F12 dumps).

This is a **deliberate simplification.** Encore delegates input handling to QEMU's
well-tested input layer, avoiding the "reinvent SDL event dispatch" trap. The tradeoff is
that some convenience features (one-key screenshot, Y-flip toggle) are not as ergonomic.
A future enhancement could add these via QEMU HMP commands (Human Monitor Protocol) or
via a separate input-injection script.

> [!IMPORTANT]
> If you're migrating from the legacy Encore and expect `F1` to flush savedata, note that
> Encore now persists savedata automatically on exit via QEMU exit notifiers (see
> [docs/09-savedata.md](09-savedata.md)). `Ctrl+C` will exit cleanly and flush BAR2 NVRAM
> and BAR3 flash atomically (unless `P2K_NO_SAVEDATA=1`, including via `--no-savedata`).

## Cross-references

* LPT switch matrix: [26-lpt-board.md](26-lpt-board.md)
* Savedata (why keys don't persist settings): [09-savedata.md](09-savedata.md)
* CLI reference for run-qemu.sh flags: [03-cli-reference.md](03-cli-reference.md)
* Troubleshooting input issues: [04-troubleshooting.md](04-troubleshooting.md)

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
