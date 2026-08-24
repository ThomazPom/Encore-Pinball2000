#!/usr/bin/env bash
# Debian packages needed while running Encore and its selected display backend.

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
                libegl1 libgles2 libgl1-mesa-dri
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
    local package answer
    local -a missing=()
    for package in "$@"; do
        dpkg-query -W -f='${Status}' "$package" 2>/dev/null |
            grep -q '^install ok installed$' || missing+=("$package")
    done
    ((${#missing[@]})) || return 0

    echo "[run-qemu] missing runtime packages: ${missing[*]}" >&2
    [[ -t 0 ]] || {
        echo "[run-qemu] install them with: apt-get install ${missing[*]}" >&2
        return 2
    }
    read -r -p "Install them with APT now? [Y/n] " answer
    [[ ! "$answer" =~ ^[Nn]$ ]] || return 2

    if [[ $EUID -eq 0 ]]; then
        apt-get update
        DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${missing[@]}"
    elif command -v run0 >/dev/null 2>&1 && command -v pkttyagent >/dev/null 2>&1; then
        run0 --description="Encore runtime prerequisites" -- apt-get update
        run0 --description="Encore runtime prerequisites" -- \
            env DEBIAN_FRONTEND=noninteractive apt-get install -y \
            --no-install-recommends "${missing[@]}"
    elif command -v sudo >/dev/null 2>&1; then
        sudo apt-get update
        sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y \
            --no-install-recommends "${missing[@]}"
    elif command -v pkexec >/dev/null 2>&1; then
        pkexec apt-get update
        pkexec env DEBIAN_FRONTEND=noninteractive apt-get install -y \
            --no-install-recommends "${missing[@]}"
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
    if [[ $EUID -eq 0 && -n "${ENCORE_PREFLIGHT_USER:-}" &&
          "$ENCORE_PREFLIGHT_USER" != root ]]; then
        local owner_home
        owner_home="$(getent passwd "$ENCORE_PREFLIGHT_USER" | cut -d: -f6)"
        [[ -n "$owner_home" ]] || return 2
        runuser -u "$ENCORE_PREFLIGHT_USER" -- env HOME="$owner_home" "$@"
    else
        "$@"
    fi
}

encore_acquire_qemu() {
    local root="$1" answer owner_home="${HOME:?}" release_dir build_qemu
    [[ -n "${QEMU_BIN:-}" && -x "$QEMU_BIN" ]] && return 0
    if [[ $EUID -eq 0 && -n "${ENCORE_PREFLIGHT_USER:-}" &&
          "$ENCORE_PREFLIGHT_USER" != root ]]; then
        owner_home="$(getent passwd "$ENCORE_PREFLIGHT_USER" | cut -d: -f6)"
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
    if [[ -n "$backend" ]]; then
        mapfile -t backend_packages < <(encore_runtime_packages "$backend")
        packages+=("${backend_packages[@]}")
    fi
    encore_offer_runtime_packages "${packages[@]}"
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
