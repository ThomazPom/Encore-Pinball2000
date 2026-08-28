#!/usr/bin/env bash
# Install Encore as a native Wayland or direct-console cabinet session.
# Owns system integration; the runner owns emulator/backend prerequisites.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
CONF_DIR=/etc/encore-pinball2000
STATE=/var/lib/encore-pinball2000
GETTY_DROPIN=/etc/systemd/system/getty@tty1.service.d/49-encore.conf
MAINTENANCE_DROPIN=/run/systemd/system/getty@tty1.service.d/50-encore-maintenance.conf
GRUB_DROPIN=/etc/default/grub.d/99-encore-pinball2000.cfg
GRUB_QUIET_SCRIPT=/etc/grub.d/01_encore_pinball2000_quiet
ROOT_SERVICE=/etc/systemd/system/encore-pinball2000-root.service
NETWORK_TAP_SERVICE=/etc/systemd/system/encore-pinball2000-network.service
CABINET_SHELL=/usr/local/libexec/encore-pinball2000-session
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

read -r -p "Game [auto/swe1/rfm] (auto): " game
game="${game:-auto}"
case "$game" in auto|swe1|rfm) ;; *) echo "Invalid game" >&2; exit 2 ;; esac

echo
echo "Parallel-port policy:"
echo "  auto      detect a real board, otherwise use keyboard emulation"
echo "  emulated  ignore physical ports and always use keyboard emulation"
echo "  required  require a recognized real board or stop"
read -r -p "LPT device [auto/emulated/required] (auto): " lpt_device
lpt_device="${lpt_device:-auto}"
case "$lpt_device" in auto|emulated|required) ;; *) echo "Invalid LPT device" >&2; exit 2 ;; esac

network=0
http_port=""
network_bridge=""
echo
echo "Optional Pinball 2000 network card:"
echo "  Encore can expose the original SMC8416T-compatible Ethernet hardware"
echo "  either in an isolated virtual network or on an existing Linux bridge."
echo "  The game keeps control of its IP settings."
if ask "Enable the emulated network card?" N; then
    network=1
    read -r -p "Network attachment [isolated/bridge] (isolated): " network_mode
    network_mode="${network_mode:-isolated}"
    case "$network_mode" in
        isolated)
            echo "Leave the next answer empty unless you want local access to the game's"
            echo "built-in HTTP server. It is bound to 127.0.0.1 only."
            read -r -p "Local HTTP port [disabled]: " http_port
            if [[ -n "$http_port" ]]; then
                if [[ ! "$http_port" =~ ^[0-9]+$ ]] ||
                   (( 10#$http_port < 1 || 10#$http_port > 65535 )); then
                    echo "Invalid HTTP port" >&2
                    exit 2
                fi
                http_port="$((10#$http_port))"
            fi
            ;;
        bridge)
            echo "Advanced: XINA will be directly reachable from the selected LAN."
            echo "Encore will create a managed TAP and attach it to an existing Linux bridge."
            echo "The existing bridge itself is never created or reconfigured."
            read -r -p "Existing Linux bridge name: " network_bridge
            [[ "$network_bridge" =~ ^[A-Za-z0-9_.-]{1,15}$ &&
               -d "/sys/class/net/$network_bridge/bridge" ]] || {
                echo "Invalid or missing Linux bridge" >&2
                exit 2
            }
            ;;
        *) echo "Invalid network attachment" >&2; exit 2 ;;
    esac
fi

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
echo "  LPT device   : $lpt_device"
if [[ -n "$network_bridge" ]]; then
    echo "  network      : SMC8416T on bridge $network_bridge (LAN-exposed)"
else
    echo "  network      : $([[ $network -eq 1 ]] && echo 'isolated SMC8416T' || echo disabled)"
fi
[[ -z "$http_port" ]] || echo "  local HTTP   : http://127.0.0.1:$http_port/"
echo "  flipscreen   : $([[ $start_flipped -eq 1 ]] && echo enabled || echo disabled)"
echo "  execution    : $([[ $run_as_root -eq 1 ]] && echo 'root (diagnostic)' || echo 'session user')"
echo "  host audio   : $([[ $cabinet_audio -eq 1 ]] && echo 'unmute and set 100% at startup' || echo unchanged)"
[[ "$backend" == display-manager ]] || echo "  maintenance  : $maintenance"
echo "  quiet boot   : $([[ $quiet_boot -eq 1 ]] && echo enabled || echo disabled)"
if command -v update-grub >/dev/null 2>&1; then
    echo "  GRUB timeout : $([[ $zero_grub_timeout -eq 1 ]] && echo hidden/zero || echo unchanged)"
fi
ask "Proceed?" || exit 0

missing_packages=()
command -v systemd-inhibit >/dev/null 2>&1 || missing_packages+=(systemd)
if ((${#missing_packages[@]})); then
    echo "Missing installer package/tool: ${missing_packages[*]}"
    if command -v apt-get >/dev/null 2>&1 && ask "Install the missing Debian packages now?"; then
        DEBIAN_FRONTEND=noninteractive apt-get update
        DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
            "${missing_packages[@]}"
    else
        echo "install.sh: required tools are missing" >&2; exit 2
    fi
fi

launch_args=()
[[ "$start_flipped" -eq 0 ]] || launch_args+=(--flipscreen)
launch_args+=(--lpt-device "$lpt_device")
if [[ $network -eq 1 ]]; then
    if [[ -n "$network_bridge" ]]; then
        launch_args+=(--network-bridge "$network_bridge")
    else
        launch_args+=(--network)
        [[ -z "$http_port" ]] || launch_args+=(--http-port "$http_port")
    fi
fi

build_qemu="$session_home/.cache/p2k-qemu-build/qemu-10.0.8/build/qemu-system-i386"
release_dir="$session_home/.cache/encore-qemu-release"
release_qemu="$release_dir/qemu-system-i386"
# The runner owns QEMU acquisition and every runtime dependency, including
# access to a selected physical parallel port. Replay the future launch path;
# --preflight changes only its terminal action. The following reboot makes a
# newly granted supplementary group effective before Encore starts.
preflight_args=(--preflight --fullscreen --game "$game" --lpt-device "$lpt_device")
if [[ $network -eq 1 ]]; then
    if [[ -n "$network_bridge" ]]; then
        preflight_args+=(--network-bridge "$network_bridge")
    else
        preflight_args+=(--network)
        [[ -z "$http_port" ]] || preflight_args+=(--http-port "$http_port")
    fi
fi
[[ "$backend" == direct-console ]] || preflight_args+=("--$backend")
if [[ "$backend" == direct-console ]]; then
    ENCORE_RUNTIME_USER="$session_user" \
    SDL_VIDEODRIVER=KMSDRM HOME="$session_home" \
        "$ROOT/scripts/run-qemu.sh" "${preflight_args[@]}"
else
    ENCORE_RUNTIME_USER="$session_user" \
    HOME="$session_home" "$ROOT/scripts/run-qemu.sh" "${preflight_args[@]}"
fi

qemu_bin="$release_qemu"
[[ -x "$build_qemu" ]] && qemu_bin="$build_qemu"
[[ -x "$ROOT/qemu-system-i386" ]] && qemu_bin="$ROOT/qemu-system-i386"
[[ -x "$qemu_bin" ]] || { echo "install.sh: runner produced no QEMU binary" >&2; exit 3; }
"$qemu_bin" -M help | grep -q pinball2000 || {
    echo "install.sh: $qemu_bin does not contain the Encore machine" >&2; exit 3; }

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
ExecStartPre="$ROOT/scripts/internal/encore-session.sh" --wait-wayland
ExecStart="$ROOT/scripts/internal/encore-session.sh" --desktop
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
    install -m 0755 "$ROOT/scripts/internal/encore-session.sh" "$CABINET_SHELL"
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
ExecStart="$ROOT/scripts/internal/encore-session.sh" --root-service
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

if [[ -n "$network_bridge" ]]; then
    cat > "$NETWORK_TAP_SERVICE" <<EOF
[Unit]
Description=Encore Pinball 2000 managed bridge TAP
After=NetworkManager.service systemd-networkd.service networking.service
Before=display-manager.service getty@tty1.service

[Service]
Type=oneshot
ExecStart=/bin/bash "$ROOT/scripts/internal/runtime-packages.sh" --network-tap-root "$session_user" "$network_bridge"
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF
    chmod 0644 "$NETWORK_TAP_SERVICE"
else
    systemctl disable encore-pinball2000-network.service 2>/dev/null || true
    rm -f "$NETWORK_TAP_SERVICE"
fi

systemctl daemon-reload
if [[ -n "$network_bridge" ]]; then
    systemctl enable encore-pinball2000-network.service
fi
if [[ "$run_as_root" -eq 1 ]]; then
    systemctl enable encore-pinball2000-root.service
fi
echo
printf '%s\n' encore > "$CABINET_LOCK"
echo "Encore cabinet profile installed: $backend"
echo "User: $session_user   Game: $game"
echo "Reboot to enter the cabinet session. Run ./uninstall.sh to restore the host."
