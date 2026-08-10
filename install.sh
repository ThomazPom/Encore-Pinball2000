#!/usr/bin/env bash
# Install Encore as a native Wayland or direct-console cabinet session.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
CONF_DIR=/etc/encore-pinball2000
STATE=/var/lib/encore-pinball2000
GETTY_DROPIN=/etc/systemd/system/getty@tty1.service.d/49-encore.conf
AUTOSTART=/etc/xdg/autostart/encore-cabinet.desktop

usage() {
    cat <<'EOF'
Usage: ./install.sh [--display-manager|--cage|--weston|--direct-console]

  --display-manager  use the existing Wayland desktop session (recommended)
  --cage             standalone minimal Wayland kiosk
  --weston           standalone reference Wayland kiosk
  --direct-console   SDL2 KMSDRM, with no compositor
EOF
}

ask() {
    local prompt="$1" default="${2:-Y}" answer
    if [[ "$default" == Y ]]; then
        read -r -p "$prompt [Y/n] " answer
        [[ ! "$answer" =~ ^[Nn]$ ]]
    else
        read -r -p "$prompt [y/N] " answer
        [[ "$answer" =~ ^[Yy]$ ]]
    fi
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
esac

if [[ ${EUID} -ne 0 ]]; then
    for esc in run0 sudo pkexec; do
        command -v "$esc" >/dev/null 2>&1 || continue
        echo "[install.sh] re-launching under $esc..."
        case "$esc" in
            run0) exec run0 --description="Encore cabinet installer" -- "$ROOT/install.sh" "$@" ;;
            *) exec "$esc" "$ROOT/install.sh" "$@" ;;
        esac
    done
    echo "install.sh: root privileges are required" >&2
    exit 1
fi

case "$ROOT/" in
    /tmp/*)
        echo "WARNING: $ROOT is volatile and may disappear after reboot." >&2
        ask "Continue from /tmp?" N || exit 2
        ;;
esac

[[ ! -e "$STATE/install-mode" ]] || {
    echo "install.sh: Encore cabinet integration is already installed." >&2
    echo "Run ./uninstall.sh before changing its profile." >&2
    exit 2
}

dm_service=""
if systemctl cat display-manager.service >/dev/null 2>&1; then
    dm_service="$(readlink -f /etc/systemd/system/display-manager.service 2>/dev/null || true)"
    case "$dm_service" in *gdm*|*sddm*) ;; *) dm_service="" ;; esac
fi

backend=""
case "${1:-}" in
    --display-manager) backend=display-manager ;;
    --cage) backend=cage ;;
    --weston) backend=weston ;;
    --direct-console|--framebuffer|--console) backend=direct-console ;;
    "") ;;
    *) echo "install.sh: unknown profile '$1'" >&2; usage >&2; exit 2 ;;
esac

if [[ -z "$backend" ]]; then
    choices=()
    echo "Which cabinet setup do you want to use?"
    if [[ -n "$dm_service" ]]; then
        choices+=(display-manager)
        echo "1. Existing Wayland display manager — Recommended"
    fi
    choices+=(cage weston direct-console)
    for ((i=0; i<${#choices[@]}; i++)); do
        [[ "${choices[i]}" == display-manager ]] && continue
        case "${choices[i]}" in
            cage) label="Cage — minimal Wayland kiosk" ;;
            weston) label="Weston — reference Wayland kiosk" ;;
            direct-console) label="Framebuffer / direct console — SDL2 KMSDRM" ;;
        esac
        printf '%d. %s\n' "$((i + 1))" "$label"
    done
    echo
    echo "Every graphical profile is native SDL2/Wayland. Encore installs no Xorg"
    echo "session, X11 window manager, or XWayland path."
    read -r -p "Setup [1]: " pick
    pick="${pick:-1}"
    [[ "$pick" =~ ^[0-9]+$ && "$pick" -ge 1 && "$pick" -le ${#choices[@]} ]] || {
        echo "Invalid setup" >&2; exit 2; }
    backend="${choices[pick - 1]}"
fi

[[ "$backend" != display-manager || -n "$dm_service" ]] || {
    echo "install.sh: no supported GDM or SDDM service is installed" >&2; exit 2; }

default_user=""
for uid in "${SUDO_UID:-}" "${PKEXEC_UID:-}"; do
    [[ -n "$uid" ]] || continue
    default_user="$(getent passwd "$uid" | cut -d: -f1 || true)"
    [[ -n "$default_user" ]] && break
done
[[ -n "$default_user" ]] || default_user="$(logname 2>/dev/null || true)"
if [[ -z "$default_user" || "$default_user" == root ]]; then
    default_user="$(getent passwd | awk -F: '$3>=1000&&$3<65534&&$7!~/(nologin|false)$/ {print $1;exit}')"
fi
read -r -p "Cabinet session user [$default_user]: " session_user
session_user="${session_user:-$default_user}"
session_uid="$(id -u "$session_user" 2>/dev/null)" || { echo "Unknown user" >&2; exit 2; }
[[ "$session_uid" -ge 1000 ]] || { echo "Cabinet user must have UID >= 1000" >&2; exit 2; }
session_home="$(getent passwd "$session_user" | cut -d: -f6)"
session_group="$(id -gn "$session_user")"

read -r -p "Game [swe1/rfm] (swe1): " game
game="${game:-swe1}"
case "$game" in swe1|rfm) ;; *) echo "Invalid game" >&2; exit 2 ;; esac

start_flipped=1
echo
echo "F2 vertically reverses the displayed image."
echo "The cabinet setup enables that same state from startup by default."
if ! ask "Start with flipscreen enabled?"; then
    start_flipped=0
fi

missing_packages=()
case "$backend" in
    cage) command -v cage >/dev/null 2>&1 || missing_packages+=(cage) ;;
    weston) command -v weston >/dev/null 2>&1 || missing_packages+=(weston) ;;
esac
command -v python3 >/dev/null 2>&1 || missing_packages+=(python3)
command -v systemd-inhibit >/dev/null 2>&1 || missing_packages+=(systemd)
if ((${#missing_packages[@]})); then
    echo "Missing runtime packages/tools: ${missing_packages[*]}"
    if command -v apt-get >/dev/null 2>&1 && ask "Install the missing Debian packages now?"; then
        DEBIAN_FRONTEND=noninteractive apt-get update
        DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
            "${missing_packages[@]}"
    else
        echo "install.sh: required tools are missing" >&2; exit 2
    fi
fi

sdl_has_driver() {
    local wanted="$1"
    SDL_WANTED_DRIVER="$wanted" python3 - <<'PY'
import ctypes, ctypes.util, sys
import os
name = ctypes.util.find_library("SDL2")
if not name:
    sys.exit(1)
sdl = ctypes.CDLL(name)
sdl.SDL_GetVideoDriver.restype = ctypes.c_char_p
drivers = {sdl.SDL_GetVideoDriver(i).decode() for i in range(sdl.SDL_GetNumVideoDrivers())}
sys.exit(0 if os.environ["SDL_WANTED_DRIVER"] in drivers else 1)
PY
}

# Query SDL itself rather than inferring support from library filenames. A
# stock Debian SDL2 normally exposes both drivers, but release users may have a
# smaller host runtime.
sdl_driver=wayland
[[ "$backend" == direct-console ]] && sdl_driver=KMSDRM
if ! sdl_has_driver "$sdl_driver"; then
    echo "The installed SDL2 does not expose its $sdl_driver video backend."
    if command -v apt-get >/dev/null 2>&1 &&
       ask "Install Debian's SDL2 and graphics runtime now?"; then
        DEBIAN_FRONTEND=noninteractive apt-get update
        if [[ "$sdl_driver" == wayland ]]; then
            DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
                libsdl2-2.0-0 libwayland-client0 libegl1 libgles2 libgl1-mesa-dri
        else
            DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
                libsdl2-2.0-0 libdrm2 libgbm1 libegl1 libgles2 libgl1-mesa-dri
        fi
    fi
    if ! sdl_has_driver "$sdl_driver"; then
        if [[ "$sdl_driver" == KMSDRM ]]; then
            echo "install.sh: SDL2 still has no KMSDRM backend; choose a Wayland profile." >&2
        else
            echo "install.sh: SDL2 still has no native Wayland backend." >&2
        fi
        exit 2
    fi
fi

qemu_bin="$session_home/.cache/p2k-qemu-build/qemu-10.0.8/build/qemu-system-i386"
[[ -x "$ROOT/qemu-system-i386" ]] && qemu_bin="$ROOT/qemu-system-i386"
if [[ ! -x "$qemu_bin" ]]; then
    echo "Encore's custom qemu-system-i386 has not been built yet."
    ask "Install build dependencies and build it now?" || exit 2
    command -v apt-get >/dev/null 2>&1 || { echo "Automatic build setup requires APT" >&2; exit 2; }
    DEBIAN_FRONTEND=noninteractive apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        ca-certificates build-essential pkg-config git curl patch ninja-build \
        python3 python3-venv xz-utils libsdl2-dev libglib2.0-dev \
        libpixman-1-dev zlib1g-dev libslirp-dev libvorbis-dev libogg-dev
    runuser -u "$session_user" -- env HOME="$session_home" \
        "$ROOT/scripts/build-qemu.sh"
    [[ -x "$qemu_bin" ]] || { echo "install.sh: build completed without expected binary" >&2; exit 3; }
fi
"$qemu_bin" -M help | grep -q pinball2000 || {
    echo "install.sh: $qemu_bin does not contain the Encore machine" >&2; exit 3; }

launch_args=()
add_lp_group=0
[[ "$start_flipped" -eq 0 ]] || launch_args+=(--flipscreen)
if [[ -e /dev/parport0 ]]; then
    echo
    echo "Real cabinet interface detected: /dev/parport0"
    echo "WARNING: Encore's powered-playfield validation is still pending."
    echo "The documented sequence is: emulated benchmark, playfield power OFF,"
    echo "real-port trace, then one low-risk device class at a time."
    if ask "Have those checks passed, and enable /dev/parport0 at boot?" N; then
        launch_args+=(--cabinet --lpt-device /dev/parport0)
        if ! id -nG "$session_user" | tr ' ' '\n' | grep -qx lp; then
            add_lp_group=1
        fi
    else
        echo "Keeping the emulated driver board. Follow docs/46-real-lpt-passthrough.md"
        echo "before reinstalling with real cabinet I/O enabled."
    fi
fi

# Refuse path collisions before writing installation state, so a failed
# preflight cannot leave an installation marker behind.
if [[ "$backend" == display-manager ]]; then
    if [[ -e "$AUTOSTART" ]] && ! grep -q '^Name=Encore Pinball 2000 cabinet$' "$AUTOSTART"; then
        echo "install.sh: refusing unrelated existing $AUTOSTART" >&2
        exit 3
    fi
    if [[ "$dm_service" == *gdm* ]]; then
        gdm_conf=/etc/gdm3/daemon.conf
        [[ -f "$gdm_conf" ]] || gdm_conf=/etc/gdm/custom.conf
        [[ -f "$gdm_conf" ]] || { echo "GDM configuration not found" >&2; exit 3; }
    fi
elif [[ -e "$GETTY_DROPIN" ]] && ! grep -q 'encore-session.sh' "$GETTY_DROPIN"; then
    echo "install.sh: refusing unrelated existing $GETTY_DROPIN" >&2
    exit 3
fi

install -d -m 0755 "$CONF_DIR" "$STATE"
{
    printf 'ROOT=%q\n' "$ROOT"
    printf 'SESSION_USER=%q\n' "$session_user"
    printf 'SESSION_UID=%q\n' "$session_uid"
    printf 'BACKEND=%q\n' "$backend"
    printf 'GAME=%q\n' "$game"
    printf 'QEMU_BIN=%q\n' "$qemu_bin"
} > "$CONF_DIR/session.conf"
: > "$CONF_DIR/launch.args"
if ((${#launch_args[@]})); then
    printf '%s\n' "${launch_args[@]}" > "$CONF_DIR/launch.args"
fi
chmod 0644 "$CONF_DIR/session.conf" "$CONF_DIR/launch.args"
printf '%s\n' "$backend" > "$STATE/install-mode"
systemctl get-default > "$STATE/previous-default-target"
systemctl is-enabled getty@tty1.service > "$STATE/getty-tty1-was-enabled" 2>/dev/null || true
if [[ "$add_lp_group" -eq 1 ]]; then
    usermod -aG lp "$session_user"
    printf '%s\n' "$session_user" > "$STATE/lp-group-added"
    echo "Added $session_user to group lp for /dev/parport0."
fi

if [[ "$backend" == display-manager ]]; then
    install -d -m 0755 /etc/xdg/autostart
    cat > "$AUTOSTART" <<EOF
[Desktop Entry]
Type=Application
Name=Encore Pinball 2000 cabinet
Exec=$ROOT/scripts/encore-session.sh --desktop
Terminal=false
NoDisplay=true
X-GNOME-Autostart-enabled=true
EOF
    case "$dm_service" in
        *gdm*)
            sed -i '/^# >>> encore autologin >>>$/,/^# <<< encore autologin <<<$/{d}' "$gdm_conf"
            cat >> "$gdm_conf" <<EOF
# >>> encore autologin >>>
[daemon]
AutomaticLoginEnable=true
AutomaticLogin=$session_user
# <<< encore autologin <<<
EOF
            ;;
        *sddm*)
            install -d -m 0755 /etc/sddm.conf.d
            cat > /etc/sddm.conf.d/49-encore.conf <<EOF
[Autologin]
User=$session_user
Relogin=false
EOF
            ;;
    esac
    systemctl set-default graphical.target
else
    # The empty standard hush file suppresses distro MOTD/legal text without
    # changing PAM. Track the inode so uninstall never removes a user file that
    # was pre-existing or later replaced.
    hushlogin="$session_home/.hushlogin"
    if [[ ! -e "$hushlogin" ]]; then
        install -m 0600 -o "$session_uid" -g "$session_group" /dev/null "$hushlogin"
        { printf '%s\n' "$hushlogin"; stat -c '%d:%i' "$hushlogin"; } > "$STATE/hushlogin-created"
    fi
    install -d -m 0755 "$(dirname "$GETTY_DROPIN")"
    cat > "$GETTY_DROPIN" <<EOF
[Service]
ExecStart=
ExecStart=-/sbin/agetty --skip-login --login-program "$ROOT/scripts/encore-session.sh" --login-options '--login $session_user tty1' --noissue --noclear %I \$TERM
StandardOutput=journal
StandardError=journal
TTYVTDisallocate=no
Restart=always
RestartSec=1
EOF
    systemctl set-default multi-user.target
    systemctl enable getty@tty1.service
fi

systemctl daemon-reload
echo
echo "Encore cabinet profile installed: $backend"
echo "User: $session_user   Game: $game"
echo "Reboot to enter the cabinet session. Run ./uninstall.sh to restore the host."
