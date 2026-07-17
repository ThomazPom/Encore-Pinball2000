# 41 — Desktop controls

Encore converts selected host keys into cabinet switch-matrix activity through
`qemu/p2k-lpt-board.c`. These are cabinet controls, not PC keyboard input sent
to XINA.

## Gameplay and emulator controls

| Key | Action |
|---|---|
| `Space` / `S` | Start |
| `F10` / `C` | Pulse coin slot 1 |
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

Each `F10` or `C` press starts a 60-scan coin-switch pulse. Pressing again while
the pulse is active restarts that duration. The game awards credits according
to its pricing adjustments, so one pulse is not necessarily one credit.

## Modes without desktop keys

- `--display none` has no graphical input window.
- `--cabinet-purist` disables this host key handler; all cabinet switches must
  arrive from the real LPT device.
- `--serial` controls COM1 in the terminal. It is separate from cabinet keys.

For automated input, use the QEMU monitor `sendkey` command. For low-level
state, press `F12` or enable `--lpt-trace`.

Details: [LPT board](26-lpt-board.md) and
[CLI reference](03-cli-reference.md).

---

← [Documentation index](README.md) · [Project README](../README.md)
