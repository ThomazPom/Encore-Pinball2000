#!/usr/bin/env python3
"""
sym_dump.py — Pinball 2000 XINU symbol-table reader.

Encore's update-flash assembler consumes each `*_symbols.rom` blob together
with the other update components. This script is the offline reader used to
dump entries and perform name/address lookups.

File layout (verified across the preserved SWE1/RFM update set):

    +0x00  "SYMBOL TABLE"                      (12 B magic)
    +0x0C  u32 checksum
    +0x10  u32 num_entries
    +0x14  u32 string_table_size
    +0x18  entries[num_entries] = (u32 addr, u32 name_off)
    +end   string table (NUL-terminated cstrings)

The string table starts immediately after the number of entries declared in
the header. Reverse lookup is done by scanning for `name\\0` and keeping the
occurrence whose `(pos - str_base)` matches a real entry.

Symbol coverage differs by release. Callers must test the exact lookup result
rather than infer that a symbol is present from the game family or version.
"""
import argparse
import os
import struct

MAGIC = b"SYMBOL TABLE"
HDR   = 24


def parse(path):
    with open(path, "rb") as source:
        d = source.read()
    if d[:12] != MAGIC:
        raise ValueError(f"bad magic: {d[:12]!r}")
    if len(d) < HDR:
        raise ValueError(f"truncated symbol-table header: {len(d)} bytes")
    chk, n, str_sz = struct.unpack_from("<III", d, 12)
    str_base = HDR + n * 8
    if str_base > len(d):
        raise ValueError(
            f"truncated symbol entries: need 0x{str_base:x}, have 0x{len(d):x}"
        )

    by_no = {}
    for i in range(n):
        addr, no = struct.unpack_from("<II", d, HDR + i * 8)
        if str_base + no >= len(d):
            raise ValueError(
                f"entry {i} name offset 0x{no:x} lies outside string table"
            )
        by_no.setdefault(no, []).append(addr)
    return d, n, str_base, by_no, {
        "chk": chk,
        "n_hdr": n,
        "str_sz": str_sz,
    }


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
          f"str_sz=0x{hdr['str_sz']:x}")

    if args.all or args.grep:
        for i in range(n):
            addr, no = struct.unpack_from("<II", d, HDR + i * 8)
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
            addr, no = struct.unpack_from("<II", d, HDR + i * 8)
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
