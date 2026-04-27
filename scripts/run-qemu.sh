#!/usr/bin/env bash
# Run Pinball 2000 firmware under QEMU.
#
# IMPORTANT: stock qemu-system-i386 cannot boot Pinball 2000.  The game
# entry point is EIP=0x801D9 inside the PRISM option ROM (first 32 KiB
# of game ROM bank 0), reached from a hand-built protected-mode state.
# A custom 'pinball2000' machine type is required — see qemu/README.md.
#
# Args:
#   --game <id>       Game ROM bank (swe1, rfm). Default: swe1.
#   --headless        No graphics window; serial to stdout.
#   --debug <opts>    QEMU -d options (e.g. int,cpu_reset,in_asm).
#   --tcg-only        Smoke-test the host QEMU binary alone (no Pinball
#                     2000 hardware, no game boot — just confirms TCG runs).
set -euo pipefail

GAME=swe1
HEADLESS=0
TCG_ONLY=0
DEBUG=""
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --game)      GAME="$2"; shift 2 ;;
    --headless)  HEADLESS=1; shift ;;
    --tcg-only)  TCG_ONLY=1; shift ;;
    --debug)     DEBUG="$2"; shift 2 ;;
    -h|--help)
      sed -n '2,/^set -euo/p' "$0" | sed 's/^# \?//; /^set -euo/d'
      exit 0 ;;
    *) echo "Unknown arg: $1" >&2; exit 2 ;;
  esac
done

QEMU_BIN="${QEMU_BIN:-$HOME/.cache/p2k-qemu-build/qemu-10.0.8/build/qemu-system-i386}"
[[ -x "$QEMU_BIN" ]] || QEMU_BIN="qemu-system-i386"
ARGS=( -no-reboot -m 16 )
[[ $HEADLESS -eq 1 ]] && ARGS+=( -nographic ) || ARGS+=( -serial stdio )
[[ -n "$DEBUG" ]]    && ARGS+=( -d "$DEBUG" -D /tmp/p2k_qemu.log )

if [[ $TCG_ONLY -eq 1 ]]; then
  ARGS=( -M isapc "${ARGS[@]}" -bios "$ROOT/roms/bios.bin" )
  echo "[run-qemu] TCG smoke-test (NOT a Pinball 2000 boot)"
  exec "$QEMU_BIN" "${ARGS[@]}"
fi

ARGS=( -M pinball2000,game="$GAME",roms-dir="$ROOT/roms" "${ARGS[@]}" )
echo "[run-qemu] $QEMU_BIN ${ARGS[*]}"
exec "$QEMU_BIN" "${ARGS[@]}"
