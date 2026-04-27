#!/usr/bin/env bash
# Build a patched qemu-system-i386 with the 'pinball2000' machine type.
#
# NOT YET IMPLEMENTED — see qemu/README.md for the planned recipe.
# The intent is:
#   1. git submodule add upstream QEMU under qemu-src/ at a pinned tag.
#   2. Symlink qemu/*.c qemu/*.h into qemu-src/hw/i386/.
#   3. Patch qemu-src/hw/i386/meson.build and Kconfig to register them.
#   4. ./configure --target-list=i386-softmmu --disable-werror && make -j.
#   5. Install resulting binary as build/qemu-system-i386.
set -euo pipefail
echo "build-qemu.sh: not implemented yet — see qemu/README.md" >&2
exit 1
