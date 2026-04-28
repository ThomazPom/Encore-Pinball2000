#!/usr/bin/env bash
# Run Williams Pinball 2000 firmware under our custom QEMU 'pinball2000'
# machine. Stock qemu-system-i386 cannot boot — see qemu/README.md.
#
# Args:
#   --game <id>           swe1 | rfm. Default: swe1.
#   --roms <dir>          Override ROM directory. Default: <repo>/roms.
#   --savedata <dir>      Override savedata directory (default: <repo>/savedata).
#                         The QEMU machine reads <savedata>/<game>.{flash,nvram2}
#                         from the process cwd, so we cd into the parent.
#   --no-savedata         Run without persistent savedata (clean boot).
#                         Implemented by cd'ing into a temp dir with no
#                         savedata/ subdir for this run only.
#   --update <dir>        Stage an update bundle. Currently informational —
#                         NOTE: the machine does not yet consume updates/.
#   --display <mode>      QEMU -display backend (sdl, gtk, none).
#                         Default: sdl on a graphical session, none otherwise.
#   --headless            Shortcut for --display none -serial stdio.
#   --monitor <spec>      QEMU -monitor target (e.g. stdio, tcp:127.0.0.1:55555).
#   --debug <opts>        QEMU -d options (e.g. int,cpu_reset,in_asm).
#   --uart-quiet          Pass P2K_NO_UART_STDERR=1 to silence the UART mirror.
#   --audio <driver>      Enable DCS audio with the named QEMU audio backend
#                         (pa, alsa, sdl, coreaudio, dsound, none). Sets
#                         P2K_DCS_AUDIO=1 + `-audio driver=<driver>` so the
#                         AUD_register_card binds. Default: off (silent).
#   --no-audio            Force DCS audio off even if P2K_DCS_AUDIO is set
#                         in the environment.
#   -v|-vv|-vvv           Increase verbosity (info/debug/trace) — currently
#                         maps to P2K_DIAG=1 plus optional QEMU -d trace levels.
#   --tcg-only            Smoke-test the host QEMU binary alone (no Pinball
#                         2000 hardware, no game boot).
#   --                    All remaining args are passed straight to qemu-system-i386.
#
# Useful env passthrough (see qemu/README.md table):
#   P2K_NO_UART_STDERR P2K_PIC_FIXUP P2K_NO_PIC_FIXUP P2K_NO_IRQ0_SHIM
#   P2K_WATCHDOG_SCRIBBLER P2K_NO_WATCHDOG P2K_NO_MEM_DETECT_PATCH
#   P2K_DIAG P2K_UART_INPUT
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GAME=swe1
ROMS_DIR="$ROOT/roms"
SAVEDATA_DIR="$ROOT/savedata"
UPDATE_DIR=""
DISPLAY_MODE=""
HEADLESS=0
NO_SAVEDATA=0
MONITOR=""
DEBUG=""
TCG_ONLY=0
VERBOSITY=0
AUDIO=""
EXTRA=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --game)         GAME="$2"; shift 2 ;;
    --roms)         ROMS_DIR="$2"; shift 2 ;;
    --savedata)     SAVEDATA_DIR="$2"; shift 2 ;;
    --no-savedata)  NO_SAVEDATA=1; shift ;;
    --update)       UPDATE_DIR="$2"; shift 2 ;;
    --display)      DISPLAY_MODE="$2"; shift 2 ;;
    --headless)     HEADLESS=1; shift ;;
    --monitor)      MONITOR="$2"; shift 2 ;;
    --debug)        DEBUG="$2"; shift 2 ;;
    --uart-quiet)   export P2K_NO_UART_STDERR=1; shift ;;
    --audio)        AUDIO="$2"; export P2K_DCS_AUDIO=1; shift 2 ;;
    --no-audio)     AUDIO="none"; unset P2K_DCS_AUDIO; shift ;;
    -v)             VERBOSITY=1; shift ;;
    -vv)            VERBOSITY=2; shift ;;
    -vvv)           VERBOSITY=3; shift ;;
    --tcg-only)     TCG_ONLY=1; shift ;;
    --)             shift; EXTRA+=("$@"); break ;;
    -h|--help)
      sed -n '2,/^set -euo/p' "$0" | sed 's/^# \?//; /^set -euo/d'
      exit 0 ;;
    *) echo "Unknown arg: $1" >&2; exit 2 ;;
  esac
done

QEMU_BIN="${QEMU_BIN:-$HOME/.cache/p2k-qemu-build/qemu-10.0.8/build/qemu-system-i386}"
[[ -x "$QEMU_BIN" ]] || QEMU_BIN="qemu-system-i386"

if [[ -z "$DISPLAY_MODE" ]]; then
  if [[ $HEADLESS -eq 1 ]]; then
    DISPLAY_MODE=none
  elif [[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
    DISPLAY_MODE=sdl
  else
    DISPLAY_MODE=none
  fi
fi

# Audio auto-detect: if the user didn't pick anything, try to pick a
# working host backend so DCS audio is audible by default. We probe
# PulseAudio first (most common on Linux desktops), then ALSA. If
# nothing answers, fall back to silent ("none" = no -audio, hook stays
# wired but inaudible). User can always override with --audio / --no-audio.
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

if [[ $VERBOSITY -ge 1 ]]; then export P2K_DIAG=1; fi

if [[ -n "$UPDATE_DIR" ]]; then
  echo "[run-qemu] WARNING: --update <dir> not yet consumed by machine; staging directory only" >&2
fi

# The machine reads savedata/<game>.* relative to cwd. Choose cwd accordingly.
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

ARGS=( -no-reboot -m 16 -display "$DISPLAY_MODE" )
[[ -n "$MONITOR" ]] && ARGS+=( -monitor "$MONITOR" )
[[ $HEADLESS -eq 1 ]] && ARGS+=( -serial stdio )
[[ -n "$DEBUG" ]] && ARGS+=( -d "$DEBUG" -D /tmp/p2k_qemu.log )

# Audio: opt-in. -audio driver=<x> is the simplest way to set the
# default audiodev; QEMU's AUD_register_card binds to it. With no
# --audio flag, no audiodev is configured and DCS audio stays silent
# regardless of P2K_DCS_AUDIO. Suggested values:
#   pa | alsa | sdl | dsound | coreaudio | none
if [[ -n "$AUDIO" && "$AUDIO" != "none" ]]; then
  ARGS+=( -audio "driver=$AUDIO" )
fi

if [[ $TCG_ONLY -eq 1 ]]; then
  ARGS=( -M isapc "${ARGS[@]}" -bios "$ROMS_DIR/bios.bin" )
  echo "[run-qemu] TCG smoke-test (NOT a Pinball 2000 boot)"
  cd "$RUN_CWD"
  exec "$QEMU_BIN" "${ARGS[@]}" "${EXTRA[@]}"
fi

ARGS=( -M pinball2000,game="$GAME",roms-dir="$ROMS_DIR" "${ARGS[@]}" )
echo "[run-qemu] cwd=$RUN_CWD"
echo "[run-qemu] $QEMU_BIN ${ARGS[*]} ${EXTRA[*]:-}"
cd "$RUN_CWD"
exec "$QEMU_BIN" "${ARGS[@]}" "${EXTRA[@]}"
