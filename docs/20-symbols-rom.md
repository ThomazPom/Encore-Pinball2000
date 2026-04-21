# 20 — The SYMBOL TABLE ROM

Every Pinball 2000 update bundle ships with a fifth payload alongside
`bootdata.rom`, `im_flsh0.rom` and `game.rom`: a `symbols.rom` blob
containing a plain symbol table. Williams used it internally for
post-mortem analysis; Encore uses it at runtime to avoid hard-coding
bundle-specific addresses.

Source: `src/symbols.c` (207 lines), offline dumper:
`tools/sym_dump.py`.

## File format

The blob is a simple, endian-little, 32-bit-wide records structure:

```
+0x00  "SYMBOL TABLE"          (12-byte magic)
+0x0C  u32 checksum
+0x10  u32 num_entries
+0x14  u32 string_table_size
+0x18  u32 guest_base            (usually 0x10000000, or 0)
+0x1C  entries[num_entries]:
           u32 name_offset       (relative to string table base)
           u32 addr              (guest virtual address)
+end_of_entries  small zero padding
+str_base        NUL-terminated C strings, packed back-to-back
```

`num_entries` is sometimes off by one or two compared to the actual
on-disk entry count; `symbols.c`'s walker trusts the data, not the
header:

```c
for (i = 0; i < n_hdr + 32; i++) {
    p = HDR + i * 8;
    if (p + 8 > size) break;
    read (no, addr) at p;
    if (addr < 0x100000 || addr > 0x500000) break;
    n++;
}
```

`str_base` is found by trying offsets just past the entries array and
keeping the first that resolves several entries to printable strings.

## Discovery inside the flash

`sym_init()` runs `memmem` over the entire 4 MB flash looking for the
magic. Typical hit is around offset `0x320000` (within the
`symbols.rom` portion of the bundle). We register the pointer and
entry count:

```c
[sym] table @ 0x003148d0 (flash off 0x0020e8d0): 4820 entries
```

Failure to find the magic is silent — `sym_lookup()` becomes a
no-op, and callers fall back to hardcoded addresses.

## Lookup API

```c
uint32_t sym_lookup(const char *name);
uint32_t sym_lookup_first(const char *const *names);
```

`sym_lookup()` returns the first matching address, or 0 on miss.
`sym_lookup_first()` accepts a NULL-terminated array of candidate
names and returns the first hit — useful for name variants across
C-vs-C++ mangled entries.

Typical call-site:

```c
uint32_t a = sym_lookup("Fatal(char const *,...)");
if (!a) a = 0x0022722Cu;       /* SWE1 v1.19 fallback */
```

## What's in a bundle

The shipped tables are **stripped** to various degrees. RFM v1.6 and
v2.6 and SWE1 v1.5 ship with only the public API plus a handful of
XINU internals; SWE1 v2.1 keeps most XINU internals (clkruns,
Fatal, NonFatal, pci_read_watchdog). So sym_lookup coverage depends
on the bundle.

Realistically-present symbols across all bundles include:

* `Fatal(char const *,...)` — the RTOS panic entry
* `NonFatal(char const *,...)` — the non-fatal diagnostic
* `PCI_RegDump` — a debug helper referenced by many callers
* `SwitchMatrix::…` — switch-matrix accessors
* Public hooks from XINA (menu entry points)

Internal functions like `clkint`, `ctxsw`, `resched`, `create` are
**not** guaranteed — they are only shipped with debug builds.

## Mangling

Symbols are stored as compiler-mangled names, but *not* the standard
Itanium mangling: Williams used a homegrown format that roughly
mirrors the human-readable argument-list form (`Fatal(char const
*,...)`). Encore's lookups use the same literal text you see in the
dumper's output.

## Relationship with pattern scanning

Encore's patching philosophy is explicit in
[21-patching-philosophy.md](21-patching-philosophy.md). For a given
patch site we try, in order:

1. A **pattern scan** of the code (best — works even on stripped
   bundles).
2. A **symbol lookup** via `sym_lookup()` (portable across game
   versions).
3. A **hardcoded address** for a specific known bundle (last resort).

Most patches use #1 and never need #2. A handful of patches needing a
specific *function* entry point (not a pattern) use #2 with a fallback
to #3.

## Offline workflow

To see what a bundle ships, run:

```sh
python3 tools/sym_dump.py --list updates/pin2000_50069_0210_*/symbols.rom | head -20
```

Output:

```
00101040 clkint()
00101128 clkruns(int)
00105afc ctxsw()
0010e120 create(char *, unsigned int, ...)
0010f8ac Fatal(char const *, ...)
0010fa14 NonFatal(char const *, ...)
...
```

See [29-tools-sym-dump.md](29-tools-sym-dump.md) for the complete
tool usage.

## Checksum

The `u32` at offset `0x0C` of the table is a simple additive checksum
of the rest of the blob. Encore does **not** validate it — if the
table parses and the entry addresses are plausible, we use it. Williams'
own service tool did validate the checksum, but we have seen
mid-bundle corruption that still produces a usable table for lookup.

## Future uses

The symbol table is currently consumed only by the patch sites. A
future diagnostic UI could surface it for live guest-memory inspection
(e.g. type a symbol, get its value). Today that lives only in the
offline dumper.
