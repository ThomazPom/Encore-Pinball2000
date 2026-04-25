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
| `--dcs-mode` | `bar4-patch` / `io-handled` | `io-handled` | Sound topology |
| `--fullscreen` | — | off | Open SDL window fullscreen |
| `--flipscreen` | — | off | Invert the framebuffer vertically |
| `--bpp` | `16` / `32` | `32` | Output texture depth |
| `--splash-screen` | `none` / *PATH* | embedded | Startup splash image (see [49-splash-screen.md](49-splash-screen.md)) |
| `--serial-tcp` | *port* | 0 | Expose COM1 as TCP bridge |
| `--keyboard-tcp` | *port* | 0 | Inject PS/2 scancodes over TCP |
| `--headless` | — | off | Skip SDL window & audio |
| `--lpt-device` | *path* / `0xBASE` / `none` / `emu` | (auto) | Real cabinet passthrough |
| `--lpt-trace` | *file.csv* | — | Capture LPT bus cycles to CSV |
| `--lpt-bus-pace` | `auto` / *µs* | `auto` | Settling delay for the cabinet driver board |
| `--lpt-managed-dir` | — | off | Encore rewrites bit-5 of CTL writes (legacy) |
| `--lpt-purist` | — | — | Back-compat alias; verbatim CTL is now the default |
| `--cabinet-purist` | — | off | Skip selected bring-up shims while real LPT is open |
| `--verbose [N]`, `-v…` | level | 0 | Increase logging verbosity |
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

### `--dcs-mode io-handled` (default)

No CPU code is rewritten. Encore primes the BT-107 watchdog cell
with `0x0000FFFF` (sentinel-safe for all bundles) and scribbles
that value every tick until `xinu_ready` fires; from then on the
scribble flips to `0x00000000`. The game's own DCS-detect probe
runs *after* `xinu_ready`, sees `cell != 0xFFFF` → returns 1 →
stores `dcs_mode=1` → game takes its BAR4 sound path normally.
DCS commands are delivered through `sound.c`. Boots and produces
audio on every bundle in the matrix, including the three "pattern
absent" cases (SWE1 v1.3, RFM/SWE1 `--update none`). Details:
[13-dcs-mode-duality.md](13-dcs-mode-duality.md).

### `--dcs-mode bar4-patch` (legacy / regression)

Pattern-scans for the `CMP EAX,1 ; JNE +0x21 ; MOV [slot],EAX`
idiom inside the game's DCS-detect probe and replaces the 5-byte
CMP/JNE prologue with `MOV EAX,1`. Forces `dcs_mode=1` →
identical downstream BAR4 path. Equivalent to `io-handled` on
every bundle that ships the prologue, but a no-op (silent attract)
on SWE1 v1.3 and `--update none`. Kept because it makes A/B
regression against the legacy patch path a one-flag flip.

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

### `--splash-screen none | PATH`

Controls the image shown in the SDL window from the moment it is
created until the guest draws its first non-zero pixel (i.e. the
white PRISM "PLEASE WAIT — VALIDATING UPDATE" frame takes over).

* default — use the JPEG embedded in the binary from
  `assets/splash-screen.jpg`. Builders can drop in any JPEG of that
  name and rebuild to ship a custom default; the file is linked
  verbatim by `ld -r -b binary` (see [28-build-system.md](28-build-system.md)).
* `none` — disable; the window starts black.
* `PATH` — load `PATH` at runtime. Any format `stb_image` supports
  works (JPEG, PNG, BMP, TGA, PSD, PIC, PNM). The image is letterboxed
  to the window's aspect; it is **not** stretched. If `PATH` can't be
  read or decoded, Encore falls back to the embedded JPEG with a
  warning so a typo never leaves the user staring at a black window.

Full design + extension notes are in
[49-splash-screen.md](49-splash-screen.md).

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

### `--lpt-device PATH | 0xBASE | none | emu`

Drive a physical Pinball 2000 driver board through one of two
backends, selected by argument format:

* **`/dev/parportN`** — Linux `ppdev` backend (unprivileged once your
  user is in the `lp` group).
* **`0xBASE`** (e.g. `0x378`) — raw `inb`/`outb` backend; needs
  `CAP_SYS_RAWIO` (root or one-time `setcap cap_sys_rawio+ep`).
* **`none`** / **`emu`** — force the emulated LPT path even if a
  parport device is present (handy on a laptop with `/dev/parport0`).

Without this flag, Encore silently probes `/dev/parport0` and uses it
if present; with it set explicitly, a missing device is a fatal error
(so you do not accidentally emulate when you wanted hardware). Backend
choice is about install ergonomics and tracing, not wire speed — see
[19-real-lpt-passthrough.md](19-real-lpt-passthrough.md) for the
kernel module / permissions recipe and the bus-protocol details.

### `--lpt-trace FILE.csv`

Append every LPT bus cycle (read or write) to `FILE.csv`. Useful for
post-mortem of cabinet sessions and for diffing against captures from
other implementations.

### `--lpt-bus-pace auto | N`

Insert a busywait around real-LPT transactions so the cabinet
driver-board level shifters get setup/hold time.

* `auto` (default) — no pacing while no real board is detected;
  switches to a built-in default once passthrough activates.
* `N` (microseconds) — force exactly N. `0` disables pacing entirely.

Full discussion in
[19-real-lpt-passthrough.md](19-real-lpt-passthrough.md#bus-pacing---lpt-bus-pace).

### `--lpt-managed-dir`

Opt back into the legacy behaviour where Encore rewrites bit 5 of
every CTL write to manage the bus direction. Verbatim CTL forwarding
is now the default; this flag is for diagnostic A/B with the old code
path.

### `--lpt-purist`

Back-compat no-op: verbatim CTL forwarding is now the default for the
raw backend, so this flag does nothing. Kept so older scripts still
parse.

### `--cabinet-purist`

Skip selected emulator-side bring-up shims (sgc fixups in particular)
while a real LPT cabinet is open, so the guest sees as close as
possible to what real hardware would. Off by default — most users
want the shims.

## Config / help

### `--config FILE.yaml`

Load options from a simple `key: value` file. CLI wins on conflicts.
When no CLI args are given, `./encore.yaml` is auto-loaded if it
exists. Full grammar: [04-config-yaml.md](04-config-yaml.md).

### `--verbose [LEVEL]`, `-v`, `-vv`, `-vvv`

Increase logging verbosity. `--verbose` alone bumps the level to 1;
`--verbose N` sets it explicitly (use the space form — `--verbose=N`
is not parsed). The short forms `-v`, `-vv`, `-vvv`… set the level by
the count of `v`s. Higher levels can perturb timing-sensitive tests.

### `--help` / `-h`

Prints the full help screen with worked examples. The help screen and
`src/main.c` `apply_option()` are authoritative — if either disagrees
with this document, the code wins.

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
