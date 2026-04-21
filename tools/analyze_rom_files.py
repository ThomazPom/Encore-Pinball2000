#!/usr/bin/env python3
"""
analyze_rom_files.py — Survey all .bin ROM files in emulator/P2K/roms/

Checks each file for:
  - Container format (P2K.bin-style header: version/hdr_sz/count/entry_sz)
  - Known binary signatures (JPEG, OGG, ELF, PE, GZIP, RIFF, …)
  - Shannon entropy (high entropy ≈ compressed/encrypted; low ≈ plaintext code)

Findings (2026-04-01):
  rfm_P2K.bin  / swe1_P2K.bin  → P2K sound library (OGG, 866/689 entries)
  rfm_minimization.bin  / swe1_minimization.bin  → identical format, Minimization variant (same counts)
  rfm_u109.bin    / rfm_u110.bin     → high entropy (~7.9 bits/byte) — likely encrypted
                                        or compressed video/code ROM, format unknown
  bios.bin                           → raw x86 flat binary (not ELF), entropy ~5.5

Usage:
    cd composis
    python3 reverseEngineering/analyze_rom_files.py [roms_dir]

    Default roms_dir: emulator/P2K/roms
"""

import os
import sys
import math
import struct

ROMS_DIR = os.path.join(os.path.dirname(__file__), "..", "emulator", "P2K", "roms")
KEY = 0x3A  # XOR key used by P2K sound library

SIGNATURES = [
    (b"OggS",           "OGG Vorbis"),
    (b"\xff\xd8\xff",   "JPEG"),
    (b"\x89PNG",        "PNG"),
    (b"BM",             "BMP"),
    (b"RIFF",           "RIFF/WAV/AVI"),
    (b"\x00\x00\x01\xb3","MPEG video"),
    (b"\x00\x00\x01\xba","MPEG-2"),
    (b"MZ",             "PE/DOS"),
    (b"\x7fELF",        "ELF"),
    (b"\x1f\x8b",       "GZIP"),
    (b"PK\x03\x04",     "ZIP"),
]


def entropy(data: bytes) -> float:
    counts = [0] * 256
    for b in data:
        counts[b] += 1
    ent = 0.0
    n = len(data)
    for c in counts:
        if c:
            p = c / n
            ent -= p * math.log2(p)
    return ent


def try_P2K_header(data: bytes) -> dict | None:
    """Return parsed header dict if data looks like a P2K container, else None."""
    if len(data) < 16:
        return None
    ver, hdr_sz, count, entry_sz = struct.unpack_from("<IIII", data, 0)
    if ver == 1 and hdr_sz == 16 and entry_sz == 72 and count > 0:
        return {"version": ver, "count": count, "entry_size": entry_sz}
    return None


def sample_entries(data: bytes, header: dict, n: int = 3) -> list[dict]:
    """Decode first n index entries from a P2K container."""
    entries = []
    hdr_sz = 16
    entry_sz = header["entry_size"]
    for i in range(min(n, header["count"])):
        off = hdr_sz + i * entry_sz
        dec = bytes(b ^ KEY for b in data[off : off + entry_sz])
        n1 = dec.find(b"\x00")
        name = dec[:n1].decode("latin-1") if n1 >= 0 else "?"
        n2 = dec.find(b"\x00", n1 + 1)
        fmt = dec[n1 + 1 : n2].decode("latin-1") if n2 > n1 + 1 else "?"
        data_off, data_sz = struct.unpack_from("<II", dec, entry_sz - 8)
        blob_magic = bytes(b ^ KEY for b in data[data_off : data_off + 4]) if data_off < len(data) else b""
        entries.append({"name": name, "fmt": fmt, "offset": data_off, "size": data_sz, "blob_magic": blob_magic.hex()})
    return entries


def analyze(path: str):
    with open(path, "rb") as f:
        data = f.read()

    size_kb = len(data) // 1024
    ent = entropy(data)
    print(f"\n{'='*60}")
    print(f"  {os.path.basename(path)}  ({size_kb} KB, entropy={ent:.3f} bits/byte)")
    print(f"{'='*60}")

    # Check P2K container format
    hdr = try_P2K_header(data)
    if hdr:
        print(f"  ✓ P2K container: version={hdr['version']} count={hdr['count']} entry_size={hdr['entry_size']}")
        for e in sample_entries(data, hdr):
            print(f"    [{e['name']!r:18s}] fmt={e['fmt']} offset=0x{e['offset']:07x} size={e['size']:7d} magic={e['blob_magic']}")
    else:
        print(f"  ✗ Not a P2K container")

    # Check known signatures
    hits = [(label, data.count(magic)) for magic, label in SIGNATURES if data.count(magic) > 0]
    if hits:
        print("  Signatures found:")
        for label, count in hits:
            print(f"    {label}: {count}×")
    else:
        print("  No known signatures found")

    # ELF detection
    if data[:4] == b"\x7fELF":
        bits = "32-bit" if data[4] == 1 else "64-bit"
        e_machine = struct.unpack_from("<H", data, 18)[0]
        print(f"  ELF {bits}, machine=0x{e_machine:04x}")

    # Entropy interpretation
    if ent > 7.8:
        print("  ⚠ Very high entropy — likely encrypted or compressed")
    elif ent < 5.0:
        print("  ✓ Low entropy — likely plaintext code or data")


def main():
    roms_dir = sys.argv[1] if len(sys.argv) > 1 else ROMS_DIR
    roms_dir = os.path.realpath(roms_dir)

    if not os.path.isdir(roms_dir):
        print(f"ERROR: directory not found: {roms_dir}", file=sys.stderr)
        sys.exit(1)

    print(f"Scanning: {roms_dir}")
    bin_files = sorted(f for f in os.listdir(roms_dir) if f.endswith(".bin") or f.endswith(".rom"))
    for fn in bin_files:
        analyze(os.path.join(roms_dir, fn))

    print("\nDone.")


if __name__ == "__main__":
    main()
