#!/usr/bin/env bash
# Remove only system integration created by install.sh.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
STATE=/var/lib/encore-pinball2000
LEGACY_AUTOSTART=/etc/xdg/autostart/encore-cabinet.desktop
GETTY_DROPIN=/etc/systemd/system/getty@tty1.service.d/49-encore.conf
MAINTENANCE_DROPIN=/run/systemd/system/getty@tty1.service.d/50-encore-maintenance.conf
GRUB_DROPIN=/etc/default/grub.d/99-encore-pinball2000.cfg
GRUB_QUIET_SCRIPT=/etc/grub.d/01_encore_pinball2000_quiet
ROOT_SERVICE=/etc/systemd/system/encore-pinball2000-root.service
CABINET_SHELL=/usr/local/libexec/encore-pinball2000-session

if [[ ${EUID} -ne 0 ]]; then
    for esc in run0 sudo pkexec; do
        command -v "$esc" >/dev/null 2>&1 || continue
        [[ "$esc" != run0 ]] || command -v pkttyagent >/dev/null 2>&1 || continue
        case "$esc" in
            run0) exec run0 --description="Encore cabinet uninstaller" -- "$ROOT/uninstall.sh" "$@" ;;
            *) exec "$esc" "$ROOT/uninstall.sh" "$@" ;;
        esac
    done
    echo "uninstall.sh: root privileges are required." >&2
    echo "Install polkitd for run0, install pkexec, or run through sudo." >&2
    exit 1
fi

if [[ ! -r "$STATE/install-mode" ]]; then
    echo "Encore cabinet integration is not installed; nothing changed."
    exit 0
fi

mode=""
[[ -r "$STATE/install-mode" ]] && mode="$(sed -n '1p' "$STATE/install-mode")"
systemctl stop encore-pinball2000-root.service 2>/dev/null || true
systemctl disable encore-pinball2000-root.service 2>/dev/null || true
rm -f "$ROOT_SERVICE"
if [[ -s "$STATE/user-service-path" ]]; then
    user_service="$(sed -n '1p' "$STATE/user-service-path")"
    case "$user_service" in
        /*/.config/systemd/user/encore-pinball2000.service)
            if [[ -r /etc/encore-pinball2000/session.conf ]]; then
                # shellcheck source=/dev/null
                source /etc/encore-pinball2000/session.conf
                if [[ -S "/run/user/$SESSION_UID/bus" ]]; then
                    runuser -u "$SESSION_USER" -- env \
                        XDG_RUNTIME_DIR="/run/user/$SESSION_UID" \
                        DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$SESSION_UID/bus" \
                        systemctl --user stop encore-pinball2000.service 2>/dev/null || true
                fi
            fi
            user_want="$(dirname "$user_service")/graphical-session.target.wants/encore-pinball2000.service"
            if [[ -f "$user_service" ]] &&
               grep -q '^Description=Encore Pinball 2000 cabinet session$' "$user_service"; then
                rm -f -- "$user_want" "$user_service"
                rmdir "$(dirname "$user_want")" 2>/dev/null || true
                rmdir "$(dirname "$user_service")" 2>/dev/null || true
                if [[ -n "${SESSION_UID:-}" && -S "/run/user/$SESSION_UID/bus" ]]; then
                    runuser -u "$SESSION_USER" -- env \
                        XDG_RUNTIME_DIR="/run/user/$SESSION_UID" \
                        DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$SESSION_UID/bus" \
                        systemctl --user daemon-reload 2>/dev/null || true
                fi
            elif [[ -e "$user_service" ]]; then
                echo "Leaving changed user service in place: $user_service" >&2
            fi
            ;;
    esac
fi
if [[ -s "$STATE/autostart-path" ]]; then
    autostart="$(sed -n '1p' "$STATE/autostart-path")"
    case "$autostart" in
        /*/.config/autostart/encore-cabinet.desktop)
            if [[ ! -e "$autostart" ]]; then
                :
            elif [[ -f "$autostart" ]] &&
               grep -q '^Name=Encore Pinball 2000 cabinet$' "$autostart"; then
                rm -f -- "$autostart"
                rmdir "$(dirname "$autostart")" 2>/dev/null || true
            else
                echo "Leaving changed autostart file in place: $autostart" >&2
            fi
            ;;
    esac
fi
if [[ -f "$LEGACY_AUTOSTART" ]] &&
   grep -q '^Name=Encore Pinball 2000 cabinet$' "$LEGACY_AUTOSTART"; then
    rm -f "$LEGACY_AUTOSTART"
fi
rm -f "$GETTY_DROPIN" "$MAINTENANCE_DROPIN"
rmdir /etc/systemd/system/getty@tty1.service.d 2>/dev/null || true

if [[ -s "$STATE/session-shell-user" && -s "$STATE/original-shell" ]]; then
    shell_user="$(sed -n '1p' "$STATE/session-shell-user")"
    original_shell="$(sed -n '1p' "$STATE/original-shell")"
    if id "$shell_user" >/dev/null 2>&1 &&
       [[ "$(getent passwd "$shell_user" | cut -d: -f7)" == "$CABINET_SHELL" ]]; then
        usermod --shell "$original_shell" "$shell_user"
    fi
fi
if [[ -s "$STATE/shells-line-added" ]]; then
    sed -i "\|^$CABINET_SHELL$|d" /etc/shells
fi
if [[ -f "$CABINET_SHELL" ]] &&
   grep -q '^# Cabinet session entry point\.' "$CABINET_SHELL"; then
    rm -f "$CABINET_SHELL"
    rmdir /usr/local/libexec 2>/dev/null || true
fi

grub_changed=0
if [[ -f "$GRUB_DROPIN" ]] &&
   grep -q '^# encore-pinball2000 managed boot presentation$' "$GRUB_DROPIN"; then
    rm -f "$GRUB_DROPIN"
    grub_changed=1
fi
if [[ -f "$GRUB_QUIET_SCRIPT" ]] &&
   grep -q '^# encore-pinball2000 managed silent GRUB handoff$' "$GRUB_QUIET_SCRIPT"; then
    rm -f "$GRUB_QUIET_SCRIPT"
    grub_changed=1
fi
if [[ "$grub_changed" -eq 1 ]]; then
    rmdir /etc/default/grub.d 2>/dev/null || true
    if command -v update-grub >/dev/null 2>&1; then
        update-grub
    else
        echo "update-grub unavailable; regenerate GRUB configuration manually" >&2
    fi
fi

strip_block() {
    local file="$1" tmp
    [[ -f "$file" ]] || return 0
    grep -q '^# >>> encore autologin >>>$' "$file" || return 0
    tmp="$(mktemp)"
    awk '/^# >>> encore autologin >>>$/ {skip=1;next}
         /^# <<< encore autologin <<<$/ {skip=0;next}
         !skip {print}' "$file" > "$tmp"
    install -m 0644 "$tmp" "$file"
    rm -f "$tmp"
}
strip_block /etc/gdm3/daemon.conf
strip_block /etc/gdm/custom.conf

rm -f /etc/sddm.conf.d/49-encore.conf
rmdir /etc/sddm.conf.d 2>/dev/null || true

if [[ -s "$STATE/hushlogin-created" ]]; then
    hushlogin="$(sed -n '1p' "$STATE/hushlogin-created")"
    hush_id="$(sed -n '2p' "$STATE/hushlogin-created")"
    case "$hushlogin" in
        /*/.hushlogin)
            if [[ -f "$hushlogin" && ! -s "$hushlogin" &&
                  "$(stat -c '%d:%i' "$hushlogin" 2>/dev/null)" == "$hush_id" ]]; then
                rm -f -- "$hushlogin"
            else
                echo "Leaving changed user file in place: $hushlogin" >&2
            fi
            ;;
    esac
fi

rm -f /etc/encore-pinball2000/session.conf /etc/encore-pinball2000/launch.args
rmdir /etc/encore-pinball2000 2>/dev/null || true
systemctl daemon-reload

if [[ -s "$STATE/previous-default-target" ]]; then
    previous="$(sed -n '1p' "$STATE/previous-default-target")"
    case "$previous" in *.target) systemctl set-default "$previous" ;; esac
fi
if [[ "$mode" != display-manager ]]; then
    getty_state=""
    [[ -r "$STATE/getty-tty1-was-enabled" ]] && \
        getty_state="$(sed -n '1p' "$STATE/getty-tty1-was-enabled")"
    case "$getty_state" in
        enabled|enabled-runtime|alias|static)
            systemctl enable getty@tty1.service 2>/dev/null || true
            ;;
        masked|masked-runtime)
            systemctl disable getty@tty1.service 2>/dev/null || true
            systemctl mask getty@tty1.service 2>/dev/null || true
            ;;
        *) systemctl disable getty@tty1.service 2>/dev/null || true ;;
    esac
fi
if [[ -s "$STATE/lp-group-added" ]]; then
    lp_user="$(sed -n '1p' "$STATE/lp-group-added")"
    if [[ -n "$lp_user" ]] && id "$lp_user" >/dev/null 2>&1; then
        gpasswd -d "$lp_user" lp >/dev/null 2>&1 || true
    fi
fi
rm -f "$STATE/install-mode" "$STATE/previous-default-target" \
      "$STATE/getty-tty1-was-enabled" "$STATE/hushlogin-created" \
      "$STATE/lp-group-added" "$STATE/autostart-path" \
      "$STATE/user-service-path" "$STATE/session-shell-user" \
      "$STATE/original-shell" "$STATE/shells-line-added"
rmdir "$STATE" 2>/dev/null || true

echo "Encore cabinet integration removed. Project files, ROMs and savedata were untouched."
