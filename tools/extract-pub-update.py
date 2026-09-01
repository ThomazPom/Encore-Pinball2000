#!/usr/bin/env python3
"""Extract a Pinball 2000 update bundle from a classic PUB installer."""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
from datetime import datetime
from pathlib import Path


ARCHIVE_MAGIC = bytes.fromhex("13 5d 65 8c 3a 01 02 00")
HEADER = struct.Struct("<IIHHHIIIIBBBIIIIHII")
ROM_NAME = re.compile(
    r"^pin2000_(?P<game>\d{5})_(?P<version>\d{4})_"
    r"(?P<kind>bootdata|game|im_flsh0|pubboot|sf|symbols)\.rom$"
)
REQUIRED_KINDS = {"bootdata", "game", "im_flsh0", "pubboot", "sf", "symbols"}

# idecomp is small, pure Python, and understands InstallShield 3 multipart
# archives. Keep the third-party source out of this repository and cache one
# reviewed revision instead.
IDECOMP_COMMIT = "bd2b77624b96bb2a4f347518d087126759296a03"
IDECOMP_ARCHIVE_SHA256 = "57c05965c69aae2af1d02aa6009f9051269621a2a5e566b20e38d3fe2638d483"
IDECOMP_URL = f"https://codeload.github.com/lephilousophe/idecomp/tar.gz/{IDECOMP_COMMIT}"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def cache_root() -> Path:
    base = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache"))
    return base / "encore-pinball2000" / "tools" / "idecomp" / IDECOMP_COMMIT


def acquire_idecomp() -> Path:
    root = cache_root()
    program = root / "idecomp.py"
    helper = root / "pwexplode.py"
    if program.is_file() and helper.is_file():
        return program

    root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="encore-idecomp-") as temporary:
        archive = Path(temporary) / "idecomp.tar.gz"
        print(f"Downloading pinned idecomp revision {IDECOMP_COMMIT[:12]}...")
        urllib.request.urlretrieve(IDECOMP_URL, archive)
        actual = sha256(archive)
        if actual != IDECOMP_ARCHIVE_SHA256:
            raise RuntimeError(
                "idecomp archive checksum mismatch: "
                f"expected {IDECOMP_ARCHIVE_SHA256}, got {actual}"
            )

        with tarfile.open(archive, "r:gz") as bundle:
            members = {Path(member.name).name: member for member in bundle.getmembers()}
            for name in ("idecomp.py", "pwexplode.py", "LICENSE.md"):
                member = members.get(name)
                if member is None or not member.isfile():
                    raise RuntimeError(f"idecomp archive is missing {name}")
                source = bundle.extractfile(member)
                if source is None:
                    raise RuntimeError(f"could not read {name} from idecomp archive")
                (root / name).write_bytes(source.read())

    return program


def archive_headers(data: bytes) -> list[tuple[int, tuple[int, ...]]]:
    headers = []
    offset = 0
    while True:
        offset = data.find(ARCHIVE_MAGIC, offset)
        if offset < 0:
            break
        if offset + HEADER.size <= len(data):
            fields = HEADER.unpack_from(data, offset)
            volume_total = fields[9]
            volume_number = fields[10]
            toc_address = fields[14]
            if volume_number >= 1 and (volume_total == 0 or volume_total >= volume_number):
                if toc_address < len(data) - offset:
                    headers.append((offset, fields))
        offset += 1
    return headers


def multipart_groups(headers: list[tuple[int, tuple[int, ...]]]) -> list[list[int]]:
    groups = []
    for index, (offset, fields) in enumerate(headers):
        total = fields[9]
        number = fields[10]
        if number != 1 or total < 2:
            continue
        group = [offset]
        cursor = index + 1
        for wanted in range(2, total + 1):
            while cursor < len(headers) and headers[cursor][1][10] != wanted:
                cursor += 1
            if cursor == len(headers):
                group = []
                break
            group.append(headers[cursor][0])
            cursor += 1
        if group:
            groups.append(group)
    return groups


def carve_group(data: bytes, starts: list[int], all_starts: list[int], output: Path) -> Path:
    output.mkdir(parents=True)
    for number, start in enumerate(starts, 1):
        later = [candidate for candidate in all_starts if candidate > start]
        end = min(later) if later else len(data)
        (output / f"PAYLOAD.{number}").write_bytes(data[start:end])
    return output / "PAYLOAD.1"


def extract_roms(installer: Path, idecomp: Path, temporary: Path) -> list[Path]:
    data = installer.read_bytes()
    headers = archive_headers(data)
    groups = multipart_groups(headers)
    if not groups:
        raise RuntimeError("no InstallShield 3 multipart payload found")

    all_starts = [offset for offset, _ in headers]
    for index, group in enumerate(groups, 1):
        carved = temporary / f"candidate-{index}"
        first_part = carve_group(data, group, all_starts, carved)
        result = subprocess.run(
            [sys.executable, str(idecomp), "-a", first_part.name, "*.rom"],
            cwd=carved,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        roms = sorted(path for path in carved.rglob("*.rom") if path.is_file())
        if result.returncode == 0 and roms:
            return roms
    raise RuntimeError("InstallShield payload contained no extractable PB2K ROM set")


def identify_set(roms: list[Path]) -> tuple[str, str, dict[str, Path]]:
    identified: dict[str, Path] = {}
    game = version = None
    for path in roms:
        match = ROM_NAME.fullmatch(path.name)
        if not match:
            continue
        if game is None:
            game, version = match["game"], match["version"]
        if (match["game"], match["version"]) != (game, version):
            raise RuntimeError("installer contains more than one PB2K game/version set")
        identified[match["kind"]] = path

    missing = REQUIRED_KINDS - identified.keys()
    if game is None or version is None or missing:
        raise RuntimeError(f"incomplete PB2K update; missing: {', '.join(sorted(missing))}")
    return game, version, identified


def default_bundle_name(game: str, version: str, roms: dict[str, Path]) -> str:
    newest = max(roms[kind].stat().st_mtime for kind in ("bootdata", "game"))
    date = datetime.fromtimestamp(newest).strftime("%m%d%Y")
    return f"pin2000_{game}_{version}_{date}_B_10000000"


def write_bundle(
    destination: Path, game: str, roms: dict[str, Path], force: bool
) -> None:
    if destination.exists():
        if not force:
            raise RuntimeError(f"destination already exists: {destination} (use --force)")
        if destination.is_dir():
            shutil.rmtree(destination)
        else:
            destination.unlink()

    game_dir = destination / game
    game_dir.mkdir(parents=True)
    for kind in sorted(REQUIRED_KINDS):
        shutil.copy2(roms[kind], game_dir / roms[kind].name)

    (destination / "gamelist.txt").write_text(
        "08/10/1999\r\n"
        "50070 Revenge From Mars\r\n"
        "50069 Star Wars Episode I\r\n"
        "60077 Test Fixture\r\n",
        encoding="ascii",
        newline="",
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Extract an Encore-compatible PB2K update from a classic PUB installer."
    )
    parser.add_argument("installer", type=Path, help="classic Windows PUB update .exe")
    parser.add_argument("-o", "--output", type=Path, help="output bundle directory")
    parser.add_argument("--force", action="store_true", help="replace an existing output directory")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    installer = args.installer.expanduser().resolve()
    if not installer.is_file():
        print(f"error: installer not found: {installer}", file=sys.stderr)
        return 2

    try:
        idecomp = acquire_idecomp()
        with tempfile.TemporaryDirectory(prefix="encore-pub-update-") as temporary:
            roms = extract_roms(installer, idecomp, Path(temporary))
            game, version, identified = identify_set(roms)
            destination = (
                args.output.expanduser().resolve()
                if args.output
                else Path.cwd() / default_bundle_name(game, version, identified)
            )
            write_bundle(destination, game, identified, args.force)
        print(f"Extracted PB2K {game} version {version} to {destination}")
        for path in sorted((destination / game).iterdir()):
            print(f"  {path.name}: {path.stat().st_size} bytes  sha256={sha256(path)}")
        return 0
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
