# 04 — Troubleshooting

Start from the ordinary launcher and change one variable at a time. A useful
first record keeps the normal window, audio and timing path while preserving
verbose output:

```sh
set -o pipefail
scripts/run-qemu.sh --game swe1 -v 2>&1 | tee /tmp/encore-run.log
```

Record the exact command, game/update, Encore commit and whether the failure is
repeatable. Do not add `--speed-target`, change DCS engines or switch to
headless mode before reproducing the original symptom; those choices change
the system being diagnosed.

> [!TIP]
> The default launcher is intentionally quiet. `-v` restores the guest UART
> stream and enables the read-only PIT/PIC/IDT/XINU diagnostic sampler. Use
> `-vv` for DCS audio events and `-vvv` only when byte-level DCS traffic is
> necessary.

## Preparation without a game boot

Run the common setup path through asset and runtime preparation, then stop
before QEMU or cache generation starts:

```sh
scripts/run-qemu.sh --preflight --game swe1 \
  --lpt-device emulated --no-savedata
```

Preflight can still offer dependencies, acquire QEMU and download absent asset
trees. It proves preparation, not that the guest boots or plays correctly.

## Launcher cannot find or start QEMU

Encore needs its custom `pinball2000` machine; a distribution
`qemu-system-i386` is not a substitute. The launcher searches, in order:

1. `QEMU_BIN` when it names an executable;
2. `./qemu-system-i386`;
3. the pinned local build cache;
4. the verified release cache.

When none exists, an interactive source checkout offers a local build or the
latest published binary. A packaged release downloads a verified replacement.
Non-interactive acquisition stops instead of guessing.

Verify a candidate directly:

```sh
QEMU_BIN="$HOME/.cache/p2k-qemu-build/qemu-10.0.8/build/qemu-system-i386"
"$QEMU_BIN" -M help | grep pinball2000
ldd "$QEMU_BIN" | grep 'not found' || true
```

The first command must list `pinball2000`; the second must not list a missing
shared library. Rebuild the checkout's pinned QEMU with:

```sh
scripts/build-qemu.sh
```

The builder checks commands, development libraries and Python virtual-
environment support before it downloads or modifies its cached upstream tree.
Use the version reported by `scripts/build-qemu.sh --help` unless deliberately
validating another supported QEMU release.

| Build symptom | Action |
|---|---|
| symlink or permission failures on a shared filesystem | keep `P2K_QEMU_BUILD_DIR` on a native Linux filesystem; the default cache already does |
| an edited `qemu/` source is absent from the binary | rerun `scripts/build-qemu.sh`; it refreshes the out-of-tree machine graft before Ninja |
| a missing compiler, Python venv or library header | install the exact command/module set printed by the builder |

If the launcher rejects an option, start with `scripts/run-qemu.sh --help`.
Display/audio support, update specifications, paths and incompatible modes are
validated at different preparation stages, so keep the complete error text.

## ROMs or updates are missing

When the complete `roms/` or `updates/` directory is absent, the launcher
clones the offline asset trees before preflight or launch. An existing
directory is deliberately never inspected, refreshed or repaired. Therefore a
partial directory can survive until QEMU reports a specific missing chip.

Use the filename in the error to check the selected tree:

```sh
find roms -maxdepth 2 -type f -printf '%P %s bytes\n' | sort
find updates -maxdepth 3 -type f -name '*.rom' -printf '%P %s bytes\n' | sort
```

To trigger safe acquisition, move a known-incomplete default tree aside rather
than deleting it, then rerun preflight:

```sh
mv roms roms.incomplete
scripts/run-qemu.sh --preflight --game swe1 --lpt-device emulated
```

An explicit `--update` version that is absent is an error. `--update auto`
selects the newest matching local bundle. Remember that the installed update
also lives in `<game>.flash`; `--update none` disables discovery but still
loads that persistent flash. See [Persistent cabinet state](09-savedata.md).

Bank 0 requires the selected game's U100 and U101 chips. A complete update
inner directory requires matching `bootdata`, `im_flsh0`, `game` and `symbols`
ROM components. Native ADSP audio additionally needs U109/U110 and a valid
1 MiB sound flash; a game boot can therefore succeed while that audio engine
still reports missing assets.

## The guest stops during boot

First repeat the run with `-v` and note the last stable guest message.

| Last symptom | Check |
|---|---|
| missing U100/U101 or another ROM chip | selected `--roms` tree and `--game` |
| no saved flash and no update bundle near `[STARTING GAME CODE]` | use a valid update, or intentionally test base ROMs with `--update none --no-savedata` |
| repeated `Retrieve Resource (get &) Failed` | `<game>.nvram2` presence and exact 196,608-byte size; compare with a separate disposable profile |
| invalid or short SEEPROM warning | `<game>.see` must be exactly 128 bytes; the emulator falls back to built-in defaults |
| savedata directory fallback warning | permissions on the requested path and the actual XDG fallback selected |

Do not overwrite the only copy of a failing profile. Compare it with a
read-only disposable run:

```sh
scripts/run-qemu.sh --game swe1 --no-savedata -v
```

If only persistent state reproduces the problem, stop Encore and copy all
three game files together before further experiments. See
[Persistent cabinet state](09-savedata.md) for backup and reset semantics.

## No window, black output or display failure

- From a graphical desktop, the normal path uses Encore's direct framebuffer
  renderer.
- From a local text VT, the launcher prepares SDL2/KMSDRM.
- From SSH without a graphical session or local VT, use `--headless`; it has no
  playable input window.
- Explicit `--wayland` requires both `WAYLAND_DISPLAY` and
  `XDG_RUNTIME_DIR`.

For a presentation-only A/B test, keep the same game, update and savedata and
compare the two renderer paths:

```sh
# Default direct framebuffer
scripts/run-qemu.sh --game swe1 --framebuffer

# QEMU display-surface path
scripts/run-qemu.sh --game swe1 --qemu-framebuffer --display sdl
```

An unavailable explicit display backend fails fast and prints those compiled
into the selected QEMU. A screenshot with `F3` can help distinguish a guest
rendering failure from host presentation; the two paths use different capture
implementations. If the image alone is vertically reversed, press `F2` before
changing renderers. See [Desktop controls](41-cli-keyboard-guide.md).

## No sound or broken sound

The launcher prints the automatically selected QEMU audio backend. Auto tries
`sdl`, `pa`, `alsa`, `oss`, `sndio` and `dbus` in that order, but only when the
backend is compiled in and its host check passes. If none qualifies, the game
runs silently with a warning.

Useful isolations are:

```sh
# Prove whether audio is involved in another failure
scripts/run-qemu.sh --game swe1 --audio none

# Select a backend explicitly; unsupported names fail before launch
scripts/run-qemu.sh --game swe1 --audio sdl

# Add audio events and per-second renderer status
scripts/run-qemu.sh --game swe1 -vv
```

Also check the host sink, mute and volume. Cabinet-session volume policy is a
separate host action and logs `[encore-audio]` decisions. Do not change the DCS
engine as a first sound fix: engines are implementation alternatives, not host
backend selectors. See [DCS sound](25-dcs-sound.md).

## Controls do not reach the game

First give the graphical window keyboard focus. Then press `Tab` and read the
temporary `CABINET KEYS` / `XINA KEYBOARD` banner.
Plain keys reach XINA rather than cabinet switches in keyboard mode.

With a custom A–Z map, run with `-v`; one malformed line rejects the complete
custom map, after which built-in controls remain active. Use `F12` in cabinet
mode to print current LPT and switch state.

For a physical cabinet:

```sh
ls -l /dev/parport*
id -nG
```

The device normally needs `ppdev` and membership in `lp`. `auto` may use the
emulated board when no recognized physical board is available; use
`--lpt-device required` when silent fallback would invalidate the test. An
explicit `/dev/parportN` is authoritative and never falls back after an
open/claim failure. Load `parport_pc` and `ppdev` when the character device is
absent; release the printer `lp` driver if it owns the port. `EACCES` indicates
access rights and `EBUSY` another owner. `/dev/usb/lpN` is a printer interface,
not the bidirectional `ppdev` register interface Encore accepts. See
[Real LPT passthrough](46-real-lpt-passthrough.md) before testing powered
cabinet hardware.

## Serial console is unavailable

`--serial` needs `nc` and occupies the invoking terminal. It cannot be combined
with `--headless`, `--serial-tcp` or `--uart-tcp`. For a second-terminal
session:

```sh
scripts/run-qemu.sh --game swe1 --serial-tcp 4444
nc 127.0.0.1 4444
```

`--uart-quiet` suppresses both the UART mirror and chardev output; it wins over
`-v`. The launcher also supplies a bounded boot input sequence in quiet mode to
avoid leaving XINA in a synchronous polled read. See
[Console scripting](42-console-scripting.md) for repeatable automation.

## Timing, stalls and rare fatal monitors

`--strict` is now a compatibility alias for the sole natural QEMU i8254+i8259
IRQ0 path; it is not an alternative timing implementation. Likewise,
`--speed-target` deliberately changes the PIT divisor and should not be used to
hide a default-speed defect.

Run the isolated two-pass self-diagnostic with ordinary display and audio
defaults:

```sh
scripts/run-qemu.sh --bench --game swe1
```

The benchmark uses disposable state, injects and restores a temporary guest-
RAM IRQ probe in pass 1, then launches a new unpatched guest for LPT/PDB
measurement in pass 2. Its report includes absolute guest speed, IRQ delivery
and jitter, maximum `clkint` depth, minimum XINU IStack margin, PDB window
distribution and DCS health. It prints the `/tmp/p2k-bench-*` artifact path and
returns status 2 for an abnormal verdict.

Use `--bench-long` only for final validation; it changes warmup from 10 to 30
guest seconds, not the measured window. `--bench-guest-load` is a separate
cooperative-load experiment and must be labelled as such.

> [!IMPORTANT]
> Do not diagnose a scheduler or IRQ defect from one worst host gap alone. Keep
> the complete window distribution, delivery ratio, depth and IStack margin
> together with the command and artifact directory.

## Preserve a live crash before closing it

If XINA is already in a fatal monitor or the guest is wedged, leave QEMU alive
and capture it first:

```sh
tools/capture-live-crash.sh
```

The tool briefly pauses one running Encore process under GDB, saves guest RAM,
host mappings, thread backtraces, command line, metadata and hashes, then
detaches without terminating QEMU or writing guest memory. It requires `gdb`,
`objdump`, `sha256sum`, normal ptrace permission and a QEMU binary retaining
the required symbols. If several instances run, pass `--pid`.

Preserve `/tmp/encore-run.log`, the benchmark artifact when applicable and the
live-capture directory together. See
[Live crash capture](51-live-crash-capture.md) for output contents and optional
guest disassembly ranges.

## Escalation checklist

Include:

- exact launcher command and whether the normal UI was used;
- `git rev-parse HEAD` and the QEMU path printed by the launcher;
- game, update selection and whether savedata was normal, fresh or disabled;
- complete log from before boot through failure;
- host display/audio choice and physical-board mode;
- benchmark `report.md` plus `results.json` for timing claims;
- live RAM capture for a fatal monitor that remained on screen.

Remove ROM payloads and cabinet-private data before sharing evidence outside
their authorized audience.

---

← [Persistent cabinet state](09-savedata.md) ·
[Documentation index](README.md) · [Architecture](10-architecture.md)
