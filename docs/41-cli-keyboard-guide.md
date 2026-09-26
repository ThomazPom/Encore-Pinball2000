# 41 — Desktop controls

Encore routes host keys either to the emulated cabinet controls or to XINA's
AT keyboard. This guide describes the default direct framebuffer and QEMU
display paths; both ultimately use the same input router and cabinet state.

## Input modes

Emulated, physical and deliberately disconnected LPT modes begin with the AT
keyboard unplugged from XINA. Press `Tab` to switch between:

- **CABINET KEYS** — host keys operate the emulated driver-board inputs;
- **XINA KEYBOARD** — ordinary keys reach the guest's AT keyboard.

The direct renderer briefly displays the selected mode. `Tab` remains owned by
Encore so it can switch back. While the keyboard is unplugged, XINA receives
no host keystrokes and cannot present keyboard-driven guest controls during a
normal cabinet boot.

When XINA keyboard mode is active, plain `F1`, `F2` and `F3` belong to the
guest. Hold `Alt` with those keys to invoke Encore's Quit, flipscreen and
screenshot actions.

> [!NOTE]
> `--lpt-device none` installs no guest LPT device and connects the XINA
> keyboard immediately. There is no cabinet input router in that diagnostic
> mode.

## Cabinet keys

These controls are active with the emulated board and with explicit hybrid
input. A held key keeps the corresponding contact closed unless the table says
the action is a pulse or toggle.

| Key | Cabinet or host action |
|---|---|
| `F1` | Request clean emulator shutdown. |
| `F2` | Toggle vertical reversal relative to the normal display orientation. |
| `F3` | Capture a screenshot. |
| `F4` | Toggle the emulated coin-door interlock. |
| `F5`, `Enter`, `KP Enter` | Fire an approximately 60-LPT-frame Enter pulse. |
| `F6` | Hold the left action button. |
| `F7` | Hold the left flipper. |
| `F8` | Hold the right flipper. |
| `F9` | Hold the right action button. |
| `F10`, `C` | Hold coin-slot contact 1. |
| `Space`, `S` | Hold matrix switch 13, the Start button. |
| `Esc`, `Left` | Hold the service-panel Escape input. |
| `Down`, `KP -` | Hold Volume Down / menu Down. |
| `Up`, `=`, `KP +` | Hold Volume Up / menu Up. |
| `Right` | Hold Begin Test / Enter. |
| `F12` | Print current LPT/input state to the terminal. |

Coin is a contact, not a hard-coded credit operation. Whether one closure adds
a credit depends on the game's current pricing and saved adjustments.

> [!TIP]
> For a basic desktop start: tap or hold `C` as required for credits, then
> press `S`. Use `F7` and `F8` for the flippers and `F1` to exit.

## Window and display actions

| Display path | Fullscreen toggle |
|---|---|
| Default direct framebuffer | `F11` or `Alt+Enter` |
| QEMU SDL selected with `--display sdl` | `Ctrl+Alt+F` |

Closing the direct framebuffer window requests the same clean shutdown as
`F1`.

`F3` writes under `/tmp` by default. Select an existing directory with:

```sh
scripts/run-qemu.sh --screenshot-dir ./screens
```

The direct framebuffer captures a 640×480 BMP. QEMU display paths prefer JPEG
when `cjpeg`, `magick` or `convert` is available and otherwise write PPM.

Use `--flipscreen` to start in the same reversed state toggled by `F2`.

## Any matrix switch

To operate a switch by its two-digit matrix number:

1. type a column and row from `11` through `88`;
2. hold either `Ctrl` key for as long as the switch should remain closed;
3. release `Ctrl` to open it.

The two digits must each be from 1 through 8. The selected number remains
available, so another `Ctrl` hold repeats the same switch. Typing a new pair
replaces it. Numeric-`Ctrl` holds and configured-letter holds are tracked
independently, so releasing either input does not release a switch that the
other one still holds.

Example: type `1`, then `3`, then hold `Ctrl` to operate Start as switch 13.

## Custom A–Z bindings

Encore creates an editable starter map on the first emulated-board or hybrid
input launch:

```text
$XDG_CONFIG_HOME/encore/switch-keymap.yaml
```

When `XDG_CONFIG_HOME` is unset, the path is
`~/.config/encore/switch-keymap.yaml`. Select another path with:

```sh
scripts/run-qemu.sh --switch-keymap ./my-switches.yaml
```

A missing selected file and its parent directory are initialized
automatically. The file uses a deliberately small YAML subset:

```yaml
switches:
  a: 13
  b: 28
  z: 88
```

Rules:

- the only top-level key is `switches:`;
- each indented entry maps one A–Z letter to a matrix number;
- both switch digits must be from 1 through 8;
- blank lines and `#` comments are accepted;
- tabs, duplicate letters, trailing text and malformed entries reject the
  complete custom map rather than loading it partially.

Letter case is ignored. Holding a configured letter holds its switch. Several
letters may map to the same switch; that switch remains closed until every
mapped key currently holding it has been released.

Configured letters take precedence over built-in letter shortcuts. For
example, mapping `c` replaces the normal `C` coin binding for that run. Invalid
custom maps disable only the custom bindings, so built-in controls remain
available; run with `-v` for line-specific parser warnings.

The generated starter map currently contains:

| Letter | Switch |
|---|---:|
| `X` | 28 |
| `F` | 58 |
| `D` | 53 |
| `G` | 54 |
| `E` | 55 |
| `T` | 56 |
| `L` | 52 |

These are editable defaults, not additional hard-coded controls.
The map is read once during machine initialization; restart Encore after
editing it.

## Physical, hybrid and disconnected boards

- **Physical-only:** the real board supplies cabinet inputs. Keyboard cabinet
  closures are blocked, while `F1`–`F3` remain available as host actions.
- **Hybrid:** physical reads remain authoritative and keyboard closures are
  added to them. Outputs, protocol traffic, keepalive and the coin-door
  interlock remain physical; `F4` is therefore ignored.
- **Disconnected:** the guest sees an open LPT bus. Cabinet closures are
  blocked, while `F1`–`F3` and the `Tab` route to XINA remain available.

In physical and disconnected modes, `Tab` can connect the XINA keyboard but
does not silently enable emulated cabinet switches. See
[LPT driver-board interface](26-lpt-board.md) and
[Real LPT passthrough](46-real-lpt-passthrough.md).

> [!WARNING]
> Keyboard behavior validated in emulation does not validate a powered
> playfield or real cabinet switch wiring.

## Automation

`--display none` creates no graphical input window, and `--serial` controls
COM1 rather than cabinet switches.

For repeatable tests, prefer console scripts over desktop key timing. Scripts
can wait for guest state, hold keys or exact matrix switches, repeat actions
and capture screenshots or audio. See
[Console scripting](42-console-scripting.md).

---

← [Quickstart](02-quickstart.md) · [Documentation index](README.md) ·
[Command-line reference](03-cli-reference.md)
