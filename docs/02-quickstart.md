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
sudo apt install -y \
     build-essential pkg-config git \
     libsdl2-dev libsdl2-mixer-dev \
     libunicorn-dev unzip
```

Versions known to work:

| Package         | Minimum | Tested |
|-----------------|--------:|-------:|
| gcc             |     10  |   12.2 |
| libunicorn-dev  |   2.0   |    2.0 |
| libsdl2-dev     |   2.0.20|  2.26  |
| libsdl2-mixer-dev | 2.6   |   2.6  |

Older Unicorn releases (1.x) do not implement `uc_ctl_flush_tlb`, which
Encore calls every 64 execution batches to keep DC_TIMING2 visible to
the VSYNC poll. Upgrade if your distro still ships 1.x.

## 2. Clone and build

```sh
git clone <your-mirror>/encore-pinball2000 encore
cd encore
make                                  # produces ./build/encore
```

The build is a single non-parallel invocation of `gcc` over twelve C
sources. On a modest laptop it completes in under four seconds and
produces an 800 KB stripped binary. There are no generated files, no
autoconf, no meson, no per-distro configuration step. See
[28-build-system.md](28-build-system.md) for target-by-target detail.

## 3. Get ROM files

Encore needs two kinds of files for each bundle:

* the chip ROMs (`U100…U107` for the game banks, `U109/U110` for DCS
  sound), which are unchanged across every update revision;
* exactly one update bundle, in any of four forms Encore accepts (see
  [09-update-loader.md](09-update-loader.md)):
  * a pre-concatenated `update.bin` file,
  * a directory containing `bootdata.rom`, `im_flsh0.rom`, `game.rom`
    and `symbols.rom`,
  * a Williams-branded `.exe` installer (a renamed ZIP),
  * a version token like `2.1` or `210`, resolved against `./updates/`.

Place the chip ROMs under `./roms/` in the layout documented in
[08-rom-loading-pipeline.md](08-rom-loading-pipeline.md); place the
update bundles under `./updates/`. The repository ships with empty
placeholders for both directories.

## 4. First run

```sh
./build/encore --game swe1 --update 2.1
```

Expected output, abbreviated:

```
╔══════════════════════════════════════════════════╗
║  Encore — Pinball 2000 Emulator                 ║
║  CPU: Unicorn Engine (i386)                     ║
║  Video: SDL2 | Audio: SDL2_mixer                ║
╚══════════════════════════════════════════════════╝

[main] --update: resolved '2.1' → ./updates/pin2000_50069_0210_….
[main] --update: detected SWE1 (game_id=50069) → forcing prefix=swe1
[init] Game: swe1 | ROMs: ./roms | Savedata: ./savedata
[rom] Bank 0: 8 chip files found, interleaving…
[rom] Save data ID: swe1_21
[cpu] Starting Encore in protected mode...
[sgc] applying minimal post-start fixes for watchdog/mem_detect/…
[sgc] watchdog scan: string at 0x…
[irq] clkint detected: IDT[0x20]=0x0022a4c8 EIP=…
[irq] XINU ready: timer injection enabled EIP=…
[init] DCS-mode pattern hit @0x001931e4 slot=0x0034a714 — patched
[disp] FPS: 60.0 (300 frames / 5000 ms)
```

An SDL window appears and renders the WMS boot logo, the XINU banner
and finally the attract-mode loop. DCS audio starts when the "BONG"
sample plays.

## 5. If something goes wrong

| Symptom | Likely cause |
|---|---|
| `Failed to load ROMs` | Missing chip files under `./roms/`. Verify U100…U107 are present and the correct sizes. |
| `--update: could not resolve '2.1'` | No directory named `pin2000_50069_0210_*` under `./updates/`. Use `--update /full/path/to/bundle.bin` as a fallback. |
| Black SDL window, no DCS activity | `--dcs-mode bar4-patch` is the default and required for most bundles. If you forced `--dcs-mode io-handled` this is expected on every bundle except SWE1 v1.5. |
| Exits immediately with `SDL_Init failed` | No display. Pass `--headless`; attach with `--serial-tcp 4444` and `nc localhost 4444` to see the serial console. |

See [27-troubleshooting.md](27-troubleshooting.md) for a fuller list.

## 6. Bindings during play

Once the window is up, the most useful keys are:

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
