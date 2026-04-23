# 02 — Quickstart

From a fresh Debian 12 / Ubuntu 24.04 install to attract mode in under
five minutes.

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## 1. Install dependencies

```sh
sudo apt update
sudo apt install -y build-essential pkg-config git \
                    libsdl2-dev libsdl2-mixer-dev unzip
```

### Unicorn engine

Encore needs **Unicorn ≥ 2.0** (we call `uc_ctl_flush_tlb`, added in 2.x).

```sh
# Try the distro package first (Debian 12, Ubuntu 24.04, Fedora 38+):
sudo apt install -y libunicorn-dev || {
  # Fallback (Ubuntu 22.04 etc. ship only 1.x or nothing) — build from source:
  sudo apt install -y cmake
  git clone --depth 1 https://github.com/unicorn-engine/unicorn.git ~/unicorn
  cmake -S ~/unicorn -B ~/unicorn/build \
        -DCMAKE_BUILD_TYPE=Release -DUNICORN_ARCH=x86
  cmake --build ~/unicorn/build -j"$(nproc)"
  sudo cmake --install ~/unicorn/build && sudo ldconfig
}
```

> **Important:** if you build from source, keep the `-DCMAKE_BUILD_TYPE=Release`.
> Without it Unicorn ships unoptimised and Encore runs 5–10× slower.

Versions known to work: gcc ≥ 10, libunicorn ≥ 2.0, libsdl2 ≥ 2.0.20,
libsdl2-mixer ≥ 2.6.

### Real-cabinet prerequisites (skip if emulator-only)

If you are wiring Encore to an actual Pinball 2000 cabinet via the host's
parallel port, do **this once** before the first run — otherwise
`--lpt-device /dev/parport0` will fail with `PPCLAIM EBUSY` /
`Permission denied` and the cabinet will not respond:

```sh
# If `sudo` says "user is not in the sudoers file" (Debian default —
# the first user is NOT a sudoer), become root with su instead. The
# block below works for both cases: a sudoer can prefix each line with
# `sudo`; a non-sudoer runs the whole `su -` block as root in one go.

su -                                       # enter root shell (Debian root pwd)
  apt install -y parport                   # usually pulled in already
  modprobe ppdev parport parport_pc        # make /dev/parport0 appear
  rmmod lp 2>/dev/null || true             # printer driver squats on the port
  usermod -aG sudo "$SUDO_USER" 2>/dev/null \
      || usermod -aG sudo "$(logname)"     # OPTIONAL: grant yourself sudo
                                           #          for the future
  usermod -aG lp   "$SUDO_USER" 2>/dev/null \
      || usermod -aG lp   "$(logname)"     # REQUIRED: /dev/parport0 access
  exit                                     # leave root shell

newgrp lp                                  # activate lp in THIS shell —
                                           #   no logout/relogin needed;
                                           #   spawns a subshell with lp
                                           #   active. Use `exit` to return
                                           #   to the parent shell. Use
                                           #   `sg lp -c './build/encore …'`
                                           #   for a one-shot run instead.
ls -l /dev/parport0                        # expect: crw-rw---- root lp
```

> Why `newgrp` instead of "log out and back in": `usermod -aG` only takes
> effect for *new* login sessions. `newgrp lp` re-execs your shell with
> the group already applied, so the very next `./build/encore` call sees
> the right credentials.

Encore runs **fully unprivileged** — no `ioperm()`, no setuid, no `/dev/port`.
Everything goes through Linux `ppdev` ioctls, so once your user is in the
`lp` group and the kernel `lp` driver is unloaded, `./build/encore` from a
normal shell is enough. Full background and troubleshooting in
[19-real-lpt-passthrough.md](19-real-lpt-passthrough.md).

## 2. Clone and build

```sh
git clone https://github.com/ThomazPom/Encore-Pinball2000.git encore
cd encore
make                                  # produces ./build/encore
```

The build is a single non-parallel invocation of `gcc` over thirteen C
sources. On a modest laptop it completes in under four seconds and
produces an 800 KB stripped binary. There are no generated files, no
autoconf, no meson, no per-distro configuration step. See
[28-build-system.md](28-build-system.md) for target-by-target detail.

> **Make it yours in 5 seconds.** Drop any JPEG into
> `assets/splash-screen.jpg` before `make` and Encore links it straight
> into the binary as your startup splash — no code, no flags, no extra
> files at runtime. End users can also point `--splash-screen PATH` at
> any image (JPEG / PNG / BMP / TGA / …) without rebuilding. See
> [49-splash-screen.md](49-splash-screen.md).

## 3. ROMs and update bundles

The repository **already ships** every chip ROM and every dearchived
update bundle Encore needs — they are committed under `./roms/` and
`./updates/`. You don't need to download or place anything for the
default games to run.

For reference, Encore needs two kinds of files per bundle:

* the chip ROMs (`U100…U107` for the game banks, `U109/U110` for DCS
  sound), which are unchanged across every update revision and are
  loaded from `./roms/`;
* exactly one update bundle, in any of four forms `--update` accepts
  (see [09-update-loader.md](09-update-loader.md)):
  * a pre-concatenated `update.bin` file,
  * a directory containing `bootdata.rom`, `im_flsh0.rom`, `game.rom`
    and `symbols.rom`,
  * a Williams-branded `.exe` installer (a renamed ZIP),
  * a **version token** like `0150`, `0180`, `1.5` or `latest`,
    resolved against the directory names under `./updates/`
    (e.g. `pin2000_50069_0150_…` ⇒ `--update 0150`).

The canonical form is the **4-digit version field** that appears in
every shipped bundle directory name (`0150`, `0180`, `0140`, …);
shorter forms like `1.5`, `150`, `1.8` are accepted and normalised to
the same `0vvv` value internally. Pass `--update none` to skip update
loading entirely (boots straight into the chip ROMs).

Only the original Williams official update bundles (1999-2003) ship
with the repo. Community/post-Williams updates by mypinballs (SWE1
v2.10, RFM v2.50, v2.60, …) are supported by Encore but not
redistributed here — see [47-community-updates.md](47-community-updates.md)
for how to install them and grab the latest versions from
<https://mypinballs.com>.

## 4. First run

```sh
./build/encore --game swe1 --update 0150
```

Expected output, abbreviated (every line below is what the binary
actually prints — copy-paste from a fresh run):

```
╔══════════════════════════════════════════════════╗
║  Encore — Pinball 2000 Emulator                 ║
║  CPU: Unicorn Engine (i386)                     ║
║  Video: SDL2 | Audio: SDL2_mixer                ║
╚══════════════════════════════════════════════════╝

[main] --update: resolved '0150' → ./updates/pin2000_50069_0150_….
[main] --update: bundle for SWE1 → forcing prefix=swe1
[init] Game: swe1 | ROMs: ./roms | Savedata: ./savedata
[rom] Bank 0 chip u100: ./roms/swe1_u100.rom
[rom] Bank 0 chip u101: ./roms/swe1_u101.rom
[rom] Bank 0: raw interleaved load complete (16 MB)
…
[cpu] Starting Encore in protected mode...
[sgc] applying minimal post-start fixes for watchdog/mem_detect/display bring-up
[sgc] watchdog scan: string at 0x…
[irq] clkint detected: IDT[0x20]=0x0022a4c8 EIP=…
[irq] XINU ready: timer injection enabled EIP=…
[disp] FPS: 60.0 (300 frames / 5000 ms)
```

An SDL window appears and renders the WMS boot logo, the XINU banner
and finally the attract-mode loop. DCS audio starts when the "BONG"
sample plays.

## 5. If something goes wrong

| Symptom | Likely cause |
|---|---|
| `Failed to load ROMs` | Chip files missing or wrong size under `./roms/`. The repo ships these — you should only see this if you replaced the directory or pointed `--roms` at an empty path. Verify U100…U107 are present and the right sizes. |
| `--update: could not resolve '0150'` | No directory named `pin2000_50069_0150_*` (or `_50070_` for RFM) under `./updates/`. Use `--update /full/path/to/bundle.bin` as a fallback, or `--update latest` to pick the highest-versioned shipped bundle for the selected game. |
| Black SDL window, no DCS activity | The default `--dcs-mode io-handled` produces audio on every shipped bundle. If you forced `--dcs-mode bar4-patch` and hit a "pattern absent" bundle (SWE1 v1.3, `--update none`), the BAR4 patch is a no-op and DCS stays silent — drop the flag to fall back to the default. |
| Exits immediately with `SDL_Init failed` | No display. Pass `--headless`; attach with `--serial-tcp 4444` and `nc localhost 4444` to see the serial console. |

See [27-troubleshooting.md](27-troubleshooting.md) for a fuller list.

## 6. Bindings during emulation

Once the SDL window is up, the most useful keys are (these go to the
emulated cabinet, not to a "play" mode — there is no separate play vs.
service mode at the keyboard layer):

* `F10` or `C` — insert a credit
* `SPACE` or `S` — press the Start button
* `F7 / F8` — left / right flippers
* `F4` — toggle the coin-door interlock
* `F1` — clean exit

The full cheat-sheet lives in [36-cli-keyboard-guide.md](36-cli-keyboard-guide.md).

## 7. Savedata

NVRAM, SEEPROM and EMS state land under `./savedata/<game_id>.*` on
clean exit (F1, SIGINT/SIGTERM). Pass `--no-savedata` to keep those
files untouched across the run — useful for CI and regression work.
Full details in [10-savedata.md](10-savedata.md).

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
