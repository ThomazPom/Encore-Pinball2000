#!/usr/bin/env python3
"""
extract_rom_strings.py — Deinterleave paired ROM chips and extract readable strings.

The Pinball 2000 u100-u107 ROM pairs are stored as chip-interleaved images
(2 bytes from even chip, 2 bytes from odd chip, alternating). This script
deinterleaves a pair and extracts all printable ASCII strings, optionally
filtering by keyword categories.

Confirmed findings (2026-04-01) from rfm_u100+u101 (bank 0, entropy ~6.7):
  - XINU kernel (Xinu version, trap handler, PID management, semaphores)
  - XINA adaptation layer (XINA Version, XINACMOS, kill_destructs/resources)
  - C++ game engine (~4700 classes: MarsKneadsWomen, DeffRevengeLogo, ...)
  - Manager/Deff process architecture (Run/Render/timeout XINU processes)
  - Williams copyright 1998-1999, build path c:/Pin2000/games/revenge/

Usage:
    # Deinterleave and dump all strings:
    python3 extract_rom_strings.py rfm_u100.rom rfm_u101.rom

    # Filter by keyword (case-insensitive):
    python3 extract_rom_strings.py rfm_u100.rom rfm_u101.rom --filter xinu

    # Show category summary:
    python3 extract_rom_strings.py rfm_u100.rom rfm_u101.rom --summary

    # Save deinterleaved binary:
    python3 extract_rom_strings.py rfm_u100.rom rfm_u101.rom --save-bin bank0.bin
"""

import re
import sys
import os
import argparse


def deinterleave(even_path: str, odd_path: str) -> bytes:
    """Deinterleave two ROM chip files in 2-byte word chunks."""
    with open(even_path, "rb") as f:
        even = f.read()
    with open(odd_path, "rb") as f:
        odd = f.read()

    result = bytearray()
    length = min(len(even), len(odd))
    for i in range(0, length - 1, 2):
        result += even[i:i+2]
        result += odd[i:i+2]
    return bytes(result)


CATEGORIES = {
    "XINU/XINA OS": [
        "xinu", "xina", "kernel", "PID ", "semaphore", "process",
        "suicide", "scheduler", "suspend", "resume",
    ],
    "Build / Version": [
        "c:/", "C:/", "version", "Version", "Copyright", "copyright",
        "Software", ".ROM", "GCC",
    ],
    "C++ RTTI": [
        "::", "type_info", "destructor", "constructor",
    ],
    "RFM game": [
        "Revenge", "revenge", "Mars", "mars", "MarsKneads",
        "Deff", "deff", "Williams", "Pin2000", "PRISM",
    ],
    "SWE1 game": [
        "Jedi", "jedi", "Star", "star", "Episode", "Anakin",
        "Skywalker", "Vader", "swe1", "SWE1",
    ],
    "Manager framework": [
        "Manager(", "Run() process", "Render() process", "timeout process",
        "pinball ", "ball search", "ball serve",
    ],
}


def categorize(strings: list[bytes]) -> dict[str, list[str]]:
    result = {cat: [] for cat in CATEGORIES}
    for s in strings:
        decoded = s.decode("latin-1", errors="replace")
        dl = decoded.lower()
        for cat, kws in CATEGORIES.items():
            if any(kw.lower() in dl for kw in kws):
                result[cat].append(decoded)
                break
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("even_rom", help="Even-byte chip ROM file (e.g. rfm_u100.rom)")
    parser.add_argument("odd_rom",  help="Odd-byte chip ROM file  (e.g. rfm_u101.rom)")
    parser.add_argument("--filter", metavar="KEYWORD", help="Only show strings containing KEYWORD (case-insensitive)")
    parser.add_argument("--min-len", type=int, default=8, help="Minimum string length (default: 8)")
    parser.add_argument("--summary", action="store_true", help="Show per-category counts and samples")
    parser.add_argument("--save-bin", metavar="PATH", help="Save deinterleaved binary to PATH")
    args = parser.parse_args()

    print(f"Deinterleaving {args.even_rom} + {args.odd_rom} ...", file=sys.stderr)
    data = deinterleave(args.even_rom, args.odd_rom)
    print(f"  Deinterleaved: {len(data)//1024} KB", file=sys.stderr)

    if args.save_bin:
        with open(args.save_bin, "wb") as f:
            f.write(data)
        print(f"  Saved to {args.save_bin}", file=sys.stderr)

    pattern = rb"[ -~]{" + str(args.min_len).encode() + rb",}"
    strings = re.findall(pattern, data)
    print(f"  Found {len(strings)} strings (min_len={args.min_len})", file=sys.stderr)

    if args.summary:
        cats = categorize(strings)
        for cat, items in cats.items():
            print(f"\n=== {cat} ({len(items)}) ===")
            for item in sorted(set(items))[:20]:
                print(f"  {item[:120]}")
        return

    kw = args.filter.lower() if args.filter else None
    for s in strings:
        decoded = s.decode("latin-1", errors="replace")
        if kw is None or kw in decoded.lower():
            print(decoded)


if __name__ == "__main__":
    main()
