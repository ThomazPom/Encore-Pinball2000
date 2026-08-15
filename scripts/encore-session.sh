#!/usr/bin/env bash
# Cabinet session entry point. Standalone modes reach this through
# agetty -> login -> PAM/logind; display-manager mode reaches it through the
# selected user's graphical-session.target.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONF=/etc/encore-pinball2000/session.conf
ARGS_FILE=/etc/encore-pinball2000/launch.args

if [[ "${1:-}" == --root-service ]]; then
    [[ $(id -u) -eq 0 ]] || exit 1
    # shellcheck source=/dev/null
    source "$CONF"
    runtime_dir="/run/user/$SESSION_UID"
    env_file="$runtime_dir/encore-pinball2000/display-environment"
    for _ in {1..1800}; do [[ -f "$env_file" ]] && break; sleep 0.1; done
    [[ -f "$env_file" && "$(stat -c %u "$env_file")" == "$SESSION_UID" ]] || exit 4
    wayland_display="$(sed -n 's/^WAYLAND_DISPLAY=//p' "$env_file" | head -n1)"
    cleanup_root_service() { rm -f "$env_file"; }
    trap cleanup_root_service EXIT INT TERM
    export HOME="$(getent passwd "$SESSION_USER" | cut -d: -f6)"
    export QEMU_BIN
    export XDG_RUNTIME_DIR="$runtime_dir"
    export DBUS_SESSION_BUS_ADDRESS="unix:path=$runtime_dir/bus"
    [[ -z "$wayland_display" ]] || export WAYLAND_DISPLAY="$wayland_display"
    mapfile -t launch_args < "$ARGS_FILE"
    video_args=(--direct-console --framebuffer)
    [[ "$BACKEND" == direct-console ]] || video_args=(--wayland --framebuffer)
    systemd-inhibit --what=idle:sleep:shutdown --mode=block \
        --why="Encore cabinet root diagnostic" -- \
        "$ROOT/scripts/run-qemu.sh" "${video_args[@]}" --fullscreen \
        --game "$GAME" "${launch_args[@]}"
    exit $?
fi

if [[ "${1:-}" == --wait-wayland ]]; then
    # ExecStart is spawned only after this readiness process exits, so it gets
    # the user manager's freshly imported environment without copying it.
    runtime_dir="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
    for _ in {1..1800}; do
        manager_environment="$(systemctl --user show-environment 2>/dev/null || true)"
        wayland_display="$(sed -n 's/^WAYLAND_DISPLAY=//p' <<< "$manager_environment" | head -n1)"
        session_type="$(sed -n 's/^XDG_SESSION_TYPE=//p' <<< "$manager_environment" | head -n1)"
        case "$wayland_display" in
            ""|*/*|.|..) ;;
            *)
                if [[ "$session_type" == wayland && -S "$runtime_dir/$wayland_display" ]]; then
                    exit 0
                fi
                ;;
        esac
        sleep 0.1
    done
    echo "encore-session: Wayland login environment timeout" >&2
    exit 4
fi

if [[ "${1:-}" == --maintenance ]]; then
    [[ $(id -u) -eq 0 ]] || exit 1
    # ExecStopPost reaches this only after agetty/login and PAM/logind have
    # closed the cabinet session. Replace subsequent starts during this boot
    # with an ordinary password-backed maintenance getty.
    install -d -m 0755 /run/systemd/system/getty@tty1.service.d
    cat > /run/systemd/system/getty@tty1.service.d/50-encore-maintenance.conf <<'EOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --noclear %I $TERM
StandardOutput=tty
StandardError=tty
Restart=always
RestartSec=0.2
EOF
    systemctl daemon-reload
    exit 0
fi

[[ -r "$CONF" ]] || { echo "encore-session: run ./install.sh first" >&2; exit 1; }
# The file is root-owned, mode 0644, and contains only installer-generated
# scalar assignments.
# shellcheck source=/dev/null
source "$CONF"
export QEMU_BIN

# Standalone installation temporarily makes the system copy of this script
# the account's login shell. Only tty1 is the cabinet entry point. SSH, other
# VTs and the post-game maintenance getty retain the user's original shell.
if [[ "${STANDALONE_LOGIN_SHELL:-0}" -eq 1 && -z "${1:-}" ]]; then
    login_tty="$(tty 2>/dev/null || true)"
    if [[ "$login_tty" != /dev/tty1 || -e /run/systemd/system/getty@tty1.service.d/50-encore-maintenance.conf ]]; then
        exec "$ORIGINAL_SHELL" -l
    fi
fi

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
    if [[ "${RUN_AS_ROOT:-0}" -eq 1 ]]; then
        runtime_dir="${XDG_RUNTIME_DIR:?XDG_RUNTIME_DIR is required}"
        session_dir="$runtime_dir/encore-pinball2000"
        env_file="$session_dir/display-environment"
        umask 077
        mkdir -p "$session_dir"
        tmp="$(mktemp "$session_dir/environment.XXXXXX")"
        [[ "$video" != wayland ]] || printf 'WAYLAND_DISPLAY=%s\n' "${WAYLAND_DISPLAY:-}" > "$tmp"
        mv -f "$tmp" "$env_file"
        while [[ -e "$env_file" ]]; do sleep 0.2; done
        rmdir "$session_dir" 2>/dev/null || true
        return 0
    fi
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

dismiss_session_overview() {
    # SDL owns the portable fullscreen/focus requests. Some full desktop
    # shells deliberately keep a login overview above newly mapped clients;
    # dismiss only an overview that the running compositor publicly exposes.
    # Kiosk/tiling compositors need no adapter and remain untouched.
    command -v busctl >/dev/null 2>&1 || return 0
    (
        for _ in {1..50}; do
            if busctl --user --quiet status org.gnome.Shell >/dev/null 2>&1; then
                busctl --user set-property org.gnome.Shell /org/gnome/Shell \
                    org.gnome.Shell OverviewActive b false >/dev/null 2>&1 || true
            fi
            if busctl --user --quiet status org.kde.KWin >/dev/null 2>&1; then
                active_effects="$(busctl --user get-property org.kde.KWin /Effects \
                    org.kde.kwin.Effects activeEffects 2>/dev/null || true)"
                if [[ "$active_effects" == *'"overview"'* ]]; then
                    busctl --user call org.kde.KWin /Effects \
                        org.kde.kwin.Effects toggleEffect s overview \
                        >/dev/null 2>&1 || true
                fi
            fi
            sleep 0.1
        done
    ) &
}

case "${1:-}" in
    --client)
        [[ "${2:-}" == wayland ]] || exit 2
        launch_encore wayland
        exit $?
        ;;
    --desktop)
        [[ "${XDG_SESSION_TYPE:-}" == wayland ]] || {
            echo "encore-session: the selected display-manager session is not Wayland" >&2
            exit 1
        }
        dismiss_session_overview
        launch_encore wayland
        exit $?
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
