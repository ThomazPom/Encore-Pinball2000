#!/usr/bin/env python3
"""Assemble update.bin from a de-archived P2K-runtime bundle.

Layout (verified from rfm_15_update.bin):
    0x000000  bootdata.rom   (32 KB)
    0x008000  im_flsh0.rom   (~615 KB)
    0x09e1f4  game.rom       (~2.5 MB)   ← offset varies with im_flsh0 size
    0x308ff4  symbols.rom    (~750 KB)   ← offset varies with game size

Each part is concatenated with NO padding between, except bootdata at 0x0
and im_flsh0 starts at 0x8000. Subsequent offsets follow naturally.

Usage:
    build_update_bin.py BUNDLE_DIR [OUTPUT_FILE]
"""
import os, sys, glob

def main():
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    bundle = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(bundle, "update.bin")

    # Find rom files (in bundle/<game_id>/ or bundle/ directly)
    candidates = glob.glob(os.path.join(bundle, "**", "*.rom"), recursive=True)
    by_kind = {}
    for c in candidates:
        name = os.path.basename(c).lower()
        for kind in ("bootdata","im_flsh0","game","symbols","pubboot","sf"):
            if "_" + kind + "." in name:
                by_kind[kind] = c
                break
    needed = ["bootdata","im_flsh0","game","symbols"]
    missing = [k for k in needed if k not in by_kind]
    if missing:
        print(f"ERROR: missing {missing}; found: {list(by_kind)}"); sys.exit(2)

    bootdata = open(by_kind["bootdata"],"rb").read()
    im_flsh0 = open(by_kind["im_flsh0"],"rb").read()
    game     = open(by_kind["game"],"rb").read()
    symbols  = open(by_kind["symbols"],"rb").read()

    buf = bytearray(4*1024*1024)  # 4MB max
    # bootdata @ 0
    buf[0:len(bootdata)] = bootdata
    # im_flsh0 @ 0x8000
    off = 0x8000
    buf[off:off+len(im_flsh0)] = im_flsh0
    # game starts right after (no padding)
    off += len(im_flsh0)
    buf[off:off+len(game)] = game
    # symbols right after game
    off += len(game)
    buf[off:off+len(symbols)] = symbols
    total = off + len(symbols)
    # trim
    buf = buf[:total]

    with open(out,"wb") as f:
        f.write(buf)
    print(f"wrote {out} ({len(buf)} bytes = 0x{len(buf):x})")
    print(f"  bootdata @ 0x000000  ({len(bootdata)} B)")
    print(f"  im_flsh0 @ 0x008000  ({len(im_flsh0)} B)")
    print(f"  game     @ 0x{0x8000+len(im_flsh0):06x}  ({len(game)} B)")
    print(f"  symbols  @ 0x{0x8000+len(im_flsh0)+len(game):06x}  ({len(symbols)} B)")

if __name__ == "__main__":
    main()
