# 04 — Troubleshooting

When things go wrong, this guide walks you through the most common symptoms and their fixes. We've organized it by what you see—not by which subsystem is misbehaving—because "my screen is black" is more helpful than "MediaGX display controller issues."

> **Status:** Behaviour described here is based on emulator testing only. Real-cabinet validation is pending. If you are testing cabinet paths, keep notes and compare with [26-lpt-board.md](26-lpt-board.md).

## Quick triage

Start with `-v` and try isolating the display from everything else:

> [!TIP]
> Start with `-v` for verbose diagnostics. Use `--display none --serial-tcp 4444` to isolate display issues.

```sh
scripts/build-qemu.sh
scripts/run-qemu.sh --game swe1 -v
scripts/run-qemu.sh --game swe1 --display none --serial-tcp 4444
```

| Quick check | Command |
|---|---|
| Did the machine build? | `~/.cache/p2k-qemu-build/qemu-10.0.8/build/qemu-system-i386 -M help \| grep -i pinball` |
| Display or emulator issue? | Try `--display none --serial-tcp 4444` and `nc 127.0.0.1 4444`. |
| Serial noise? | Try `--uart-quiet`, `--uart-drop`, or `--uart-no-filter`. |
| ROM/update layout wrong? | Read [15-rom-loading.md](15-rom-loading.md). |

---

## Build failed: missing dependencies

**What you see:** `scripts/build-qemu.sh` stops with missing headers, Meson, SDL, GLib, pixman errors.

**What's happening:** We build upstream QEMU from source, so you need QEMU's full build toolchain plus SDL2 headers.

> [!IMPORTANT]
> The script builds upstream QEMU, not a standalone C file. It needs QEMU's full build dependencies plus SDL2 headers.

**Fix:**

```sh
sudo apt update
sudo apt install -y build-essential pkg-config git curl patch ninja-build \
                    python3 python3-venv libsdl2-dev libglib2.0-dev \
                    libpixman-1-dev zlib1g-dev libslirp-dev
scripts/build-qemu.sh
```

For GTK display backend:

```sh
sudo apt install -y libgtk-3-dev
scripts/build-qemu.sh
```

See [23-mediagx-and-display.md](23-mediagx-and-display.md) for display details.

---

## Build fails with weird symlink errors

**What you see:** QEMU extraction or build fails with symlink or filesystem oddities.

**What's happening:** Shared folders (vmhgfs, vboxsf) often don't support the symlinks QEMU's tarball uses. The build script defaults to `$HOME/.cache/p2k-qemu-build` to avoid this, but if you've overridden it you might be hitting a bad filesystem.

> [!WARNING]
> Shared folders such as vmhgfs/vboxsf may not support symlinks the QEMU tarball relies on. Keep `P2K_QEMU_BUILD_DIR` on a real Linux filesystem.

**Fix:**

```sh
P2K_QEMU_BUILD_DIR=$HOME/.cache/p2k-qemu-build scripts/build-qemu.sh
```

---

## QEMU says "unknown machine type"

**What you see:** QEMU reports unknown machine, or `-M help` doesn't list `pinball2000`.

**What's happening:** The wrapper found a system QEMU that wasn't built by our script, or the build didn't finish.

**Fix:** Rebuild and point the wrapper at your custom binary:

```sh
scripts/build-qemu.sh
QEMU_BIN=$HOME/.cache/p2k-qemu-build/qemu-10.0.8/build/qemu-system-i386 \
  scripts/run-qemu.sh --game swe1
```

See [10-architecture.md](10-architecture.md) and [11-machine-init.md](11-machine-init.md) for architecture details.

---

## "Missing ROM chip" errors

**What you see:** QEMU exits with messages like `missing ROM chip swe1_u100.{rom,bin}`.

**What's happening:** We need bank0 chips (`u100` and `u101`) to boot. Extra banks and DCS ROMs are best-effort, but bank0 is mandatory.

> [!IMPORTANT]
> Bank0 chips (`u100`/`u101`) are mandatory. Extra banks and DCS ROM are best-effort. Missing bank0 aborts machine init.

**Fix:** Point `--roms` at the directory with your chip files:

```sh
scripts/run-qemu.sh --game swe1 --roms ./roms
scripts/run-qemu.sh --game rfm  --roms /data/p2k/roms
```

See [15-rom-loading.md](15-rom-loading.md) and [13-memory-map.md](13-memory-map.md) for loading rules.

---

## Update version token doesn't resolve

**What you see:** `--update '2.10' did not resolve to a bundle dir`.

**What's happening:** The wrapper matches version tokens against `updates/pin2000_<gid>_<vvvv>_*/<gid>/` where `gid` is `50069` for `swe1` and `50070` for `rfm`. If there's no matching directory, it can't resolve.

**Fix:** Use `latest`, `auto`, or pass an explicit bundle directory:

```sh
scripts/run-qemu.sh --game swe1 --update latest
scripts/run-qemu.sh --game swe1 --update auto
scripts/run-qemu.sh --game swe1 --update /data/p2k/pin2000_50069_0210_x/50069
```

See [15-rom-loading.md](15-rom-loading.md).

---

## Serial port already in use

**What you see:** `--serial-tcp 4444` fails because the TCP port is busy.

**What's happening:** Another process already owns that port.

**Fix:** Pick a different port or stop the old process. Or use `--serial` which picks an ephemeral port for you:

```sh
scripts/run-qemu.sh --game swe1 --serial-tcp 4445
scripts/run-qemu.sh --game swe1 --serial
```

> [!NOTE]
> `--serial` requires `nc`; install `netcat-openbsd` if the wrapper says it cannot find `nc`.

Serial details: [27-isa-stubs.md](27-isa-stubs.md).

---

## "nc is missing" when using --serial

**What you see:** `[run-qemu] --serial needs 'nc' (apt install netcat-openbsd)`.

**What's happening:** The convenience path launches QEMU in the background, then runs `nc` so you can interact with COM1 from your terminal.

**Fix:**

```sh
sudo apt install -y netcat-openbsd
scripts/run-qemu.sh --game swe1 --serial
```

Or use `--serial-tcp` and connect with whatever TCP client you like. `rlwrap nc 127.0.0.1 4444` is nice but not required.

---

## Display backend not compiled in

**What you see:** `--display: 'gtk' not compiled into this qemu binary`.

**What's happening:** The wrapper validates `--display` against what QEMU was built with. GTK support needs `gtk+-3.0` development headers at build time.

**Fix:** Use an available backend, or install the dev package and rebuild:

```sh
scripts/run-qemu.sh --game swe1 --display sdl
sudo apt install -y libgtk-3-dev
scripts/build-qemu.sh
scripts/run-qemu.sh --game swe1 --display gtk
```

Display details: [23-mediagx-and-display.md](23-mediagx-and-display.md) and [24-vsync.md](24-vsync.md).

---

## Mouse disappears or gets grabbed

**What you see:** Clicking the QEMU window captures the pointer or hides the cursor.

**What's happening:** The wrapper auto-appends `show-cursor=on,grab-on-hover=off` for SDL and GTK unless you pass a custom display string that already has those options.

**Fix:** Just use the wrapper defaults, or include the options yourself:

```sh
scripts/run-qemu.sh --game swe1 --display sdl
scripts/run-qemu.sh --game swe1 --display sdl,show-cursor=on,grab-on-hover=off
```

If you're already grabbed, `Ctrl+Alt+G` usually releases. `Ctrl+Alt+F` toggles fullscreen.

---

## No graphics window appears

**What you see:** The game seems to start, but you never see a window.

**What's happening:** You either asked for no graphics (`--display none` or `--headless`), or the wrapper defaulted to `none` because there's no `DISPLAY` or `WAYLAND_DISPLAY` set.

> [!WARNING]
> `--display none` is working as designed: it means no graphics.

**Fix:** On a desktop, force SDL. On a server, keep `none` and use serial:

```sh
scripts/run-qemu.sh --game swe1 --display sdl
scripts/run-qemu.sh --game swe1 --display none --serial-tcp 4444
nc 127.0.0.1 4444
```

---

## Audio crackles, fails, or is unwanted

**What you see:** DCS audio crackles, the host backend errors, or you're running in CI and don't want sound.

**What's happening:** By default the wrapper tries `pa`, `alsa`, `sdl`, `oss`, `sndio`, then `dbus`, but only selects a backend present in QEMU's compiled-in audio driver list. `pa` also requires `pactl info`; `alsa` requires a non-empty `/proc/asound/cards`; later backends rely on QEMU to initialize. If nothing matches, it falls back to `none` and warns. Explicit `--audio <backend>` is validated against QEMU's compiled-in audio drivers and fails fast if unsupported.

> [!TIP]
> Try `--sound-loading preload` to avoid first-trigger hitches, use `--audio alsa` if PulseAudio is problematic, or use `--audio none` to suppress auto-detect warnings in silent environments.

**Fix:**

```sh
scripts/run-qemu.sh --game swe1 --audio alsa
scripts/run-qemu.sh --game swe1 --sound-loading preload
scripts/run-qemu.sh --game swe1 --no-audio
```

DCS internals: [25-dcs-sound.md](25-dcs-sound.md).

---

## Serial console full of debug spam

**What you see:** UART output dominated by `swd Debug:` lines.

**What's happening:** The wrapper's default UART filter drops `swd Debug:`. If you used `--uart-no-filter` or changed the filters, the spam returns.

> [!TIP]
> The wrapper's default UART filter drops `swd Debug:`. Use `--uart-drop` to add custom filters or `--uart-quiet` to silence UART entirely.

**Fix:**

```sh
scripts/run-qemu.sh --game swe1 --serial-tcp 4444
scripts/run-qemu.sh --game swe1 --uart-drop "swd Debug:" --serial-tcp 4444
scripts/run-qemu.sh --game swe1 --uart-quiet
```

For raw debugging:

```sh
scripts/run-qemu.sh --game swe1 --uart-no-filter --serial-tcp 4444
```

Related: [27-isa-stubs.md](27-isa-stubs.md) and [26-lpt-board.md](26-lpt-board.md).

---

## Can't open parallel port device

**What you see:** `--lpt-device /dev/parport0` fails with "does not exist," "permission denied," or "device is busy."

**What's happening:** Real parport passthrough uses Linux ppdev. The device node must exist, ppdev must be loaded, you usually need `lp` group membership, and another driver might already own the port.

> [!CAUTION]
> For emulator-only use, stay with the default `emu`. Real parport passthrough is for hardware testing only and requires ppdev, permissions, and an available port.

**Fix:** For desktop use, keep the default emulated LPT. For hardware testing:

```sh
scripts/run-qemu.sh --game swe1 --lpt-device emu
sudo modprobe ppdev parport_pc
sudo usermod -aG lp "$USER"
newgrp lp
scripts/run-qemu.sh --game swe1 --lpt-device /dev/parport0
```

> [!WARNING]
> `--lpt-device none` disables LPT installation and the game will not boot; it is diagnostic only.

See [26-lpt-board.md](26-lpt-board.md).

---

## Cabinet-purist mode refuses to start

**What you see:** `[run-qemu] --cabinet-purist requires --lpt-device <hostdev>`.

**What's happening:** Cabinet-purist means "no desktop switch-matrix fallback; require the real driver board." The wrapper catches accidental use without a real parport device.

**Fix:** Either remove `--cabinet-purist` or pair it with a real ppdev device:

```sh
scripts/run-qemu.sh --game swe1
scripts/run-qemu.sh --game swe1 --cabinet-purist --lpt-device /dev/parport0
```

---

## Boot problems that appear only in certain update modes

**What you see:** The game boots fine with one update but fails with another, or only fails in base/museum mode, or breaks after disabling an environment-gated patch.

**What's happening:** A few files have `STATUS:` blocks marking them as temporary compatibility shims while we develop fuller device models. These patches are intentional and documented.

> [!WARNING]
> Some files at HEAD are marked with `STATUS:` blocks and sunset criteria. These are intentional compatibility shims while fuller QEMU device behavior is developed. Do not cargo-cult environment overrides.

| Source file | Symptom | Next step |
|---|---|---|
| `qemu/p2k-pci.c` | PCI probing or PLX discovery oddities | [20-plx-pci.md](20-plx-pci.md), [30-symptom-patches.md](30-symptom-patches.md) |
| `qemu/p2k-mem-detect.c` | Boot/memory detection failures | [33-mem-detect.md](33-mem-detect.md), [14-boot-recipe.md](14-boot-recipe.md) |
| `qemu/p2k-gp-blt.c` | Graphics blitter status reads | [23-mediagx-and-display.md](23-mediagx-and-display.md) |

**Fix:** Don't cargo-cult environment overrides. Reproduce with `-v`, note your game/update, and read the relevant architecture page before filing a bug.

## See also

* [↑ Documentation index](README.md)
* [02-quickstart.md](02-quickstart.md)
* [03-cli-reference.md](03-cli-reference.md)
* [10-architecture.md](10-architecture.md)
* [11-machine-init.md](11-machine-init.md)
* [12-cpu-and-timers.md](12-cpu-and-timers.md)
* [13-memory-map.md](13-memory-map.md)
* [14-boot-recipe.md](14-boot-recipe.md)
* [15-rom-loading.md](15-rom-loading.md)
* [20-plx-pci.md](20-plx-pci.md)
* [21-flash-bar3.md](21-flash-bar3.md)
* [22-sram-bar2.md](22-sram-bar2.md)
* [23-mediagx-and-display.md](23-mediagx-and-display.md)
* [24-vsync.md](24-vsync.md)
* [25-dcs-sound.md](25-dcs-sound.md)
* [26-lpt-board.md](26-lpt-board.md)
* [27-isa-stubs.md](27-isa-stubs.md)
* [30-symptom-patches.md](30-symptom-patches.md)
* [31-mediagx-gate.md](31-mediagx-gate.md)
* [31-mediagx-gate.md](31-mediagx-gate.md)
* [33-mem-detect.md](33-mem-detect.md)
* [34-probe-cell-shim.md](34-probe-cell-shim.md)
* [36-roadmap.md](36-roadmap.md)
