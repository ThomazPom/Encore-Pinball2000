#!/usr/bin/env bash
set -euo pipefail

ROOT="$1"
ROMS_DIR="$2"
UPDATES_DIR="$3"
REPO_URL="${P2K_ASSETS_REPO:-https://github.com/ThomazPom/Encore-Pinball2000.git}"

need_roms=0
need_updates=0
[[ -d "$ROMS_DIR" ]] || need_roms=1
[[ -d "$UPDATES_DIR" ]] || need_updates=1
(( need_roms || need_updates )) || exit 0

command -v git >/dev/null 2>&1 || {
  echo "[run-qemu] git is required to download missing ROMs/updates" >&2
  exit 1
}

tmp="$(mktemp -d /tmp/encore-assets.XXXXXX)"
cleanup() { rm -rf "$tmp"; }
trap cleanup EXIT INT TERM

echo "[run-qemu] ROMs or updates missing; downloading complete offline assets..." >&2
git clone --depth 1 --filter=blob:none "$REPO_URL" "$tmp/repo" >/dev/null 2>&1 || {
  echo "[run-qemu] failed to clone assets from $REPO_URL" >&2
  exit 1
}

install_tree() {
  local name="$1" destination="$2" source="$tmp/repo/$1"
  local partial="${destination}.partial.$$"
  [[ -d "$source" ]] || {
    echo "[run-qemu] cloned repository does not contain '$name/'" >&2
    exit 1
  }
  rm -rf "$partial"
  mkdir -p "$(dirname "$destination")"
  cp -a "$source" "$partial"
  mv "$partial" "$destination"
  echo "[run-qemu] installed $name/ for offline use" >&2
}

(( need_roms )) && install_tree roms "$ROMS_DIR"
(( need_updates )) && install_tree updates "$UPDATES_DIR"
