#!/usr/bin/env bash
# Canonical Debian-family package groups for Encore's host-side tooling.
#
# This file only declares data.  It never invokes APT.  Mutating the host is
# the responsibility of install.sh or a CI job that explicitly sources it.

ENCORE_APT_QEMU_BUILD=(
  ca-certificates build-essential coreutils pkg-config curl patch ninja-build
  python3 python3-venv tar xz-utils libsdl2-dev libglib2.0-dev
  libpixman-1-dev zlib1g-dev libslirp-dev libvorbis-dev libogg-dev
)

ENCORE_APT_RELEASE_TOOLS=(
  binutils gh
)

ENCORE_APT_DOWNLOAD=(
  ca-certificates curl tar coreutils
)

declare -A ENCORE_APT_COMMAND_PACKAGE=(
  [python3]=python3
  [systemd-inhibit]=systemd
  [cage]=cage
  [weston]=weston
  [modprobe]=kmod
)

ENCORE_APT_SDL_WAYLAND=(
  libsdl2-2.0-0 libwayland-client0 libegl1 libgles2 libgl1-mesa-dri
)

ENCORE_APT_SDL_KMSDRM=(
  libsdl2-2.0-0 libdrm2 libgbm1 libegl1 libgles2 libgl1-mesa-dri
)
