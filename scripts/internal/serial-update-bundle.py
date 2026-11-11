#!/usr/bin/env python3
"""Promote a completed XINA serial update into an Encore update bundle.

The guest programs two devices: the 4 MiB PRISM BAR3 flash and the 1 MiB DCS
sound flash.  This tool treats those final device images as the source of
truth, reconstructs the five runtime ROM components, reuses an identical
official bundle when possible, replaces the game's single custom bundle, and
atomically updates a per-game pin.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import struct
import tempfile
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path


BAR3_SIZE = 0x400000
BOOTDATA_SIZE = 0x8000
SOUND_FLASH_SIZE = 0x100000
GAME_IDS = {50069: "swe1", 50070: "rfm"}
KINDS = ("bootdata", "im_flsh0", "game", "symbols", "sf")


@dataclass(frozen=True)
class UpdateIdentity:
    game_id: int
    game: str
    major: int
    minor: int
    version: str
    build_timestamp: str
    build_date: str
    image_size: int
    game_size: int
    symbols_size: int

    @property
    def stem(self) -> str:
        return f"pin2000_{self.game_id}_{self.version}"

    @property
    def outer_name(self) -> str:
        return f"{self.stem}_{self.build_date}_B_10000000"


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def normalized_version(major: int, minor: int) -> str:
    # Factory 0.x/1.x builds encode a single decimal digit as 7/5 for
    # 0.70/1.50. Community 2.x builds use the minor field directly, so 2.01
    # is stored as 1 rather than 10.
    display_minor = minor * 10 if major < 2 and minor < 10 else minor
    value = major * 100 + display_minor
    if value < 0 or value > 9999:
        raise ValueError(f"implausible update version {major}.{minor}")
    return f"{value:04d}"


def parse_identity(flash: bytes) -> UpdateIdentity:
    if len(flash) != BAR3_SIZE:
        raise ValueError(
            f"BAR3 image must be exactly {BAR3_SIZE} bytes, got {len(flash)}"
        )

    raw_timestamp = flash[:24].split(b"\0", 1)[0]
    try:
        timestamp = raw_timestamp.decode("ascii")
        parsed_timestamp = datetime.strptime(timestamp, "%a %b %d %H:%M:%S %Y")
    except (UnicodeDecodeError, ValueError) as error:
        raise ValueError(f"invalid bootdata build timestamp: {raw_timestamp!r}") from error

    image_size = struct.unpack_from("<I", flash, 0x2C)[0] * 4
    game_id = struct.unpack_from("<I", flash, 0x3C)[0]
    major = struct.unpack_from("<I", flash, 0x40)[0]
    minor = struct.unpack_from("<I", flash, 0x44)[0]
    game_size = struct.unpack_from("<I", flash, 0x4C)[0] * 4
    symbols_size = struct.unpack_from("<I", flash, 0x54)[0] * 4

    if game_id not in GAME_IDS:
        raise ValueError(f"unsupported game id in bootdata: {game_id}")
    for label, size in (
        ("im_flsh0", image_size),
        ("game", game_size),
        ("symbols", symbols_size),
    ):
        if size <= 0 or size > BAR3_SIZE or size % 4:
            raise ValueError(f"implausible {label} size in bootdata: {size}")
    total = BOOTDATA_SIZE + image_size + game_size + symbols_size
    if total > BAR3_SIZE:
        raise ValueError(f"declared update contents exceed BAR3: 0x{total:x}")

    return UpdateIdentity(
        game_id=game_id,
        game=GAME_IDS[game_id],
        major=major,
        minor=minor,
        version=normalized_version(major, minor),
        build_timestamp=timestamp,
        build_date=parsed_timestamp.strftime("%m%d%Y"),
        image_size=image_size,
        game_size=game_size,
        symbols_size=symbols_size,
    )


def split_images(flash: bytes, sound: bytes) -> tuple[UpdateIdentity, dict[str, bytes]]:
    identity = parse_identity(flash)
    if len(sound) != SOUND_FLASH_SIZE:
        raise ValueError(
            f"DCS flash must be exactly {SOUND_FLASH_SIZE} bytes, got {len(sound)}"
        )

    im_start = BOOTDATA_SIZE
    game_start = im_start + identity.image_size
    symbols_start = game_start + identity.game_size
    end = symbols_start + identity.symbols_size
    components = {
        "bootdata": flash[:BOOTDATA_SIZE],
        "im_flsh0": flash[im_start:game_start],
        "game": flash[game_start:symbols_start],
        "symbols": flash[symbols_start:end],
        "sf": sound,
    }
    return identity, components


def component_paths(inner: Path) -> dict[str, Path] | None:
    result: dict[str, Path] = {}
    for kind in KINDS:
        matches = sorted(inner.glob(f"*_{kind}.rom"))
        if len(matches) != 1:
            return None
        result[kind] = matches[0]
    return result


def component_hashes(components: dict[str, bytes]) -> dict[str, str]:
    return {kind: sha256_bytes(components[kind]) for kind in KINDS}


def find_identical_bundle(
    identity: UpdateIdentity,
    expected_hashes: dict[str, str],
    roots: list[Path],
) -> Path | None:
    for root in roots:
        if not root.is_dir():
            continue
        candidates = sorted(root.glob(f"pin2000_{identity.game_id}_*/{identity.game_id}"))
        for inner in candidates:
            paths = component_paths(inner)
            if paths is None:
                continue
            if all(sha256_file(paths[kind]) == expected_hashes[kind] for kind in KINDS):
                return inner.resolve()
    return None


def write_file(path: Path, data: bytes) -> None:
    with path.open("xb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def fsync_directory(path: Path) -> None:
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def install_bundle(
    identity: UpdateIdentity,
    components: dict[str, bytes],
    hashes: dict[str, str],
    install_root: Path,
) -> Path:
    install_root.mkdir(parents=True, exist_ok=True)
    # A newly reconstructed serial installation is not necessarily the
    # canonical full release named by its BAR3 bootdata: the original tool's
    # `game` and `sfonly` modes deliberately produce mixed BAR3/DCS states.
    # Exact known five-component bundles are reused before this function is
    # called. Everything created here is the one current custom state for the
    # game; a later custom state replaces it instead of accumulating every
    # possible BAR3/DCS combination under a content-derived name.
    destination = install_root / identity.game

    temporary = Path(tempfile.mkdtemp(prefix=".incoming-", dir=install_root))
    previous: Path | None = None
    try:
        inner = temporary / str(identity.game_id)
        inner.mkdir()
        for kind in KINDS:
            write_file(inner / f"{identity.stem}_{kind}.rom", components[kind])
        manifest = {
            "format": 1,
            "source": "xina-fupdate-com1",
            "game": identity.game,
            "game_id": identity.game_id,
            "version": identity.version,
            "build_timestamp": identity.build_timestamp,
            "sha256": hashes,
        }
        manifest_data = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8")
        write_file(temporary / "serial-install.json", manifest_data)
        fsync_directory(inner)
        fsync_directory(temporary)

        if destination.exists() or destination.is_symlink():
            if destination.is_symlink() or not destination.is_dir():
                raise FileExistsError(
                    f"refusing unmanaged serial-update destination: {destination}"
                )
            try:
                existing_manifest = json.loads(
                    (destination / "serial-install.json").read_text(encoding="utf-8")
                )
            except (OSError, ValueError) as error:
                raise FileExistsError(
                    f"refusing unmanaged serial-update destination: {destination}"
                ) from error
            if (existing_manifest.get("source") != "xina-fupdate-com1" or
                    existing_manifest.get("game") != identity.game):
                raise FileExistsError(
                    f"refusing unmanaged serial-update destination: {destination}"
                )
            previous = Path(tempfile.mkdtemp(
                prefix=f".previous-{identity.game}-", dir=install_root
            ))
            previous.rmdir()
            os.rename(destination, previous)
        os.rename(temporary, destination)
        fsync_directory(install_root)
        if previous is not None:
            try:
                shutil.rmtree(previous)
                fsync_directory(install_root)
            except OSError as error:
                print(
                    f"[serial-update] warning: could not remove replaced custom "
                    f"bundle {previous}: {error}",
                    file=os.sys.stderr,
                )
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        if previous is not None and previous.exists() and not destination.exists():
            os.rename(previous, destination)
        raise
    return (destination / str(identity.game_id)).resolve()


def write_pin(pin: Path, inner: Path) -> None:
    pin.parent.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix=f".{pin.name}.", dir=pin.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            stream.write(str(inner.resolve()) + "\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, pin)
        fsync_directory(pin.parent)
    except Exception:
        temporary.unlink(missing_ok=True)
        raise


def remove_managed_bundle(install_root: Path, game: str) -> None:
    """Remove the game's replaceable custom bundle after selecting official data."""
    destination = install_root / game
    if not destination.exists():
        return
    if destination.is_symlink() or not destination.is_dir():
        return
    try:
        manifest = json.loads(
            (destination / "serial-install.json").read_text(encoding="utf-8")
        )
    except (OSError, ValueError):
        return
    if (manifest.get("source") != "xina-fupdate-com1" or
            manifest.get("game") != game):
        return
    try:
        shutil.rmtree(destination)
    except OSError as error:
        print(
            f"[serial-update] warning: could not remove obsolete custom "
            f"bundle {destination}: {error}",
            file=os.sys.stderr,
        )
        return
    try:
        fsync_directory(install_root)
    except OSError as error:
        print(
            f"[serial-update] warning: could not sync custom bundle "
            f"directory {install_root}: {error}",
            file=os.sys.stderr,
        )


def promote(
    game_flash: Path,
    sound_flash: Path,
    install_root: Path,
    search_roots: list[Path],
    pin: Path,
    expected_game: str | None = None,
) -> tuple[Path, UpdateIdentity, dict[str, str], bool]:
    identity, components = split_images(game_flash.read_bytes(), sound_flash.read_bytes())
    if expected_game and identity.game != expected_game:
        raise ValueError(
            f"serial update is for {identity.game}, expected {expected_game}"
        )
    hashes = component_hashes(components)
    roots = [root.resolve() for root in search_roots]
    inner = find_identical_bundle(identity, hashes, roots)
    created = inner is None
    if inner is None:
        inner = install_bundle(identity, components, hashes, install_root)
    write_pin(pin, inner)
    if not created:
        remove_managed_bundle(install_root, identity.game)
    return inner, identity, hashes, created


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Promote completed BAR3/DCS serial-update flashes into a bundle"
    )
    parser.add_argument("--game-flash", required=True, type=Path)
    parser.add_argument("--sound-flash", required=True, type=Path)
    parser.add_argument("--install-root", required=True, type=Path)
    parser.add_argument("--search-root", action="append", default=[], type=Path)
    pin = parser.add_mutually_exclusive_group(required=True)
    pin.add_argument("--pin", type=Path)
    pin.add_argument(
        "--pin-dir", type=Path,
        help="write <detected-game>.update-pin in this configuration directory",
    )
    parser.add_argument("--expected-game", choices=("swe1", "rfm"))
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        pin = args.pin
        if pin is None:
            detected = parse_identity(args.game_flash.read_bytes())
            pin = args.pin_dir / f"{detected.game}.update-pin"
        inner, identity, hashes, created = promote(
            game_flash=args.game_flash,
            sound_flash=args.sound_flash,
            install_root=args.install_root,
            search_roots=args.search_root,
            pin=pin,
            expected_game=args.expected_game,
        )
    except (OSError, ValueError) as error:
        print(f"serial-update: {error}", file=os.sys.stderr)
        return 1

    action = "created" if created else "reused"
    print(
        f"[serial-update] {action} {identity.game} {identity.version} "
        f"({identity.build_timestamp}) at {inner}"
    )
    for kind in KINDS:
        print(f"[serial-update] {hashes[kind]}  {kind}")
    print(f"[serial-update] pinned by {pin}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
