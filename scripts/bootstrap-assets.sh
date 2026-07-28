#!/usr/bin/env bash
# Download the complete ROM and update data set on first use so later runs work
# offline. Called by run-qemu.sh after CLI parsing.
set -euo pipefail

ROOT="${1:?repository root required}"
ROMS_DIR="${2:?ROM directory required}"
ASSET_REPO="${P2K_ASSET_REPO:-https://github.com/ThomazPom/Encore-Pinball2000.git}"
UPDATES_DIR="$ROOT/updates"

# Existing installations are left untouched. If either data directory is
# absent, fetch once and install every missing data set.
if [[ -d "$ROMS_DIR" && -d "$UPDATES_DIR" ]]; then
  exit 0
fi

command -v git >/dev/null 2>&1 || {
  echo "[run-qemu] ROMs/updates are missing and git is not installed." >&2
  exit 1
}

TMP_DIR="$(mktemp -d /tmp/encore-assets.XXXXXX)"
cleanup() {
  rm -rf -- "$TMP_DIR"
}
trap cleanup EXIT INT TERM

echo "[run-qemu] ROMs or updates missing; downloading the complete offline data set..." >&2
if ! git clone --depth 1 --filter=blob:none --no-checkout "$ASSET_REPO" "$TMP_DIR/repo" >&2; then
  echo "[run-qemu] Unable to download assets from $ASSET_REPO" >&2
  exit 1
fi

# Sparse checkout prevents unrelated source/build files from being materialized.
git -C "$TMP_DIR/repo" sparse-checkout init --cone >/dev/null
git -C "$TMP_DIR/repo" sparse-checkout set roms updates >/dev/null
if ! git -C "$TMP_DIR/repo" checkout --quiet; then
  echo "[run-qemu] Asset checkout failed." >&2
  exit 1
fi

[[ -d "$TMP_DIR/repo/roms" ]] || {
  echo "[run-qemu] Downloaded repository does not contain roms/." >&2
  exit 1
}
[[ -d "$TMP_DIR/repo/updates" ]] || {
  echo "[run-qemu] Downloaded repository does not contain updates/." >&2
  exit 1
}

if [[ ! -d "$ROMS_DIR" ]]; then
  mkdir -p "$(dirname "$ROMS_DIR")"
  cp -a "$TMP_DIR/repo/roms" "$ROMS_DIR"
fi
if [[ ! -d "$UPDATES_DIR" ]]; then
  cp -a "$TMP_DIR/repo/updates" "$UPDATES_DIR"
fi

echo "[run-qemu] ROMs and updates installed; future launches can run offline." >&2
