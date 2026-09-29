#!/usr/bin/env bash
# Launch a normal graphical SWE1 2.10 session and attach a read-only GDB trace
# to the seven native new-game eligibility checks.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GDB_COMMANDS="$ROOT/tools/start-gates-swe1-0210.gdb"
PORT="${P2K_START_GDB_PORT:-1235}"
STAMP="$(date +%Y%m%d-%H%M%S)"
TRACE_LOG="${P2K_START_TRACE_LOG:-/tmp/encore-start-gates-$STAMP.log}"
QEMU_LOG="${P2K_START_QEMU_LOG:-/tmp/encore-start-gates-$STAMP.qemu.log}"
QEMU_PID=""

usage() {
  cat <<'EOF'
Usage: tools/trace-start-gates.sh [normal run-qemu options]

Starts SWE1 2.10 with the emulated cabinet and the normal graphical/audio
path, then attaches GDB without stopping boot.  Additional arguments are
forwarded to run-qemu.sh, except options that would change the fixed game,
update, display mode or GDB endpoint.

Environment:
  P2K_START_GDB_PORT   GDB port (default 1235)
  P2K_START_TRACE_LOG  decision log path (default under /tmp)
  P2K_START_QEMU_LOG   launcher/QEMU log path (default under /tmp)
EOF
}

case "$PORT" in
  ''|*[!0-9]*) echo "trace-start-gates: invalid port '$PORT'" >&2; exit 2 ;;
esac
if (( PORT < 1024 || PORT > 65535 )); then
  echo "trace-start-gates: port must be between 1024 and 65535" >&2
  exit 2
fi

for argument in "$@"; do
  case "$argument" in
    -h|--help) usage; exit 0 ;;
    --game|--update|--headless|--display|--lpt-device|-gdb|-S)
      echo "trace-start-gates: '$argument' conflicts with the fixed SWE1 2.10 graphical trace" >&2
      exit 2
      ;;
  esac
done

for command_name in gdb ss; do
  command -v "$command_name" >/dev/null 2>&1 || {
    echo "trace-start-gates: missing required command: $command_name" >&2
    exit 2
  }
done

if ss -ltnH "sport = :$PORT" | grep -q .; then
  echo "trace-start-gates: TCP port $PORT is already in use" >&2
  exit 2
fi

cleanup() {
  if [[ -n "$QEMU_PID" ]] && kill -0 "$QEMU_PID" 2>/dev/null; then
    kill -TERM "$QEMU_PID" 2>/dev/null || true
    wait "$QEMU_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT HUP INT TERM

echo "[start-trace] decision log: $TRACE_LOG"
echo "[start-trace] QEMU log:     $QEMU_LOG"
echo "[start-trace] launching normal graphical SWE1 2.10 session"
echo "[start-trace] press Start deliberately (about once per second); GDB pauses are intrusive"

"$ROOT/scripts/run-qemu.sh" \
  --game swe1 --update 2.10 --lpt-device emulated \
  "$@" -- -gdb "tcp:127.0.0.1:$PORT" >"$QEMU_LOG" 2>&1 &
QEMU_PID=$!

ready=0
for _ in $(seq 1 100); do
  if ! kill -0 "$QEMU_PID" 2>/dev/null; then
    echo "trace-start-gates: Encore exited before GDB became ready" >&2
    tail -80 "$QEMU_LOG" >&2 || true
    exit 1
  fi
  if ss -ltnH "sport = :$PORT" | grep -q .; then
    ready=1
    break
  fi
  sleep 0.1
done
if (( ! ready )); then
  echo "trace-start-gates: GDB port $PORT did not become ready" >&2
  exit 1
fi

echo "[start-trace] attaching GDB; press Start repeatedly in the game window"
gdb -q -batch \
  -ex "set logging file $TRACE_LOG" \
  -ex "target remote 127.0.0.1:$PORT" \
  -x "$GDB_COMMANDS"
