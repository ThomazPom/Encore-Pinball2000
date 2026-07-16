#!/usr/bin/env python3
"""Print the PCM-cache key for the sound bytes consumed by the DSP."""

import argparse
import hashlib
import os
from pathlib import Path


def chip_path(roms: Path, game: str, number: int) -> Path:
    for path in (roms / f"{game}_u{number}.rom",
                 roms / f"{game}_u{number}.bin",
                 roms / game / f"u{number}.rom",
                 roms / game / f"u{number}.bin"):
        if path.is_file():
            return path
    raise FileNotFoundError(f"missing {game} U{number} sound ROM")


def flash_path(roms: Path, game: str, update: Path | None) -> Path:
    override = os.environ.get("P2K_DCS_SOUND_FLASH")
    if override:
        return Path(override)
    if update:
        matches = sorted(update.glob("*_sf.rom"))
        if matches:
            return matches[0]
    for path in (roms / f"{game}_28f800.rom",
                 roms / game / "28f800.rom"):
        if path.is_file():
            return path
    raise FileNotFoundError("missing 1 MiB DCS sound flash")


def sound_key(roms: Path, game: str, update: Path | None) -> str:
    chips = [chip_path(roms, game, number).read_bytes()
             for number in (109, 110)]
    dcs_rom = bytearray(8 * 1024 * 1024)
    for lane, chip in enumerate(chips):
        for source in range(0, len(chip) - 1, 2):
            target = (source // 2) * 4 + lane * 2
            if target + 2 > len(dcs_rom):
                break
            dcs_rom[target:target + 2] = chip[source:source + 2]
    flash = flash_path(roms, game, update).read_bytes()
    if len(flash) != 1024 * 1024:
        raise ValueError("DCS sound flash must be exactly 1 MiB")
    digest = hashlib.sha256()
    digest.update(dcs_rom)
    digest.update(flash)
    return digest.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", required=True)
    parser.add_argument("--roms", required=True, type=Path)
    parser.add_argument("--update", type=Path)
    args = parser.parse_args()
    print(sound_key(args.roms.resolve(), args.game,
                    args.update.resolve() if args.update else None))


if __name__ == "__main__":
    main()
