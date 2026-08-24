#!/usr/bin/env bash
# Download and verify the latest published Encore QEMU binary.
set -euo pipefail

DESTINATION="${XDG_CACHE_HOME:-$HOME/.cache}/encore-qemu-release"
REPOSITORY="${ENCORE_RELEASE_REPOSITORY:-ThomazPom/Encore-Pinball2000}"
ASSET=encore-pinball2000-linux-x86_64.tar.gz

while [[ $# -gt 0 ]]; do
    case "$1" in
        --destination) [[ -n "${2:-}" ]] || { echo "$1 needs a directory" >&2; exit 2; }; DESTINATION=$2; shift 2 ;;
        -h|--help)
            echo "Usage: $0 [--destination DIR]"
            exit 0 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done

case "$(uname -m)" in
    x86_64|amd64) ;;
    *) echo "Published Encore QEMU builds currently require x86_64." >&2; exit 2 ;;
esac
for command_name in curl tar sha256sum; do
    command -v "$command_name" >/dev/null 2>&1 || {
        echo "Missing download prerequisite: $command_name" >&2
        exit 2
    }
done

base="${ENCORE_RELEASE_BASE_URL:-https://github.com/$REPOSITORY/releases/latest/download}"
work=$(mktemp -d "${TMPDIR:-/tmp}/encore-qemu-release.XXXXXX")
trap 'rm -rf -- "$work"' EXIT

echo "[download-qemu] downloading latest published x86_64 build"
curl -fL --retry 3 -o "$work/$ASSET" "$base/$ASSET"
curl -fL --retry 3 -o "$work/$ASSET.sha256" "$base/$ASSET.sha256"
(
    cd "$work"
    sha256sum -c "$ASSET.sha256"
)
mkdir "$work/package"
tar -xzf "$work/$ASSET" -C "$work/package"

release_root="$work/package/Encore-Pinball2000"
binary="$release_root/qemu-system-i386"
[[ -x "$binary" ]] || { echo "Release archive contains no executable qemu-system-i386" >&2; exit 3; }
if ! ldd "$binary" 2>/dev/null | grep -q 'not found'; then
    "$binary" -M help | grep -q pinball2000 || {
        echo "Downloaded QEMU does not contain the pinball2000 machine" >&2
        exit 3
    }
else
    echo "[download-qemu] runtime packages are not installed yet; validation is deferred"
fi

mkdir -p "$DESTINATION"
install -m 0755 "$binary" "$DESTINATION/qemu-system-i386.new"
mv -f "$DESTINATION/qemu-system-i386.new" "$DESTINATION/qemu-system-i386"
for metadata in build-info.txt runtime-packages.txt; do
    [[ ! -f "$release_root/$metadata" ]] ||
        install -m 0644 "$release_root/$metadata" "$DESTINATION/$metadata"
done

echo "[download-qemu] installed $DESTINATION/qemu-system-i386"
[[ ! -f "$DESTINATION/build-info.txt" ]] || sed 's/^/[download-qemu] /' "$DESTINATION/build-info.txt"
