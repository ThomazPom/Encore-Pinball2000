# 41 — Build, environment and runtime knobs

This document covers everything you need to actually *build* and *run*
Encore in different configurations: prerequisites, build flavours,
parallel builds, environment variables read by the binary or by the
SDL stack underneath it, runtime keyboard shortcuts, and the optional
features (LPT passthrough, network bridges, headless mode).

If you only want a one-liner build, see
[02-quickstart.md](02-quickstart.md). This doc is for everything else.

---

## 1. Build prerequisites

Reference platform: Debian 12 (bookworm) and Ubuntu 24.04 (noble).
Other modern Linux distributions work as long as they ship the same
package versions (or newer).

| Package | Version (minimum) | Purpose |
|---|---|---|
| `build-essential` | gcc 10+ | C compiler, make, headers |
| `pkg-config`      | any | flag discovery for SDL2 |
| `libsdl2-dev`     | 2.0.20+ | display, input, window management |
| `libsdl2-mixer-dev` | 2.0.4+ | DCS sample playback backend |
| `libunicorn-dev`  | 2.0+ | guest CPU emulation (i386 mode) |

Install on Debian/Ubuntu:

```sh
sudo apt install -y build-essential pkg-config \
                    libsdl2-dev libsdl2-mixer-dev \
                    libunicorn-dev
```

On Arch / Manjaro:

```sh
sudo pacman -S --needed base-devel pkgconf sdl2 sdl2_mixer unicorn
```

On Fedora:

```sh
sudo dnf install -y @development-tools pkgconfig \
                    SDL2-devel SDL2_mixer-devel unicorn-devel
```

Encore does not depend on any other libraries at runtime — no Python,
no Lua, no Wine, no QEMU.

---

## 2. Building

```sh
make           # build everything (default target = all)
make clean     # delete build/ entirely
make -jN       # parallel build using N jobs
```

`make -j$(nproc)` works correctly: every `.o` target is independent.
On a four-core machine the build typically finishes in well under
two seconds. The link step naturally serialises.

The output is a single ELF binary at `build/encore` (~2 MB with debug
symbols, ~800 KB stripped).

### 2.1 Build flavours

Encore's Makefile is intentionally minimal — there is no `./configure`
step and no auto-detection. To change build flavour, override
variables on the `make` command line:

```sh
# Strict release build (no asserts, no debug symbols)
make CFLAGS="-O3 -DNDEBUG -Wall -Wextra -Iinclude $(pkg-config --cflags sdl2 SDL2_mixer)"

# Debug build with sanitizers
make CFLAGS="-O0 -g -fsanitize=address,undefined -Wall -Wextra -Iinclude $(pkg-config --cflags sdl2 SDL2_mixer)" \
     LDFLAGS="-fsanitize=address,undefined $(pkg-config --libs sdl2 SDL2_mixer) -lunicorn -lm -lpthread"

# Use clang instead of gcc
make CC=clang
```

The default is `-O2 -g`: optimised code with full debug symbols. `gdb`
works without rebuilding.

### 2.2 Reproducible builds

The build is reproducible byte-for-byte on the same compiler version
because there are no generated files, no timestamps embedded, and no
optional code paths gated on host features.

If you need bit-identical binaries across machines, pin the toolchain
(`gcc-12`, `pkg-config 0.29`, etc.) and build inside a container.

### 2.3 Stripped binary for distribution

```sh
make
strip build/encore
```

Strips debug info; halves the binary size. Recommended only for
distribution; keep the unstripped binary for development.

---

## 3. Environment variables

Encore itself reads exactly **one** environment variable directly:

| Variable | Read by | Effect if unset |
|---|---|---|
| `DISPLAY` | `src/display.c` | Encore refuses to open a window unless either `DISPLAY` is set OR `--headless` is passed. This protects against opening a useless window over SSH. |
| `ENCORE_SCREENSHOT_DIR` | `src/display.c` (`screenshot_dir()`) | Override the directory where F3 captures are written. Defaults to `./screenshots/` in the CWD. The directory is created on first capture. |

Everything else flows through the SDL2 stack and is documented by
SDL itself. The most useful ones for Encore are:

| Variable | Effect |
|---|---|
| `SDL_VIDEODRIVER=dummy` | Disable graphics output entirely. Equivalent to `--headless` from SDL's point of view. Useful in CI. |
| `SDL_AUDIODRIVER=dummy` | Disable audio output. Combine with `SDL_VIDEODRIVER=dummy` for fully silent CI smoke tests. |
| `SDL_AUDIODRIVER=alsa` / `pulse` / `pipewire` | Pin the audio backend. Needed on systems where SDL's auto-detection picks the wrong one. |
| `SDL_VIDEODRIVER=wayland` / `x11` | Pin the video backend on hybrid systems. Wayland works but X11 has had more testing. |
| `SDL_RENDER_DRIVER=opengl` / `software` | Pick the SDL renderer. `opengl` is the default on most Linux setups. |

Example: silent headless run for CI / smoke testing:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    timeout 10 ./build/encore --update 210 --headless
```

### Scripted screenshot capture (offscreen Xvfb / nested X)

Encore has no `--screenshot-at-frame` flag — `F3` is the only way to
emit a PNG (see [4. Runtime keyboard shortcuts](#4-runtime-keyboard-shortcuts)
and the `save_screenshot()` path in `src/display.c`). To capture
attract / gameplay screens unattended (CI, doc generation, asset
refresh) the recipe is:

1. Render Encore against a real or virtual X display.
2. Inject `F3` keypresses from a sidecar process.
3. Read the PNGs back from `$ENCORE_SCREENSHOT_DIR` (or
   `./screenshots/`).

Headless box / CI? Use `Xvfb` as the offscreen display:

```sh
Xvfb :99 -screen 0 640x480x24 &        # offscreen X server
export DISPLAY=:99
export ENCORE_SCREENSHOT_DIR=./out
./build/encore --game rfm --update latest &
ENCORE_PID=$!
sleep 8                                 # let it reach attract
python3 tools/snap_attract.py Encore 6 4   # 6 captures, 4 s apart
sleep 2
kill $ENCORE_PID
```

On a developer workstation with a real X server, drop the `Xvfb`
line and `export DISPLAY=:0` instead. `tools/snap_attract.py` is a
~50-line `python-xlib` helper that walks the X tree, finds the first
window whose `WM_NAME` contains a substring (default `Encore`),
focuses it, and synthesises the requested number of `F3` presses
spaced by the requested interval. It depends on the `python3-xlib`
package (`pip install --break-system-packages python-xlib`, or
`apt install python3-xlib`).

This approach was used to refresh `docs/images/{swe1,rfm}-attract.png`
after the community-update purge — see
[47-community-updates.md](47-community-updates.md) for context.

---

## 4. Runtime keyboard shortcuts

These are the in-window shortcuts handled by `src/display.c`. None of
them have command-line equivalents; they are designed for interactive
use during a play session.

| Key | Action |
|---|---|
| `F1` | Quit Encore (clean shutdown) |
| `F2` | Toggle vertical-flip of the framebuffer (debug aid) |
| `F3` | Save a timestamped PNG screenshot to the hardcoded `SCREENSHOT_DIR` path (see `src/display.c`) |
| `F4` | Toggle the cabinet coin-door interlock (game-side effect) |
| `F10` or `c` | Queue a coin pulse (insert credit) |
| `F11` | Toggle fullscreen / windowed |
| `F12` | Dump the guest's view of the LPT switch matrix to stderr |
| `Alt+Enter` | Alternative fullscreen toggle (canonical emulator hotkey) |
| `[` / `]` | Move the LPT probe column (advanced, see [18-lpt-emulation.md](18-lpt-emulation.md)) |

When raw keyboard capture is enabled (e.g. for typing into a guest-OS
shell), most of these are suppressed; only `F1` (quit) survives so you
can always escape.

`Space`, `Enter`, `s` and the F-keys are also logged to stderr at
keydown time so you can confirm which scancodes actually reach SDL —
some window managers silently consume `F11`.

---

## 5. Optional runtime features

### 5.1 Headless mode

```sh
./build/encore --headless --update 210
```

No window, no audio device opened. The CPU and emulation pipeline run
normally; useful for:

* CI smoke testing
* boot benchmarks (`exec_count` is logged periodically)
* fuzzing / crash reproduction without graphics

If `DISPLAY` is unset and you forget `--headless`, Encore exits with a
clear error rather than crash inside SDL.

### 5.2 LPT passthrough (real cabinet only)

```sh
./build/encore --lpt-device /dev/parport0 --update 210
```

Forwards the guest's parallel-port traffic to a real `ppdev` device.
Requires:

* a host with an actual parallel port (or a USB-LPT adapter exposed
  via `ppdev`);
* the `ppdev` kernel module loaded (`sudo modprobe ppdev`);
* read/write permission on `/dev/parportN` (typically the user must
  be in group `lp`).

Encore auto-detects which game (SWE1 vs RFM) to drive based on the
loaded ROMs; the LPT path then maps the cabinet's switch matrix and
coil/lamp drive correctly without further configuration.

This is the path that **needs real hardware to validate**. See
[42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
how to help.

### 5.3 Network console / keyboard injection

```sh
./build/encore --serial-tcp 12345 --keyboard-tcp 12346 --update 210
```

Opens TCP listeners that bridge into the guest's COM1 serial port and
PS/2 keyboard controller respectively. Useful for:

* attaching a remote serial debugger to the XINA OS;
* automating keypresses from a test harness without touching SDL;
* feeding the guest typed input from a terminal on another machine.

See [05-architecture.md](05-architecture.md) and
[16-irq-pic.md](16-irq-pic.md) for the wiring.

### 5.4 Save-data and NVRAM

By default Encore loads the guest's NVRAM/SEEPROM from a host file
under `./savedata/` (created on first run). To start each session
clean:

```sh
./build/encore --no-savedata --update 210
```

Useful for reproducing first-boot behaviour and for crash bisection.

---

## 6. Config YAML

`--config FILE` reads a small subset of options from a YAML file.
Every CLI flag has a YAML equivalent. See
[04-config-yaml.md](04-config-yaml.md) for the schema and
precedence rules (CLI > YAML > defaults).

---

## 7. Troubleshooting build & runtime

| Symptom | Likely cause | Fix |
|---|---|---|
| `pkg-config: command not found` | `pkg-config` not installed | `apt install pkg-config` |
| `SDL.h: No such file` | `libsdl2-dev` missing | `apt install libsdl2-dev libsdl2-mixer-dev` |
| `cannot find -lunicorn` | `libunicorn-dev` missing | `apt install libunicorn-dev` |
| Window opens then immediately closes | `DISPLAY` unset over SSH | export `DISPLAY=:0` or pass `--headless` |
| No sound, no error | wrong SDL audio backend | `SDL_AUDIODRIVER=alsa` or `pulse` |
| `Permission denied: /dev/parport0` | user not in `lp` group | `sudo usermod -aG lp $USER`, log out/in |
| `make` warning about `-Wstringop-truncation` in `main.c` | Known cosmetic warning | Harmless; the buffer is intentionally bounded |
| Build fails with sanitizer link errors | sanitizer libs missing | `apt install libasan6 libubsan1` |

For runtime issues see [27-troubleshooting.md](27-troubleshooting.md).

---

## 8. Cross-references

* Step-by-step first build: [02-quickstart.md](02-quickstart.md)
* Makefile internals: [28-build-system.md](28-build-system.md)
* Every CLI flag: [03-cli-reference.md](03-cli-reference.md)
* Keyboard shortcut reference: [36-cli-keyboard-guide.md](36-cli-keyboard-guide.md)
* LPT passthrough deep-dive: [19-real-lpt-passthrough.md](19-real-lpt-passthrough.md)
* Cabinet-testing call to action: [42-cabinet-testing-call.md](42-cabinet-testing-call.md)

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
