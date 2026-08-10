#!/usr/bin/env bash
# Remove only system integration created by install.sh.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
STATE=/var/lib/encore-pinball2000
AUTOSTART=/etc/xdg/autostart/encore-cabinet.desktop
GETTY_DROPIN=/etc/systemd/system/getty@tty1.service.d/49-encore.conf

if [[ ${EUID} -ne 0 ]]; then
    for esc in run0 sudo pkexec; do
        command -v "$esc" >/dev/null 2>&1 || continue
        case "$esc" in
            run0) exec run0 --description="Encore cabinet uninstaller" -- "$ROOT/uninstall.sh" "$@" ;;
            *) exec "$esc" "$ROOT/uninstall.sh" "$@" ;;
        esac
    done
    echo "uninstall.sh: root privileges are required" >&2
    exit 1
fi

if [[ ! -r "$STATE/install-mode" ]]; then
    echo "Encore cabinet integration is not installed; nothing changed."
    exit 0
fi

mode=""
[[ -r "$STATE/install-mode" ]] && mode="$(sed -n '1p' "$STATE/install-mode")"
rm -f "$AUTOSTART" "$GETTY_DROPIN"
rmdir /etc/systemd/system/getty@tty1.service.d 2>/dev/null || true

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
      "$STATE/lp-group-added"
rmdir "$STATE" 2>/dev/null || true

echo "Encore cabinet integration removed. Project files, ROMs and savedata were untouched."
