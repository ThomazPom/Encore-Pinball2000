#!/usr/bin/env bash
# Debian packages needed while running Encore and its selected display backend.

ENCORE_DIRECT_INPUT_RULE=/etc/udev/rules.d/70-encore-pinball2000-input.rules
ENCORE_DIRECT_INPUT_MARKER='# encore-pinball2000 managed direct-console input'
ENCORE_RUNTIME_APPROVED=${ENCORE_RUNTIME_APPROVED:-0}
ENCORE_NETWORK_TAP=encore-p2k0
ENCORE_NETWORK_TAP_MARKER='encore-pinball2000 managed bridge tap'

encore_root_prepare_runtime() {
    local prepare_input="$1"
    shift
    if (($#)); then
        apt-get update
        DEBIAN_FRONTEND=noninteractive apt-get install -y \
            --no-install-recommends "$@"
    fi
    if [[ "$prepare_input" == 1 ]]; then
        if [[ -e "$ENCORE_DIRECT_INPUT_RULE" ]] &&
           ! grep -qxF "$ENCORE_DIRECT_INPUT_MARKER" "$ENCORE_DIRECT_INPUT_RULE"; then
            echo "[run-qemu] refusing unrelated $ENCORE_DIRECT_INPUT_RULE" >&2
            return 3
        fi
        install -d -m 0755 "$(dirname "$ENCORE_DIRECT_INPUT_RULE")"
        printf '%s\n%s\n' "$ENCORE_DIRECT_INPUT_MARKER" \
            'SUBSYSTEM=="input", KERNEL=="event*", TAG+="uaccess"' \
            > "$ENCORE_DIRECT_INPUT_RULE"
        chmod 0644 "$ENCORE_DIRECT_INPUT_RULE"
        udevadm control --reload
        udevadm trigger --subsystem-match=input --action=change
    fi
}

encore_prepare_parport_access() {
    local owner="${ENCORE_RUNTIME_USER:-}" device="${P2K_LPT_DEVICE:-auto}"
    [[ "$device" != emulated && "$device" != disconnected &&
       "$device" != none ]] || return 0
    [[ "$device" == auto || "$device" == required ||
       "$device" == /dev/parport[0-9]* ]] || return 0
    [[ -n "$owner" && "$owner" != root ]] || return 0
    id "$owner" >/dev/null 2>&1 || {
        echo "[run-qemu] parallel-port user does not exist: $owner" >&2
        return 2
    }
    id -nG "$owner" | tr ' ' '\n' | grep -qx lp && return 0
    getent group lp >/dev/null || {
        echo "[run-qemu] the host has no lp group" >&2
        return 2
    }
    command -v usermod >/dev/null || {
        echo "[run-qemu] usermod is required to grant parallel-port access" >&2
        return 2
    }
    usermod -aG lp "$owner"
    echo "[run-qemu] added $owner to lp for automatic parallel-port access"
}

encore_bridge_tap_needs_root() {
    local bridge="${P2K_NETWORK_BRIDGE:-}" owner="${ENCORE_RUNTIME_USER:-$(id -un)}" owner_uid
    [[ -n "$bridge" ]] || return 1
    command -v ip >/dev/null 2>&1 || return 0
    [[ -d "/sys/class/net/$ENCORE_NETWORK_TAP" ]] || return 0
    [[ "$(cat "/sys/class/net/$ENCORE_NETWORK_TAP/ifalias" 2>/dev/null || true)" == "$ENCORE_NETWORK_TAP_MARKER" ]] || return 0
    [[ "$(basename "$(readlink -f "/sys/class/net/$ENCORE_NETWORK_TAP/master" 2>/dev/null || true)")" == "$bridge" ]] || return 0
    owner_uid="$(id -u "$owner" 2>/dev/null)" || return 0
    ip tuntap show dev "$ENCORE_NETWORK_TAP" 2>/dev/null |
        grep -Eq "(^|[[:space:]])user[[:space:]]+($owner|$owner_uid)([[:space:]]|$)" || return 0
    ip -o link show dev "$ENCORE_NETWORK_TAP" 2>/dev/null |
        grep -q '<[^>]*UP[^>]*>' || return 0
    return 1
}

encore_prepare_bridge_tap() {
    local bridge="${P2K_NETWORK_BRIDGE:-}" owner="${ENCORE_RUNTIME_USER:-$(id -un)}" owner_uid
    [[ -n "$bridge" ]] || return 0
    [[ -d "/sys/class/net/$bridge/bridge" ]] || {
        echo "[run-qemu] network bridge no longer exists: $bridge" >&2
        return 2
    }
    command -v ip >/dev/null 2>&1 || {
        echo "[run-qemu] iproute2 is required for bridge networking" >&2
        return 2
    }
    id "$owner" >/dev/null 2>&1 || {
        echo "[run-qemu] bridge TAP owner does not exist: $owner" >&2
        return 2
    }
    owner_uid="$(id -u "$owner")"
    if [[ -d "/sys/class/net/$ENCORE_NETWORK_TAP" ]]; then
        [[ "$(cat "/sys/class/net/$ENCORE_NETWORK_TAP/ifalias" 2>/dev/null || true)" == "$ENCORE_NETWORK_TAP_MARKER" ]] || {
            echo "[run-qemu] refusing unrelated interface $ENCORE_NETWORK_TAP" >&2
            return 3
        }
        if ! ip tuntap show dev "$ENCORE_NETWORK_TAP" 2>/dev/null |
             grep -Eq "(^|[[:space:]])user[[:space:]]+($owner|$owner_uid)([[:space:]]|$)"; then
            [[ $EUID -eq 0 ]] || return 2
            ip link set dev "$ENCORE_NETWORK_TAP" nomaster 2>/dev/null || true
            ip tuntap del dev "$ENCORE_NETWORK_TAP" mode tap
        fi
    fi
    if [[ ! -d "/sys/class/net/$ENCORE_NETWORK_TAP" ]]; then
        [[ $EUID -eq 0 ]] || return 2
        ip tuntap add dev "$ENCORE_NETWORK_TAP" mode tap user "$owner"
        ip link set dev "$ENCORE_NETWORK_TAP" alias "$ENCORE_NETWORK_TAP_MARKER"
        echo "[run-qemu] created managed TAP $ENCORE_NETWORK_TAP for $owner"
    fi
    if encore_bridge_tap_needs_root; then
        [[ $EUID -eq 0 ]] || return 2
        ip link set dev "$ENCORE_NETWORK_TAP" nomaster 2>/dev/null || true
        ip link set dev "$ENCORE_NETWORK_TAP" master "$bridge"
        ip link set dev "$ENCORE_NETWORK_TAP" up
    fi
    echo "[run-qemu] bridge TAP: $ENCORE_NETWORK_TAP -> $bridge (owner $owner)"
}

encore_runtime_packages() {
    case "$1" in
        display-manager|wayland|cage|weston)
            printf '%s\n' \
                libsdl2-2.0-0 libwayland-client0 \
                libegl1 libgles2 libgl1-mesa-dri
            case "$1" in
                cage) printf '%s\n' cage ;;
                weston) printf '%s\n' weston ;;
            esac
            ;;
        direct-console)
            printf '%s\n' \
                libsdl2-2.0-0 libdrm2 libgbm1 \
                libegl1 libgles2 libgl1 libgl1-mesa-dri
            ;;
        *)
            echo "runtime-packages.sh: unknown SDL backend '$1'" >&2
            return 2
            ;;
    esac
}

encore_offer_runtime_packages() {
    command -v dpkg-query >/dev/null 2>&1 || return 0
    command -v apt-get >/dev/null 2>&1 || return 0
    local package answer prepare_input=0
    local -a missing=()
    for package in "$@"; do
        dpkg-query -W -f='${Status}' "$package" 2>/dev/null |
            grep -q '^install ok installed$' || missing+=("$package")
    done
    if [[ "${ENCORE_RUNTIME_PREPARE_INPUT:-0}" == 1 ]]; then
        if [[ -e "$ENCORE_DIRECT_INPUT_RULE" ]] &&
           ! grep -qxF "$ENCORE_DIRECT_INPUT_MARKER" "$ENCORE_DIRECT_INPUT_RULE"; then
            echo "[run-qemu] refusing unrelated $ENCORE_DIRECT_INPUT_RULE" >&2
            return 3
        fi
        grep -qxF "$ENCORE_DIRECT_INPUT_MARKER" "$ENCORE_DIRECT_INPUT_RULE" \
            2>/dev/null || prepare_input=1
    fi
    ((${#missing[@]} || prepare_input)) || return 0

    ((${#missing[@]})) &&
        echo "[run-qemu] missing runtime packages: ${missing[*]}" >&2
    [[ $prepare_input -eq 0 ]] ||
        echo "[run-qemu] direct-console needs active-session keyboard access" >&2
    [[ -t 0 ]] || {
        echo "[run-qemu] install them with: apt-get install ${missing[*]}" >&2
        return 2
    }
    if [[ "$ENCORE_RUNTIME_APPROVED" -ne 1 ]]; then
        echo "[run-qemu] some runtime dependencies are missing." >&2
        read -r -p "Install all required runtime dependencies now? [Y/n] " answer
        [[ ! "$answer" =~ ^[Nn]$ ]] || return 2
        ENCORE_RUNTIME_APPROVED=1
    fi

    if [[ $EUID -eq 0 ]]; then
        encore_root_prepare_runtime "$prepare_input" "${missing[@]}"
    elif command -v run0 >/dev/null 2>&1 && command -v pkttyagent >/dev/null 2>&1; then
        run0 --description="Encore runtime prerequisites" -- \
            bash "$ENCORE_RUNTIME_PACKAGES_HELPER" --root-prepare \
            "$prepare_input" "${missing[@]}"
    elif command -v sudo >/dev/null 2>&1; then
        sudo bash "$ENCORE_RUNTIME_PACKAGES_HELPER" --root-prepare \
            "$prepare_input" "${missing[@]}"
    elif command -v pkexec >/dev/null 2>&1; then
        pkexec bash "$ENCORE_RUNTIME_PACKAGES_HELPER" --root-prepare \
            "$prepare_input" "${missing[@]}"
    else
        echo "[run-qemu] no supported privilege helper; install with:" >&2
        echo "  apt-get install ${missing[*]}" >&2
        return 2
    fi
}

encore_sdl_has_driver() {
    SDL_WANTED_DRIVER="$1" python3 - <<'PY'
import ctypes, ctypes.util, os, sys
name = ctypes.util.find_library("SDL2-2.0") or ctypes.util.find_library("SDL2")
if not name:
    sys.exit(1)
sdl = ctypes.CDLL(name)
sdl.SDL_GetVideoDriver.restype = ctypes.c_char_p
drivers = {sdl.SDL_GetVideoDriver(i).decode()
           for i in range(sdl.SDL_GetNumVideoDrivers())}
sys.exit(0 if os.environ["SDL_WANTED_DRIVER"] in drivers else 1)
PY
}

encore_run_as_owner() {
    if [[ $EUID -eq 0 && -n "${ENCORE_RUNTIME_USER:-}" &&
          "$ENCORE_RUNTIME_USER" != root ]]; then
        local owner_home
        owner_home="$(getent passwd "$ENCORE_RUNTIME_USER" | cut -d: -f6)"
        [[ -n "$owner_home" ]] || return 2
        runuser -u "$ENCORE_RUNTIME_USER" -- env HOME="$owner_home" "$@"
    else
        "$@"
    fi
}

encore_runtime_needs_root_phase() {
    local backend="$1" package metadata
    local -a packages=(git python3) backend_packages=() qemu_packages=()
    [[ -z "${P2K_NETWORK_BRIDGE:-}" ]] || packages+=(iproute2)
    [[ "${P2K_NETWORK_PASST:-0}" != 1 ]] || packages+=(passt)
    [[ "${P2K_NETWORK_MIRROR:-0}" != 1 ]] || packages+=(iproute2)
    [[ "$backend" != direct-console ]] ||
        grep -qxF "$ENCORE_DIRECT_INPUT_MARKER" "$ENCORE_DIRECT_INPUT_RULE" \
            2>/dev/null || return 0
    if [[ "${P2K_LPT_DEVICE:-auto}" != emulated &&
          "${P2K_LPT_DEVICE:-auto}" != disconnected &&
          "${P2K_LPT_DEVICE:-auto}" != none ]]; then
        id -nG "$(id -un)" | tr ' ' '\n' | grep -qx lp || return 0
    fi
    if [[ -n "$backend" ]]; then
        mapfile -t backend_packages < <(encore_runtime_packages "$backend")
        packages+=("${backend_packages[@]}")
    fi
    [[ -n "${QEMU_BIN:-}" && -x "$QEMU_BIN" ]] || return 0
    metadata="$(dirname "$QEMU_BIN")/runtime-packages.txt"
    if [[ -s "$metadata" ]]; then
        mapfile -t qemu_packages < <(
            sed -E 's/:[a-z0-9]+$//' "$metadata" |
                grep -E '^[a-z0-9][a-z0-9+.-]*$' | sort -u
        )
        packages+=("${qemu_packages[@]}")
    fi
    for package in "${packages[@]}"; do
        dpkg-query -W -f='${Status}' "$package" 2>/dev/null |
            grep -q '^install ok installed$' || return 0
    done
    encore_bridge_tap_needs_root && return 0
    return 1
}

encore_runtime_root_phase() {
    local root="$1" backend="$2" owner="$3" owner_home="$4" qemu_bin="$5" lpt_device="${6:-auto}" network_bridge="${7:-}" network_passt="${8:-0}" network_mirror="${9:-0}"
    ROOT="$root"
    HOME="$owner_home"
    ENCORE_RUNTIME_USER="$owner"
    QEMU_BIN="$qemu_bin"
    export P2K_LPT_DEVICE="$lpt_device"
    export P2K_NETWORK_BRIDGE="$network_bridge"
    export P2K_NETWORK_PASST="$network_passt"
    export P2K_NETWORK_MIRROR="$network_mirror"
    encore_prepare_runtime "$backend"
}

encore_acquire_qemu() {
    local root="$1" answer owner_home="${HOME:?}" release_dir build_qemu
    [[ -n "${QEMU_BIN:-}" && -x "$QEMU_BIN" ]] && return 0
    if [[ $EUID -eq 0 && -n "${ENCORE_RUNTIME_USER:-}" &&
          "$ENCORE_RUNTIME_USER" != root ]]; then
        owner_home="$(getent passwd "$ENCORE_RUNTIME_USER" | cut -d: -f6)"
    fi
    release_dir="$owner_home/.cache/encore-qemu-release"
    build_qemu="$owner_home/.cache/p2k-qemu-build/qemu-10.0.8/build/qemu-system-i386"

    if [[ -x "$root/scripts/build-qemu.sh" ]]; then
        echo "Encore's custom QEMU is missing."
        echo "1. Build locally — Recommended (longer; matches this checkout)"
        echo "2. Download latest release — Faster (may lag behind this checkout)"
        [[ -t 0 ]] || {
            echo "[run-qemu] QEMU acquisition requires an interactive terminal" >&2
            return 2
        }
        read -r -p "Choice [1]: " answer
        answer="${answer:-1}"
    else
        echo "[run-qemu] packaged QEMU missing; downloading a verified replacement"
        answer=2
    fi

    case "$answer" in
        1)
            encore_offer_runtime_packages \
                ca-certificates build-essential pkg-config git curl patch \
                ninja-build python3 python3-venv xz-utils libsdl2-dev \
                libglib2.0-dev libpixman-1-dev zlib1g-dev libslirp-dev \
                libvorbis-dev libogg-dev
            encore_run_as_owner "$root/scripts/build-qemu.sh"
            QEMU_BIN="$build_qemu"
            ;;
        2)
            encore_offer_runtime_packages ca-certificates curl tar coreutils
            encore_run_as_owner "$root/scripts/internal/download-qemu-release.sh" \
                --destination "$release_dir"
            QEMU_BIN="$release_dir/qemu-system-i386"
            ;;
        *) echo "[run-qemu] expected choice 1 or 2" >&2; return 2 ;;
    esac
    [[ -x "$QEMU_BIN" ]] || {
        echo "[run-qemu] QEMU acquisition produced no executable" >&2
        return 2
    }
}

encore_prepare_runtime() {
    local backend="${1:-}" driver="" qemu_path metadata
    local -a packages=(git python3) backend_packages=() qemu_packages=()
    [[ -z "${P2K_NETWORK_BRIDGE:-}" ]] || packages+=(iproute2)
    [[ "${P2K_NETWORK_PASST:-0}" != 1 ]] || packages+=(passt)
    [[ "${P2K_NETWORK_MIRROR:-0}" != 1 ]] || packages+=(iproute2)
    if [[ -n "$backend" ]]; then
        mapfile -t backend_packages < <(encore_runtime_packages "$backend")
        packages+=("${backend_packages[@]}")
    fi
    if [[ "$backend" == direct-console ]]; then
        ENCORE_RUNTIME_PREPARE_INPUT=1
    else
        ENCORE_RUNTIME_PREPARE_INPUT=0
    fi

    # A packaged QEMU already has its dependency manifest. Include it in the
    # initial transaction so a fresh release needs only one authorization.
    if [[ -n "$QEMU_BIN" && -x "$QEMU_BIN" ]]; then
        metadata="$(dirname "$QEMU_BIN")/runtime-packages.txt"
        if [[ -s "$metadata" ]]; then
            mapfile -t qemu_packages < <(
                sed -E 's/:[a-z0-9]+$//' "$metadata" |
                    grep -E '^[a-z0-9][a-z0-9+.-]*$' | sort -u
            )
            packages+=("${qemu_packages[@]}")
        fi
    fi
    encore_offer_runtime_packages "${packages[@]}"
    encore_prepare_parport_access
    encore_prepare_bridge_tap
    encore_acquire_qemu "$ROOT"

    qemu_path="$QEMU_BIN"
    metadata="$(dirname "$qemu_path")/runtime-packages.txt"
    if [[ -s "$metadata" ]]; then
        mapfile -t qemu_packages < <(
            sed -E 's/:[a-z0-9]+$//' "$metadata" |
                grep -E '^[a-z0-9][a-z0-9+.-]*$' | sort -u
        )
        encore_offer_runtime_packages "${qemu_packages[@]}"
    fi

    if [[ -x "$qemu_path" ]] && ldd "$qemu_path" 2>/dev/null | grep -q 'not found'; then
        echo "[run-qemu] QEMU still has missing runtime libraries:" >&2
        ldd "$qemu_path" | grep 'not found' >&2 || true
        return 2
    fi

    case "$backend" in
        display-manager|wayland|cage|weston) driver=wayland ;;
        direct-console) driver=KMSDRM ;;
        "") return 0 ;;
    esac
    encore_sdl_has_driver "$driver" || {
        echo "[run-qemu] installed SDL2 does not expose its $driver backend" >&2
        return 2
    }
    ENCORE_SDL_DRIVER="$driver"
}

ENCORE_RUNTIME_PACKAGES_HELPER="${BASH_SOURCE[0]}"
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    case "${1:-}" in
        --root-prepare)
            [[ $EUID -eq 0 ]] || exit 2
            shift
            encore_root_prepare_runtime "$@"
            ;;
        --runtime-root-phase)
            [[ $EUID -eq 0 ]] || exit 2
            shift
            encore_runtime_root_phase "$@"
            ;;
        --network-tap-root)
            [[ $EUID -eq 0 && $# -eq 3 ]] || exit 2
            ENCORE_RUNTIME_USER="$2"
            P2K_NETWORK_BRIDGE="$3"
            export ENCORE_RUNTIME_USER P2K_NETWORK_BRIDGE
            for _ in {1..300}; do
                [[ -d "/sys/class/net/$P2K_NETWORK_BRIDGE/bridge" ]] && break
                sleep 0.1
            done
            encore_prepare_bridge_tap
            ;;
        *) exit 2 ;;
    esac
    exit
fi
