#!/usr/bin/env bash
# Build a minimal qemu-system-i386 that knows the 'pinball2000' machine.
#
# Strategy: NO vendoring, NO fork.  Download a pinned upstream QEMU
# release tarball into qemu-build/, copy our small out-of-tree machine
# source from qemu/ into hw/i386/, append two lines to hw/i386/meson.build
# + hw/i386/Kconfig, configure --target-list=i386-softmmu only, build.
#
# Output: qemu-build/qemu-<ver>/build/qemu-system-i386
#
# Idempotent: re-running just refreshes the copies and rebuilds (ninja).
set -euo pipefail

QEMU_VER="${QEMU_VER:-10.0.8}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# Build tree must live on a real Linux fs (vmhgfs / vboxsf can't make symlinks
# that QEMU's source tarball relies on).  Default outside the repo.
WORK="${P2K_QEMU_BUILD_DIR:-$HOME/.cache/p2k-qemu-build}"
SRC="$WORK/qemu-$QEMU_VER"
TARBALL="qemu-$QEMU_VER.tar.xz"
URL="https://download.qemu.org/$TARBALL"

mkdir -p "$WORK"
cd "$WORK"

if [[ ! -d "$SRC" ]]; then
  if [[ ! -f "$TARBALL" ]]; then
    echo "[build-qemu] downloading $URL"
    curl -L --progress-bar -o "$TARBALL.part" "$URL"
    mv "$TARBALL.part" "$TARBALL"
  fi
  echo "[build-qemu] extracting $TARBALL"
  tar -xf "$TARBALL"
fi

# --- Inject our machine source ---------------------------------------------
HW_I386="$SRC/hw/i386"
echo "[build-qemu] copying qemu/{pinball2000,p2k-*}.{c,h} -> $HW_I386/"
# Headers first so the .c files compile
cp "$ROOT/qemu/pinball2000.h" "$HW_I386/pinball2000.h"
cp "$ROOT/qemu/p2k-internal.h" "$HW_I386/p2k-internal.h"
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
  ./configure \
    --target-list=i386-softmmu \
    --disable-docs \
    --disable-tools \
    --disable-guest-agent \
    --disable-gtk \
    --disable-vnc \
    --disable-werror \
    --enable-sdl \
    --enable-debug
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
