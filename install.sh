#!/usr/bin/env bash
# Install Encore as a native Wayland or direct-console cabinet session.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
CONF_DIR=/etc/encore-pinball2000
STATE=/var/lib/encore-pinball2000
GETTY_DROPIN=/etc/systemd/system/getty@tty1.service.d/49-encore.conf
MAINTENANCE_DROPIN=/run/systemd/system/getty@tty1.service.d/50-encore-maintenance.conf
GRUB_DROPIN=/etc/default/grub.d/99-encore-pinball2000.cfg
GRUB_QUIET_SCRIPT=/etc/grub.d/01_encore_pinball2000_quiet
ROOT_SERVICE=/etc/systemd/system/encore-pinball2000-root.service
CABINET_SHELL=/usr/local/libexec/encore-pinball2000-session
DIRECT_INPUT_RULE=/etc/udev/rules.d/70-encore-pinball2000-input.rules
CABINET_LOCK=/var/lib/pinball2000-cabinet.lock

usage() {
    cat <<'EOF'
Usage: ./install.sh [--display-manager|--cage|--weston|--direct-console]

  --display-manager  use the existing Wayland desktop session (recommended)
  --cage             standalone minimal Wayland kiosk
  --weston           standalone reference Wayland kiosk
  --direct-console   SDL2 KMSDRM, with no compositor or display server
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
        [[ "$esc" != run0 ]] || command -v pkttyagent >/dev/null 2>&1 || continue
        echo "[install.sh] re-launching under $esc..."
        case "$esc" in
            run0) exec run0 --description="Encore cabinet installer" -- "$ROOT/install.sh" "$@" ;;
            *) exec "$esc" "$ROOT/install.sh" "$@" ;;
        esac
    done
    echo "install.sh: root privileges are required." >&2
    echo "Install polkitd for run0, install pkexec, or run through sudo." >&2
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
if [[ -e "$CABINET_LOCK" ]]; then
    lock_owner="$(sed -n '1p' "$CABINET_LOCK" 2>/dev/null || true)"
    echo "install.sh: a ${lock_owner:-unknown} cabinet integration is installed; uninstall it first." >&2
    exit 2
fi

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
            direct-console) label="Direct console — SDL2 KMSDRM" ;;
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
original_shell="$(getent passwd "$session_user" | cut -d: -f7)"
session_group="$(id -gn "$session_user")"
AUTOSTART="$session_home/.config/autostart/encore-cabinet.desktop"
USER_UNIT_DIR="$session_home/.config/systemd/user"
USER_SERVICE="$USER_UNIT_DIR/encore-pinball2000.service"
USER_WANT="$USER_UNIT_DIR/graphical-session.target.wants/encore-pinball2000.service"

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

run_as_root=0
echo
echo "Encore is designed to run as the unprivileged session user."
echo "Root mode is a diagnostic fallback for comparing unresolved hardware issues."
ask "Run Encore as root instead?" N && run_as_root=1

cabinet_audio=0
echo
echo "The game has its own volume controls. For their full usable range, Encore"
echo "can unmute the host's default output and set it to 100% at every startup."
echo "Only the current default output is changed; no audio service or device is selected."
ask "Allow cabinet startup to unmute the host output and set it to 100%?" Y && cabinet_audio=1

maintenance=tty
if [[ "$backend" != display-manager && -n "$dm_service" ]]; then
    maintenance=display-manager
    read -r -p "After Encore exits [display-manager/tty] ($maintenance): " answer
    maintenance=${answer:-$maintenance}
    case "$maintenance" in
        display-manager|tty) ;;
        *) echo "install.sh: expected display-manager or tty" >&2; exit 2 ;;
    esac
elif [[ "$backend" != display-manager ]]; then
    echo "After Encore exits: password-backed tty1 login (no display manager detected)."
fi

quiet_boot=0
zero_grub_timeout=0
ask "Use the distribution's quiet boot presentation?" && quiet_boot=1
if command -v update-grub >/dev/null 2>&1; then
    ask "Hide the GRUB menu and use a zero-second timeout?" && zero_grub_timeout=1
fi

echo
echo "About to install:"
echo "  setup        : $backend"
echo "  session user : $session_user (unprivileged)"
echo "  game         : $game"
echo "  flipscreen   : $([[ $start_flipped -eq 1 ]] && echo enabled || echo disabled)"
echo "  execution    : $([[ $run_as_root -eq 1 ]] && echo 'root (diagnostic)' || echo 'session user')"
echo "  host audio   : $([[ $cabinet_audio -eq 1 ]] && echo 'unmute and set 100% at startup' || echo unchanged)"
[[ "$backend" == display-manager ]] || echo "  maintenance  : $maintenance"
echo "  quiet boot   : $([[ $quiet_boot -eq 1 ]] && echo enabled || echo disabled)"
if command -v update-grub >/dev/null 2>&1; then
    echo "  GRUB timeout : $([[ $zero_grub_timeout -eq 1 ]] && echo hidden/zero || echo unchanged)"
fi
ask "Proceed?" || exit 0

# savedata/ is intentionally ignored by Git and by the clean-room VM copy, so
# a fresh checkout may not contain the directory at all. Create only the
# writable state directory; never seed it from the developer's checkout.
install -d -o "$session_user" -g "$session_group" -m 0755 "$ROOT/savedata"

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
name = ctypes.util.find_library("SDL2-2.0") or ctypes.util.find_library("SDL2")
if not name:
    sys.exit(1)
sdl = ctypes.CDLL(name)
sdl.SDL_GetVideoDriver.restype = ctypes.c_char_p
drivers = {sdl.SDL_GetVideoDriver(i).decode() for i in range(sdl.SDL_GetNumVideoDrivers())}
sys.exit(0 if os.environ["SDL_WANTED_DRIVER"] in drivers else 1)
PY
}

# Query SDL itself rather than inferring support from library filenames.
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
        echo "install.sh: SDL2 still has no $sdl_driver backend." >&2
        exit 2
    fi
fi

build_qemu="$session_home/.cache/p2k-qemu-build/qemu-10.0.8/build/qemu-system-i386"
release_dir="$session_home/.cache/encore-qemu-release"
release_qemu="$release_dir/qemu-system-i386"
qemu_bin="$release_qemu"
[[ -x "$build_qemu" ]] && qemu_bin="$build_qemu"
[[ -x "$ROOT/qemu-system-i386" ]] && qemu_bin="$ROOT/qemu-system-i386"

if [[ ! -x "$qemu_bin" ]]; then
    echo
    echo "How should Encore obtain its custom QEMU?"
    echo "1. Build locally — Recommended (longer; guaranteed to match this checkout)"
    echo "2. Download latest release — Faster (may lag behind this checkout)"
    read -r -p "Choice [1]: " qemu_choice
    qemu_choice="${qemu_choice:-1}"
    [[ "$qemu_choice" == 1 || "$qemu_choice" == 2 ]] || {
        echo "install.sh: invalid QEMU choice" >&2; exit 2; }

    if [[ "$qemu_choice" == 2 ]]; then
        command -v apt-get >/dev/null 2>&1 || {
            echo "Automatic release setup requires APT" >&2; exit 2; }
        DEBIAN_FRONTEND=noninteractive apt-get update
        DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
            ca-certificates curl tar coreutils
        if runuser -u "$session_user" -- env HOME="$session_home" \
            "$ROOT/scripts/download-qemu-release.sh" --destination "$release_dir"; then
            if [[ -s "$release_dir/runtime-packages.txt" ]]; then
                mapfile -t release_packages < <(
                    sed -E 's/:[a-z0-9]+$//' "$release_dir/runtime-packages.txt" |
                        grep -E '^[a-z0-9][a-z0-9+.-]*$' | sort -u
                )
                available_packages=()
                for package_name in "${release_packages[@]}"; do
                    apt-cache show "$package_name" >/dev/null 2>&1 &&
                        available_packages+=("$package_name")
                done
                if ((${#available_packages[@]})); then
                    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
                        "${available_packages[@]}"
                fi
            fi
            qemu_bin="$release_qemu"
        else
            echo "The published build could not be downloaded or verified." >&2
            ask "Build locally instead?" Y || exit 2
            qemu_choice=1
        fi
    fi

    if [[ "$qemu_choice" == 1 ]]; then
        command -v apt-get >/dev/null 2>&1 || {
            echo "Automatic build setup requires APT" >&2; exit 2; }
        DEBIAN_FRONTEND=noninteractive apt-get update
        DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
            ca-certificates build-essential pkg-config git curl patch ninja-build \
            python3 python3-venv xz-utils libsdl2-dev libglib2.0-dev \
            libpixman-1-dev zlib1g-dev libslirp-dev libvorbis-dev libogg-dev
        runuser -u "$session_user" -- env HOME="$session_home" \
            "$ROOT/scripts/build-qemu.sh"
        qemu_bin="$build_qemu"
    fi
    [[ -x "$qemu_bin" ]] || {
        echo "install.sh: QEMU acquisition completed without the expected binary" >&2
        exit 3
    }
fi
if ldd "$qemu_bin" 2>/dev/null | grep -q 'not found'; then
    echo "install.sh: the selected QEMU still has missing runtime libraries:" >&2
    ldd "$qemu_bin" | grep 'not found' >&2 || true
    exit 3
fi
"$qemu_bin" -M help | grep -q pinball2000 || {
    echo "install.sh: $qemu_bin does not contain the Encore machine" >&2; exit 3; }

launch_args=()
add_lp_group=0
[[ "$start_flipped" -eq 0 ]] || launch_args+=(--flipscreen)

# A physical port may be numbered parport1 (or later), and ppdev may not have
# been loaded yet even though the kernel has registered the underlying port.
# Ask the kernel for ppdev only when sysfs proves that a parport exists.
shopt -s nullglob
parport_devices=(/dev/parport[0-9]*)
kernel_parports=(/sys/class/parport/parport[0-9]*)
if ((${#parport_devices[@]} == 0 && ${#kernel_parports[@]} > 0)); then
    if ! command -v modprobe >/dev/null 2>&1; then
        echo "A kernel parallel port exists, but the tool needed to load ppdev is missing."
        if command -v apt-get >/dev/null 2>&1 && ask "Install the distribution's kmod package now?" Y; then
            DEBIAN_FRONTEND=noninteractive apt-get update
            DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends kmod
        else
            echo "install.sh: kmod is required to enable the detected parallel port" >&2
            exit 2
        fi
    fi
    modprobe ppdev || {
        echo "install.sh: the running kernel cannot load its ppdev module" >&2
        exit 2
    }
    command -v udevadm >/dev/null 2>&1 && udevadm settle 2>/dev/null || true
    parport_devices=(/dev/parport[0-9]*)
    ((${#parport_devices[@]} > 0)) || {
        echo "install.sh: the kernel sees a parallel port, but ppdev created no /dev/parportN device" >&2
        exit 2
    }
fi
shopt -u nullglob

real_parport=""
if ((${#parport_devices[@]} == 1)); then
    real_parport="${parport_devices[0]}"
elif ((${#parport_devices[@]} > 1)); then
    echo
    echo "Parallel interfaces detected: ${parport_devices[*]}"
    read -r -p "Real cabinet interface [${parport_devices[0]}]: " real_parport
    real_parport="${real_parport:-${parport_devices[0]}}"
    [[ " ${parport_devices[*]} " == *" $real_parport "* ]] || {
        echo "install.sh: '$real_parport' is not one of the detected parallel interfaces" >&2
        exit 2
    }
fi

if [[ -n "$real_parport" ]]; then
    [[ -c "$real_parport" ]] || {
        echo "install.sh: '$real_parport' is not a character device" >&2
        exit 2
    }
    echo
    echo "Real cabinet interface detected: $real_parport"
    echo "Enable it to communicate with the real Pinball 2000 hardware."
    echo "Answering no keeps Encore on its emulated board for demonstration use."
    if ask "Use the real cabinet interface?" Y; then
        launch_args+=(--cabinet --lpt-device "$real_parport")
        if ! id -nG "$session_user" | tr ' ' '\n' | grep -qx lp; then
            add_lp_group=1
        fi
    else
        echo "Keeping the emulated driver board."
    fi
else
    echo
    echo "No Linux ppdev interface (/dev/parportN) was detected."
    echo "Encore will use its emulated demonstration board."
fi

# Refuse path collisions before writing installation state, so a failed
# preflight cannot leave an installation marker behind.
if [[ "$backend" == display-manager ]]; then
    if [[ -e "$USER_SERVICE" ]] &&
       ! grep -q '^Description=Encore Pinball 2000 cabinet session$' "$USER_SERVICE"; then
        echo "install.sh: refusing unrelated existing $USER_SERVICE" >&2
        exit 3
    fi
    if [[ "$dm_service" == *gdm* ]]; then
        gdm_conf=/etc/gdm3/daemon.conf
        [[ -f "$gdm_conf" ]] || gdm_conf=/etc/gdm/custom.conf
        [[ -f "$gdm_conf" ]] || { echo "GDM configuration not found" >&2; exit 3; }
    elif [[ -e /etc/sddm.conf.d/49-encore.conf ]] &&
         ! grep -q '^# Managed by Encore Pinball 2000$' /etc/sddm.conf.d/49-encore.conf; then
        echo "install.sh: refusing unrelated existing /etc/sddm.conf.d/49-encore.conf" >&2
        exit 3
    fi
else
    if [[ -e "$GETTY_DROPIN" ]] && ! grep -q 'encore-pinball2000-session' "$GETTY_DROPIN"; then
        echo "install.sh: refusing unrelated existing $GETTY_DROPIN" >&2
        exit 3
    fi
    # A standalone cabinet owns tty1's login lifecycle. Detect other cabinet
    # launchers and administrator drop-ins even when they use a different
    # filename; silently stacking two ExecStart/ExecStopPost definitions can
    # make one emulator hand the seat to the other's maintenance fallback.
    for tty_dropin in /etc/systemd/system/getty@tty1.service.d/*.conf \
                      /run/systemd/system/getty@tty1.service.d/*.conf; do
        [[ -e "$tty_dropin" ]] || continue
        [[ "$tty_dropin" != "$GETTY_DROPIN" ]] || continue
        if grep -qE '^[[:space:]]*Exec(Start|StopPost)=' "$tty_dropin"; then
            echo "install.sh: tty1 is already managed by $tty_dropin" >&2
            echo "Remove the other cabinet integration before installing a standalone Encore profile." >&2
            exit 3
        fi
    done
fi
if [[ "$backend" != display-manager && -e "$CABINET_SHELL" ]] &&
   ! grep -q '^# Cabinet session entry point\.' "$CABINET_SHELL"; then
    echo "install.sh: refusing unrelated existing $CABINET_SHELL" >&2
    exit 3
fi
if [[ "$quiet_boot" -eq 1 || "$zero_grub_timeout" -eq 1 ]]; then
    if [[ -e "$GRUB_DROPIN" ]] &&
       ! grep -q '^# encore-pinball2000 managed boot presentation$' "$GRUB_DROPIN"; then
        echo "install.sh: refusing unrelated existing $GRUB_DROPIN" >&2
        exit 3
    fi
    if [[ "$quiet_boot" -eq 1 && -e "$GRUB_QUIET_SCRIPT" ]] &&
       ! grep -q '^# encore-pinball2000 managed silent GRUB handoff$' "$GRUB_QUIET_SCRIPT"; then
        echo "install.sh: refusing unrelated existing $GRUB_QUIET_SCRIPT" >&2
        exit 3
    fi
fi

install -d -m 0755 "$CONF_DIR"
install -d -m 0700 "$STATE"
{
    printf 'ROOT=%q\n' "$ROOT"
    printf 'SESSION_USER=%q\n' "$session_user"
    printf 'SESSION_UID=%q\n' "$session_uid"
    printf 'BACKEND=%q\n' "$backend"
    printf 'GAME=%q\n' "$game"
    printf 'QEMU_BIN=%q\n' "$qemu_bin"
    printf 'RUN_AS_ROOT=%q\n' "$run_as_root"
    printf 'CABINET_AUDIO=%q\n' "$cabinet_audio"
    printf 'MAINTENANCE=%q\n' "$maintenance"
    printf 'ORIGINAL_SHELL=%q\n' "$original_shell"
    printf 'STANDALONE_LOGIN_SHELL=%q\n' "$([[ "$backend" == display-manager ]] && echo 0 || echo 1)"
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
    echo "Added $session_user to group lp for $real_parport."
fi

# SDL's KMSDRM backend opens evdev devices directly. Unlike a compositor, it
# has no privileged input broker, and standard Debian intentionally does not
# grant ordinary users permanent membership of the global input group. Mark
# event devices for logind's active-seat ACL only in the unprivileged direct
# console profile. The permission then follows the real PAM/login session.
if [[ "$backend" == direct-console && "$run_as_root" -eq 0 ]]; then
    if [[ -e "$DIRECT_INPUT_RULE" ]] &&
       ! grep -q '^# encore-pinball2000 managed direct-console input$' "$DIRECT_INPUT_RULE"; then
        echo "install.sh: refusing unrelated existing $DIRECT_INPUT_RULE" >&2
        exit 3
    fi
    cat > "$DIRECT_INPUT_RULE" <<'EOF'
# encore-pinball2000 managed direct-console input
SUBSYSTEM=="input", KERNEL=="event*", TAG+="uaccess"
EOF
    chmod 0644 "$DIRECT_INPUT_RULE"
    udevadm control --reload
    udevadm trigger --subsystem-match=input --action=change
fi

if [[ "$quiet_boot" -eq 1 || "$zero_grub_timeout" -eq 1 ]]; then
    install -d -m 0755 /etc/default/grub.d
    {
        echo '# encore-pinball2000 managed boot presentation'
        if [[ "$quiet_boot" -eq 1 ]]; then
            cat <<'EOF'
for encore_boot_arg in quiet loglevel=3 systemd.show_status=false rd.udev.log_level=3 vt.global_cursor_default=0; do
    case " $GRUB_CMDLINE_LINUX_DEFAULT " in
        *" $encore_boot_arg "*) ;;
        *) GRUB_CMDLINE_LINUX_DEFAULT="$GRUB_CMDLINE_LINUX_DEFAULT $encore_boot_arg" ;;
    esac
done
if command -v plymouth >/dev/null 2>&1; then
    case " $GRUB_CMDLINE_LINUX_DEFAULT " in
        *' splash '*) ;;
        *) GRUB_CMDLINE_LINUX_DEFAULT="$GRUB_CMDLINE_LINUX_DEFAULT splash" ;;
    esac
fi
unset encore_boot_arg
EOF
        fi
        if [[ "$zero_grub_timeout" -eq 1 ]]; then
            cat <<'EOF'
GRUB_TIMEOUT_STYLE=hidden
GRUB_TIMEOUT=0
GRUB_RECORDFAIL_TIMEOUT=0
GRUB_THEME=""
GRUB_BACKGROUND=""
GRUB_TERMINAL_OUTPUT=console
EOF
        fi
    } > "$GRUB_DROPIN"
    chmod 0644 "$GRUB_DROPIN"

    if [[ "$quiet_boot" -eq 1 ]]; then
        cat > "$GRUB_QUIET_SCRIPT" <<'EOF'
#!/bin/sh
# encore-pinball2000 managed silent GRUB handoff
cat <<'GRUB_EOF'
if [ "${recordfail}" != 1 ]; then
  set color_normal=black/black
fi
GRUB_EOF
EOF
        chmod 0755 "$GRUB_QUIET_SCRIPT"
    else
        rm -f "$GRUB_QUIET_SCRIPT"
    fi
    update-grub
fi

if [[ "$backend" == display-manager ]]; then
    # The graphical-session target is reached by the distribution-managed
    # GNOME/KWin Wayland session after its environment has been imported into
    # systemd --user. Avoid both XDG-autostart timing and a root-side broker.
    rm -f "$AUTOSTART"
    if [[ -f /etc/xdg/autostart/encore-cabinet.desktop ]] &&
       grep -q '^Name=Encore Pinball 2000 cabinet$' /etc/xdg/autostart/encore-cabinet.desktop; then
        rm -f /etc/xdg/autostart/encore-cabinet.desktop
    fi
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
# Managed by Encore Pinball 2000
[Autologin]
User=$session_user
Relogin=false
EOF
            ;;
    esac
    runuser -u "$session_user" -- mkdir -p \
        "$USER_UNIT_DIR/graphical-session.target.wants"
    cat > "$USER_SERVICE" <<EOF
[Unit]
Description=Encore Pinball 2000 cabinet session
Documentation=file:$ROOT/docs/01-cabinet-installation.md
PartOf=graphical-session.target
After=graphical-session-pre.target

[Service]
Type=simple
WorkingDirectory=$ROOT
ExecStartPre="$ROOT/scripts/encore-session.sh" --wait-wayland
ExecStart="$ROOT/scripts/encore-session.sh" --desktop
Restart=no
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=graphical-session.target
EOF
    chown "$session_user:$session_group" "$USER_SERVICE"
    chmod 0644 "$USER_SERVICE"
    ln -sfn ../encore-pinball2000.service "$USER_WANT"
    chown -h "$session_user:$session_group" "$USER_WANT"
    printf '%s\n' "$USER_SERVICE" > "$STATE/user-service-path"
    if [[ -S "/run/user/$session_uid/bus" ]]; then
        runuser -u "$session_user" -- env \
            XDG_RUNTIME_DIR="/run/user/$session_uid" \
            DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$session_uid/bus" \
            systemctl --user daemon-reload || true
    fi
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
    install -d -m 0755 "$(dirname "$CABINET_SHELL")"
    install -m 0755 "$ROOT/scripts/encore-session.sh" "$CABINET_SHELL"
    if ! grep -Fxq "$CABINET_SHELL" /etc/shells 2>/dev/null; then
        printf '%s\n' "$CABINET_SHELL" >> /etc/shells
        printf '%s\n' "$CABINET_SHELL" > "$STATE/shells-line-added"
    fi
    printf '%s\n' "$session_user" > "$STATE/session-shell-user"
    printf '%s\n' "$original_shell" > "$STATE/original-shell"
    usermod --shell "$CABINET_SHELL" "$session_user"
    rm -f "$MAINTENANCE_DROPIN"
    cat > "$GETTY_DROPIN" <<EOF
[Unit]
StartLimitIntervalSec=60
StartLimitBurst=3

[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin $session_user --noissue --noclear %I \$TERM
ExecStopPost=$CABINET_SHELL --maintenance
StandardOutput=journal
StandardError=journal
TTYVTDisallocate=no
Restart=no
EOF
    systemctl set-default multi-user.target
    systemctl enable getty@tty1.service
fi

if [[ "$run_as_root" -eq 1 ]]; then
    cat > "$ROOT_SERVICE" <<EOF
[Unit]
Description=Encore Pinball 2000 root diagnostic launcher
After=systemd-user-sessions.service

[Service]
Type=simple
WorkingDirectory=$ROOT
ExecStart="$ROOT/scripts/encore-session.sh" --root-service
Restart=no
KillMode=control-group
TimeoutStopSec=15
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=$([[ "$backend" == display-manager ]] && echo graphical.target || echo multi-user.target)
EOF
    chmod 0644 "$ROOT_SERVICE"
else
    rm -f "$ROOT_SERVICE"
fi

systemctl daemon-reload
if [[ "$run_as_root" -eq 1 ]]; then
    systemctl enable encore-pinball2000-root.service
fi
echo
printf '%s\n' encore > "$CABINET_LOCK"
echo "Encore cabinet profile installed: $backend"
echo "User: $session_user   Game: $game"
echo "Reboot to enter the cabinet session. Run ./uninstall.sh to restore the host."
