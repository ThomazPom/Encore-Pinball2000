#!/usr/bin/env python3
"""
extract_P2K_sounds.py
========================
Extract OGG Vorbis sound files from a Pinball 2000 {game}_P2K.bin sound library.

These files are XOR-obfuscated with key 0x3A (except the 16-byte file header
which is stored in plain form).

File format (confirmed by reverse engineering, 2026-04-01):
  [0x00-0x0F]  Header (NOT XOR'd), 4 × uint32 LE:
                 version     = 1
                 header_size = 16
                 count       = number of sound entries
                 entry_size  = 72 (0x48)
  [0x10-...]   Index table  (XOR'd with 0x3A), count × 72 bytes per entry:
                 [0:?]   name   — null-terminated ASCII (e.g. "S0001-LP", "dcs-bong")
                 [?+1:?] format — null-terminated ASCII (always "ogg")
                 [64:68] offset — uint32 LE, absolute offset in file
                 [68:72] size   — uint32 LE, byte length of blob
  [offset...]  Data blobs (XOR'd with 0x3A): OGG Vorbis audio
                 All samples: mono, 22050 Hz, Vorbis codec.
                 Naming: S[XXXX]-LP  = single looping sample (hex sound ID)
                         S[XXXX]-LP1 / -LP2 = two-part looping sample
                         dcs-bong    = DCS audio system startup sound (entry 0)

DCS context:
  The original Pinball 2000 hardware had a dedicated DCS (Digital Compression
  System) audio board. P2K emulates this board in software, loading all
  game audio from this .bin file rather than the real DCS ROM chips.
  The samples are OGG Vorbis re-encodings of the original DCS audio streams.

Usage:
  python3 extract_P2K_sounds.py rfm_P2K.bin [output_dir]
  python3 extract_P2K_sounds.py swe1_P2K.bin [output_dir]
"""

import sys
import struct
import os

XOR_KEY = 0x3A


def xor_decode(data: bytes) -> bytes:
    return bytes(b ^ XOR_KEY for b in data)


def parse_entry(raw: bytes, offset: int, entry_sz: int):
    enc = raw[offset: offset + entry_sz]
    dec = xor_decode(enc)
    n1 = dec.find(b'\x00')
    name = dec[:n1].decode('latin1') if n1 >= 0 else dec[:32].decode('latin1')
    n2 = dec.find(b'\x00', n1 + 1)
    fmt = dec[n1 + 1:n2].decode('latin1') if n2 > n1 + 1 else 'ogg'
    data_off, data_sz = struct.unpack_from("<II", dec, entry_sz - 8)
    return name, fmt, data_off, data_sz


def extract(bin_path: str, out_dir: str):
    with open(bin_path, "rb") as f:
        raw = f.read()

    # Header is NOT XOR'd
    version, hdr_sz, count, entry_sz = struct.unpack_from("<IIII", raw, 0)
    print(f"File   : {bin_path} ({len(raw)/1024/1024:.1f} MB)")
    print(f"Version: {version}  header_size: {hdr_sz}  entries: {count}  entry_size: {entry_sz}")

    if version != 1 or hdr_sz != 16 or entry_sz != 72:
        print("WARNING: unexpected header values — may not be a P2K.bin")

    os.makedirs(out_dir, exist_ok=True)

    ok = 0
    errors = 0
    for i in range(count):
        entry_off = hdr_sz + i * entry_sz
        name, fmt, data_off, data_sz = parse_entry(raw, entry_off, entry_sz)

        if data_off + data_sz > len(raw):
            print(f"  [{i:4d}] {name}: out-of-bounds offset {data_off}+{data_sz} > {len(raw)}, skipping")
            errors += 1
            continue

        blob = xor_decode(raw[data_off: data_off + data_sz])
        out_path = os.path.join(out_dir, f"{name}.{fmt}")
        with open(out_path, "wb") as f:
            f.write(blob)
        ok += 1

    print(f"Extracted: {ok} files → {out_dir}/")
    if errors:
        print(f"Errors   : {errors}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <game>_P2K.bin [output_dir]")
        sys.exit(1)

    bin_path = sys.argv[1]
    out_dir  = sys.argv[2] if len(sys.argv) > 2 else \
               os.path.splitext(os.path.basename(bin_path))[0] + "_sounds"

    extract(bin_path, out_dir)
