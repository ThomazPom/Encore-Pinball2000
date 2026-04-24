# 02 — Quickstart

From a fresh Debian/Ubuntu install to Pinball 2000 attract mode in
three (and a half) brief steps:

* **Install dependencies** — one `apt install` line.
* **Clone and build** — `git clone`, then `make` (under four seconds).
* **First run** — `./build/encore --game swe1 --update 0150`. ROMs and
  updates already ship in the repo.
* *(optional ½ step)* — plug in a real Pinball 2000 cabinet over LPT.

Each section below expands the matching step.

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

<details>
<summary><b>🔽 ONLY IF — <code>sudo</code> refuses you with “user is not in the sudoers file” (Debian default)</b></summary>

Debian's default install does not add the first user to the `sudo` group.
Grant yourself sudo once (root password is set at install time), then
re-run the command above:

```sh
su -c "/usr/sbin/usermod -aG sudo $(whoami)"
newgrp sudo                                    # active in THIS shell, no logout
sudo -v                                        # sanity check (asks YOUR pwd)
```
</details>

### Unicorn engine

Encore needs **Unicorn ≥ 2.0** (we call `uc_ctl_flush_tlb`, added in 2.x).
One copy-paste covers both cases — distro package if available, source
build as a transparent fallback (Ubuntu 22.04, RHEL, …):

```sh
sudo apt install -y libunicorn-dev || {
  # Fallback — build Unicorn 2.x from source:
  sudo apt install -y cmake
  git clone --depth 1 https://github.com/unicorn-engine/unicorn.git ~/unicorn
  cmake -S ~/unicorn -B ~/unicorn/build \
        -DCMAKE_BUILD_TYPE=Release -DUNICORN_ARCH=x86
  cmake --build ~/unicorn/build -j"$(nproc)"
  sudo cmake --install ~/unicorn/build && sudo ldconfig
}
```

> **Important:** if you build from source, keep `-DCMAKE_BUILD_TYPE=Release`.
> Without it Unicorn ships unoptimised and Encore runs 5–10× slower.

Versions known to work: gcc ≥ 10, libunicorn ≥ 2.0, libsdl2 ≥ 2.0.20,
libsdl2-mixer ≥ 2.6.

<details>
<summary><b>🔽 ONLY IF — you’re wiring Encore to a real Pinball 2000 cabinet (skip for emulator-only use)</b></summary>

Without these, `--lpt-device /dev/parport0` fails with `PPCLAIM EBUSY`
or `Permission denied` and the cabinet doesn't respond:

```sh
sudo modprobe ppdev parport_pc                       # make /dev/parport0 appear
sudo rmmod lp 2>/dev/null || true                    # printer driver squats on it
sudo usermod -aG lp $USER && newgrp lp               # group access, active now
ls -l /dev/parport* /dev/usb/lp* 2>/dev/null         # find your device node
```

`newgrp lp` activates the new group in the current shell so no logout is
needed (`sg lp -c './build/encore …'` works too as a one-shot). The
`ppdev`, `parport` and `parport_pc` modules ship with the stock
Debian/Ubuntu kernel — nothing to apt-install.

**USB→LPT dongle?** If `ls` shows `/dev/usb/lp0` but no `/dev/parport*`,
your adapter is printer-class only and **cannot** drive the cabinet
(the `usblp` kernel driver has no bidirectional / control-register
support). A PCIe LPT card (Moschip MCS9865/9900, ~€20) is the cheap
fix. If you see `/dev/parport1` instead of `parport0`, pass
`--lpt-device /dev/parport1`. Full compatibility table in
[19-real-lpt-passthrough.md](19-real-lpt-passthrough.md#which-device-node-will-i-get).

Encore runs **fully unprivileged** — no `ioperm()`, no setuid, no
`/dev/port`. Everything goes through Linux `ppdev` ioctls, so once
your user is in the `lp` group and the kernel `lp` driver is
unloaded, `./build/encore` from a normal shell is enough.
</details>

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

## 3. First run

```sh
./build/encore --game swe1 --update 0150
```

The repository ships every chip ROM and every Williams update bundle
needed for SWE1 and RFM, so this command works straight after `make`
with no downloads or paths to configure (full inventory in **§ 4**).

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

## 4. ROMs and update bundles (reference — already shipped)

Skip this section unless you want to load a different bundle (community
update, your own dump, etc.). For the default games, everything is
already in place under `./roms/` and `./updates/`.

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
