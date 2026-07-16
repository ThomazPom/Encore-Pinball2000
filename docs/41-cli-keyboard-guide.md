# 41 — Desktop controls

Encore converts selected host keys into cabinet switch-matrix activity through
`qemu/p2k-lpt-board.c`. These are cabinet controls, not PC keyboard input sent
to XINA.

## Gameplay and emulator controls

| Key | Action |
|---|---|
| `Space` / `S` | Start |
| `F10` / `C` | Insert credit |
| `F4` | Open or close the coin door |
| `F7` / `F8` | Left/right flipper |
| `F6` / `F9` | Left/right action button |
| `F2` | Toggle vertical display flip |
| `F3` | Save a screenshot under `--screenshot-dir` (default `/tmp`) |
| `F12` | Print LPT registers and switch state |
| `F1` | Request a clean shutdown |
| `Ctrl+Alt+F` | Toggle SDL fullscreen through QEMU |

## Coin-door service panel

Open the coin door with `F4` before using service volume and menu controls.

| Key | Cabinet button | Typical use |
|---|---|---|
| `Esc` / `Left` | Escape | Service credits or menu back |
| `Down` / keypad `-` | Volume down | Lower volume or menu down |
| `Up` / `=` / keypad `+` | Volume up | Raise volume or menu up |
| `Right` | Begin test | Enter the service menu |
| `F5` / `Enter` / keypad `Enter` | Enter | Select a service item |

The exact menu reaction belongs to the running Williams software and can vary
by game state or update.

## Credit pulses

Each `F10` or `C` press queues a discrete coin-switch pulse. Rapid presses are
serialized so the guest sees separate coin closures instead of one long held
switch. This is also the easiest way to test DCS credit music.

## Modes without desktop keys

- `--display none` has no graphical input window.
- `--cabinet-purist` disables this host key handler; all cabinet switches must
  arrive from the real LPT device.
- `--serial` controls COM1 in the terminal. It is separate from cabinet keys.

For automated input, use the QEMU monitor `sendkey` command. For low-level
state, press `F12` or enable `--lpt-trace`.

See [26 — LPT board](26-lpt-board.md), [03 — CLI reference](03-cli-reference.md)
and [04 — Troubleshooting](04-troubleshooting.md).

---

← [Documentation index](README.md) · [Project README](../README.md)
