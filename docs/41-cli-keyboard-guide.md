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

## Custom A-Z switch bindings

Hold a configured letter to close its matrix switch; release the letter to
open it. Multiple mapped letters can be held together. They use an independent
input layer, so a mapped key cannot release a switch still held through the
numeric Ctrl selector.

The default file is created on first launch:

```text
${XDG_CONFIG_HOME}/encore/switch-keymap.yaml
```

When `XDG_CONFIG_HOME` is unset, this is
`~/.config/encore/switch-keymap.yaml`. Select another file with:

```sh
scripts/run-qemu.sh --switch-keymap ./my-switches.yaml
```

The file uses a strict, dependency-free YAML subset:

```yaml
switches:
  a: 13
  x: 28
  f: 58
```

- Keys are single letters from A through Z, matched without case sensitivity.
- Values are matrix numbers `11` through `88`; both digits must be `1..8`.
- Blank lines and `#` comments are accepted.
- Entries must be indented beneath one top-level `switches:` mapping.
- Duplicate letters, tabs, trailing text, extra sections and other YAML
  features reject the complete file. Built-in controls remain available.

A binding overrides that letter's built-in action. For example, binding `c`
replaces the `C` coin shortcut; `F10` still pulses the coin switch. The file is
read once at launch.

With ROMs and update 2.10 installed, exercise parsing, simultaneous holds,
same-switch reference counts and built-in fallback with:

```sh
python3 scripts/tests/smoke-switch-keymap.py
```

## Any matrix switch

Encore accepts standard two-digit matrix switch numbers. The first digit is
the column and the second is the row; both range from 1 through 8.

1. Type the two-digit switch number on the number row or keypad.
2. Press and hold `Ctrl` to close that switch.
3. Release `Ctrl` to open it again.

The switch remains closed for the real duration of the Ctrl press. The number
stays selected, so another Ctrl hold repeats the same switch. For example, type
`13`, hold Ctrl for three seconds, release it, then hold Ctrl again whenever
Start should be pressed again. Typing another two-digit number replaces the
selection; digits must be from 1 through 8.

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
- In physical mode, cabinet switches must arrive from hardware. Host-only F1
  quit, F2 flipscreen, F3 screenshot and Tab mode cycling remain active. Tab
  selects hybrid keyboard overlay, then full emulation, then physical again.
- `--serial` controls COM1 in the terminal. It is separate from cabinet keys.

For automated input, use the QEMU monitor `sendkey` command. For low-level
state, press `F12` or enable `--lpt-trace`.

Details: [LPT board](26-lpt-board.md) and
[CLI reference](03-cli-reference.md).

---

← [Documentation index](README.md) · [Project README](../README.md)
