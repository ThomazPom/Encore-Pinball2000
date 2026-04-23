# 03 — CLI Reference

Every option accepted by `encore` is listed below, in the order they
appear in `src/main.c` `apply_option()` / the `--help` screen. Every
option also works as a `key: value` line inside a YAML config file;
see [04-config-yaml.md](04-config-yaml.md) for the loader rules and
precedence.

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## Summary table

| Flag | Argument | Default | Purpose |
|---|---|---|---|
| `--game` | `swe1` / `rfm` / `auto` | `auto` | Which title to boot |
| `--roms` | *path* | `./roms` | Chip-ROM directory |
| `--savedata` | *path* | `./savedata` | NVRAM/SEEPROM output |
| `--update` | *file / dir / version* | — | Force an update bundle |
| `--no-savedata` | — | off | Skip load *and* save |
| `--dcs-mode` | `bar4-patch` / `io-handled` | `bar4-patch` | Sound topology |
| `--fullscreen` | — | off | Open SDL window fullscreen |
| `--flipscreen` | — | off | Invert the framebuffer vertically |
| `--bpp` | `16` / `32` | `32` | Output texture depth |
| `--serial-tcp` | *port* | 0 | Expose COM1 as TCP bridge |
| `--keyboard-tcp` | *port* | 0 | Inject PS/2 scancodes over TCP |
| `--headless` | — | off | Skip SDL window & audio |
| `--lpt-device` | *path* / `none` | (auto) | Real cabinet passthrough |
| `--config` | *file.yaml* | — | Explicit config load |
| `-h`, `--help` | — | — | Full help + examples |

## Game / paths

### `--game swe1 | rfm | auto`

Selects which title to auto-detect chip ROMs for and which savedata ID
to use. `auto` (default) resolves as follows:

1. If `--update FILE` was given, `game_id` is read at file offset
   `0x3C`. 50069 ⇒ SWE1, 50070 ⇒ RFM.
2. Else, if a real LPT cabinet was opened, `lpt_passthrough_detect_game`
   bit-bangs a 3-register probe and classifies the playfield.
3. Else, defaults to `swe1` (the single-developer's daily driver).

Example:

```sh
./build/encore --game rfm
./build/encore --game auto --lpt-device /dev/parport0  # board says which
```

### `--roms /path` and `--savedata /path`

Override the default `./roms` and `./savedata` search roots. Both must
be writable directories — the savedata loader will `mkdir -p` as
needed, but the ROM loader does *not* create `roms/`. See
[08-rom-loading-pipeline.md](08-rom-loading-pipeline.md) for the
expected sub-structure.

## Update bundle

### `--update FILE | DIR | installer.exe | VERSION`

Four accepted shapes:

* **FILE** — a pre-built concatenated `update.bin` (any name, any path).
* **DIR** — a directory containing the four component ROMs
  (`*_bootdata.rom`, `*_im_flsh0.rom`, `*_game.rom`, `*_symbols.rom`).
  Encore assembles them on the fly, bit-for-bit identical to
  `tools/build_update_bin.py`.
* **installer.exe** — a Williams-branded installer (actually a ZIP).
  `unzip` is invoked in a temp directory and the four ROMs are picked
  out.
* **VERSION** — a short string like `210`, `2.1`, `180`, `1.8`. Encore
  searches `./updates` (and `../updates`, `updates`) for a directory
  named `pin2000_<gid>_<vvvv>_*`, where `vvvv` is `major*100 + minor*10`
  (so "1.5" → 0150, "1.80" → 0180). If `--game` is `swe1` or `rfm`,
  only matching `gid` (50069 or 50070) is considered.

Full semantics: [09-update-loader.md](09-update-loader.md).

## Savedata

### `--no-savedata`

Skip both the load at boot *and* the save at clean exit. The emulator
still writes to its in-memory NVRAM/SEEPROM/EMS state, but nothing hits
disk. Useful for:

* CI pipelines (deterministic state every run);
* regression burn-in across many configurations;
* preserving a known-good save while experimenting.

## Sound

### `--dcs-mode bar4-patch` (default)

Pattern-scans for the `CMP EAX,1 ; JNE +0x21 ; MOV [slot],EAX` idiom
inside the game's DCS-detect probe and replaces the 5-byte CMP/JNE
prologue with `MOV EAX,1`. Forces `dcs_mode=1` → the game takes its
BAR4-based sound path → DCS commands are delivered through
`sound.c`. Works on every bundle tested. Details:
[13-dcs-mode-duality.md](13-dcs-mode-duality.md).

### `--dcs-mode io-handled`

Skips the code patch; relies on the unmodified PCI probe returning 1
naturally. Encore scribbles `0` into the shared probe cell each tick
(see [14-dcs-probe-polarity.md](14-dcs-probe-polarity.md)), which flips
the probe's "absent" semantics to "present". DCS commands then flow
through the UART ports `0x138..0x13F`. Currently partial — the I/O
command-stream pump is still WIP on some bundles.

## Display

### `--fullscreen`

Pass `SDL_WINDOW_FULLSCREEN_DESKTOP` when creating the window. Can also
be toggled at runtime with `F11` or `ALT+ENTER`.

### `--flipscreen`

Begin with a vertically-flipped framebuffer. Some cabinets ship with the
monitor rotated 180°; this matches that. Runtime toggle: `F2`.

### `--bpp 16 | 32`

Selects the SDL texture format. `32` (default) uses ARGB8888; `16`
uses RGB565. 24 falls back to 32 with a warning.

## Headless / networking

### `--serial-tcp PORT`

Listen on `127.0.0.1:PORT` and bridge bidirectionally to the emulated
COM1 UART at I/O 0x3F8. One client at a time; new connection replaces
the old one. Used for XINA shell access — see
[36-cli-keyboard-guide.md](36-cli-keyboard-guide.md) for example
sessions.

### `--keyboard-tcp PORT`

Listen on `127.0.0.1:PORT` and convert inbound ASCII bytes into PS/2
Set-1 scancodes dropped into port 0x60. Experimental: the KBC stub
only answers the BIOS self-test commands, so key-input reception in
XINA depends on its driver's probing behaviour.

### `--headless`

Skip SDL window *and* audio initialization. Useful for CI runners and
for deploying Encore on a server. Combine with `--serial-tcp` to
preserve observability.

## Real cabinet

### `--lpt-device PATH | none | emu`

Drive a physical Pinball 2000 driver board through Linux `ppdev`.
Without this flag, Encore silently probes `/dev/parport0` and uses it
if present; with it set explicitly, a missing device is a fatal error
(so you do not accidentally emulate when you wanted hardware).

See [19-real-lpt-passthrough.md](19-real-lpt-passthrough.md) for
the kernel module / permissions recipe.

## Config / help

### `--config FILE.yaml`

Load options from a simple `key: value` file. CLI wins on conflicts.
When no CLI args are given, `./encore.yaml` is auto-loaded if it
exists. Full grammar: [04-config-yaml.md](04-config-yaml.md).

### `--help` / `-h`

Prints the full 200-line help with worked examples. The help screen
is authoritative — if it disagrees with this document, the code wins.

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
