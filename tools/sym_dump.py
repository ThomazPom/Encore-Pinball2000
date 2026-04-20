#!/usr/bin/env python3
"""
sym_dump.py — Pinball 2000 XINU symbol-table reader.

The build pipeline embeds a `*_symbols.rom` blob into the update flash
(strategy 2 in src/rom.c).  The blob is also parsed at runtime by
src/symbols.c.  This script is the offline twin: dump entries and
do name<->addr lookups so we can replace hardcoded patch addresses
with sym_lookup() calls.

File layout (verified for SWE1 v1.5 / v2.1, RFM v1.6 / v2.6):

    +0x00  "SYMBOL TABLE"                      (12 B magic)
    +0x0C  u32 checksum
    +0x10  u32 num_entries
    +0x14  u32 string_table_size
    +0x18  u32 (likely guest base = 0x10000000)
    +0x1C  entries[num_entries] = (u32 name_off, u32 addr)
    +end   small zero pad, then string table (NUL-terminated cstrings)

`str_base` is found by trying offsets just past the entries array and
keeping the first one that resolves several entries to printable
strings.  Reverse lookup is done by scanning the file for `name\\0` and
keeping the occurrence whose `(pos - str_base)` matches a real entry.

NB: production RFM v1.6/v2.6 and SWE1 v1.5 ship STRIPPED tables
(no XINU internals like clkruns / Fatal); SWE1 v2.1 keeps most of them.
"""
import struct, sys, os, argparse

MAGIC = b"SYMBOL TABLE"
HDR   = 28


def parse(path):
    d = open(path, "rb").read()
    assert d[:12] == MAGIC, f"bad magic: {d[:12]!r}"
    chk, n_hdr, str_sz, base = struct.unpack_from("<IIII", d, 12)
    # Walk entries until addr leaves a plausible range; n_hdr is sometimes
    # off by a couple, so trust the walk.
    n = 0
    for i in range(n_hdr + 32):
        p = HDR + i * 8
        if p + 8 > len(d):
            break
        no, addr = struct.unpack_from("<II", d, p)
        if addr < 0x100000 or addr > 0x500000:
            break
        n += 1
    end = HDR + n * 8
    # Anchor str_base by trying byte offsets just past the entries
    # and picking the one for which entry-0..7 resolve to printable strings.
    by_no = {}
    for i in range(n):
        no, addr = struct.unpack_from("<II", d, HDR + i * 8)
        by_no.setdefault(no, []).append(addr)
    str_base = None
    for cand in range(end, end + 256):
        ok = 0
        for j in range(min(8, n)):
            nj, _ = struct.unpack_from("<II", d, HDR + j * 8)
            pj = cand + nj
            if pj <= 0 or pj >= len(d):
                break
            c = d[pj]
            if not (ord('A') <= c <= ord('z') or c == ord('_') or c == ord('~')):
                break
            ok += 1
        if ok >= 6:
            str_base = cand
            break
    if str_base is None:
        raise RuntimeError("could not anchor string table base")
    return d, n, str_base, by_no, {"chk": chk, "n_hdr": n_hdr, "str_sz": str_sz, "base": base}


def name_at(d, str_base, no):
    p = str_base + no
    end = d.find(b"\x00", p)
    if end < 0:
        return None
    return d[p:end].decode("ascii", "replace")


def lookup(d, n, str_base, by_no, name):
    needle = name.encode() + b"\x00"
    pos = 0
    while True:
        i = d.find(needle, pos)
        if i < 0:
            return None
        if i >= str_base and (i == str_base or d[i - 1] == 0):
            no = i - str_base
            if no in by_no:
                return by_no[no][0]
        pos = i + 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("symbols_rom")
    ap.add_argument("--all", action="store_true", help="dump every entry")
    ap.add_argument("--lookup", action="append", default=[],
                    help="resolve NAME -> address (repeatable)")
    ap.add_argument("--addr", action="append", default=[],
                    help="resolve 0xADDR -> name (repeatable)")
    ap.add_argument("--grep", action="append", default=[],
                    help="case-sensitive substring filter (repeatable)")
    args = ap.parse_args()

    d, n, sb, by_no, hdr = parse(args.symbols_rom)
    print(f"# {os.path.basename(args.symbols_rom)}  "
          f"entries={n} str_base=0x{sb:x} hdr_n={hdr['n_hdr']} "
          f"str_sz=0x{hdr['str_sz']:x} base=0x{hdr['base']:x}")

    if args.all or args.grep:
        for i in range(n):
            no, addr = struct.unpack_from("<II", d, HDR + i * 8)
            name = name_at(d, sb, no) or "?"
            if args.grep and not any(g in name for g in args.grep):
                continue
            print(f"  {addr:08x}  {name}")

    for name in args.lookup:
        a = lookup(d, n, sb, by_no, name)
        print(f"lookup  {name!r:40s} -> {('0x%x' % a) if a else 'MISS'}")

    for s in args.addr:
        target = int(s, 0)
        # Find nearest entry at or below target
        best = None
        for i in range(n):
            no, addr = struct.unpack_from("<II", d, HDR + i * 8)
            if addr <= target and (best is None or addr > best[0]):
                best = (addr, no)
        if best is None:
            print(f"addr  {s} -> MISS")
        else:
            name = name_at(d, sb, best[1]) or "?"
            delta = target - best[0]
            print(f"addr  {s} -> {best[0]:08x} {name}{'' if delta == 0 else f' + 0x{delta:x}'}")


if __name__ == "__main__":
    main()
