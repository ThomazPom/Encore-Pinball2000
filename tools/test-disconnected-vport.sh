#!/usr/bin/env bash
# Lab helper: create a real ppdev port over an unimplemented ISA address, run Encore
# against that open bus, then remove the temporary port on every exit path.
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
IO_ADDRESS=0x278
PORT=/dev/parport0

if [[ $EUID -eq 0 ]]; then
    echo "Run this helper as the desktop user, not as root." >&2
    exit 2
fi

if [[ -d /sys/module/parport_pc ]] || compgen -G '/dev/parport[0-9]*' >/dev/null; then
    echo "A parallel-port configuration already exists; refusing to alter it." >&2
    exit 2
fi

if command -v run0 >/dev/null 2>&1; then
    elevate=(run0 --description="Encore disconnected LPT test" --)
elif command -v sudo >/dev/null 2>&1; then
    elevate=(sudo)
else
    echo "This helper needs run0 or sudo to load the temporary kernel port." >&2
    exit 2
fi

loaded_ppdev=0
loaded_parport_pc=0
cleanup() {
    local status=$?
    trap - EXIT INT TERM HUP
    if ((loaded_ppdev)); then
        "${elevate[@]}" modprobe -r ppdev 2>/dev/null || true
    fi
    if ((loaded_parport_pc)); then
        "${elevate[@]}" modprobe -r parport_pc 2>/dev/null || true
    fi
    echo "Temporary disconnected LPT vport removed."
    exit "$status"
}
trap cleanup EXIT INT TERM HUP

echo "Creating an open-bus ppdev vport at ISA $IO_ADDRESS..."
"${elevate[@]}" modprobe parport_pc io="$IO_ADDRESS" irq=none
loaded_parport_pc=1
"${elevate[@]}" modprobe ppdev
loaded_ppdev=1
"${elevate[@]}" udevadm settle 2>/dev/null || true

[[ -c $PORT ]] || {
    echo "The kernel did not create $PORT." >&2
    exit 3
}

# The node disappears with the module, so temporary ownership needs no
# restoration. Keep the emulator itself unprivileged and in the live desktop
# session so Wayland/XDG/audio variables remain normal.
"${elevate[@]}" chown "$(id -u):$(id -g)" "$PORT"
"${elevate[@]}" chmod 0600 "$PORT"

echo "Launching Encore through real ppdev with no board behind it."
echo "The ROM should report CHECK F108 AND POWER CABLE. F1 exits."
exec 3>&-
"$ROOT/scripts/run-qemu.sh" \
    --cabinet \
    --lpt-device "$PORT" \
    --game swe1 \
    --framebuffer \
    --fullscreen \
    "$@"
