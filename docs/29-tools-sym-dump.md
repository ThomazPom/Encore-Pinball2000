# 29 — tools/sym_dump.py

`tools/sym_dump.py` is an offline XINU symbol-table reader. It
processes the `*_symbols.rom` file (also called `SYMBOLS.ROM`) that is
embedded in every Pinball 2000 update bundle and lets you resolve
function names to addresses and vice-versa.

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## Why this tool exists

Encore's patching philosophy prefers pattern scans and symbol lookups
over hardcoded addresses (see [21-patching-philosophy.md](21-patching-philosophy.md)).
The runtime symbol reader in `src/symbols.c` serves live lookups during
emulation. `sym_dump.py` is the offline companion for development: it
lets you verify that a given function exists in a specific bundle,
compare address layouts across versions, and discover new patch targets
without running the emulator.

## Symbol table format

```
+0x00  "SYMBOL TABLE"         12-byte magic
+0x0C  u32 checksum
+0x10  u32 num_entries
+0x14  u32 string_table_size
+0x18  u32 guest_base (typically 0x10000000)
+0x1C  entries[N] = { u32 name_offset, u32 address }
       (address is in the 0x100000–0x500000 guest range)
[end]  NUL-terminated ASCII string table
```

Entries are dense and the string table immediately follows them. The
`str_base` anchor is found heuristically: the script tries byte offsets
just past the entry array until it finds one where the first eight
entries resolve to printable ASCII names.

## Usage

```
tools/sym_dump.py SYMBOLS_ROM [options]
```

### Options

| Option | Purpose |
|---|---|
| `--all` | Dump every entry (address + name) |
| `--lookup NAME` | Resolve a symbol name → guest address |
| `--addr 0xADDR` | Resolve guest address → nearest symbol + offset |
| `--grep SUBSTR` | Filter dump output by substring (repeatable) |

### Examples

```sh
# List all symbols in an SWE1 v2.1 symbols ROM:
python3 tools/sym_dump.py updates/pin2000_50069_0210_*/symbols.rom --all

# Resolve one name:
python3 tools/sym_dump.py updates/pin2000_50069_0210_*/symbols.rom \
    --lookup clkruns

# Find what is near address 0x1931e6:
python3 tools/sym_dump.py updates/pin2000_50069_0210_*/symbols.rom \
    --addr 0x1931e6

# Find all DCS-related symbols:
python3 tools/sym_dump.py updates/pin2000_50069_0210_*/symbols.rom \
    --grep dcs
```

### Sample output

```
# pin2000_50069_0210_symbols.rom  entries=2341 str_base=0x4b18 hdr_n=2341 str_sz=0x6800 base=0x10000000
  00191870  dcs_cmd_queue_push
  00191900  dcs_cmd_flush
  001931e4  dcs_mode_select
lookup  'clkruns'                        -> 0x3444b0
addr    0x1931e6                         -> 001931e4 dcs_mode_select + 0x2
```

## Notes on stripped tables

Production RFM v1.6/v2.6 and SWE1 v1.5 ship with stripped symbol
tables — only exported API names are present; XINU internals like
`clkruns` and `Fatal` are absent. SWE1 v2.1 retains most of the XINU
symbols. The script handles both cases gracefully; `--lookup` returns
`MISS` for absent symbols.

## Relationship to runtime symbols.c

`src/symbols.c` implements the same parsing logic in C and is called
by `cpu.c` during boot to look up `clkruns`, the VSYNC callback, and
other dynamically-located addresses. `sym_dump.py` and `symbols.c` must
agree on the table format; if either is updated, check both.

## Cross-references

* Runtime symbol reader: [20-symbols-rom.md](20-symbols-rom.md)
* Patching philosophy: [21-patching-philosophy.md](21-patching-philosophy.md)
* Update bundle layout: [30-tools-build-update-bin.md](30-tools-build-update-bin.md)
