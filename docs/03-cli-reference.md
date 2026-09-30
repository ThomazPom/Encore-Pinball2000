# 03 — Command-line reference

Encore's public launcher is `scripts/run-qemu.sh`. It prepares the selected host
path, locates or acquires the custom QEMU executable, resolves game assets and
then starts the `pinball2000` machine.

```sh
scripts/run-qemu.sh [OPTIONS] [-- QEMU_OPTIONS...]
```

Run `scripts/run-qemu.sh --help` for the executable copy of this interface.
Options after `--` pass directly to the custom `qemu-system-i386` process.

> [!IMPORTANT]
> A stock `qemu-system-i386` has no `pinball2000` machine. Let the launcher
> acquire the binary, build it with `scripts/build-qemu.sh`, or select a known
> compatible executable with `QEMU_BIN`.

## Default launch

With no arguments, the launcher uses these policies:

| Concern | Default |
|---|---|
| Game | `auto`: identify a recognized physical board, otherwise SWE1 |
| Update | `auto`: newest matching local bundle, otherwise base ROMs |
| ROM directory | `roms/` in the Encore tree |
| Savedata | persistent `savedata/` in the Encore tree |
| LPT board | `auto`: physical ppdev board when recognized, otherwise emulated |
| Display | direct SDL framebuffer in a graphical desktop session |
| Audio backend | first supported and host-available backend |
| DCS engine | `adsp-hybrid-thread` |
| Game speed | 100% |
| UART terminal output | quiet |

For an unambiguous disposable desktop run, use:

```sh
scripts/run-qemu.sh \
  --game swe1 \
  --lpt-device emulated \
  --no-savedata
```

See the [Quickstart](02-quickstart.md) for the complete first-run sequence.

## Game, ROM, update and state

| Option | Effect |
|---|---|
| `--game auto\|swe1\|rfm` | Select the title. `auto` is the default. |
| `--roms DIR` | Use `DIR` instead of the repository `roms/` tree. |
| `--savedata DIR` | Read and write `<game>.flash`, `<game>.nvram2` and `<game>.see` under `DIR`. |
| `--no-savedata` | Ignore persistent state, disable its writes and use a fresh throwaway working directory. |
| `--fresh` | Ignore existing state for this boot, then replace it with newly initialized state in the same directory on exit. |
| `--update SPEC` | Select `auto`, `latest`, `none`, `r2`, a short version code or an explicit inner bundle directory. |
| `--pub-card DIR` | Experimental Prism Update Board backed by a bundle directory; incompatible with network modes because both boards decode `0xD0000`. |
| `--guest-extensions` | Inject supported volatile serial-shell extensions into guest RAM; ROM files remain unchanged. |
| `--setip IP MASK GATEWAY` | Enable guest extensions and persist the supplied XINA network resources immediately before `netstart`. |
| `--dns ADDRESS` | Enable guest extensions and persist XINA's `DNSIPA` resource before `netstart`; independent of `--setip`. |
| `--tournament IP [on\|off] [no-free]` | Persist Tourney IP, Tournament Play and Free Play. Defaults to `on` and Free Play enabled; `no-free` disables Free Play. |

These options use the same volatile startup payload. `--tournament` configures
guest resources only: it neither starts a server, chooses a network backend nor
emulates the COM2 barcode reader. They do not control the narrower UDP
compatibility patch: when the NIC's actual backend is Slirp, Encore
automatically changes XINU's volatile unicast `udpsend()` TTL default from 1
to 64 before `netstart`. Passt and bridge backends retain guest code unchanged.
See [Optional networking](48-network.md#slirp-udp-guest-extension).

`--fresh` and `--no-savedata` are mutually exclusive. `--update r2` selects the
RFM 0.80 revision-2 base ROMs and is invalid with SWE1.

Update specifications:

| Specification | Resolution |
|---|---|
| `auto` | Leave selection to the machine's local auto-discovery; this is the default. |
| `latest` | Highest four-digit version present for the selected game. |
| `none` | Suppress update staging and auto-discovery; an existing persistent `<game>.flash` seed still loads. |
| `r2` | RFM revision-2 prototype ROM names; no update bundle. |
| `0210`, `210`, `2.10` | Resolve a locally present short version code. |
| directory | Use an explicit inner game directory containing the update ROM components. |

An explicit version or directory that cannot be resolved is an error. For a
guaranteed base-ROM test that preserves installed state, combine
`--update none` with `--no-savedata`. Details and layouts belong in
[ROM and update loading](15-rom-loading.md); persistence belongs in
[Savedata](09-savedata.md).

## Display and host session

| Option | Effect |
|---|---|
| `--display BACKEND` | Select a QEMU display backend after checking it against this QEMU build. |
| `--headless` | Use display `none` and bind the UART to standard I/O. |
| `--fullscreen` | Request a fullscreen QEMU or direct-framebuffer presentation. |
| `--bpp 16\|32` | Select native RGB555 or converted ARGB8888 display depth; default 32. |
| `--framebuffer` | Force Encore's direct SDL framebuffer renderer. |
| `--wayland` | Force the direct renderer as a native Wayland client. |
| `--display-manager` | Use the current Wayland display-manager session. |
| `--cage` | Start the launcher inside a standalone Cage Wayland kiosk. |
| `--weston` | Start the launcher inside a standalone Weston kiosk. |
| `--preflight` | Prepare dependencies, assets and the selected host path, then stop before a compositor or QEMU. |
| `--flipscreen` | Start with the vertically reversed display state. |
| `--switch-keymap FILE` | Load or initialize the editable A–Z switch map. Default: `$XDG_CONFIG_HOME/encore/switch-keymap.yaml`, otherwise `~/.config/encore/switch-keymap.yaml`. |
| `--qemu-framebuffer` | Use the experimental fast renderer inside QEMU's display path. |
| `--qemu-framebuffer-async` | Add the experimental asynchronous QEMU surface submit worker. |
| `--qemu-framebuffer-async-driver MODE` | Select `auto`, `wayland`, `x11` or `software` for async A/B measurements. |

`--display none` removes the window but does not configure the UART;
`--headless` does both. The direct framebuffer and QEMU framebuffer paths are
mutually exclusive. Explicit framebuffer modes cannot be combined with
`--headless`. A headless launch also enables at least the first verbose tier so
the session is observable unless `--uart-quiet` is supplied.

The complete keyboard mapping is in [Desktop controls](41-cli-keyboard-guide.md).

## Audio and clock

| Option | Effect |
|---|---|
| `--audio auto\|none\|DRIVER` | Auto-select, disable or explicitly select a QEMU audio driver. |
| `--no-audio` | Force audio off, overriding `--audio`. |
| `--dcs-engine ENGINE` | Select `pb2kslib`, `pb2kslib-adsp`, `adsp`, `adsp-thread`, `adsp-clock-thread` or `adsp-hybrid-thread`. |
| `--dcs-pcm-cpu CPU` | Experimentally pin a threaded ADSP producer to one Linux logical CPU. |
| `--dcs-sound-flash FILE` | Use an explicit 1 MiB ADSP sound-flash image. |
| `--pb2kslib FILE` | Override the pb2kslib container instead of `<roms>/<game>_sound.bin`. |
| `--clear-pb2kslib-cache` | Remove the generated update-derived PCM cache before launch. |
| `--pb2kslib-cache-workers N` | Use 1–32 DSP worker processes for missing PCM generation; default 6. |
| `--sound-loading lazy\|preload` | Decode extracted samples on demand or preload them at startup. |
| `--dcs-mode io-handled\|bar4-patch` | Select a compatibility label; both currently use the same BAR4/UART core. |
| `--speed-target PERCENT` | Deliberately scale guest game-clock speed from 25% through 300%; default 100%. |
| `--strict` | Compatibility alias for the sole natural i8254+i8259 IRQ0 path. |

`--audio auto` is Encore's host-aware selection, not QEMU's `driver=auto`.
It tries `sdl`, `pa`, `alsa`, `oss`, `sndio` and `dbus` in that order, while
requiring both QEMU support and the relevant host check. Explicit drivers are
rejected when absent from the selected QEMU binary.

The native ADSP engines fall back to `pb2kslib` when their original-format
assets are incomplete. If neither native assets nor a valid library are
available, the audio device remains present but sample lookups miss.

See [DCS sound](25-dcs-sound.md) and [CPU and timing](12-cpu-and-timers.md)
before changing engines or clock speed.

## Network

Every network option adds the emulated SMC8416T-compatible card. XINA retains
ownership of guest network startup and configuration.

| Option | Effect |
|---|---|
| `--network` | Isolated QEMU user network without outside access. |
| `--network-nat` | Conventional unprivileged QEMU user-mode NAT. |
| `--network-auto` | Rootless NAT that adapts to XINA's active IPv4 address. |
| `--network-mirror` | Experimental libslirp topology mirroring the host IPv4 subnet. |
| `--network-passt` | Unprivileged host-network translation through `passt`. |
| `--network-bridge NAME` | Attach a managed TAP to an existing Linux bridge. |
| `--expose-services` | Publish host TCP 8080 to guest HTTP port 80 on every host interface. |
| `--forward HOST:GUEST` | Publish one guest TCP port on every host interface; repeatable. |
| `--forward-local HOST:GUEST` | Publish one guest TCP port on `127.0.0.1`; repeatable. |
| `--http-port PORT` | Publish guest `10.0.2.15:80` on one localhost port. |

> [!CAUTION]
> `--forward` and `--expose-services` deliberately expose the historical guest
> network stack beyond localhost. Use `--forward-local` unless remote access is
> intentional and the host network is trusted.

Bridge mode cannot be combined with NAT or forwarding. `--network-auto`,
mirror, passt and bridge transports have additional mutual-exclusion checks.
See [Optional network card](48-network.md) for topology and guest setup.

## Serial console, automation and diagnostics

| Option | Effect |
|---|---|
| `--serial` | Open an interactive local XINA console through a temporary TCP UART and foreground `nc`. |
| `--uart-tcp HOST:PORT` | Expose COM1 as a bidirectional TCP server. |
| `--serial-tcp PORT` | Alias for `--uart-tcp 127.0.0.1:PORT`. |
| `--script FILE` | Validate and execute a console/cabinet automation script, leaving QEMU open afterward. |
| `--console-script FILE` | Compatibility alias for `--script`. |
| `--uart-quiet` | Disable the UART sink and stderr mirror; bounded boot input is pre-stuffed unless overridden. |
| `--uart-drop TEXT` | Remove UART lines containing `TEXT`; repeatable. |
| `--uart-no-filter` | Disable the default `swd Debug:` filter and custom drop rules. |
| `--monitor SPEC` | Set the QEMU monitor target. |
| `--debug ITEMS` | Enable QEMU `-d` items and write them to `/tmp/p2k_qemu.log`. |
| `--diag` | Enable the read-only PIT/PIC/IDT/XINU change sampler. |
| `--trace-timing` | Compatibility alias for `--diag`. |
| `--timing-snapshots` | Emit the lightweight three-second timing subset used by benchmarks. |
| `--trace-audio` | Trace DCS audio events and periodic renderer status. |
| `--trace-dcs` | Trace DCS UART traffic byte by byte. |
| `-v` | Restore the UART stderr mirror and enable `--diag`. |
| `-vv` | Add audio tracing. |
| `-vvv` | Add DCS byte tracing. |
| `--irq0-stack-trace` | Sample record-low XINU IStack margin at IRQ0 acknowledgement; publish it only in periodic/exit timing reports. |
| `--irq0-stack-guard ADDR` | Restrict stack observation to one guest stack-guard address. |
| `--irq0-stack-dump FILE` | Schedule a deferred 8 KiB stack dump once observed margin reaches 128 bytes. |
| `--screenshot-dir DIR` | Select the directory used by the F3 screenshot action; default `/tmp`. |
| `--record-video FILE` | Record the run through FFmpeg without overwriting an existing file. |

`--script` owns its UART and monitor and cannot be combined with `--serial`, a
manual UART TCP target, a manual monitor or `--headless`. Script syntax is in
[Console scripting](42-console-scripting.md). `--serial` requires an `nc`
implementation such as Debian's `netcat-openbsd`. The screenshot directory
and the parent directory of a video output must already exist.

## Self-diagnostic

`--bench` starts two isolated emulator passes with disposable state:

1. a temporary RAM-only XINU `clkint` probe measures guest IRQ intervals and
   is restored before exit;
2. a new unpatched guest measures LPT and PDB05 behavior.

Both passes collect IRQ0 depth and IStack margin, exercise cabinet input, wait
through guest warmup and time a guest `sleep 10`. The benchmark requires
`gdb`, `as`, `ld` and `objcopy`.

| Option | Effect |
|---|---|
| `--bench` | Run the two-pass self-diagnostic and write a temporary artifact directory. |
| `--bench-long` | Use a 30-second rather than 10-second guest warmup; the measured window is unchanged. |
| `--bench-guest-load` | Add a temporary cooperative low-priority XINU worker during both passes. |

Exit status 2 means abnormal speed or IRQ delivery, mean PDB p99 above 1 ms,
or gaps above 2.5 ms in at least 10% of complete windows with a minimum of two
affected windows. A non-repeated breach is retained as `PASS WITH WARNINGS` and
does not fail the command by itself.

## Cabinet LPT

| Option | Effect |
|---|---|
| `--lpt-device auto\|emulated\|required\|disconnected\|none\|/dev/parportN` | Select physical, emulated, open-bus or absent driver-board behavior. |
| `--lpt-ioport ADDRESS` | Change the guest LPT base address from the default `0x378`. |
| `--lpt-input physical\|hybrid` | With a real board, accept physical switches only or add keyboard closures. |
| `--lpt-trace FILE` | Append microsecond-timestamped LPT reads and writes to a trace file. |

`required` refuses emulated fallback. An explicit `/dev/parportN` is
authoritative. `hybrid` is valid only with a physical-capable selection. See
[LPT driver-board interface](26-lpt-board.md) and
[Real LPT passthrough](46-real-lpt-passthrough.md).

Physical, emulated and deliberately disconnected cabinet modes begin with the
XINA AT keyboard unplugged. `Tab` connects it; with a physical-only board this
does not enable emulated cabinet switches.

> [!WARNING]
> Presence of the real-LPT implementation is not certification for a powered
> playfield. Follow the physical validation procedure before cabinet use.

## Escape hatches

| Option | Effect |
|---|---|
| `--tcg-only` | Start the selected QEMU binary as a plain `isapc` smoke test without Pinball 2000 hardware. |
| `-- QEMU_OPTIONS...` | Pass all remaining arguments directly to QEMU. |
| `-h`, `--help` | Print launcher help and exit. |

Arguments passed after `--` bypass Encore's option validation. They are for
QEMU debugging and controlled experiments, not ordinary game configuration.

## QEMU build command

`scripts/build-qemu.sh` creates a minimal i386 system emulator with the Encore
machine grafted into a pinned upstream source tree:

```sh
scripts/build-qemu.sh [VERSION|OPTIONS]
```

| Form | Effect |
|---|---|
| no argument | Build pinned QEMU 10.0.8. |
| `VERSION` | Build an explicit `X.Y.Z` or `X.Y.Z-rcN`. |
| `--qemu-version VERSION`, `-V VERSION` | Explicit version form. |
| `--latest` | Select the newest entry in the script's known-good list. |
| `--unstable` | With `--latest` or `--list`, include release candidates from the mirror. |
| `--clean` | Remove only the selected version's extracted source/build tree, retain its downloaded tarball, then rebuild from a pristine extraction. |
| `--list`, `--list-qemu-versions` | Query and print versions available on the configured mirror. |
| `-h`, `--help` | Print build help. |

The default output is
`$HOME/.cache/p2k-qemu-build/qemu-<version>/build/qemu-system-i386`.
Relevant environment overrides are:

| Variable | Effect |
|---|---|
| `P2K_QEMU_BUILD_DIR` | Replace the build/cache root. |
| `P2K_QEMU_MIRROR` | Replace `https://download.qemu.org`. |
| `P2K_ENABLE_GTK=1` | Add GTK to the otherwise SDL-focused minimal build. |
| `QEMU_VER` | Legacy default-version override. |

The current known-good list contains QEMU 10.0.8 and 10.2.4, so `--latest`
selects 10.2.4 while the pinned no-argument default remains 10.0.8. Keep the
build root on a Linux filesystem that supports the symlinks used by QEMU's
source tree.

`--clean` composes with the version selectors. For example,
`scripts/build-qemu.sh --clean -V 10.2.4` cleans and rebuilds 10.2.4 without
touching the cached 10.0.8 tree or either downloaded archive.

## Binary and asset acquisition

The launcher searches, in order:

1. executable `QEMU_BIN` supplied by the environment;
2. `qemu-system-i386` at the Encore repository root;
3. the pinned local-build cache;
4. the verified release cache.

When the source build script exists and no binary is found, an interactive
launcher offers a local build or a verified release download. A packaged
release without its binary downloads the replacement automatically.

Advanced acquisition variables:

| Variable | Effect |
|---|---|
| `QEMU_BIN` | Select an already compatible executable. |
| `P2K_ASSETS_REPO` | Replace the Git source used when `roms/` or `updates/` is absent. |
| `ENCORE_RELEASE_REPOSITORY` | Replace the GitHub `owner/repository` used for binary releases. |
| `ENCORE_RELEASE_BASE_URL` | Replace the complete release-download base URL. |
| `XDG_CACHE_HOME` | Relocate the release and generated-audio caches that honor it. |

The standalone binary downloader accepts an optional destination:

```sh
scripts/internal/download-qemu-release.sh [--destination DIR]
```

It supports x86-64 hosts, verifies the published SHA-256 file and checks that
the downloaded executable advertises the `pinball2000` machine when its shared
libraries are already available. The default destination is
`$XDG_CACHE_HOME/encore-qemu-release`, or
`$HOME/.cache/encore-qemu-release` when `XDG_CACHE_HOME` is unset.

## Cabinet installation commands

`install.sh` configures a bootable cabinet session and escalates through
`run0`, `sudo` or `pkexec` when necessary:

```sh
./install.sh [--display-manager|--cage|--weston|--direct-console]
```

Run `./uninstall.sh` to remove only integration owned by Encore. Installation
changes host services and boot/session configuration; follow
[Cabinet installation](01-cabinet-installation.md) rather than treating these
commands as desktop-launch shortcuts.

## Forensic capture

When a guest is still alive but wedged, `tools/capture-live-crash.sh` can attach
GDB briefly, preserve RAM and host state, then detach:

```sh
tools/capture-live-crash.sh [--pid PID] [--output DIR] \
  [--disassemble ADDRESS[:LENGTH]]...
```

See [Live crash capture](51-live-crash-capture.md) before using it on an
incident.

---

← [Quickstart](02-quickstart.md) · [Documentation index](README.md) ·
[Troubleshooting](04-troubleshooting.md)
