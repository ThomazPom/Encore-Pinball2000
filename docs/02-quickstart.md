# 02 — Quickstart

Let's get you from a fresh checkout to a running game in five minutes. We'll build QEMU once, point it at some ROMs, and watch Star Wars Episode I boot.

> **Status:** Behaviour described here is based on emulator testing only. Real-cabinet validation is pending. Use the default emulated LPT board unless you are deliberately testing cabinet hardware.

## The happy path

```sh
git clone https://github.com/ThomazPom/Encore-Pinball2000.git
cd Encore-Pinball2000
scripts/build-qemu.sh
scripts/run-qemu.sh --game swe1
# or:
scripts/run-qemu.sh --game rfm
```

That's it. Build once, run often.

> [!NOTE]
> The first build takes approximately 5 minutes on modern hardware. Re-running `scripts/build-qemu.sh` is safe—it refreshes Encore's `qemu/` sources inside the QEMU build tree and lets `ninja` rebuild only what changed.

## Before you build

You'll need the normal QEMU build toolchain—compiler, ninja, SDL2 headers—plus whatever QEMU's configure script wants. On Debian or Ubuntu:

> [!IMPORTANT]
> `build-qemu.sh` builds upstream QEMU from source, so you need the full QEMU build dependencies: compiler toolchain, `curl`, `patch`, `ninja`, Meson/Python, SDL2 development headers, and optional GTK3 if you want `--display gtk`.

```sh
sudo apt update
sudo apt install -y build-essential pkg-config git curl patch ninja-build \
                    python3 python3-venv libsdl2-dev libglib2.0-dev \
                    libpixman-1-dev zlib1g-dev libslirp-dev libvorbis-dev
```

Optional extras if you want GTK display or serial console convenience:

```sh
sudo apt install -y libgtk-3-dev netcat-openbsd rlwrap
```

| Package | Why |
|---|---|
| `libvorbis-dev` | Provides `vorbis/vorbisfile.h` and `libvorbisfile`, required by Encore's DCS OGG playback code. |
| `libgtk-3-dev` | Adds GTK display backend to QEMU. |
| `netcat-openbsd` | Needed by `scripts/run-qemu.sh --serial`. |
| `rlwrap` | Makes manual TCP serial sessions more comfortable (not required). |

## What the build script does

We don't vendor a QEMU fork—we download upstream QEMU (currently pinned to 10.0.8), copy our `qemu/` sources into `hw/i386/`, patch the build system to register the `pinball2000` machine, and compile `i386-softmmu`.

> [!TIP]
> The build defaults to `$HOME/.cache/p2k-qemu-build` to avoid shared-folder symlink issues. Set `P2K_QEMU_BUILD_DIR` if you need a different location.

If everything works you'll see:

```text
[build-qemu] OK: /home/you/.cache/p2k-qemu-build/qemu-10.0.8/build/qemu-system-i386
```

The first build takes about five minutes on modern hardware. Running `scripts/build-qemu.sh` again is safe—it refreshes our sources and lets ninja rebuild only what changed.

## What the run script does

The wrapper translates friendly options into QEMU flags. When you run it on a desktop it picks SDL for display, sniffs for PulseAudio or ALSA, defaults to `swe1`, looks for ROMs in `roms/`, keeps savedata in `savedata/`, and auto-discovers the newest matching update bundle. You'll see something like:

```text
[run-qemu] ... qemu-system-i386 -M pinball2000,game=swe1,roms-dir=... -no-reboot -m 16 -display sdl,...
```

The QEMU window is your playfield. Use `Space` to start, `F10` to insert a
credit and `F4` to open the coin door. See
[41 — Desktop controls](41-cli-keyboard-guide.md) for the complete table.

## Common tasks

| What you want | Command |
|---|---|
| Run Star Wars Episode I | `scripts/run-qemu.sh --game swe1` |
| Run Revenge from Mars | `scripts/run-qemu.sh --game rfm` |
| Pin to a specific update | `scripts/run-qemu.sh --game swe1 --update 0210` |
| Museum/base ROM mode | `scripts/run-qemu.sh --game swe1 --update none --no-savedata` |
| Headless serial console | `scripts/run-qemu.sh --headless --game swe1` |
| Serial in this terminal | `scripts/run-qemu.sh --game swe1 --serial` |
| Serial on TCP 4444 | `scripts/run-qemu.sh --game swe1 --serial-tcp 4444` |
| No graphics window | `scripts/run-qemu.sh --game swe1 --display none` |
| Silence audio | `scripts/run-qemu.sh --game swe1 --no-audio` |
| Timing self-diagnostic | `scripts/run-qemu.sh --bench --game swe1 --update 0200` |
| Deliberate 75% game speed | `scripts/run-qemu.sh --speed-target 75` |
| Native ADSP sound | `scripts/run-qemu.sh --dcs-engine adsp` |
| Threaded ADSP experiment | `scripts/run-qemu.sh --dcs-engine adsp-thread` |
| Preload sample audio | `scripts/run-qemu.sh --dcs-engine pb2kslib --sound-loading preload` |

> [!TIP]
> Use `--update none` for museum/base mode or `--serial` for interactive serial in your terminal.

`--bench` performs an isolated boot with the normal windowed display and audio
defaults, connects to the XINU console, wall-times `sleep 10`, and prints
current IRQ0 delivery/drift, HOTLOOP adaptive state, LPT rate, and PDB05 jitter.
Before measuring, it advances 30 seconds of guest time so boot-time interrupt
and device initialization behavior is excluded from the reported steady-state
windows. The result prints boot/warmup statistics separately, including its
worst IRQ0 and PDB05 gaps, followed by the steady-state measurements. It returns
status 2 when HOTLOOP fails to reach its established real-time range. Add
`--strict` to benchmark the natural PIT path instead. Add `--display none
--no-audio` when a deliberately headless baseline is required.

`--speed-target 25..300` deliberately scales the XINU game clock, with 100 as
the default physical-cabinet rate. Strict mode scales the i8254 divisor while
keeping PIT as the only IRQ0 source. HOTLOOP-only scales its adaptive target;
combined mode scales both PIT and HOTLOOP so HOTLOOP fills only the remaining
delivery deficit. At high strict-mode targets, PIC/TCG coalescing can make the
achieved game speed slightly lower than the requested PIT frequency; `--bench`
reports both values. Audio sample pitch is not resampled by this option.

## A few flags explained

**`--game swe1|rfm`** — Pick your game. Default is `swe1`.

**`--update auto|latest|none|0210|<dir>`** — Default is `auto`: we find the newest matching bundle under `updates/` and fall back to base ROMs if nothing's there. Use `none` for museum mode, `latest` to force the newest, or pass a version token like `2.10`:

```sh
scripts/run-qemu.sh --game swe1 --update latest
scripts/run-qemu.sh --game swe1 --update 2.10
scripts/run-qemu.sh --game swe1 --update ./updates/pin2000_50069_0210_example/50069
```

See [15-rom-loading.md](15-rom-loading.md) for the full rulebook.

**`--display sdl|gtk|none`** — Pick your QEMU display backend. Desktop defaults to SDL; servers default to `none`. If you want no graphics at all, `--display none` is what you want (paired with `--serial` so you can see *something*).

> [!WARNING]
> `--display none` means no graphics window. It is useful for CI and serial diagnostics, not for playing.

**`--serial`, `--serial-tcp`, `--uart-tcp`** — `--serial` hijacks your terminal and runs `nc` so you can interact with COM1. `--serial-tcp 4444` exposes COM1 on TCP 4444. `--uart-tcp <host:port>` is the raw form.

> [!WARNING]
> `--serial` takes over stdin/stdout. Your terminal becomes the serial console. Use `--serial-tcp` instead if you want QEMU in the background.

## If something goes wrong

Add `-v` for verbose output, then head to [04-troubleshooting.md](04-troubleshooting.md) and match your symptom.

> [!IMPORTANT]
> Use the troubleshooting guide rather than guessing. Add `-v` for verbose diagnostics.

```sh
scripts/run-qemu.sh --game swe1 -v
```

Device deep-dives live in [20-plx-pci.md](20-plx-pci.md), [23-mediagx-and-display.md](23-mediagx-and-display.md), [25-dcs-sound.md](25-dcs-sound.md), and [26-lpt-board.md](26-lpt-board.md).

## See also

* [↑ Documentation index](README.md)
* [01-overview.md](01-overview.md)
* [03-cli-reference.md](03-cli-reference.md)
* [04-troubleshooting.md](04-troubleshooting.md)
* [14-boot-recipe.md](14-boot-recipe.md)
* [15-rom-loading.md](15-rom-loading.md)
* [23-mediagx-and-display.md](23-mediagx-and-display.md)
* [25-dcs-sound.md](25-dcs-sound.md)
