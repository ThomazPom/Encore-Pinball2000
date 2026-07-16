#!/usr/bin/env bash
# Run a 40 s headless/audio matrix and capture full audit panel to logs.
# Usage: scripts/run-matrix.sh <tag> [extra env exports]
# Files: docs/measurements/partc/<tag>_<scenario>.log
set -u
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
TAG="${1:?usage: run-matrix.sh <tag>}"
shift || true
LOGDIR="$ROOT/docs/measurements/partc"
mkdir -p "$LOGDIR"

run_one() {
  local scenario="$1"; shift
  local logf="$LOGDIR/${TAG}_${scenario}.log"
  echo "[matrix] $TAG/$scenario -> $logf"
  env P2K_DIAG=1 "$@" timeout 40 bash "$ROOT/scripts/run-qemu.sh" \
      ${P2K_MATRIX_ARGS:-} >"$logf" 2>&1 || true
}

# headless swe1
P2K_MATRIX_ARGS="--headless --game swe1 --update none --no-savedata" \
    run_one swe1_headless "$@"
sleep 2
# headless rfm
P2K_MATRIX_ARGS="--headless --game rfm --update none --no-savedata" \
    run_one rfm_headless "$@"
sleep 2
# sdl + pulseaudio swe1 (needs DISPLAY; -v keeps info: lines for parsing)
P2K_MATRIX_ARGS="--game swe1 --update none --no-savedata --display sdl --audio pa -v" \
    run_one swe1_sdlpa "$@"
