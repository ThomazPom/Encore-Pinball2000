# 03 — CLI Reference

This doc covers every user-facing option accepted by `scripts/run-qemu.sh` and `scripts/build-qemu.sh` at HEAD, with synopsis, semantics, defaults, and examples.

> **Status:** Behaviour described here is based on emulator testing only. Real-cabinet validation is pending; cabinet and parport flags are documented because the wrapper exposes them, not because they are cabinet-certified.

## `scripts/run-qemu.sh` synopsis

```sh
scripts/run-qemu.sh [OPTIONS] [-- <qemu passthrough>]
```

Runs Williams Pinball 2000 firmware under the custom QEMU `pinball2000` machine. Stock `qemu-system-i386` cannot boot this machine unless it was built with Encore's `qemu/` sources.

## Run wrapper summary

| Flag | Argument | Default | Semantics | Example |
|---|---:|---|---|---|
| `--game` | `swe1` \| `rfm` | `swe1` | Selects game ROM/update family and passes `game=` to the machine. | `scripts/run-qemu.sh --game rfm` |
| `--roms` | directory | `<repo>/roms` | Directory containing `<game>_u100.rom`/`.bin` etc.; passed as `roms-dir=`. | `scripts/run-qemu.sh --roms /data/p2k/roms` |
| `--savedata` | directory | `<repo>/savedata` | Uses this directory as persistent `savedata/` by running QEMU from a temporary cwd with a symlink. | `scripts/run-qemu.sh --savedata ./my-save` |
| `--no-savedata` | — | off | Exports `P2K_NO_SAVEDATA=1` and runs from a fresh throwaway cwd with no `savedata/` subdir; savedata seeds are skipped and exit writes are discarded. | `scripts/run-qemu.sh --no-savedata` |
| `--update` | spec | `auto` | Selects update bundle: `auto`, `latest`, `none`, version token, or explicit inner bundle dir. | `scripts/run-qemu.sh --update 0210` |
| `--display` | backend | `sdl` if desktop, else `none` | Validates against `qemu-system-i386 -display help` and passes QEMU `-display`. | `scripts/run-qemu.sh --display gtk` |
| `--headless` | — | off | Shortcut for display `none` plus serial stdio unless `--uart-quiet` is used. Promotes verbosity to at least `-v`. | `scripts/run-qemu.sh --headless --game swe1` |
| `--fullscreen` | — | off | Adds QEMU `-full-screen`; ignored with `--display none`. | `scripts/run-qemu.sh --fullscreen` |
| `--bpp` | `16` \| `32` | `32` | `16` exports `P2K_DISPLAY_BPP=16` for native x1r5g5b5; `32` keeps ARGB8888 path. | `scripts/run-qemu.sh --bpp 16` |
| `--audio` | `auto` \| `none` \| QEMU audio driver | `auto` | Autodetects the first QEMU-supported/host-available backend in order: `pa`, `alsa`, `sdl`, `oss`, `sndio`, `dbus`; falls back to `none` with a warning. Explicit backends are validated against QEMU `-audio help`. | `scripts/run-qemu.sh --audio alsa` |
| `--no-audio` | — | off | Forces DCS audio off; overrides `--audio`. | `scripts/run-qemu.sh --no-audio` |
| `--speed-target` | percent | `100` | Deliberate XINU game-clock speed from 25 through 300. Scales PIT and/or HOTLOOP according to timing mode. | `scripts/run-qemu.sh --speed-target 75` |
| `--strict` | — | off | Disables HOTLOOP and uses the natural i8254/i8259 IRQ0 path. Intended for diagnostic comparison. | `scripts/run-qemu.sh --strict` |
| `--with-pit` | — | off | Runs adaptive HOTLOOP together with the natural PIT. Retained for controlled timing tests. | `scripts/run-qemu.sh --with-pit` |
| `--bench` | — | off | Runs the graphical steady-state self-diagnostic and reports guest clock, IRQ0, jitter, LPT rate and PDB05 gaps. | `scripts/run-qemu.sh --bench` |

> [!TIP]
> Use `--audio auto` to let the wrapper choose the first backend supported by both the QEMU build and host checks; use `--audio none` to run silent without the auto-detect warning, or `--sound-loading preload` to avoid first-trigger audio hitches.
| `--pb2kslib` | path | `<roms>/<game>_sound.bin` lookup in machine | Exports `P2K_PB2KSLIB` to override the pb2kslib container. | `scripts/run-qemu.sh --pb2kslib ./roms/swe1_sound.bin` |
| `--dcs-engine` | `pb2kslib` \| `adsp` \| `adsp-thread` | `pb2kslib` | Selects sample playback, synchronous native ADSP, or the condition-driven ADSP mailbox worker. | `scripts/run-qemu.sh --dcs-engine adsp` |
| `--dcs-sound-flash` | path | auto from selected update or ROM directory | Selects the 1 MiB sound-flash image used by a native ADSP engine. | `scripts/run-qemu.sh --dcs-engine adsp --dcs-sound-flash ./roms/swe1_28f800.rom` |
| `--sound-loading` | `lazy` \| `preload` | `lazy` | Lazy decodes samples on first use; preload exports `P2K_DCS_PRELOAD=1`. | `scripts/run-qemu.sh --sound-loading preload` |
| `--serial` | — | off | Interactive COM1 in current terminal via temporary TCP UART plus foreground `nc`; requires `nc`; mutually exclusive with `--serial-tcp`, `--uart-tcp`, `--headless`. | `scripts/run-qemu.sh --serial` |
| `--uart-quiet` | — | off | Silences COM1/UART stderr mirror and uses `-serial null` in headless mode. Wins over `-v`. | `scripts/run-qemu.sh --uart-quiet` |

> [!WARNING]
> `--serial` blocks your terminal. Use `--serial-tcp` if you want QEMU in the background.
| `--uart-drop` | substring | drops `swd Debug:` by default | Repeatable line filter for UART output before stdout/TCP/stderr. | `scripts/run-qemu.sh --uart-drop NonFatal` |
| `--uart-no-filter` | — | off | Exports empty `P2K_UART_DROP`, disabling the default `swd Debug:` filter and custom drops. | `scripts/run-qemu.sh --uart-no-filter` |
| `--uart-tcp` | `host:port` | off | Binds COM1 to QEMU TCP serial server. Compatible with `--headless`. | `scripts/run-qemu.sh --uart-tcp 127.0.0.1:4444` |
| `--serial-tcp` | port | off | Alias for `--uart-tcp 127.0.0.1:<port>`; port must be numeric. | `scripts/run-qemu.sh --serial-tcp 4444` |
| `--monitor` | QEMU spec | off | Adds QEMU `-monitor <spec>`. | `scripts/run-qemu.sh --monitor stdio` |
| `--debug` | QEMU `-d` opts | off | Adds `-d <opts> -D /tmp/p2k_qemu.log`. | `scripts/run-qemu.sh --debug int,cpu_reset` |
| `--screenshot-dir` | existing directory | `/tmp` inside QEMU machine | Exports `P2K_SCREENSHOT_DIR`; F3 writes screenshots there. | `scripts/run-qemu.sh --screenshot-dir ./screens` |
| `--diag` | — | off | Exports `P2K_DIAG=1` for PIT/PIC/IDT/XINU change-only sampler. | `scripts/run-qemu.sh --diag` |
| `--trace-dcs` | — | off | Exports `P2K_DCS_BYTE_TRACE=1` for per-byte DCS UART tracing. | `scripts/run-qemu.sh --trace-dcs` |
| `--trace-audio` | — | off | Exports `P2K_DCS_AUDIO_TRACE=1` for DCS audio event/status tracing. | `scripts/run-qemu.sh --trace-audio` |
| `--trace-timing` | — | off | Alias for `--diag`; no separate timing trace exists today. | `scripts/run-qemu.sh --trace-timing` |
| `-v` | — | quiet default | Level 1: UART stderr mirror plus `P2K_DIAG=1`. | `scripts/run-qemu.sh -v` |
| `-vv` | — | quiet default | Level 2: `-v` plus audio trace. | `scripts/run-qemu.sh -vv` |
| `-vvv` | — | quiet default | Level 3: `-v` plus audio trace and DCS byte trace. | `scripts/run-qemu.sh -vvv` |
| `--dcs-mode` | `io-handled` \| `bar4-patch` | unset | Exports `P2K_DCS_MODE`; both labels use the shared BAR4 + UART core today. | `scripts/run-qemu.sh --dcs-mode io-handled` |
| `--cabinet`, `--cabinet-purist` | — | off | Exports `P2K_CABINET_PURIST=1`; wrapper refuses to start unless a real `--lpt-device <hostdev>` is supplied. | `scripts/run-qemu.sh --cabinet --lpt-device /dev/parport0` |
| `--lpt-device`, `--lpt` | `emu` \| `emulated` \| `none` \| `/dev/parportN` \| `0xNNN` | `emu` | Configures driver-board path: emulated, disabled, ppdev passthrough, or custom emulated I/O port. | `scripts/run-qemu.sh --lpt-device emu` |

> [!CAUTION]
> `--cabinet-purist` requires a real hardware driver board. Do not use with real cabinets until you have validated the emulator behavior.
| `--lpt-trace` | file | off | Exports `P2K_LPT_TRACE_FILE`; appends LPT read/write trace lines. Parent directory must exist. | `scripts/run-qemu.sh --lpt-trace ./logs/lpt.txt` |
| `--parport` | device | off | Historical alias for ppdev passthrough; device must exist. | `scripts/run-qemu.sh --parport /dev/parport0` |

> [!NOTE]
> `--parport` is an alias. Use `--lpt-device` for clarity.
| `--tcg-only` | — | off | Smoke-tests host QEMU with `-M isapc`; does not boot Pinball 2000. | `scripts/run-qemu.sh --tcg-only` |
| `--` | QEMU args | none | Forwards remaining args verbatim to QEMU. | `scripts/run-qemu.sh -- -S` |
| `-h`, `--help` | — | — | Prints the wrapper help. | `scripts/run-qemu.sh --help` |

## Compatibility flags accepted but not advertised

| Flag | Default | Semantics | Example |
|---|---|---|---|
| `--splash-screen <path>` | ignored | Accepted and dropped so old command lines do not fail. QEMU draws too quickly for the former in-window splash approach. | `scripts/run-qemu.sh --splash-screen none` |
| `--splash-time <value>` | ignored | Accepted and dropped with its argument. | `scripts/run-qemu.sh --splash-time 2` |
| `--no-splash` | ignored | Accepted and dropped. | `scripts/run-qemu.sh --no-splash` |

> [!WARNING]
> These splash flags are compatibility no-ops in the current QEMU wrapper. Do not document them in new tutorials except to explain why old commands still parse.

## Update specs

> [!IMPORTANT]
> Update selection order is: `none` if specified, explicit directory/token, then auto-discovery. Missing updates fall back to base ROMs.

| Spec | Meaning | Example |
|---|---|---|
| `auto` | Default. Leave `-M update=` unset; the QEMU machine auto-discovers in `./updates` and falls back to base ROMs. | `scripts/run-qemu.sh --update auto` |
| `latest` | Wrapper resolves the highest version directory for the selected game. | `scripts/run-qemu.sh --game rfm --update latest` |
| `none` | Museum/base mode. Exports `P2K_NO_AUTO_UPDATE=1`; no update bundle is staged. | `scripts/run-qemu.sh --update none --no-savedata` |
| `0210`, `210`, `2.10`, `2.1` | Short version token resolved against `updates/pin2000_<gid>_<vvvv>_*/<gid>/`. | `scripts/run-qemu.sh --game swe1 --update 2.10` |
| `<dir>` | Explicit path to inner bundle directory containing `*_bootdata.rom`, `*_im_flsh0.rom`, `*_game.rom`, and `*_symbols.rom`. | `scripts/run-qemu.sh --update /data/p2k/update/50069` |

> [!TIP]
> Use `--update latest` to auto-pick the newest update, or `--update none` for museum mode with base ROMs only.

## Display modes

| Mode | When to use | Notes |
|---|---|---|
| `sdl` | Normal desktop play/testing | Default when `DISPLAY` or `WAYLAND_DISPLAY` exists. Wrapper adds `show-cursor=on,grab-on-hover=off` unless already specified. |
| `gtk` | Desktop testing with GTK UI | Only if QEMU was built with GTK; install `libgtk-3-dev` before `scripts/build-qemu.sh`. |
| `none` | CI, serial-only, or diagnostics | No graphics window. Combine with `--uart-tcp`, `--serial-tcp`, or `--headless` for observability. |
| other QEMU backends | Advanced QEMU use | Accepted only if listed by `qemu-system-i386 -display help`. |

## Serial and verbosity matrix

> [!TIP]
> Add `-v` for first-line debugging with UART mirror and diagnostics. Use `--uart-quiet` for silent CI.

| Choice | QEMU serial sink | Host UART stderr mirror | Best for |
|---|---|---|---|
| default | none unless QEMU chooses one | off (`P2K_NO_UART_STDERR=1`) | Clean desktop launch. |
| `-v` | unchanged | on plus diagnostics | First-line debugging. |
| `--headless` | `stdio` | on unless `--uart-quiet` | Headless log capture. |
| `--headless --uart-quiet` | `null` | off | Silent CI smoke test. |
| `--serial` | temporary TCP, foreground `nc` | off | One-terminal XINA interaction. |
| `--serial-tcp 4444` | `tcp:127.0.0.1:4444,server=on,wait=off` | off by default; use `-v` to mirror | Two-terminal monitor session. |

## Key bindings

Delivered by the QEMU machine, not the wrapper:

| Key | Action |
|---|---|
| `F1` | Quit / shutdown request |
| `F2` | Toggle vertical flip; default is on because source is bottom-up |
| `F3` | Screenshot to `<screenshot-dir>/p2k_screen_<ts>.jpg`, with `.ppm` fallback |
| `F4` | Toggle coin door |
| `F5`, `Enter`, `KP-Enter` | ~60-frame Enter pulse |
| `F6`, `F9` | Left / right action buttons |
| `F7`, `F8` | Left / right flippers |
| `F10`, `C` | Coin slot 1 |
| `Space`, `S` | Start |
| `Esc`, `Left arrow` | Service |
| `Down`, `KP-` | Volume down |
| `Up`, `=`, `KP+` | Volume up |
| `Right arrow` | Begin test |
| `F12` | State dump |
| `Ctrl+Alt+F` | SDL fullscreen toggle |

## `scripts/build-qemu.sh` synopsis

```sh
scripts/build-qemu.sh [VERSION]
scripts/build-qemu.sh [--qemu-version VERSION] [--latest] [--unstable] [--list]
```

Builds a minimal `qemu-system-i386` with the Encore `pinball2000` machine.

| Flag / form | Default | Semantics | Example |
|---|---|---|---|
| no args | QEMU `10.0.8` | Build the pinned default version. | `scripts/build-qemu.sh` |
| positional `X.Y.Z[-rcN]` | — | Build that QEMU version. Warns if not in the known-good list. | `scripts/build-qemu.sh 10.0.8` |
| `--qemu-version`, `-V` | — | Explicit version form. | `scripts/build-qemu.sh --qemu-version 10.0.8` |
| `--latest` | newest known-good stable | Picks newest entry from the script's `KNOWN_GOOD_VERS`. | `scripts/build-qemu.sh --latest` |
| `--unstable` | off | With `--latest`, query newest tarball including `-rcN`; with `--list`, include release candidates. | `scripts/build-qemu.sh --latest --unstable` |
| `--list`, `--list-qemu-versions` | stable only | Lists versions available on the QEMU mirror, then exits. | `scripts/build-qemu.sh --list` |
| `-h`, `--help` | — | Prints the script's usage block. | `scripts/build-qemu.sh --help` |
| `--` | — | Stops option parsing; currently there are no build passthrough args after it. | `scripts/build-qemu.sh --` |
| `QEMU_VER=...` | `10.0.8` | Legacy environment override for version. | `QEMU_VER=10.0.8 scripts/build-qemu.sh` |
| `P2K_QEMU_BUILD_DIR=...` | `$HOME/.cache/p2k-qemu-build` | Build/cache root. Use a real Linux filesystem; shared folders may not support QEMU's symlinks. | `P2K_QEMU_BUILD_DIR=$HOME/p2k-build scripts/build-qemu.sh` |

> [!WARNING]
> `--unstable` may download release candidates. Only use for testing bleeding-edge QEMU versions.
| `P2K_QEMU_MIRROR=...` | `https://download.qemu.org` | Alternate QEMU tarball mirror. | `P2K_QEMU_MIRROR=https://download.qemu.org scripts/build-qemu.sh --list` |

The build script configures QEMU with `--target-list=i386-softmmu`, `--enable-sdl`, `--enable-debug`, disabled docs/tools/guest-agent/VNC, and GTK enabled only when `pkg-config --exists gtk+-3.0` succeeds.

## See also

* [↑ Documentation index](README.md)
* [02-quickstart.md](02-quickstart.md)
* [04-troubleshooting.md](04-troubleshooting.md)
* [10-architecture.md](10-architecture.md)
* [11-machine-init.md](11-machine-init.md)
* [12-cpu-and-timers.md](12-cpu-and-timers.md)
* [15-rom-loading.md](15-rom-loading.md)
* [23-mediagx-and-display.md](23-mediagx-and-display.md)
* [25-dcs-sound.md](25-dcs-sound.md)
* [26-lpt-board.md](26-lpt-board.md)
