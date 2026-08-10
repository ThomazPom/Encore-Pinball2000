#!/usr/bin/env bash
# Cabinet session entry point. Standalone modes reach this through
# agetty -> login -> PAM/logind; desktop mode reaches it through XDG autostart.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONF=/etc/encore-pinball2000/session.conf
ARGS_FILE=/etc/encore-pinball2000/launch.args

if [[ "${1:-}" == --login ]]; then
    session_user="${2:-}"
    cabinet_tty="${3:-tty1}"
    [[ $(id -u) -eq 0 ]] || { echo "encore-session: --login requires root" >&2; exit 1; }
    session_uid="$(id -u "$session_user" 2>/dev/null)" || exit 1
    [[ "$session_uid" -ge 1000 ]] || { echo "encore-session: unsafe login user" >&2; exit 1; }

    # /bin/login creates the actual PAM/logind session. When Encore exits,
    # replace the automatic entry point with an ordinary maintenance prompt.
    /bin/login -f "$session_user" -s "$ROOT/scripts/encore-session.sh"
    exec /sbin/agetty --noclear "$cabinet_tty" "${TERM:-linux}"
fi

[[ -r "$CONF" ]] || { echo "encore-session: run ./install.sh first" >&2; exit 1; }
# The file is root-owned, mode 0644, and contains only installer-generated
# scalar assignments.
# shellcheck source=/dev/null
source "$CONF"
export QEMU_BIN

[[ "$(id -u)" -eq "$SESSION_UID" ]] || {
    echo "encore-session: expected uid $SESSION_UID, got $(id -u)" >&2
    exit 1
}

launch_args=()
if [[ -s "$ARGS_FILE" ]]; then
    mapfile -t launch_args < "$ARGS_FILE"
fi

launch_encore() {
    local video="$1"
    shift
    case "$video" in
        wayland)
            [[ -n "${WAYLAND_DISPLAY:-}" && -n "${XDG_RUNTIME_DIR:-}" ]] || {
                echo "encore-session: Wayland session is not ready" >&2
                return 1
            }
            exec systemd-inhibit --what=idle:sleep:shutdown --mode=block \
                --why="Encore cabinet session" -- \
                "$ROOT/scripts/run-qemu.sh" --wayland --framebuffer \
                --fullscreen --game "$GAME" "${launch_args[@]}" "$@"
            ;;
        direct)
            unset DISPLAY XAUTHORITY WAYLAND_DISPLAY
            exec systemd-inhibit --what=idle:sleep:shutdown --mode=block \
                --why="Encore cabinet session" -- \
                "$ROOT/scripts/run-qemu.sh" --direct-console --framebuffer \
                --fullscreen --game "$GAME" "${launch_args[@]}" "$@"
            ;;
        *) echo "encore-session: invalid video path '$video'" >&2; return 2 ;;
    esac
}

case "${1:-}" in
    --client)
        [[ "${2:-}" == wayland ]] || exit 2
        launch_encore wayland
        ;;
    --desktop)
        [[ "${XDG_SESSION_TYPE:-}" == wayland ]] || {
            echo "encore-session: the selected display-manager session is not Wayland" >&2
            exit 1
        }
        launch_encore wayland
        ;;
esac

# Keep compositor diagnostics out of the cabinet VT while retaining them in
# journalctl. The child itself inherits the compositor-created Wayland socket.
case "$BACKEND" in
    cage)
        exec systemd-cat --identifier=encore-cage -- \
            cage -d -s -- "$0" --client wayland
        ;;
    weston)
        exec systemd-cat --identifier=encore-weston -- \
            weston --backend=drm --shell=kiosk --idle-time=0 --no-config -- \
            "$0" --client wayland
        ;;
    direct-console)
        launch_encore direct
        ;;
    display-manager)
        launch_encore wayland
        ;;
    *) echo "encore-session: unsupported backend '$BACKEND'" >&2; exit 2 ;;
esac
