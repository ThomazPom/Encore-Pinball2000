# 37 — Distribution

This document describes the folder layout of a ready-to-run Encore
installation after all files are in place.

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## Top-level layout

```
encore/
├── build/
│   └── encore              ← compiled binary (~800 KB)
├── docs/                   ← this documentation tree
├── include/
│   └── encore.h
├── src/
│   ├── cpu.c  bar.c  display.c  io.c  main.c  memory.c
│   ├── netcon.c  pci.c  rom.c  sound.c  lpt_pass.c
│   ├── symbols.c  stb_impl.c
│   └── stb_image_write.h
├── tools/
│   ├── build_update_bin.py
│   ├── deinterleave_rebuild.sh
│   ├── extract_sounds.py
│   ├── sym_dump.py
│   ├── analyze_rom_files.py
│   └── extract_rom_strings.py
├── roms/                   ← chip-ROM files (not redistributable)
├── updates/                ← update bundles (not redistributable)
├── savedata/               ← NVRAM/SEEPROM state (auto-created)
├── Makefile
└── README.md
```

## roms/ — chip-ROM directory

Place all chip-ROM files here. Encore searches this directory for files
matching the naming convention below.

### SWE1 chip ROMs

```
roms/
├── swe1_u100.rom   ← game bank 0, even bytes
├── swe1_u101.rom   ← game bank 0, odd bytes
├── swe1_u102.rom   ← game bank 1, even bytes
├── swe1_u103.rom   ← game bank 1, odd bytes
├── swe1_u104.rom   ← game bank 2, even bytes
├── swe1_u105.rom   ← game bank 2, odd bytes
├── swe1_u106.rom   ← game bank 3, even bytes
├── swe1_u107.rom   ← game bank 3, odd bytes
├── swe1_u109.rom   ← DCS sound ROM, bank A
├── swe1_u110.rom   ← DCS sound ROM, bank B
└── swe1_P2K.bin    ← pb2kslib sound container (shape-detected)
```

### RFM chip ROMs

RFM ships in two chip-ROM revisions. Place both sets if available;
Encore prefers r2 for bank 0:

```
roms/
├── rfm_u100.rom     rfm_u100r2.rom   ← bank 0 (r1 and r2)
├── rfm_u101.rom     rfm_u101r2.rom
├── rfm_u102.rom  …  rfm_u107.rom     ← banks 1–3 (r1 only)
├── rfm_u109.rom     rfm_u110.rom     ← DCS sound ROM
└── rfm_P2K.bin                       ← pb2kslib sound container
```

### BIOS

```
roms/
└── bios.bin        ← MediaGX BIOS image
```

The BIOS image can also live at `./bios.bin` (next to the binary).

## updates/ — update bundle directory

Name bundles using the Williams convention so `--update VERSION` token
resolution works:

```
updates/
├── pin2000_50069_0150_<date>_B_10000000/   ← SWE1 v1.5
│   ├── <game_id>_bootdata.rom
│   ├── <game_id>_im_flsh0.rom
│   ├── <game_id>_game.rom
│   └── <game_id>_symbols.rom
├── pin2000_50069_0210_<date>_B_10000000/   ← SWE1 v2.1
├── pin2000_50070_0120_<date>_B_10000000/   ← RFM v1.2
├── pin2000_50070_0160_<date>_B_10000000/   ← RFM v1.6
├── pin2000_50070_0180_<date>_B_10000000/   ← RFM v1.8
├── pin2000_50070_0250_<date>_B_10000000/   ← RFM v2.5
└── pin2000_50070_0260_<date>_B_10000000/   ← RFM v2.6
```

Pre-assembled `update.bin` files (from `build_update_bin.py`) can live
directly inside the bundle directory or at any path passed via
`--update /path/to/update.bin`.

## savedata/ — persistent state

Created automatically on first run. Do not delete during normal use;
doing so resets all game credits, audits, adjustments and high scores.

```
savedata/
├── swe1_21.nvram    ← NVRAM (game adjustments, audits)
├── swe1_21.seeprom  ← SEEPROM (credits, coin settings)
└── swe1_21.ems      ← EMS state
```

Pass `--no-savedata` to skip loading and saving these files.

## What to zip for sharing

The binary and documentation are freely distributable. ROM and update
files are copyrighted by Williams and must not be included in any
redistribution. A shareable zip contains only:

```
encore binary (build/encore)
docs/
tools/
Makefile
README.md
src/   (source code)
include/
```

Do not include `roms/`, `updates/`, or `savedata/`.

## Cross-references

* ROM loading: [08-rom-loading-pipeline.md](08-rom-loading-pipeline.md)
* Update loader: [09-update-loader.md](09-update-loader.md)
* Savedata: [10-savedata.md](10-savedata.md)
* RFM r2 chips: [35-rfm-vs-swe1.md](35-rfm-vs-swe1.md)

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
