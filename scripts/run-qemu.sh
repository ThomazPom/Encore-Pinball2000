#!/usr/bin/env bash
# scripts/run-qemu.sh — product wrapper for QEMU Encore (Williams Pinball 2000).
#
# This wrapper aims for Unicorn-Encore CLI ergonomics on top of the custom
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

# --- defaults ---------------------------------------------------------------
GAME=swe1
ROMS_DIR="$ROOT/roms"
SAVEDATA_DIR="$ROOT/savedata"
UPDATE_TOKEN="auto"
DISPLAY_MODE=""
HEADLESS=0
FULLSCREEN=0
WINDOW_SCALE=""
NO_SAVEDATA=0
MONITOR=""
DEBUG=""
TCG_ONLY=0
VERBOSITY=0
AUDIO=""
SOUND_LOADING="lazy"
UART_TCP=""
SPLASH_PATH=""
EXTRA=()

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
                            <dir>/<game>.{flash,nvram2,see,ems}).
                            Default: <repo>/savedata.
  --no-savedata             Run without persistent savedata. Internally
                            switches cwd to a fresh tmp dir for the run.
  --update <spec>           Update bundle selection. Spec is one of:
                              auto      (default) machine auto-discovers
                                        the newest matching bundle in
                                        ./updates/ — same spirit as
                                        Unicorn. Falls back to base ROMs
                                        if no bundle is found.
                              latest    explicitly resolve to the highest
                                        version bundle for this game.
                              none      museum/base mode — no update
                                        bundle is staged. Sets
                                        P2K_NO_AUTO_UPDATE=1, isolating
                                        every museum compatibility gate
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
  --display sdl|gtk|none    QEMU -display backend. Default: sdl on a
                            graphical session, none otherwise.
  --headless                Shortcut for --display none -serial stdio.
  --fullscreen              Open the window fullscreen (-full-screen).
  --window-scale <n>        Best-effort SDL zoom hint (uses
                            -display sdl,window-close=on,gl=off and
                            QEMU's window-scale where supported). No-op
                            on -display none.
  --bpp 16|32               Display surface depth. 32 (default) keeps the
                            ARGB8888 path with RGB555→ARGB conversion. 16
                            switches the QEMU surface to native PIXMAN
                            x1r5g5b5 — the source format the GX framebuffer
                            already uses, so pixels are copied without
                            conversion (P2K_DISPLAY_BPP=16).
  --splash-screen default|none|<path>
                            Boot splash. `default`/`none` show no splash
                            (no asset is shipped). A <path> launches an
                            image viewer (feh / eog / display / xdg-open)
                            in the background for the boot duration; the
                            viewer is killed when QEMU exits.
  --splash | --no-splash    Legacy aliases. `--splash` enables the default
                            (no-op); `--no-splash` disables.

AUDIO
  --audio auto|pa|alsa|sdl|none
                            DCS audio backend. Default and `auto`:
                            host-side autodetect (PulseAudio first,
                            then ALSA); falls back to `none` when no
                            host backend answers. Sets P2K_DCS_AUDIO=1
                            and `-audio driver=<x>` so AUD_register_card
                            binds. NOTE: `auto` is the *wrapper*
                            autodetect, NOT QEMU's `driver=auto`. The
                            DCS code path is unchanged.
  --no-audio                Force DCS audio off (overrides --audio).
  --pb2kslib <path>         Override pb2kslib container path
                            (P2K_PB2KSLIB=<path>). Default lookup is
                            <roms_dir>/<game>_sound.bin. No directory walks.
  --sound-loading lazy|preload
                            lazy   (default) decode samples on-demand.
                            preload  walk every pb2k entry at install
                            time and decode now (P2K_DCS_PRELOAD=1).
                            Adds ~1 s startup cost; eliminates first-
                            trigger decode hitch.

CONSOLE / DIAGNOSTICS
  --uart-quiet              Silence the COM1/UART mirror on stderr
                            (P2K_NO_UART_STDERR=1). Default is visible.
  --uart-tcp <host:port>    Bind COM1 to a TCP server socket
                            (-serial tcp:<host:port>,server,nowait).
                            Lets `nc <host> <port>` act as the XINU
                            console. Mutually exclusive with --headless.
  --serial-tcp <port>       Unicorn-compatible alias for
                            `--uart-tcp 127.0.0.1:<port>`.
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
  -v                        Same as --diag.
  -vv                       --diag + --trace-audio.
  -vvv                      --diag + --trace-audio + --trace-dcs.
  --dcs-mode io-handled|bar4-patch
                            Documents which Unicorn-parity DCS frontend
                            label this build matches; both run the same
                            shared BAR4 + UART core today (see
                            qemu/NOTES.next.md "DCS Mode Switch").

CABINET / FUTURE
  --cabinet | --cabinet-purist
                            Unicorn-parity flag. Records intent to trust
                            the real driver-board protocol (no host
                            switch-matrix injection). Only meaningful
                            paired with --lpt-device <hostdev>; without
                            it, the emulated board still answers
                            (P2K_CABINET_PURIST=1).
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
  --parport <device>        Unicorn-compatible alias for
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
  F3                        Screenshot to <screenshot-dir>/p2k_screen_<ts>.jpg
                            (default dir /tmp; override with
                             --screenshot-dir or P2K_SCREENSHOT_DIR.
                             Falls back to .ppm if no jpeg helper —
                             cjpeg / magick / convert — is on PATH)
  (Fullscreen toggle: use SDL's default Ctrl+Alt+F.
   F2 flip-Y, F11 fullscreen-via-F-key: not implemented yet —
   see qemu/NOTES.next.md "Must Finish Next".)

ENV PASSTHROUGH (advanced; see qemu/README.md for the full table)
  P2K_NO_UART_STDERR P2K_PIC_FIXUP P2K_NO_PIC_FIXUP
  P2K_NO_MEM_DETECT_PATCH P2K_DCS_AUDIO P2K_NO_DCS_AUDIO
  P2K_DCS_AUDIO_TRACE P2K_DCS_BYTE_TRACE P2K_DCS_NO_BYTE_PAIR
  P2K_DCS_RAW_55_PAIR P2K_DIAG P2K_NO_AUTO_UPDATE
  P2K_PB2KSLIB P2K_DCS_MODE P2K_SCREENSHOT_DIR
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
    --update)          UPDATE_TOKEN="$2"; shift 2 ;;
    --display)         DISPLAY_MODE="$2"; shift 2 ;;
    --headless)        HEADLESS=1; shift ;;
    --fullscreen)      FULLSCREEN=1; shift ;;
    --window-scale)    WINDOW_SCALE="$2"; shift 2 ;;
    --bpp)
      case "$2" in
        32) ;;  # default ARGB8888 path
        16) export P2K_DISPLAY_BPP=16 ;;  # native x1r5g5b5 surface
        24) echo "[run-qemu] --bpp 24 not supported, falling back to 32 (Unicorn parity)" >&2 ;;
        *)  echo "[run-qemu] --bpp $2 invalid; using 32 (Unicorn parity)" >&2 ;;
      esac
      shift 2 ;;
    --splash | --splash-screen)
      # Unicorn-shape: --splash-screen <default|none|path>. `default`/`none`
      # disable the splash; a path launches an image viewer in the background
      # for the boot duration (best-effort: xdg-open / feh / eog / display).
      if [[ "$1" == "--splash" ]]; then
        SPLASH_PATH=""; shift  # legacy: enable default (= no-op, no asset shipped)
      else
        case "$2" in
          default|none) SPLASH_PATH=""; shift 2 ;;
          *) [[ -f "$2" ]] || { echo "[run-qemu] --splash-screen: '$2' is not a readable file" >&2; exit 2; }
             SPLASH_PATH="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
             shift 2 ;;
        esac
      fi ;;
    --no-splash)       SPLASH_PATH=""; shift ;;
    --monitor)         MONITOR="$2"; shift 2 ;;
    --debug)           DEBUG="$2"; shift 2 ;;
    --uart-quiet)      export P2K_NO_UART_STDERR=1; shift ;;
    --uart-tcp)        UART_TCP="$2"; shift 2 ;;
    --serial-tcp)
      # Unicorn-compatible alias: --serial-tcp <port> ⇒ --uart-tcp 127.0.0.1:<port>
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
        auto) AUDIO=""; shift 2 ;;  # fall through to host autodetect below
        none) AUDIO=none; unset P2K_DCS_AUDIO || true; shift 2 ;;
        pa|alsa|sdl|oss|coreaudio|dsound|pipewire)
          AUDIO="$2"; export P2K_DCS_AUDIO=1; shift 2 ;;
        *) echo "[run-qemu] --audio: expected auto|pa|alsa|sdl|none, got '$2'" >&2; exit 2 ;;
      esac ;;
    --no-audio)        AUDIO="none"; unset P2K_DCS_AUDIO || true; shift ;;
    --pb2kslib)        export P2K_PB2KSLIB="$2"; shift 2 ;;
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
      # Unicorn flag is --cabinet-purist (experimental, opt-in to "trust
      # the real board" semantics). With our wrapper there's no real
      # cabinet bus to trust unless --lpt-device <hostdev> is also set,
      # so we just record the intent for downstream forensics.
      export P2K_CABINET_PURIST=1; shift ;;
    --lpt-device|--lpt)
      # Unicorn-shape: --lpt-device <none|emu|/dev/parportN|0xNNN>.
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
        parport)           export P2K_LPT_PARPORT="/dev/parport0" ;;  # Unicorn default probe target
        *) echo "[run-qemu] $1: expected emu|none|/dev/parportN|0xNNN, got '$LPT_MODE'" >&2; exit 2 ;;
      esac
      shift 2 ;;
    --lpt-trace)
      [[ -n "${2:-}" ]] || { echo "[run-qemu] --lpt-trace: expected <file>" >&2; exit 2; }
      LPT_TRACE_DIR="$(cd "$(dirname "$2")" 2>/dev/null && pwd)" || { echo "[run-qemu] --lpt-trace: parent dir of '$2' missing" >&2; exit 2; }
      export P2K_LPT_TRACE_FILE="$LPT_TRACE_DIR/$(basename "$2")"
      shift 2 ;;
    --parport)
      # Unicorn-compatible alias: --parport <dev> ⇒ --lpt-device <dev>
      [[ -n "${2:-}" ]] || { echo "[run-qemu] --parport: expected <device>" >&2; exit 2; }
      [[ -e "$2" ]] || { echo "[run-qemu] --parport: '$2' does not exist" >&2; exit 2; }
      export P2K_LPT_PARPORT="$2"; shift 2 ;;
    --tcg-only)        TCG_ONLY=1; shift ;;
    --)                shift; EXTRA+=("$@"); break ;;
    -h|--help)         print_help; exit 0 ;;
    *) echo "Unknown arg: $1 (try --help)" >&2; exit 2 ;;
  esac
done

# --- verbosity → diag/trace tier mapping -----------------------------------
if [[ $VERBOSITY -ge 1 ]]; then export P2K_DIAG=1; fi
if [[ $VERBOSITY -ge 2 ]]; then export P2K_DCS_AUDIO_TRACE=1; fi
if [[ $VERBOSITY -ge 3 ]]; then export P2K_DCS_BYTE_TRACE=1; fi

# --- QEMU binary lookup -----------------------------------------------------
QEMU_BIN="${QEMU_BIN:-$HOME/.cache/p2k-qemu-build/qemu-10.0.8/build/qemu-system-i386}"
[[ -x "$QEMU_BIN" ]] || QEMU_BIN="qemu-system-i386"

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
if [[ -n "$WINDOW_SCALE" && "$DISPLAY_MODE" == "none" ]]; then
  echo "[run-qemu] --window-scale ignored with --display none" >&2
  WINDOW_SCALE=""
fi

# --- audio auto-detect ------------------------------------------------------
# If the user did not pick anything, try the most likely host backend so DCS
# audio is audible by default. Probe PulseAudio first (most common on Linux
# desktops), then ALSA. If nothing answers, fall back to silent ("none").
if [[ -z "$AUDIO" ]]; then
  if command -v pactl >/dev/null 2>&1 && pactl info >/dev/null 2>&1; then
    AUDIO=pa
    export P2K_DCS_AUDIO=1
    echo "[run-qemu] audio: auto-detected PulseAudio (use --no-audio to silence)" >&2
  elif [[ -e /proc/asound/cards ]] && grep -q '^.[0-9]' /proc/asound/cards 2>/dev/null; then
    AUDIO=alsa
    export P2K_DCS_AUDIO=1
    echo "[run-qemu] audio: auto-detected ALSA (use --no-audio to silence)" >&2
  else
    AUDIO=none
    echo "[run-qemu] audio: no host backend detected; running silent" >&2
  fi
fi

# --- update token resolution ------------------------------------------------
# UPDATE_TOKEN ∈ {auto, latest, none, <short-code>, <dir>}
# Output: UPDATE_DIR_ABS (empty → no -M update=...; or P2K_NO_AUTO_UPDATE=1
# in the museum case).
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
    # probe-cell shim and any other museum compat gates only arm under
    # this env (see qemu/p2k-probe-cell-shim.c, qemu/p2k-dcs-core.c).
    export P2K_NO_AUTO_UPDATE=1
    echo "[run-qemu] --update none → museum mode (P2K_NO_AUTO_UPDATE=1)" >&2
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

# --- savedata cwd handling --------------------------------------------------
# The QEMU machine reads savedata/<game>.* relative to cwd. Choose cwd
# accordingly. --no-savedata = empty tmp dir for this run only.
RUN_CWD="$ROOT"
CLEANUP=""
if [[ $NO_SAVEDATA -eq 1 ]]; then
  RUN_CWD="$(mktemp -d)"
  CLEANUP="$RUN_CWD"
  trap '[[ -n "$CLEANUP" ]] && rm -rf "$CLEANUP"' EXIT
  echo "[run-qemu] --no-savedata: running in $RUN_CWD (no savedata/ subdir)"
elif [[ "$SAVEDATA_DIR" != "$ROOT/savedata" ]]; then
  RUN_CWD="$(mktemp -d)"
  CLEANUP="$RUN_CWD"
  trap '[[ -n "$CLEANUP" ]] && rm -rf "$CLEANUP"' EXIT
  ln -s "$SAVEDATA_DIR" "$RUN_CWD/savedata"
fi

# --- assemble QEMU command line --------------------------------------------
ARGS=( -no-reboot -m 16 -display "$DISPLAY_MODE" )
[[ $FULLSCREEN -eq 1 ]] && ARGS+=( -full-screen )
[[ -n "$WINDOW_SCALE" && "$DISPLAY_MODE" == "sdl" ]] && \
  ARGS+=( -global "sdl-display.window-scale=$WINDOW_SCALE" )

if [[ -n "$UART_TCP" ]]; then
  if [[ $HEADLESS -eq 1 ]]; then
    echo "[run-qemu] ERROR: --uart-tcp is mutually exclusive with --headless" >&2
    exit 2
  fi
  ARGS+=( -serial "tcp:${UART_TCP},server,nowait" )
elif [[ $HEADLESS -eq 1 ]]; then
  ARGS+=( -serial stdio )
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

# --- splash screen (best-effort host viewer) -------------------------------
SPLASH_PID=""
if [[ -n "$SPLASH_PATH" ]]; then
  for viewer in feh eog display xdg-open; do
    if command -v "$viewer" >/dev/null 2>&1; then
      ( "$viewer" "$SPLASH_PATH" >/dev/null 2>&1 ) &
      SPLASH_PID=$!
      echo "[run-qemu] splash: $viewer $SPLASH_PATH (pid=$SPLASH_PID)"
      break
    fi
  done
  if [[ -z "$SPLASH_PID" ]]; then
    echo "[run-qemu] --splash-screen: no host viewer found (feh/eog/display/xdg-open); ignoring" >&2
  fi
fi

if [[ -n "$SPLASH_PID" ]]; then
  "$QEMU_BIN" "${ARGS[@]}" "${EXTRA[@]}"
  rc=$?
  kill "$SPLASH_PID" 2>/dev/null || true
  exit $rc
fi
exec "$QEMU_BIN" "${ARGS[@]}" "${EXTRA[@]}"
