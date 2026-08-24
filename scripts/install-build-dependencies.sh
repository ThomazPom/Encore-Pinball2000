#!/usr/bin/env bash
# Install the canonical QEMU build prerequisites on a Debian-family host.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/scripts/dependencies.sh"

command -v apt-get >/dev/null 2>&1 || {
  echo "install-build-dependencies.sh: APT is required" >&2
  exit 2
}

if [[ ${EUID} -eq 0 ]]; then
  elevate=()
elif command -v sudo >/dev/null 2>&1; then
  elevate=(sudo)
else
  echo "install-build-dependencies.sh: run as root or install sudo" >&2
  exit 2
fi

"${elevate[@]}" apt-get update
"${elevate[@]}" env DEBIAN_FRONTEND=noninteractive \
  apt-get install -y --no-install-recommends "${ENCORE_APT_QEMU_BUILD[@]}"
