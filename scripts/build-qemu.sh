#!/usr/bin/env bash
# Build a minimal qemu-system-i386 that knows the 'pinball2000' machine.
#
# Strategy: NO vendoring, NO fork.  Download a pinned upstream QEMU
# release tarball into qemu-build/, copy our small out-of-tree machine
# source from qemu/ into hw/i386/, append two lines to hw/i386/meson.build
# + hw/i386/Kconfig, configure --target-list=i386-softmmu only, build.
#
# Output: $P2K_QEMU_BUILD_DIR/qemu-<ver>/build/qemu-system-i386
#
# Idempotent: re-running just refreshes the copies and rebuilds (ninja).
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
KNOWN_GOOD_VERS=( 10.0.0 10.0.1 10.0.2 10.0.3 10.0.4 10.0.5 10.0.6 10.0.7 10.0.8 )
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

# --- Detect orphaned patches (removed from source but still baked into a
# cached extracted tree) and force a clean re-extraction if found ----------
# A patch's *effects* persist in $SRC forever once applied, even after its
# .patch file is later deleted from qemu/upstream-patches/ (e.g. an
# experiment that got ripped out). The hash-tracked sentinel loop further
# below only re-applies patches that still EXIST on disk; it has no
# mechanism to reverse-apply one whose source file is gone. Without this
# check, a deleted patch's dangling declarations/definitions/call-sites
# silently survive in the cached build tree across rebuilds.
# Root-caused 2026-07: 0004-p2k-idle-loop-tb-cutter.patch was deleted (its
# effects removed from qemu/*.c) but its `p2k_idle_loop_breaker_enabled()`
# extern declaration + call-site patched into target/i386/tcg/translate.c
# survived in every cached $SRC, producing an undefined-reference link
# failure the next time anything touched that translation unit.
PATCH_DIR="$ROOT/qemu/upstream-patches"
APPLIED_DIR="$SRC/.p2k-applied-patches"
if [[ -d "$APPLIED_DIR" ]]; then
  for sentinel in "$APPLIED_DIR"/*.patch; do
    [[ -e "$sentinel" ]] || continue
    name="$(basename "$sentinel")"
    if [[ ! -e "$PATCH_DIR/$name" ]]; then
      echo "[build-qemu] $name was removed from qemu/upstream-patches/ but" \
           "its effects are still baked into the cached $SRC -- forcing a" \
           "clean re-extraction so patches re-apply from a pristine tree"
      rm -rf "$SRC"
      break
    fi
  done
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
  echo "[build-qemu]       hw/i386/meson.build + Kconfig grafts may need"
  echo "[build-qemu]       adjustment for newer/older releases."
fi

# --- Apply our upstream-QEMU patches (idempotent) --------------------------
# Patches in qemu/upstream-patches/*.patch are applied to the extracted
# upstream source. We track applied patches via a sentinel file so re-runs
# don't re-apply (which would fail). Touching/removing the sentinel forces
# re-application on the next build (and the patches must be reverse-clean
# against the current tree, otherwise we fail loudly). Deleted patches are
# handled above (orphan-detection forces a clean re-extraction before we
# get here), so PATCH_DIR/APPLIED_DIR are already defined.
mkdir -p "$APPLIED_DIR"
if [[ -d "$PATCH_DIR" ]]; then
  for p in "$PATCH_DIR"/*.patch; do
    [[ -e "$p" ]] || continue
    name="$(basename "$p")"
    sentinel="$APPLIED_DIR/$name"
    cur_hash="$(sha1sum "$p" | awk '{print $1}')"
    if [[ -f "$sentinel" ]] && [[ "$(cat "$sentinel")" == "$cur_hash" ]]; then
      continue
    fi
    echo "[build-qemu] applying upstream patch $name"
    if ! patch -d "$SRC" -p1 --forward --silent < "$p"; then
      # Maybe a stale, different version of this patch is already applied.
      echo "[build-qemu] patch $name failed forward; trying reverse-then-apply"
      patch -d "$SRC" -p1 -R --silent < "$p" || true
      patch -d "$SRC" -p1 --silent < "$p"
    fi
    echo "$cur_hash" > "$sentinel"
  done
fi

# --- Inject our machine source ---------------------------------------------
HW_I386="$SRC/hw/i386"
echo "[build-qemu] copying qemu/{pinball2000,p2k-*}.{c,h} -> $HW_I386/"
# Headers first so the .c files compile
cp "$ROOT/qemu/pinball2000.h" "$HW_I386/pinball2000.h"
cp "$ROOT/qemu/p2k-internal.h" "$HW_I386/p2k-internal.h"
for f in "$ROOT"/qemu/p2k-*.h "$ROOT"/qemu/p2k-*.inc; do
  [[ -e "$f" ]] || continue
  cp "$f" "$HW_I386/$(basename "$f")"
done
# Machine + per-concern source files
cp "$ROOT/qemu/pinball2000.c" "$HW_I386/pinball2000.c"
for f in "$ROOT"/qemu/p2k-*.c; do
  cp "$f" "$HW_I386/$(basename "$f")"
done
P2K_C_FILES=( pinball2000.c )
for f in "$ROOT"/qemu/p2k-*.c; do
  P2K_C_FILES+=( "$(basename "$f")" )
done

# --- Patch hw/i386/meson.build (re-patched every run so new p2k-*.c get added) -
MESON="$HW_I386/meson.build"
# Strip any previous pinball2000 block (between the marker and the next blank line).
sed -i '/# --- Pinball 2000 /,/^$/d' "$MESON"
# Also strip a trailing line that may have been left without a blank separator.
sed -i '/i386_ss\.add.*pinball2000\.c/d' "$MESON"
echo "[build-qemu] patching $MESON"
{
  echo
  echo "# --- Pinball 2000 (out-of-tree, copied in by scripts/build-qemu.sh) ---"
  echo "p2k_vorbisfile_dep = dependency('vorbisfile', required: false)"
  printf "p2k_files = files("
  first=1
  for f in "${P2K_C_FILES[@]}"; do
    if [[ $first -eq 1 ]]; then first=0; else printf ", "; fi
    printf "'%s'" "$f"
  done
  printf ")\n"
  echo "i386_ss.add(when: 'CONFIG_PINBALL2000', if_true: [p2k_files, p2k_vorbisfile_dep])"
} >> "$MESON"

# --- Patch hw/i386/Kconfig (idempotent) ------------------------------------
KCONFIG="$HW_I386/Kconfig"
if ! grep -q "PINBALL2000" "$KCONFIG"; then
  echo "[build-qemu] patching $KCONFIG"
  cat >> "$KCONFIG" <<'KCONFIG_EOF'

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
fi

# Make sure the i386-softmmu target enables CONFIG_PINBALL2000.
DEFCFG="$SRC/configs/devices/i386-softmmu/default.mak"
if [[ -f "$DEFCFG" ]] && ! grep -q "PINBALL2000" "$DEFCFG"; then
  echo "[build-qemu] enabling PINBALL2000 in $DEFCFG"
  echo "CONFIG_PINBALL2000=y" >> "$DEFCFG"
fi

# --- Configure (only once) -------------------------------------------------
BUILD="$SRC/build"
if [[ ! -f "$BUILD/build.ninja" ]]; then
  echo "[build-qemu] configuring (i386-softmmu only)"
  rm -rf "$BUILD"
  cd "$SRC"
  # Enable GTK display backend if dev headers are present so users can
  # `--display gtk` from run-qemu.sh; otherwise fall back to disabling it
  # so configure doesn't error out.
  GTK_FLAG="--disable-gtk"
  if pkg-config --exists gtk+-3.0 2>/dev/null; then
    GTK_FLAG="--enable-gtk"
    echo "[build-qemu] gtk+-3.0 found → --enable-gtk"
  else
    echo "[build-qemu] gtk+-3.0 not found → --disable-gtk (apt install libgtk-3-dev to enable)"
  fi
  ./configure \
    --target-list=i386-softmmu \
    --disable-docs \
    --disable-tools \
    --disable-guest-agent \
    "$GTK_FLAG" \
    --disable-vnc \
    --disable-werror \
    --enable-sdl
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
