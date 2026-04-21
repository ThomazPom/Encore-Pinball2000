# 08 — ROM Loading Pipeline

The Pinball 2000 head boots from masked ROMs carried on the PRISM
card, plus an optional "update" flash image burned by the service tool.
Encore loads both from the host filesystem and assembles them into the
guest memory map.

Source of truth: `src/rom.c` (963 lines).

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## Chip ROMs

A complete bank is carried on eight socketed chips labelled
`U100`…`U107`. Each chip holds 2 MB of one byte-lane of a 4-way
interleaved 16 MB region — 8 chips × 2 MB × 1 byte = 16 MB per bank.
Williams populated only bank 0 on shipped boards; Encore still reserves
space for banks 1..3 but they remain zeroed.

File naming convention used under `./roms/`:

```
roms/
  swe1/
    U100_bank0.bin   (2 MB)
    U101_bank0.bin
    …
    U107_bank0.bin
    U109_dcs.bin     (4 MB DCS sound ROM half)
    U110_dcs.bin
  rfm/
    …same layout…
```

Identical ROMs are shared across every update revision of a given
title; only the update flash changes.

## De-interleaving

The interleave pattern is `byte0 of 16-MB from U100, byte1 from U101,
byte2 from U102, …, byte7 from U107, byte8 from U100, …`. That is, each
chip holds every 8th byte of the logical bank, offset by its chip index.

Actually the real pattern is 16-bit paired (pairs, not singles), and
`src/rom.c` `interleave_file()` reads two consecutive bytes at a time
from each chip and writes them into positions
`index*2 + 0` and `index*2 + 1` of the bank buffer:

```c
static long interleave_file(uint8_t *base, int index, FILE *fp)
{
    uint8_t pair[2];
    uint8_t *ptr = base + index * 2;
    while (fread(pair, 1, 2, fp) == 2) {
        ptr[0] = pair[0];
        ptr[1] = pair[1];
        ptr += 16;         /* stride = 8 chips × 2 bytes = 16 */
    }
}
```

Each chip file is read independently; failures on chips 4..7 are
tolerated because some boards were populated with only the lower half.

## Presence table

Bank 0 carries a 4-KiB DCS presence table at offset `0x10000`. The
game reads it during early init to decide whether a DCS-2 board is
fitted. On some dearchived bundles this table was zeroed during
archiving (the dearchivers' tool did not know it was meaningful).
Encore leaves whatever is on disk untouched; when it is zeroed, the
game will skip DCS init and our `--dcs-mode bar4-patch` byte-patch
becomes mandatory.

## Game auto-detect

`rom_detect_game()` runs at startup:

1. If `g_emu.game_prefix` is explicit (`swe1` / `rfm`), we use it.
2. Otherwise, look under `g_emu.roms_dir` for a subdirectory whose
   name is a valid prefix. The first match wins.
3. Derive `game_id`:
   * `swe1` → 50069 (Star Wars Episode I)
   * `rfm`  → 50070 (Revenge From Mars)
4. Derive `game_id_str` by scanning `savedata_dir` for existing
   `<prefix>_<suffix>.nvram2` or `.see` files. This lets Encore track
   multiple coexisting saves (e.g. `swe1_21` vs `swe1_14`).

When `--update FILE` is passed, `game_id` is re-derived from the
update's own header field at offset `0x3C` before rom loading even
begins; this catches mismatches between `--game` and the actual bundle
early.

## Update flash assembly

Four strategies, tried in order:

1. **Explicit `--update FILE`**, non-directory — read it verbatim.
2. **`--update DIR`** — call `assemble_update_from_dir(dir)` which
   walks for files matching `*_bootdata.rom`, `*_im_flsh0.rom`,
   `*_game.rom`, `*_symbols.rom` and concatenates them at the offsets
   documented in [07-memory-map.md](07-memory-map.md).
3. **`--update X.exe`** (detected by `unzip -l` returning 0) — a
   Williams-branded installer which happens to be a ZIP. We extract it
   to a temp directory under `./savedata/` (not `/tmp`, deliberately;
   rewritten in
   [32-tools-sound-decoder.md](32-tools-sound-decoder.md) history).
4. **No `--update`** — scan `./updates/<prefix>_*` and
   `./savedata/` for a pre-built `update.bin` and use the first hit.

The resulting 4 MiB buffer is memcpy'd into `g_emu.flash`, then
copied into guest MMIO BAR3 in `main.c` before `cpu_run()`.

## Version discovery for the resolver

`--update VERSION` needs to know which bundle directory to pick. The
convention Williams used:

```
pin2000_<game_id>_<vvvv>_<yyyymmdd>_B_10000000
```

where `vvvv = major * 100 + minor * 10` (so v2.1 → 0210, v1.80 → 0180).
The resolver normalises the user's input to 4 digits and matches
`pin2000_<gid>_<vvvv>_`. This works across all seven dearchived
bundles:

| gid   | vvvv | Title | Version |
|-------|------|-------|--------:|
| 50069 | 0150 | SWE1  |    1.5  |
| 50069 | 0210 | SWE1  |    2.1  |
| 50070 | 0120 | RFM   |    1.2  |
| 50070 | 0160 | RFM   |    1.6  |
| 50070 | 0180 | RFM   |    1.8  |
| 50070 | 0250 | RFM   |    2.5  |
| 50070 | 0260 | RFM   |    2.6  |

See [26-testing-bundle-matrix.md](26-testing-bundle-matrix.md) for
the full regression results.

## The symbol table

Immediately after `rom_load_all()` returns, `main.c` calls
`sym_init()`. This scans the freshly-loaded flash for the
`"SYMBOL TABLE"` magic (12 ASCII bytes) and indexes every entry.
Patch sites throughout `cpu.c` and `io.c` call `sym_lookup("NameOrMangled")`
and fall back to a hardcoded address when the table is missing or
stripped. See [20-symbols-rom.md](20-symbols-rom.md).

## Savedata at ROM load time

Before returning, `rom_load_all()` calls `savedata_load()` (unless
`--no-savedata` is set). This restores the four-file savedata set:

| Suffix | Target | Size |
|---|---|---:|
| `.nvram2` | `g_emu.bar2_sram` | 128 KiB |
| `.see`    | `g_emu.seeprom`   | 128 B |
| `.ems`    | `g_emu.ems`       | 16 B |
| `.flash`  | `g_emu.flash`     | 4 MiB |

If the `.flash` file is present, it shadows the update bundle from
disk. This mirrors real cabinet behaviour — the service installer
writes into flash and the flash persists across reboots.

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
