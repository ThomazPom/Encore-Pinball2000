#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
RUNTIME_ROOT=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/encore-serial-updater
CACHE_ROOT=${XDG_CACHE_HOME:-$HOME/.cache}/encore/serial-updater
UNIT=encore-serial-bridge.service
UPDATER_ENDPOINT=$RUNTIME_ROOT/updater
GUEST_ENDPOINT=$RUNTIME_ROOT/guest
DRIVERS_FILE=$RUNTIME_ROOT/tty-drivers
SHIM=$CACHE_ROOT/serial-pty-compat.so
SOURCE=$SCRIPT_DIR/serial-pty-compat.c

die() {
    echo "[serial-updater-bridge] $*" >&2
    exit 2
}

need() {
    command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"
}

build_shim() {
    need cc
    mkdir -p -- "$CACHE_ROOT"
    chmod 700 -- "$CACHE_ROOT"
    if [[ ! -r "$SHIM" || "$SOURCE" -nt "$SHIM" ]]; then
        cc -shared -fPIC -O2 -Wall -Wextra -Werror \
            -o "$SHIM.tmp" "$SOURCE" -ldl
        chmod 755 -- "$SHIM.tmp"
        mv -f -- "$SHIM.tmp" "$SHIM"
    fi
    printf '%s\n' "$SHIM"
}

bridge_ready() {
    [[ -L "$UPDATER_ENDPOINT" && -c "$UPDATER_ENDPOINT" &&
       -L "$GUEST_ENDPOINT" && -c "$GUEST_ENDPOINT" ]]
}

start_bridge() {
    need socat
    need systemctl
    need systemd-run
    mkdir -p -- "$RUNTIME_ROOT"
    chmod 700 -- "$RUNTIME_ROOT"
    printf '%s\n' 'serial_8250 /dev/full 1 7 serial' >"$DRIVERS_FILE"
    chmod 600 -- "$DRIVERS_FILE"

    if systemctl --user is-active --quiet "$UNIT" && bridge_ready; then
        return
    fi

    systemctl --user stop "$UNIT" >/dev/null 2>&1 || true
    systemctl --user reset-failed "$UNIT" >/dev/null 2>&1 || true
    rm -f -- "$UPDATER_ENDPOINT" "$GUEST_ENDPOINT"
    systemd-run --user --quiet --unit="$UNIT" --collect \
        --property=Restart=on-failure --property=RestartSec=1 \
        --description='Encore private serial update bridge' \
        "$(command -v socat)" \
        "PTY,link=$UPDATER_ENDPOINT,rawer,echo=0" \
        "PTY,link=$GUEST_ENDPOINT,rawer,echo=0"

    for _attempt in {1..50}; do
        bridge_ready && return
        sleep 0.1
    done
    systemctl --user --no-pager --full status "$UNIT" >&2 || true
    die "the PTY service did not create both endpoints"
}

stop_bridge() {
    systemctl --user stop "$UNIT" >/dev/null 2>&1 || true
    rm -f -- "$UPDATER_ENDPOINT" "$GUEST_ENDPOINT" "$DRIVERS_FILE"
}

guest_device() {
    start_bridge
    readlink -f -- "$GUEST_ENDPOINT"
}

run_updater() {
    (($# >= 1)) || die "run expects an absolute updater executable path"
    local executable=$1
    shift
    [[ "$executable" == /* && -x "$executable" ]] ||
        die "updater executable is missing or not executable: $executable"
    start_bridge
    build_shim >/dev/null
    cd -- "$(dirname -- "$executable")"
    export ENCORE_SERIAL_DRIVERS_FILE=$DRIVERS_FILE
    export ENCORE_SERIAL_UPDATER_ALIAS=/dev/full
    export ENCORE_SERIAL_UPDATER_DEVICE=$UPDATER_ENDPOINT
    export ENCORE_SERIAL_UPDATER_CRLF=1
    export LD_PRELOAD="$SHIM${LD_PRELOAD:+:$LD_PRELOAD}"
    exec "$executable" "$@"
}

usage() {
    cat <<'EOF'
Usage: tools/serial-updater-bridge/serial-updater-bridge.sh COMMAND [ARG...]

Commands:
  start                 Start the private PTY bridge user service.
  stop                  Stop it and remove its runtime endpoints.
  status                Show its user-service status and endpoint paths.
  guest-device          Print the /dev/pts device for run-qemu.sh.
  build-shim            Build the clean compatibility shim and print its path.
  run /absolute/program Launch an external updater through the private endpoint.
EOF
}

command=${1:-}
[[ $# -eq 0 ]] || shift
case "$command" in
    start)
        start_bridge
        printf 'guest:   %s\nupdater: %s\n' \
            "$(readlink -f -- "$GUEST_ENDPOINT")" "$UPDATER_ENDPOINT"
        ;;
    stop)
        stop_bridge
        ;;
    status)
        systemctl --user --no-pager --full status "$UNIT" || true
        if bridge_ready; then
            printf 'guest:   %s\nupdater: %s\n' \
                "$(readlink -f -- "$GUEST_ENDPOINT")" "$UPDATER_ENDPOINT"
        fi
        ;;
    guest-device)
        guest_device
        ;;
    build-shim)
        build_shim
        ;;
    run)
        run_updater "$@"
        ;;
    -h|--help|help)
        usage
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac
