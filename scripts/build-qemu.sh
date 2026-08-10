#!/usr/bin/env bash
# Build a minimal qemu-system-i386 that knows the 'pinball2000' machine.
#
# Strategy: NO vendoring, NO fork.  Download a pinned upstream QEMU
# release tarball into the cache, copy changed out-of-tree machine sources
# from qemu/ into hw/i386/, generate Encore-owned Meson/Kconfig files, configure
# a no-default-devices i386-softmmu target, and build only qemu-system-i386.
#
# Output: $P2K_QEMU_BUILD_DIR/qemu-<ver>/build/qemu-system-i386
#
# Idempotent: unchanged sources do not touch the graft or trigger recompiles.
#
# Usage:
#   scripts/build-qemu.sh                      # build the pinned default ($DEFAULT_VER)
#   scripts/build-qemu.sh 10.1.0               # build a specific version (positional)
#   scripts/build-qemu.sh --qemu-version 10.1.0
#   scripts/build-qemu.sh --latest             # newest STABLE from KNOWN_GOOD_VERS
#   scripts/build-qemu.sh --latest --unstable  # newest tarball on the mirror (incl. -rcN)
#   scripts/build-qemu.sh --list               # list stable versions on the mirror
#   scripts/build-qemu.sh --list --unstable    # list including -rcN
#   QEMU_VER=10.1.0 scripts/build-qemu.sh      # legacy env-var form (still works)
#
# Notes for cabinet-day:
#   The default version is the one we have validated end-to-end with
#   --update SWE1 v2.10. KNOWN_GOOD_VERS lists every release we've
#   confirmed builds cleanly with the current hw/i386 grafts. Anything
#   outside that list is best-effort and the script will warn.
#   GTK display backend (run-qemu.sh --display gtk) is auto-enabled when
#   `pkg-config --exists gtk+-3.0` succeeds.
set -euo pipefail

DEFAULT_VER="10.0.8"
# Versions known to build cleanly with the current hw/i386 grafts. Update
# this list whenever a new release is validated end-to-end. --latest will
# pick the newest entry from this list (or, with --unstable, ignore it).
# Add a version only after the complete machine has been built and boot-tested
# with the current patch set. At present, only the pinned default has that
# evidence.
KNOWN_GOOD_VERS=( 10.0.8 10.2.4 )
QEMU_VER="${QEMU_VER:-$DEFAULT_VER}"
INCLUDE_UNSTABLE=0
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# Build tree must live on a real Linux fs (vmhgfs / vboxsf can't make symlinks
# that QEMU's source tarball relies on).  Default outside the repo.
WORK="${P2K_QEMU_BUILD_DIR:-$HOME/.cache/p2k-qemu-build}"
MIRROR="${P2K_QEMU_MIRROR:-https://download.qemu.org}"

list_remote_versions() {
  # Parse the directory listing at download.qemu.org.
  # Stable releases match qemu-X.Y.Z.tar.xz (no -rcN suffix).
  # With INCLUDE_UNSTABLE=1, also include qemu-X.Y.Z-rcN.tar.xz.
  local pat='qemu-[0-9]+\.[0-9]+\.[0-9]+\.tar\.xz'
  if (( INCLUDE_UNSTABLE )); then
    pat='qemu-[0-9]+\.[0-9]+\.[0-9]+(-rc[0-9]+)?\.tar\.xz'
  fi
  curl -fsSL "$MIRROR/" \
    | grep -oE "$pat" \
    | sed -E 's/^qemu-(.*)\.tar\.xz$/\1/' \
    | sort -uV
}

latest_remote_version() {
  list_remote_versions | tail -n1
}

is_known_good() {
  local v="$1"
  for k in "${KNOWN_GOOD_VERS[@]}"; do
    [[ "$k" == "$v" ]] && return 0
  done
  return 1
}

check_build_prerequisites() {
  local -a required_commands=(
    cc pkg-config curl patch ninja python3 tar xz sha256sum
  )
  local -a required_modules=(
    glib-2.0 pixman-1 sdl2 zlib slirp vorbisfile ogg
  )
  local -a missing_commands=() missing_modules=()
  local command_name module_name
  local venv_ok=1 probe_dir=""

  for command_name in "${required_commands[@]}"; do
    command -v "$command_name" >/dev/null 2>&1 ||
      missing_commands+=("$command_name")
  done

  if command -v pkg-config >/dev/null 2>&1; then
    for module_name in "${required_modules[@]}"; do
      pkg-config --exists "$module_name" 2>/dev/null ||
        missing_modules+=("$module_name")
    done
  else
    missing_modules=("${required_modules[@]}")
  fi

  # QEMU creates a Python virtual environment during configure. Importing the
  # venv module is not sufficient on Debian: ensurepip can still be absent.
  if command -v python3 >/dev/null 2>&1; then
    probe_dir="$(mktemp -d /tmp/encore-venv-check.XXXXXX)"
    if ! python3 -m venv "$probe_dir" >/dev/null 2>&1; then
      venv_ok=0
    fi
    rm -rf -- "$probe_dir"
  else
    venv_ok=0
  fi

  if ((${#missing_commands[@]} == 0 && ${#missing_modules[@]} == 0 && venv_ok)); then
    echo "[build-qemu] prerequisites: OK"
    return 0
  fi

  echo "[build-qemu] missing build prerequisites; no download or build was started." >&2
  ((${#missing_commands[@]} == 0)) ||
    printf '  commands: %s\n' "${missing_commands[*]}" >&2
  ((${#missing_modules[@]} == 0)) ||
    printf '  pkg-config modules: %s\n' "${missing_modules[*]}" >&2
  ((venv_ok)) || echo "  Python venv creation: unavailable" >&2
  if command -v apt-get >/dev/null 2>&1; then
    cat >&2 <<'EOF'

Install the complete Debian/Ubuntu/Kali build set with:
  sudo apt update
  sudo apt install -y --no-install-recommends \
    ca-certificates build-essential pkg-config git curl patch ninja-build \
    python3 python3-venv xz-utils libsdl2-dev libglib2.0-dev \
    libpixman-1-dev zlib1g-dev libslirp-dev libvorbis-dev libogg-dev
EOF
  fi
  return 2
}

PICK_LATEST=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --qemu-version|-V)
      [[ -n "${2:-}" ]] || { echo "[build-qemu] $1: expected version" >&2; exit 2; }
      QEMU_VER="$2"; PICK_LATEST=0; shift 2 ;;
    --latest)
      # Resolved AFTER the full arg parse so --unstable can appear in
      # either order on the command line.
      PICK_LATEST=1; shift ;;
    --unstable)
      INCLUDE_UNSTABLE=1; shift ;;
    --list|--list-qemu-versions)
      list_remote_versions
      exit 0 ;;
    -h|--help)
      sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'
      exit 0 ;;
    --) shift; break ;;
    -*) echo "[build-qemu] unknown arg '$1' (try --help)" >&2; exit 2 ;;
    *)  if [[ "$1" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-rc[0-9]+)?$ ]]; then
          QEMU_VER="$1"; PICK_LATEST=0; shift
        else
          echo "[build-qemu] unexpected positional '$1' (want X.Y.Z[-rcN])" >&2; exit 2
        fi ;;
  esac
done

# Fail before version resolution, cache creation, downloading, extraction or
# source grafting. The installer may offer to install these packages; the
# builder itself never mutates the host package set.
check_build_prerequisites

if (( PICK_LATEST )); then
  if (( INCLUDE_UNSTABLE )); then
    QEMU_VER="$(latest_remote_version)" || { echo "[build-qemu] could not query $MIRROR" >&2; exit 2; }
    [[ -n "$QEMU_VER" ]] || { echo "[build-qemu] no versions parsed from $MIRROR" >&2; exit 2; }
  else
    QEMU_VER="${KNOWN_GOOD_VERS[-1]}"
  fi
  echo "[build-qemu] --latest → $QEMU_VER"
fi

SRC="$WORK/qemu-$QEMU_VER"
TARBALL="qemu-$QEMU_VER.tar.xz"
URL="$MIRROR/$TARBALL"

mkdir -p "$WORK"
cd "$WORK"

# --- Detect stale patch effects -------------------------------------------
# The extracted upstream tree is disposable. Hash the ordered family list,
# selector and only the variants selected for this QEMU version. A relevant
# change starts again from pristine source; variants for other versions do not
# invalidate this build cache.
PATCH_DIR="$ROOT/qemu/upstream-patches"
PATCH_TOOL="$ROOT/scripts/qemu-patch-series.py"
PATCHSET_HASH="$(python3 "$PATCH_TOOL" fingerprint \
  --patch-root "$PATCH_DIR" --version "$QEMU_VER")"
PATCHSET_SENTINEL="$SRC/.p2k-patchset-sha256"
if [[ -d "$SRC" ]] && \
   { [[ ! -f "$PATCHSET_SENTINEL" ]] || [[ "$(cat "$PATCHSET_SENTINEL")" != "$PATCHSET_HASH" ]]; }; then
  echo "[build-qemu] patch set changed or incomplete; refreshing cached upstream source"
  rm -rf "$SRC"
fi

if [[ ! -d "$SRC" ]]; then
  if [[ ! -f "$TARBALL" ]]; then
    echo "[build-qemu] downloading $URL"
    if ! curl -fL --progress-bar -o "$TARBALL.part" "$URL"; then
      rm -f "$TARBALL.part"
      echo "[build-qemu] download failed. Available versions on $MIRROR:" >&2
      list_remote_versions | sed 's/^/  /' >&2 || true
      exit 2
    fi
    mv "$TARBALL.part" "$TARBALL"
  fi
  echo "[build-qemu] extracting $TARBALL"
  tar -xf "$TARBALL"
fi

if ! is_known_good "$QEMU_VER"; then
  echo "[build-qemu] NOTE: $QEMU_VER is NOT in the validated list."
  echo "[build-qemu]       Known-good: ${KNOWN_GOOD_VERS[*]}."
  echo "[build-qemu]       A patch-family variant and source compatibility"
  echo "[build-qemu]       validation may be needed for this release."
fi

# --- Apply one compatible variant from every patch family -----------------
# Filename ranges declare source compatibility. The selector additionally
# requires exactly one declared variant per family to apply with zero fuzz.
if [[ ! -f "$PATCHSET_SENTINEL" ]]; then
  python3 "$PATCH_TOOL" apply \
    --patch-root "$PATCH_DIR" \
    --source "$SRC" \
    --version "$QEMU_VER"
  echo "$PATCHSET_HASH" > "$PATCHSET_SENTINEL"
fi

# --- Inject our machine source ---------------------------------------------
HW_I386="$SRC/hw/i386"
UPDATED_FILES=0
copy_if_changed() {
  local source="$1" destination="$2"
  if [[ ! -f "$destination" ]] || ! cmp -s "$source" "$destination"; then
    cp "$source" "$destination"
    UPDATED_FILES=$((UPDATED_FILES + 1))
  fi
}
# Headers first so the .c files compile
copy_if_changed "$ROOT/qemu/pinball2000.h" "$HW_I386/pinball2000.h"
copy_if_changed "$ROOT/qemu/p2k-internal.h" "$HW_I386/p2k-internal.h"
for f in "$ROOT"/qemu/p2k-*.h "$ROOT"/qemu/p2k-*.inc; do
  [[ -e "$f" ]] || continue
  copy_if_changed "$f" "$HW_I386/$(basename "$f")"
done
# Machine + per-concern source files
copy_if_changed "$ROOT/qemu/pinball2000.c" "$HW_I386/pinball2000.c"
for f in "$ROOT"/qemu/p2k-*.c; do
  copy_if_changed "$f" "$HW_I386/$(basename "$f")"
done
echo "[build-qemu] machine graft: $UPDATED_FILES changed file(s)"
P2K_C_FILES=( pinball2000.c )
for f in "$ROOT"/qemu/p2k-*.c; do
  P2K_C_FILES+=( "$(basename "$f")" )
done

# --- Generate the files consumed by the versioned build-integration patch --
# The upstream Meson/Kconfig edits live in the patch family. Their only job is
# to enter this owned subdirectory; Encore's changing source list stays here.
P2K_BUILD_DIR="$HW_I386/p2k"
mkdir -p "$P2K_BUILD_DIR"
MESON="$P2K_BUILD_DIR/meson.build"
MESON_NEW="$(mktemp "$P2K_BUILD_DIR/.meson.XXXXXX")"
{
  echo "p2k_vorbisfile_dep = dependency('vorbisfile', required: false)"
  printf "p2k_files = files('../x86-common.c'"
  for f in "${P2K_C_FILES[@]}"; do
    printf ", '../%s'" "$f"
  done
  printf ")\n"
  echo "i386_ss.add(when: 'CONFIG_PINBALL2000', if_true: [p2k_files, p2k_vorbisfile_dep])"
} > "$MESON_NEW"
if cmp -s "$MESON_NEW" "$MESON"; then
  rm -f "$MESON_NEW"
else
  echo "[build-qemu] updating Encore Meson source list"
  mv "$MESON_NEW" "$MESON"
fi

# This file is wholly owned by Encore; upstream merely sources it.
KCONFIG="$P2K_BUILD_DIR/Kconfig"
KCONFIG_NEW="$(mktemp "$P2K_BUILD_DIR/.Kconfig.XXXXXX")"
cat > "$KCONFIG_NEW" <<'KCONFIG_EOF'
config PINBALL2000
    bool
    default y
    depends on I386
    select ISA_BUS
    select I8259
    select I8254
    select MC146818RTC
    select SERIAL_ISA
KCONFIG_EOF
if cmp -s "$KCONFIG_NEW" "$KCONFIG"; then
  rm -f "$KCONFIG_NEW"
else
  echo "[build-qemu] updating Encore Kconfig"
  mv "$KCONFIG_NEW" "$KCONFIG"
fi

# --- Configure when the minimal build profile changes ----------------------
BUILD="$SRC/build"
GTK_FLAG="--disable-gtk"
if pkg-config --exists gtk+-3.0 2>/dev/null; then
  GTK_FLAG="--enable-gtk"
fi
CONFIG_ARGS=(
  --target-list=i386-softmmu
  --without-default-devices
  --disable-docs
  --disable-tools
  --disable-guest-agent
  "$GTK_FLAG"
  --disable-vnc
  --disable-werror
  --disable-plugins
  --enable-sdl
)
CONFIG_PROFILE="$(printf '%s\n' "${CONFIG_ARGS[@]}" | sha256sum | awk '{print $1}')"
CONFIG_SENTINEL="$SRC/.p2k-config-profile"
if [[ -f "$BUILD/build.ninja" ]] && \
   { [[ ! -f "$CONFIG_SENTINEL" ]] || [[ "$(cat "$CONFIG_SENTINEL")" != "$CONFIG_PROFILE" ]]; }; then
  echo "[build-qemu] minimal configure profile changed; recreating build directory"
  rm -rf "$BUILD"
fi
if [[ ! -f "$BUILD/build.ninja" ]]; then
  echo "[build-qemu] configuring minimal pinball2000/i386-softmmu build"
  rm -rf "$BUILD"
  cd "$SRC"
  # Enable GTK display backend if dev headers are present so users can
  # `--display gtk` from run-qemu.sh; otherwise fall back to disabling it
  # so configure doesn't error out.
  if [[ "$GTK_FLAG" == "--enable-gtk" ]]; then
    echo "[build-qemu] gtk+-3.0 found → --enable-gtk"
  else
    echo "[build-qemu] gtk+-3.0 not found → --disable-gtk (apt install libgtk-3-dev to enable)"
  fi
  ./configure "${CONFIG_ARGS[@]}"
  echo "$CONFIG_PROFILE" > "$CONFIG_SENTINEL"
fi

# --- Build qemu-system-i386 ------------------------------------------------
cd "$BUILD"
echo "[build-qemu] ninja qemu-system-i386"
ninja qemu-system-i386

echo
echo "[build-qemu] OK: $BUILD/qemu-system-i386"
"$BUILD/qemu-system-i386" -M help | grep -i pinball || {
  echo "[build-qemu] WARNING: pinball2000 machine not advertised by -M help" >&2
  exit 1
}
