#!/usr/bin/env bash
# scripts/run-qemu.sh — product wrapper for QEMU Encore (Williams Pinball 2000).
#
# This wrapper provides the user-facing CLI for the custom
# `pinball2000` QEMU machine. It does NOT modify guest behavior, DCS,
# timing, display, or audio protocol. It only wires CLI args to the
# existing QEMU machine options, env vars, and -display/-audio/-serial
# flags. See `--help` for the full table.
#
# Quick reference:
#   ./scripts/run-qemu.sh --game swe1
#   ./scripts/run-qemu.sh --game swe1 --update none --no-savedata
#   ./scripts/run-qemu.sh --game swe1 --update 0210
#   ./scripts/run-qemu.sh --game swe1 --update latest
#   ./scripts/run-qemu.sh --game rfm
#   ./scripts/run-qemu.sh --headless --game swe1
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# Preserve explicit environment selection; the CLI can replace it below.
export P2K_DCS_ENGINE="${P2K_DCS_ENGINE:-adsp-thread}"

# --bench is an orchestration mode, not a QEMU machine option.  Dispatch it
# before normal argument parsing and forward every other option (game, update,
# strict, etc.) to the isolated self-diagnostic runner.
for __arg in "$@"; do
  if [[ "$__arg" == "--bench" ]]; then
    __bench_args=()
    for __forward in "$@"; do
      [[ "$__forward" == "--bench" ]] || __bench_args+=("$__forward")
    done
    exec python3 "$ROOT/scripts/bench-qemu.py" "${__bench_args[@]}"
  fi
done

# --- defaults ---------------------------------------------------------------
GAME=swe1
ROMS_DIR="$ROOT/roms"
SAVEDATA_DIR="$ROOT/savedata"
UPDATE_TOKEN="auto"
DISPLAY_MODE=""
HEADLESS=0
FULLSCREEN=0
NO_SAVEDATA=0
FRESH_SAVEDATA=0
CLEAR_PB2K_ADSP_CACHE=0
PB2K_ADSP_CACHE_DIR="${P2K_PB2K_ADSP_CACHE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/encore-pb2k/pb2kslib-adsp}"
export P2K_PB2K_ADSP_CACHE_DIR="$PB2K_ADSP_CACHE_DIR"
MONITOR=""
DEBUG=""
TCG_ONLY=0
VERBOSITY=0
UART_QUIET=0
AUDIO=""
SOUND_LOADING="lazy"
UART_TCP=""
SERIAL_STDIO=0
SPEED_TARGET="${P2K_SPEED_TARGET_PERCENT:-100}"
EXTRA=()

# --- QEMU binary lookup -----------------------------------------------------
QEMU_BIN="${QEMU_BIN:-$HOME/.cache/p2k-qemu-build/qemu-10.0.8/build/qemu-system-i386}"
[[ -x "$QEMU_BIN" ]] || QEMU_BIN="qemu-system-i386"

# --- audio backend support --------------------------------------------------
AUDIO_AUTO_CANDIDATES=(pa alsa sdl oss sndio dbus)
QEMU_AUDIO_HELP=""
QEMU_AUDIO_DRIVERS=""
QEMU_AUDIO_HELP_LOADED=0

qemu_audio_load_supported_drivers() {
  if [[ $QEMU_AUDIO_HELP_LOADED -eq 0 ]]; then
    QEMU_AUDIO_HELP="$("$QEMU_BIN" -audio help 2>/dev/null || true)"
    QEMU_AUDIO_DRIVERS="$(printf '%s\n' "$QEMU_AUDIO_HELP" \
      | sed -n '/Available audio drivers:/,$p' \
      | sed '1d;/^$/d' \
      | awk '{print $1}')"
    QEMU_AUDIO_HELP_LOADED=1
  fi
}

qemu_audio_supported_drivers() {
  qemu_audio_load_supported_drivers
  printf '%s\n' "$QEMU_AUDIO_DRIVERS"
}

audio_backend_supported() {
  local backend="$1"
  qemu_audio_load_supported_drivers
  [[ $'\n'"$QEMU_AUDIO_DRIVERS"$'\n' == *$'\n'"$backend"$'\n'* ]]
}

audio_supported_list() {
  local drivers
  qemu_audio_load_supported_drivers
  drivers="${QEMU_AUDIO_DRIVERS//$'\n'/, }"
  printf '%s' "${drivers:-none}"
}

audio_validate_explicit() {
  local backend="$1"
  if ! audio_backend_supported "$backend"; then
    echo "[run-qemu] --audio: '$backend' not supported by this qemu (not compiled into this qemu binary)" >&2
    echo "  Available audio drivers:" >&2
    qemu_audio_supported_drivers | sed 's/^/    /' >&2
    exit 2
  fi
}

audio_host_reason() {
  local backend="$1"
  case "$backend" in
    pa)
      if command -v pactl >/dev/null 2>&1 && pactl info >/dev/null 2>&1; then
        echo "pulse running"; return 0
      fi
      return 1 ;;
    alsa)
      if [[ -s /proc/asound/cards ]]; then
        echo "ALSA cards present"; return 0
      fi
      return 1 ;;
    sdl) echo "SDL trusted"; return 0 ;;
    oss|sndio|dbus) echo "host check trusted"; return 0 ;;
    *) return 1 ;;
  esac
}

audio_host_services_list() {
  local backend reason out=""
  for backend in "${AUDIO_AUTO_CANDIDATES[@]}"; do
    if reason="$(audio_host_reason "$backend")"; then
      out+="${out:+, }$backend: $reason"
    else
      out+="${out:+, }$backend: unavailable"
    fi
  done
  printf '%s' "$out"
}

audio_pick_auto() {
  local backend reason
  for backend in "${AUDIO_AUTO_CANDIDATES[@]}"; do
    if audio_backend_supported "$backend" && reason="$(audio_host_reason "$backend")"; then
      AUDIO="$backend"
      export P2K_DCS_AUDIO=1
      echo "[run-qemu] audio: auto-selected $backend (host: $reason, qemu: $backend supported)" >&2
      return
    fi
  done

  AUDIO=none
  echo "[run-qemu] audio: no supported backend found in QEMU build {$(audio_supported_list)} matching host services {$(audio_host_services_list)}; running silent. Use \`--audio none\` to suppress this warning." >&2
}

# --- help -------------------------------------------------------------------
print_help() {
    cat <<'EOF'
Usage: scripts/run-qemu.sh [OPTIONS] [-- <qemu passthrough>]

Run Williams Pinball 2000 firmware under the custom QEMU `pinball2000`
machine. Stock qemu-system-i386 cannot boot — see qemu/README.md.

CORE LAUNCH
  --game swe1|rfm           Game ROM bank to load. Default: swe1.
  --roms <dir>              ROM directory. Default: <repo>/roms.
  --savedata <dir>          Persistent savedata dir (reads
                            <dir>/<game>.{flash,nvram2,see}).
                            Default: <repo>/savedata.
  --no-savedata             Run without persistent savedata (also exports
                            P2K_NO_SAVEDATA=1) and switches cwd to a fresh
                            throwaway dir for the run.
  --fresh                   Ignore existing savedata for this boot, then save
                            the newly initialized state normally on exit.
  --update <spec>           Update bundle selection. Spec is one of:
                              auto      (default) machine auto-discovers
                                        the newest matching bundle in
                                        ./updates/. Falls back to base ROMs
                                        if no bundle is found.
                              latest    explicitly resolve to the highest
                                        version bundle for this game.
                              none      base-ROM mode — no update
                                        bundle is staged. Sets
                                        P2K_NO_AUTO_UPDATE=1, isolating
                                        base-ROM compatibility support
                                        (probe-cell shim, etc.) to this
                                        mode only.
                              0210      short version code (also accepts
                                        "210", "2.10"); resolved against
                                        ./updates/pin2000_<gid>_<vvvv>_*
                              <dir>     explicit path to an inner-game
                                        bundle dir (containing
                                        *_bootdata.rom + *_im_flsh0.rom +
                                        *_game.rom + *_symbols.rom).

DISPLAY / UX
  --display <backend>       QEMU -display backend. The wrapper queries
                            `qemu-system-i386 -display help` and rejects
                            any backend the binary wasn't compiled with.
                            Out of the box this build supports: sdl,
                            none, dbus. Rebuild with `--enable-gtk` if
                            you want gtk.
  --headless                Shortcut for --display none -serial stdio
                            (so you actually see UART output).
                            Combine with --uart-quiet to suppress the
                            UART entirely (uses -serial null).
  --fullscreen              Open the window fullscreen (-full-screen).
  --bpp 16|32               Display surface depth. 32 (default) keeps the
                            ARGB8888 path with RGB555→ARGB conversion. 16
                            switches the QEMU surface to native PIXMAN
                            x1r5g5b5 — the source format the GX framebuffer
                            already uses, so pixels are copied without
                            conversion (P2K_DISPLAY_BPP=16).

AUDIO
  --audio auto|none|pa|alsa|sdl|oss|sndio|dbus|wav|<qemu-driver>
                            DCS audio backend. Default and `auto`:
                            choose the first QEMU-supported backend
                            whose host check passes (pa, alsa, sdl,
                            oss, sndio, dbus). `pa` requires pactl;
                            `alsa` requires /proc/asound/cards;
                            later backends trust QEMU. Falls back to
                            `none` with a warning if nothing matches.
                            Explicit backend names are validated
                            against the qemu binary's compiled-in
                            driver list (see `qemu-system-i386 -audio
                            help`); unsupported names fail fast. Sets
                            P2K_DCS_AUDIO=1 and `-audio driver=<x>`
                            so AUD_register_card binds. NOTE: `auto`
                            is the *wrapper* autodetect, NOT QEMU's
                            `driver=auto`. The DCS code path is
                            unchanged.
  --no-audio                Force DCS audio off (overrides --audio).
  --strict                  Disable HOTLOOP IRQ0 delivery and use the
                            natural i8254+i8259 path.
  --with-pit                Let HOTLOOP and the natural i8254 both
                            supply IRQ0. Default is HOTLOOP-only.
  --speed-target <percent>  Deliberate game-clock speed, 25..300 (default
                            100). Scales the i8254 PIT divisor in strict and
                            combo modes and the adaptive HOTLOOP target in
                            HOTLOOP modes. Example: 75 = three-quarter speed,
                            120 = 1.2x speed.
  --pb2kslib <path>         Override pb2kslib container path
                            (P2K_PB2KSLIB=<path>). Default lookup is
                            <roms_dir>/<game>_sound.bin. No directory walks.
  --dcs-sound-flash <path>  Explicit 1 MiB ADSP 28F800/sf.rom image.
                            Useful with update bundles such as SWE1 2.00
                            that contain game code but omit *_sf.rom.
  --dcs-engine pb2kslib|pb2kslib-adsp|adsp|adsp-thread
                            Audio content engine. adsp-thread is the default:
                            original firmware/assets with a condition-driven
                            mailbox worker. adsp runs the same firmware
                            synchronously. pb2kslib uses extracted samples.
                            pb2kslib-adsp generates/uses a persistent PCM
                            cache rendered by the update's native DSP.
  --clear-pb2kslib-cache   Delete the generated ADSP PCM cache before launch.
  --sound-loading lazy|preload
                            lazy   (default) decode samples on-demand.
                            preload  walk every pb2k entry at install
                            time and decode now (P2K_DCS_PRELOAD=1).
                            Adds ~1 s startup cost; eliminates first-
                            trigger decode hitch.

CONSOLE / DIAGNOSTICS
  --bench                   Run an isolated self-diagnostic using the normal
                            windowed display and audio defaults, including
                            a wall-timed XINU `sleep 10`, current IRQ0 delivery,
                            adaptive HOTLOOP state, LPT/PDB05 rates and jitter.
                            Measurement starts after 30 s of guest-time warmup,
                            excluding boot from rolling and jitter statistics.
                            Other options such as --game, --update and --strict
                            are forwarded. Pass `--display none --no-audio` for
                            the former headless benchmark. Returns 2 for
                            abnormal HOTLOOP pace.
  --serial                  Bind COM1 to THIS terminal interactively.
                            Spawns QEMU in the background with a
                            127.0.0.1 TCP UART, then runs `nc` in the
                            foreground of your current shell so you
                            get cooked-mode echo, line editing, and
                            full XINA monitor access — same UX as
                            `nc localhost <port>` would, but in one
                            window. Ctrl-C exits (kills the QEMU
                            child). Mutually exclusive with
                            --serial-tcp / --uart-tcp / --headless.
                            Implies --uart-quiet.
  --uart-quiet              Silence ALL COM1/UART output (stderr mirror
                            AND chardev sink, the latter via
                            -serial null). Wins over -v. Also pre-stuffs
                            P2K_UART_INPUT='\r\n\r\n\r\n\r\n' so XINU's
                            synchronous polled-getc loop (op=22) does not
                            wedge swe1 boot when interrupts are disabled.
                            Override by exporting P2K_UART_INPUT before
                            invocation (set to "" to keep RX truly silent).
  --uart-drop <substr>      Drop any UART line containing <substr>
                            before it reaches stdout/tcp/stderr. By
                            default the wrapper drops "swd Debug:"
                            (phantom switch debug spam from XINA).
                            Repeatable. Set the empty string via
                            --uart-no-filter to forward EVERY byte.
  --uart-no-filter          Disable the default "swd Debug:" filter
                            and any --uart-drop entries (sets
                            P2K_UART_DROP="").
  --uart-tcp <host:port>    Bind COM1 to a bidirectional TCP server
                            socket (-serial tcp:<host:port>,server=on,
                            wait=off). `nc <host> <port>` becomes a
                            two-way XINA monitor: the guest's stream
                            comes out, your keystrokes go IN to the
                            serial-tcp mode). Compatible with --headless.
  --serial-tcp <port>       Alias for
                            `--uart-tcp 127.0.0.1:<port>`. Same
                            bidirectional behavior — type XINA
                            commands like `?` or `continue` directly
                            into nc and watch the guest respond.
  --monitor <spec>          QEMU -monitor target (e.g. stdio,
                            unix:/tmp/qmon,server,nowait).
  --debug <opts>            QEMU -d options (e.g. int,cpu_reset,in_asm).
                            Output goes to /tmp/p2k_qemu.log.
  --screenshot-dir <dir>    Where F3 writes screenshots (defaults to
                            /tmp). Exported as P2K_SCREENSHOT_DIR.
  --diag                    Enable the read-only PIT/PIC/IDT/XINU
                            change-only sampler (P2K_DIAG=1).
  --trace-dcs               Per-byte DCS UART trace (P2K_DCS_BYTE_TRACE=1).
  --trace-audio             Per-event DCS audio trace + per-second
                            renderer status (P2K_DCS_AUDIO_TRACE=1).
  --trace-timing            Alias for --diag (no separate timing trace
                            module exists today).
  -v                        Verbose level 1: re-enable UART stderr
                            mirror + --diag. Default (no -v) is QUIET
                            — UART output suppressed so the terminal
                            stays clean. Use --uart-tcp to capture the
                            stream interactively without spamming the
                            console.
  -vv                       Level 2: -v + --trace-audio.
  -vvv                      Level 3: -v + --trace-audio + --trace-dcs.
                            (--headless implies at least -v so the
                            session isn't completely silent.)
  --dcs-mode io-handled|bar4-patch
                            Select a DCS frontend label. Both labels use the
                            same shared BAR4 + UART core today.

CABINET
  --cabinet | --cabinet-purist
                            Real-cabinet mode. Refuses to start without
                            a real --lpt-device <hostdev> attached, and
                            (when started) suppresses the desktop
                            switch-matrix key handler so all switches
                            must come from the driver-board over LPT
                            (P2K_CABINET_PURIST=1). Use to validate
                            that a session truly came from hardware.
  --lpt-device emu|none|/dev/parportN|0xNNN
                            Pinball driver-board wiring. `emu` (default,
                            also `emulated`) keeps the desktop-input
                            emulated board on I/O 0x378. `none` skips
                            installation entirely (P2K_LPT_DISABLE=1;
                            game will not boot — diagnostic only).
                            `/dev/parportN` switches the board to host
                            parport passthrough via Linux ppdev
                            (P2K_LPT_PARPORT) — needs the `lp` group
                            and `modprobe ppdev`. `0xNNN` relocates the
                            emulated board to a custom I/O port
                            (P2K_LPT_IOPORT).
  --lpt-trace <file>        Append every LPT read/write to <file>
                            (P2K_LPT_TRACE_FILE). Format:
                            "<ts> R|W <off>=<val>" with µs timestamps.
  --parport <device>        Alias for
                            `--lpt-device <device>`.

ESCAPE HATCHES
  --tcg-only                Smoke-test the host QEMU binary alone (no
                            Pinball 2000 hardware, no game boot).
  --                        Pass remaining args straight to qemu-system-i386.
  -h, --help                Show this help.

KEY BINDINGS (delivered by the QEMU machine, not by this wrapper)
  F1                        Quit / shutdown request
  F4                        Toggle coin door
  F5 / Enter / KP-Enter     ~60-frame Enter pulse
  F6 / F9                   Left / right action buttons
  F7 / F8                   Left / right flippers
  F10 / C                   Coin slot 1
  Space / S                 Start
  Esc / Left arrow          Service
  Down / KP-                Volume down
  Up / = / KP+              Volume up
  Right arrow               Begin test
  F12                       State dump
  F2                        Toggle vertical-flip of the framebuffer
                            (default ON: bottom-up source → top-down
                             display).
  F3                        Screenshot to <screenshot-dir>/p2k_screen_<ts>.jpg
                            (default dir /tmp; override with
                             --screenshot-dir or P2K_SCREENSHOT_DIR.
                             Falls back to .ppm if no jpeg helper —
                             cjpeg / magick / convert — is on PATH)
  (Fullscreen toggle: use SDL's default Ctrl+Alt+F.)

ENV PASSTHROUGH (advanced; see qemu/README.md for the full table)
  P2K_NO_UART_STDERR
  P2K_FRESH_SAVEDATA P2K_NO_MEM_DETECT_PATCH P2K_DCS_AUDIO P2K_NO_DCS_AUDIO
  P2K_DCS_AUDIO_TRACE P2K_DCS_BYTE_TRACE P2K_DCS_NO_BYTE_PAIR
  P2K_DCS_RAW_55_PAIR P2K_DIAG P2K_NO_AUTO_UPDATE
  P2K_PB2KSLIB P2K_DCS_ENGINE P2K_DCS_MODE P2K_SCREENSHOT_DIR
  P2K_DISPLAY_BPP P2K_LPT_DISABLE P2K_LPT_PARPORT
  P2K_LPT_IOPORT P2K_LPT_TRACE_FILE P2K_DCS_PRELOAD P2K_CABINET_PURIST
EOF
}

# --- arg parse --------------------------------------------------------------
LPT_MODE="emulated"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --game)            GAME="$2"; shift 2 ;;
    --roms)            ROMS_DIR="$2"; shift 2 ;;
    --savedata)        SAVEDATA_DIR="$2"; shift 2 ;;
    --no-savedata)     NO_SAVEDATA=1; shift ;;
    --fresh)           FRESH_SAVEDATA=1; shift ;;
    --clear-pb2kslib-cache) CLEAR_PB2K_ADSP_CACHE=1; shift ;;
    --update)          UPDATE_TOKEN="$2"; shift 2 ;;
    --display)
      __qbin="${QEMU_BIN:-$HOME/.cache/p2k-qemu-build/qemu-10.0.8/build/qemu-system-i386}"
      [[ -x "$__qbin" ]] || __qbin="qemu-system-i386"
      __dbase="${2%%,*}"
      if ! "$__qbin" -display help 2>/dev/null | grep -qx "$__dbase"; then
        echo "[run-qemu] --display: '$__dbase' not compiled into this qemu binary." >&2
        echo "  Available backends:" >&2
        "$__qbin" -display help 2>/dev/null | tail -n +2 | sed '/^Some/,$d;/^$/d;s/^/    /' >&2
        echo "  (Rebuild with scripts/build-qemu.sh after installing the relevant" >&2
        echo "   dev package, e.g. apt install libgtk-3-dev for gtk.)" >&2
        exit 2
      fi
      DISPLAY_MODE="$2"; shift 2 ;;
    --headless)        HEADLESS=1; shift ;;
    --fullscreen)      FULLSCREEN=1; shift ;;
    --bpp)
      case "$2" in
        32) ;;  # default ARGB8888 path
        16) export P2K_DISPLAY_BPP=16 ;;  # native x1r5g5b5 surface
        *)  echo "[run-qemu] --bpp: only 16 and 32 are supported (got '$2')" >&2; exit 2 ;;
      esac
      shift 2 ;;
    --splash-screen|--splash-time|--no-splash)
      # Removed: in-window splash didn't work (QEMU is too fast — guest
      # draws before splash even paints once). Silently accept-and-drop
      # so old command lines don't break.
      case "$1" in
        --no-splash) shift ;;
        *) shift 2 ;;
      esac ;;
    --serial)          SERIAL_STDIO=1; shift ;;
    --monitor)         MONITOR="$2"; shift 2 ;;
    --debug)           DEBUG="$2"; shift 2 ;;
    --uart-quiet)      UART_QUIET=1; shift ;;
    --uart-drop)
      # Drop any UART line containing this substring. Repeatable.
      # Internally joined with TAB and exported as P2K_UART_DROP.
      if [[ -z "${UART_DROP:-}" ]]; then UART_DROP="$2"
      else UART_DROP="$UART_DROP"$'\t'"$2"; fi
      shift 2 ;;
    --uart-no-filter)  export P2K_UART_DROP=""; shift ;;
    --uart-tcp)        UART_TCP="$2"; shift 2 ;;
    --serial-tcp)
      # Alias: --serial-tcp <port> ⇒ --uart-tcp 127.0.0.1:<port>
      if [[ -z "${2:-}" || "$2" =~ [^0-9] ]]; then
        echo "[run-qemu] --serial-tcp: expected numeric port, got '${2:-}'" >&2
        exit 2
      fi
      UART_TCP="127.0.0.1:$2"; shift 2 ;;
    --screenshot-dir)
      [[ -d "$2" ]] || { echo "[run-qemu] --screenshot-dir: '$2' is not a directory" >&2; exit 2; }
      export P2K_SCREENSHOT_DIR="$(cd "$2" && pwd)"; shift 2 ;;
    --audio)
      case "$2" in
        auto) AUDIO=""; shift 2 ;;  # fall through to host/QEMU autodetect below
        none) AUDIO=none; unset P2K_DCS_AUDIO || true; shift 2 ;;
        *)
          audio_validate_explicit "$2"
          AUDIO="$2"; export P2K_DCS_AUDIO=1; shift 2 ;;
      esac ;;
    --no-audio)        AUDIO="none"; unset P2K_DCS_AUDIO || true; shift ;;
    --strict)
      # Disable HOTLOOP and use natural i8254 + i8259 delivery.
      export P2K_TCG_CLKINT_HOTLOOP=0; shift ;;
    --with-pit)
      # Combined mode: HOTLOOP and the natural i8254 both raise IRQ0.
      export P2K_TCG_CLKINT_HOTLOOP_WITH_PIT=1
      unset P2K_TCG_CLKINT_HOTLOOP_NO_PIT || true
      shift ;;
    --speed-target)
      [[ -n "${2:-}" ]] || { echo "[run-qemu] --speed-target: expected percent" >&2; exit 2; }
      SPEED_TARGET="$2"; shift 2 ;;
    --pb2kslib)        export P2K_PB2KSLIB="$2"; shift 2 ;;
    --dcs-sound-flash)
      [[ -f "$2" ]] || { echo "[run-qemu] --dcs-sound-flash: '$2' is not a file" >&2; exit 2; }
      [[ "$(stat -c %s "$2")" = 1048576 ]] || {
        echo "[run-qemu] --dcs-sound-flash: '$2' must be exactly 1048576 bytes" >&2
        exit 2
      }
      export P2K_DCS_SOUND_FLASH="$(realpath "$2")"; shift 2 ;;
    --dcs-engine)
      case "$2" in
        pb2kslib|pb2kslib-adsp|adsp|adsp-thread) export P2K_DCS_ENGINE="$2" ;;
        *) echo "[run-qemu] --dcs-engine: expected pb2kslib|pb2kslib-adsp|adsp|adsp-thread, got '$2'" >&2; exit 2 ;;
      esac
      shift 2 ;;
    --sound-loading)
      case "$2" in
        lazy)    SOUND_LOADING="lazy" ;;
        preload) SOUND_LOADING="preload"; export P2K_DCS_PRELOAD=1 ;;
        *) echo "[run-qemu] --sound-loading: expected lazy|preload, got '$2'" >&2; exit 2 ;;
      esac
      shift 2 ;;
    --dcs-mode)        export P2K_DCS_MODE="$2"; shift 2 ;;
    --diag)            export P2K_DIAG=1; shift ;;
    --trace-dcs)       export P2K_DCS_BYTE_TRACE=1; shift ;;
    --trace-audio)     export P2K_DCS_AUDIO_TRACE=1; shift ;;
    --trace-timing)    export P2K_DIAG=1; shift ;;
    -v)                VERBOSITY=1; shift ;;
    -vv)               VERBOSITY=2; shift ;;
    -vvv)              VERBOSITY=3; shift ;;
    --cabinet|--cabinet-purist)
      # --cabinet-purist is an explicit request to trust
      # the real board" semantics). With our wrapper there's no real
      # cabinet bus to trust unless --lpt-device <hostdev> is also set,
      # so we just record the intent for downstream forensics.
      export P2K_CABINET_PURIST=1; shift ;;
    --lpt-device|--lpt)
      # --lpt-device accepts <none|emu|/dev/parportN|0xNNN>.
      # All four modes wire to existing P2K_LPT_* env vars consumed by
      # qemu/p2k-lpt-board.c. Real hardware passthrough is Linux-only and
      # requires the host user to be in the `lp` group with ppdev loaded.
      LPT_MODE="$2"
      case "$LPT_MODE" in
        emu|emulated) ;;
        none)              export P2K_LPT_DISABLE=1 ;;
        /dev/*)            [[ -e "$LPT_MODE" ]] || { echo "[run-qemu] $1: '$LPT_MODE' does not exist" >&2; exit 2; }
                           export P2K_LPT_PARPORT="$LPT_MODE" ;;
        0x[0-9a-fA-F]*|[0-9]*) export P2K_LPT_IOPORT="$LPT_MODE" ;;
        parport)           # Default probe target — must exist.
                           [[ -e /dev/parport0 ]] || { echo "[run-qemu] $1: '/dev/parport0' does not exist (load ppdev?)" >&2; exit 2; }
                           export P2K_LPT_PARPORT="/dev/parport0" ;;
        *) echo "[run-qemu] $1: expected emu|none|/dev/parportN|0xNNN, got '$LPT_MODE'" >&2; exit 2 ;;
      esac
      shift 2 ;;
    --lpt-trace)
      [[ -n "${2:-}" ]] || { echo "[run-qemu] --lpt-trace: expected <file>" >&2; exit 2; }
      LPT_TRACE_DIR="$(cd "$(dirname "$2")" 2>/dev/null && pwd)" || { echo "[run-qemu] --lpt-trace: parent dir of '$2' missing" >&2; exit 2; }
      export P2K_LPT_TRACE_FILE="$LPT_TRACE_DIR/$(basename "$2")"
      shift 2 ;;
    --parport)
      # Alias: --parport <dev> ⇒ --lpt-device <dev>
      [[ -n "${2:-}" ]] || { echo "[run-qemu] --parport: expected <device>" >&2; exit 2; }
      [[ -e "$2" ]] || { echo "[run-qemu] --parport: '$2' does not exist" >&2; exit 2; }
      export P2K_LPT_PARPORT="$2"; shift 2 ;;
    --tcg-only)        TCG_ONLY=1; shift ;;
    --)                shift; EXTRA+=("$@"); break ;;
    -h|--help)         print_help; exit 0 ;;
    *) echo "Unknown arg: $1 (try --help)" >&2; exit 2 ;;
  esac
done

if [[ ! "$SPEED_TARGET" =~ ^[0-9]+([.][0-9]+)?$ ]] ||
   ! awk -v value="$SPEED_TARGET" 'BEGIN { exit !(value >= 25 && value <= 300) }'; then
  echo "[run-qemu] --speed-target: expected a number from 25 through 300, got '$SPEED_TARGET'" >&2
  exit 2
fi
export P2K_SPEED_TARGET_PERCENT="$SPEED_TARGET"
export P2K_TCG_CLKINT_HOTLOOP_TARGET_HZ="$(
  awk -v percent="$SPEED_TARGET" 'BEGIN { printf "%.6f", 4003.966443 * percent / 100.0 }'
)"

# When neither --strict nor --with-pit is given, use HOTLOOP-only.
# the TCG hook. --with-pit remains available for A/B comparisons.
if [[ -z "${P2K_TCG_CLKINT_HOTLOOP_WITH_PIT:-}" && "${P2K_TCG_CLKINT_HOTLOOP:-1}" != "0" ]]; then
  export P2K_TCG_CLKINT_HOTLOOP_NO_PIT=1
fi

# Scale HOTLOOP's useful search range around the requested period. At 100%
# retain the established defaults exactly. Combined PIT+HOTLOOP keeps the
# broad 100 ms ceiling so a fast natural PIT can make HOTLOOP nearly dormant.
if [[ "$SPEED_TARGET" != "100" && "$SPEED_TARGET" != "100.0" &&
      "${P2K_TCG_CLKINT_HOTLOOP:-1}" != "0" ]]; then
  __target_period_ns="$(awk -v hz="$P2K_TCG_CLKINT_HOTLOOP_TARGET_HZ" \
    'BEGIN { printf "%.0f", 1000000000.0 / hz }')"
  if [[ -z "${P2K_TCG_CLKINT_HOTLOOP_GAP_LOW_NS:-}" ]]; then
    export P2K_TCG_CLKINT_HOTLOOP_GAP_LOW_NS="$(( __target_period_ns / 5 ))"
  fi
  if [[ -z "${P2K_TCG_CLKINT_HOTLOOP_GAP_HIGH_NS:-}" ]]; then
    if [[ -n "${P2K_TCG_CLKINT_HOTLOOP_WITH_PIT:-}" ]]; then
      export P2K_TCG_CLKINT_HOTLOOP_GAP_HIGH_NS=100000000
    else
      export P2K_TCG_CLKINT_HOTLOOP_GAP_HIGH_NS="$(( __target_period_ns * 4 ))"
    fi
  fi
fi

# --- HOTLOOP initial gap ---------------------------------------------------
# The former display/mode-specific table (145/250/300/700 µs) was calibrated
# against boots where the mem_detect scanner ran too late. The resulting
# 4 MiB heap Fatal entered XINA's monitor and looked like an IRQ0 wedge when
# UART output was suppressed. With mem_detect patched before heap init, 145 µs
# passed the full sequential matrix, including 10/10 RFM headless + --with-pit.
# Explicit P2K_TCG_CLKINT_HOTLOOP_MIN_GAP_NS wins.
if [[ -z "${P2K_TCG_CLKINT_HOTLOOP_MIN_GAP_NS:-}" && "${P2K_TCG_CLKINT_HOTLOOP:-1}" != "0" ]]; then
  export P2K_TCG_CLKINT_HOTLOOP_MIN_GAP_NS="$(
    awk -v percent="$SPEED_TARGET" 'BEGIN { printf "%.0f", 14500000.0 / percent }'
  )"
fi

# --- verbosity → diag/trace tier mapping -----------------------------------
# Default (level 0) is QUIET: the hand-rolled UART in p2k-isa-stubs.c
# would otherwise spam every XINU NonFatal byte to stderr. Use --uart-tcp
# to capture the stream cleanly; or use -v to put it back on stderr.
#   level 0  (default)  silent (P2K_NO_UART_STDERR=1)
#   level 1  -v         UART stderr mirror + P2K_DIAG (lifecycle messages)
#   level 2  -vv        + P2K_DCS_AUDIO_TRACE
#   level 3  -vvv       + P2K_DCS_BYTE_TRACE
# --headless promotes verbosity to at least 1 (otherwise the headless
# session would be completely silent — defeats the purpose).
# --serial implies --uart-quiet (otherwise every byte appears twice:
# once on stdout via the chardev, once on stderr via the mirror).
# --uart-quiet always wins (forces UART silent regardless of level).
if [[ $SERIAL_STDIO -eq 1 ]]; then
  UART_QUIET=1
fi
if [[ $HEADLESS -eq 1 && $VERBOSITY -lt 1 ]]; then
  VERBOSITY=1
fi
if [[ $VERBOSITY -ge 1 ]]; then
  export P2K_DIAG=1
  unset P2K_NO_UART_STDERR
else
  export P2K_NO_UART_STDERR=1
fi
if [[ $UART_QUIET -eq 1 ]]; then
  export P2K_NO_UART_STDERR=1
  # SWE1 (and possibly other titles) reach an XINU control() op=22 callback
  # (killsafe_waits_msg → ttysputc) early in boot which performs a SYNCHRONOUS
  # polled read on COM1 LSR.DR with interrupts disabled (cli). With -serial null
  # no byte ever arrives and the guest wedges forever — IMR=0xff, IRQ0 cannot
  # deliver, replay BH cannot fire, clock is dead. Pre-stuff a few CRs into
  # the RX FIFO so the polling loop completes and the boot can proceed. Users
  # who want a strictly silent UART can explicitly set P2K_UART_INPUT= to "".
  if [[ -z "${P2K_UART_INPUT+x}" ]]; then
    # Bounded one-shot pattern: enough CR/LF pairs to satisfy ALL op=22
    # reads during boot, then deliberately empty so the diag handler
    # exits its loop. 16 pairs was empirically insufficient (~10% wedge
    # rate): depending on boot timing, XINU exhausts the buffer before
    # the critical killsafe_waits_msg poll, leaving it spinning on COM1
    # LSR.DR with interrupts disabled (IMR=0xff, clkint frozen).
    # 48 pairs eliminates the wedge across 10/10 test runs.
    # Do NOT cycle forever — every extra CR submits an empty command at
    # the diag prompt, eliciting more output and more cli-guarded reads.
    export P2K_UART_INPUT='\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n'
  fi
fi
if [[ -n "${UART_DROP:-}" ]]; then export P2K_UART_DROP="$UART_DROP"; fi
if [[ $VERBOSITY -ge 2 ]]; then export P2K_DCS_AUDIO_TRACE=1; fi
if [[ $VERBOSITY -ge 3 ]]; then export P2K_DCS_BYTE_TRACE=1; fi

# Quiet default: filter info/warning lines from QEMU's stderr (the
# `pinball2000: loaded ...`, `mapped ... @ 0x...`, etc. noise from
# info_report / warn_report inside the machine setup). Errors and
# our own [run-qemu] lines pass through. Bypassed only by -v.
if [[ $VERBOSITY -lt 1 ]]; then
  exec 2> >(grep -v --line-buffered -E \
    '^qemu-system-i386: (info|warning):' >&2)
fi

# --- display defaults -------------------------------------------------------
if [[ -z "$DISPLAY_MODE" ]]; then
  if [[ $HEADLESS -eq 1 ]]; then
    DISPLAY_MODE=none
  elif [[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
    DISPLAY_MODE=sdl
  else
    DISPLAY_MODE=none
  fi
fi
if [[ "$DISPLAY_MODE" == "none" && $FULLSCREEN -eq 1 ]]; then
  echo "[run-qemu] --fullscreen ignored with --display none / --headless" >&2
  FULLSCREEN=0
fi

# --cabinet-purist requires a real --lpt-device <hostdev>; otherwise the
# C-side install would error out at runtime. Catch it now with a clear
# message rather than at qemu startup.
if [[ "${P2K_CABINET_PURIST:-}" == "1" && -z "${P2K_LPT_PARPORT:-}" ]]; then
  echo "[run-qemu] --cabinet-purist requires --lpt-device <hostdev> "\
"(real parport with the driver-board attached)." >&2
  exit 2
fi

# --- audio auto-detect ------------------------------------------------------
# Default/auto chooses a backend only when both the QEMU build supports it and
# the host-side service check is acceptable for that backend.
if [[ -z "$AUDIO" ]]; then
  audio_pick_auto
fi

# --- update token resolution ------------------------------------------------
# UPDATE_TOKEN ∈ {auto, latest, none, <short-code>, <dir>}
# Output: UPDATE_DIR_ABS (empty → no -M update=...; or P2K_NO_AUTO_UPDATE=1
# in base-ROM mode).
UPDATE_DIR_ABS=""

resolve_update_token() {
  local token="$1" gn=""
  case "$GAME" in
    swe1) gn=50069 ;;
    rfm)  gn=50070 ;;
    *)    echo "" ; return 1 ;;
  esac

  # Search roots: ./updates and <roms_dir>/../updates.
  local roots=()
  [[ -d "$ROOT/updates" ]] && roots+=( "$ROOT/updates" )
  local roms_parent
  roms_parent="$(cd "$ROMS_DIR" && cd .. && pwd)" 2>/dev/null || true
  [[ -n "$roms_parent" && -d "$roms_parent/updates" && "$roms_parent/updates" != "$ROOT/updates" ]] \
    && roots+=( "$roms_parent/updates" )

  if [[ "$token" == "latest" ]]; then
    local best="" best_v=""
    for r in "${roots[@]}"; do
      while IFS= read -r -d '' d; do
        local base v
        base="$(basename "$d")"
        v="${base#pin2000_${gn}_}"; v="${v%%_*}"
        [[ "$v" =~ ^[0-9]{4}$ ]] || continue
        if [[ -z "$best_v" || "$v" > "$best_v" ]]; then
          best_v="$v"; best="$d/$gn"
        fi
      done < <(find "$r" -mindepth 1 -maxdepth 1 -type d -name "pin2000_${gn}_*" -print0 2>/dev/null)
    done
    [[ -n "$best" && -d "$best" ]] && { echo "$best"; return 0; }
    return 1
  fi

  # Short version code: "0210", "210", "2.10", "2.1"
  local want=""
  if [[ "$token" =~ ^[0-9]+$ ]]; then
    printf -v want '%04d' "$((10#$token))"
  elif [[ "$token" =~ ^([0-9]+)\.([0-9]+)$ ]]; then
    local maj="${BASH_REMATCH[1]}" min="${BASH_REMATCH[2]}"
    [[ ${#min} -eq 1 ]] && min=$((10#$min * 10))
    printf -v want '%04d' "$((10#$maj * 100 + 10#$min))"
  fi

  if [[ -n "$want" ]]; then
    for r in "${roots[@]}"; do
      while IFS= read -r -d '' d; do
        local inner="$d/$gn"
        [[ -d "$inner" ]] && { echo "$inner"; return 0; }
      done < <(find "$r" -mindepth 1 -maxdepth 1 -type d -name "pin2000_${gn}_${want}_*" -print0 2>/dev/null)
    done
    return 1
  fi

  return 1
}

case "$UPDATE_TOKEN" in
  none)
    # Museum / base mode. The C code's auto-discover is suppressed; the
    # Base-ROM compatibility helpers arm only under
    # this env (see qemu/p2k-probe-cell-shim.c, qemu/p2k-dcs-core.c).
    export P2K_NO_AUTO_UPDATE=1
    echo "[run-qemu] --update none → base-ROM mode (P2K_NO_AUTO_UPDATE=1)" >&2
    ;;
  auto|"")
    # Default. Leave -M update= unset; the machine auto-discovers in
    # ./updates and falls back to base ROMs if nothing matches.
    ;;
  latest|[0-9]*|*.*)
    if [[ "$UPDATE_TOKEN" == "latest" || "$UPDATE_TOKEN" =~ ^[0-9.]+$ ]]; then
      if ! UPDATE_DIR_ABS="$(resolve_update_token "$UPDATE_TOKEN")"; then
        echo "[run-qemu] ERROR: --update '$UPDATE_TOKEN' did not resolve to a bundle dir" >&2
        echo "[run-qemu] hint: list ./updates/pin2000_*_<vvvv>_*/<gid>/" >&2
        exit 1
      fi
      echo "[run-qemu] --update $UPDATE_TOKEN → $UPDATE_DIR_ABS" >&2
    elif [[ -d "$UPDATE_TOKEN" ]]; then
      UPDATE_DIR_ABS="$(cd "$UPDATE_TOKEN" && pwd)"
      echo "[run-qemu] --update $UPDATE_TOKEN → $UPDATE_DIR_ABS" >&2
    else
      echo "[run-qemu] ERROR: --update '$UPDATE_TOKEN' is neither a known spec nor a directory" >&2
      exit 1
    fi
    ;;
  *)
    if [[ -d "$UPDATE_TOKEN" ]]; then
      UPDATE_DIR_ABS="$(cd "$UPDATE_TOKEN" && pwd)"
      echo "[run-qemu] --update $UPDATE_TOKEN → $UPDATE_DIR_ABS" >&2
    else
      echo "[run-qemu] ERROR: --update '$UPDATE_TOKEN' is neither a known spec nor a directory" >&2
      exit 1
    fi
    ;;
esac

if [[ $CLEAR_PB2K_ADSP_CACHE -eq 1 ]]; then
  rm -rf -- "$PB2K_ADSP_CACHE_DIR"
  echo "[run-qemu] cleared generated pb2kslib cache: $PB2K_ADSP_CACHE_DIR"
fi
mkdir -p "$PB2K_ADSP_CACHE_DIR"

# --- savedata cwd handling --------------------------------------------------
# The QEMU machine reads savedata/<game>.* relative to cwd. Choose cwd
# accordingly. --no-savedata exports the C-side read-only signal and keeps
# the empty throwaway cwd as a second guard so any
# cwd-relative seed probe cannot observe the repo savedata/ directory. Apply
# the same cwd isolation when the env var was set by the caller.
if [[ -n "${P2K_NO_SAVEDATA:-}" && "${P2K_NO_SAVEDATA:-}" != "0" ]]; then
  NO_SAVEDATA=1
fi
if [[ -n "${P2K_FRESH_SAVEDATA:-}" && "${P2K_FRESH_SAVEDATA:-}" != "0" ]]; then
  FRESH_SAVEDATA=1
fi
if [[ $NO_SAVEDATA -eq 1 && $FRESH_SAVEDATA -eq 1 ]]; then
  echo "[run-qemu] ERROR: --fresh and --no-savedata are mutually exclusive" >&2
  exit 2
fi
RUN_CWD="$ROOT"
CLEANUP=""
if [[ $NO_SAVEDATA -eq 1 ]]; then
  export P2K_NO_SAVEDATA=1
  RUN_CWD="$(mktemp -d "$ROOT/.run-qemu.XXXXXX")"
  CLEANUP="$RUN_CWD"
  trap '[[ -n "$CLEANUP" ]] && rm -rf "$CLEANUP"' EXIT
  echo "[run-qemu] read-only savedata: P2K_NO_SAVEDATA=1; running in $RUN_CWD (no savedata/ subdir)"
else
  if [[ $FRESH_SAVEDATA -eq 1 ]]; then
    export P2K_FRESH_SAVEDATA=1
    echo "[run-qemu] fresh savedata: ignoring existing seeds; new state will be saved on exit"
  fi
fi
if [[ $NO_SAVEDATA -eq 0 && "$SAVEDATA_DIR" != "$ROOT/savedata" ]]; then
  RUN_CWD="$(mktemp -d "$ROOT/.run-qemu.XXXXXX")"
  CLEANUP="$RUN_CWD"
  trap '[[ -n "$CLEANUP" ]] && rm -rf "$CLEANUP"' EXIT
  ln -s "$SAVEDATA_DIR" "$RUN_CWD/savedata"
fi

# --- assemble QEMU command line --------------------------------------------
# Auto-append display options so QEMU never captures the host pointer
# (we have no guest mouse device anyway, but a stray click would
# otherwise hide the cursor and toggle grab until Ctrl-Alt-G).
# Keyboard events still flow normally to the guest.
#   sdl: supports show-cursor only (no grab-on-hover keyword in -display sdl)
#   gtk: supports both show-cursor and grab-on-hover
DISPLAY_ARG="$DISPLAY_MODE"
case "${DISPLAY_MODE%%,*}" in
  sdl)
    [[ "$DISPLAY_MODE" == *show-cursor=*    ]] || DISPLAY_ARG="$DISPLAY_ARG,show-cursor=on"
    ;;
  gtk)
    [[ "$DISPLAY_MODE" == *show-cursor=*    ]] || DISPLAY_ARG="$DISPLAY_ARG,show-cursor=on"
    [[ "$DISPLAY_MODE" == *grab-on-hover=*  ]] || DISPLAY_ARG="$DISPLAY_ARG,grab-on-hover=off"
    ;;
esac
ARGS=( -no-reboot -m 16 -display "$DISPLAY_ARG" -rtc base=localtime )
[[ $FULLSCREEN -eq 1 ]] && ARGS+=( -full-screen )

if [[ $SERIAL_STDIO -eq 1 ]]; then
  if [[ -n "$UART_TCP" || $HEADLESS -eq 1 ]]; then
    echo "[run-qemu] --serial is mutually exclusive with --serial-tcp/--uart-tcp/--headless" >&2
    exit 2
  fi
  # We want UNIX `nc`-quality UX (cooked-mode terminal echo, line
  # editing, no swallowed local-echo) without forcing the user to
  # open a second terminal. Solution: bind COM1 to a TCP server on
  # 127.0.0.1:<random free port>, launch QEMU in the background,
  # then exec `nc 127.0.0.1 <port>` in the foreground of THIS
  # terminal. This keeps the console in the invoking shell.
  if ! command -v nc >/dev/null 2>&1; then
    echo "[run-qemu] --serial needs 'nc' (apt install netcat-openbsd)" >&2
    exit 2
  fi
  # Find a free ephemeral port.
  SERIAL_PORT="$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()' 2>/dev/null || echo 5570)"
  ARGS+=( -serial "tcp:127.0.0.1:${SERIAL_PORT},server=on,wait=off" )
  UART_QUIET=1
elif [[ -n "$UART_TCP" ]]; then
  ARGS+=( -serial "tcp:${UART_TCP},server=on,wait=off" )
  if [[ $HEADLESS -eq 1 ]]; then
    echo "[run-qemu] --uart-tcp + --headless: serial0 -> tcp://${UART_TCP}; "\
"host-stderr UART mirror still active (use --uart-quiet to silence)." >&2
  fi
elif [[ $HEADLESS -eq 1 ]]; then
  if [[ $UART_QUIET -eq 1 ]]; then
    ARGS+=( -serial null )
  else
    ARGS+=( -serial stdio )
  fi
fi

[[ -n "$MONITOR" ]] && ARGS+=( -monitor "$MONITOR" )
[[ -n "$DEBUG" ]] && ARGS+=( -d "$DEBUG" -D /tmp/p2k_qemu.log )

if [[ -n "$AUDIO" && "$AUDIO" != "none" ]]; then
  ARGS+=( -audio "driver=$AUDIO" )
fi

# --- TCG smoke-test escape hatch -------------------------------------------
if [[ $TCG_ONLY -eq 1 ]]; then
  ARGS=( -M isapc "${ARGS[@]}" -bios "$ROMS_DIR/bios.bin" )
  echo "[run-qemu] TCG smoke-test (NOT a Pinball 2000 boot)"
  cd "$RUN_CWD"
  exec "$QEMU_BIN" "${ARGS[@]}" "${EXTRA[@]}"
fi

# --- pinball2000 machine ----------------------------------------------------
MACHINE_OPTS="pinball2000,game=$GAME,roms-dir=$ROMS_DIR"
if [[ -n "$UPDATE_DIR_ABS" ]]; then
  MACHINE_OPTS+=",update=$UPDATE_DIR_ABS"
fi
ARGS=( -M "$MACHINE_OPTS" "${ARGS[@]}" )

echo "[run-qemu] cwd=$RUN_CWD"
echo "[run-qemu] $QEMU_BIN ${ARGS[*]} ${EXTRA[*]:-}"
cd "$RUN_CWD"

if [[ $SERIAL_STDIO -eq 1 ]]; then
  # --serial: launch QEMU in background so it owns its own stdio (no
  # raw-mode fight with the terminal), then run `nc` in the foreground
  # of THIS terminal. Cooked-mode echo, line editing, signals all work
  # as the user's shell expects. nc exits → QEMU is killed.
  "$QEMU_BIN" "${ARGS[@]}" "${EXTRA[@]}" </dev/null >/dev/null 2>&1 &
  QEMU_PID=$!
  trap '[[ -n "${QEMU_PID:-}" ]] && kill "$QEMU_PID" 2>/dev/null; [[ -n "$CLEANUP" ]] && rm -rf "$CLEANUP"' EXIT INT TERM
  # Wait for the TCP server to come up (QEMU takes ~1s to bind).
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    if (exec 3<>/dev/tcp/127.0.0.1/"$SERIAL_PORT") 2>/dev/null; then
      break
    fi
    sleep 0.3
  done
  # Use rlwrap if available for readline (history/up-arrow/Ctrl-R).
  # History is per-host-user, persisted in ~/.cache/p2k-qemu-build/.
  HIST_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/p2k-qemu-build"
  mkdir -p "$HIST_DIR" 2>/dev/null || true
  if command -v rlwrap >/dev/null 2>&1; then
    echo "[run-qemu] --serial: COM1 → 127.0.0.1:${SERIAL_PORT}; bridging via rlwrap+nc (history: ${HIST_DIR}/serial.history, Ctrl-C to quit)" >&2
    exec rlwrap -A -H "${HIST_DIR}/serial.history" nc 127.0.0.1 "$SERIAL_PORT"
  else
    echo "[run-qemu] --serial: COM1 → 127.0.0.1:${SERIAL_PORT}; bridging via nc (install 'rlwrap' for history/up-arrow; Ctrl-C to quit)" >&2
    exec nc 127.0.0.1 "$SERIAL_PORT"
  fi
fi

"$QEMU_BIN" "${ARGS[@]}" "${EXTRA[@]}" &
QEMU_PID=$!
trap 'status=$?; if [[ -n "${QEMU_PID:-}" ]]; then kill "$QEMU_PID" 2>/dev/null; wait "$QEMU_PID" 2>/dev/null || true; fi; [[ -n "$CLEANUP" ]] && rm -rf "$CLEANUP"; exit "$status"' EXIT INT TERM
wait "$QEMU_PID"
QEMU_STATUS=$?
trap - EXIT INT TERM
[[ -n "$CLEANUP" ]] && rm -rf "$CLEANUP"
exit "$QEMU_STATUS"
