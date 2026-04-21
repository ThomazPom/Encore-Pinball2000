# Encore — Pinball 2000 Emulator

Encore is a clean, self-contained x86 emulator for the Pinball 2000
platform. It bundles everything needed to boot the included games and
run the full display + audio pipeline from a single folder.

## Quick start

```sh
make                                           # build ./build/encore
./build/encore --game swe1                     # boot SWE1 with default settings
./build/encore --update 210                    # boot the SWE1 v2.1 bundle
./build/encore --update 2.6 --dcs-mode io-handled   # RFM v2.6, I/O-handled DCS
./build/encore --update latest --game rfm            # newest RFM bundle bundled here
```

## Folder layout

```
Encore-Pinball2000/
├── Makefile
├── README.md
├── build/                    # build artefacts (created by make)
├── include/
├── src/                      # C sources
├── tools/                    # helper scripts (sym_dump, update builder, etc.)
├── roms/                     # bundled P2K chip ROMs + BIOS + sound containers
├── updates/                  # 7 dearchived pin2000_* update packages
└── docs/                     # full documentation tree
```

## Command-line highlights

| Flag                        | Purpose                                        |
|-----------------------------|------------------------------------------------|
| `--game swe1\|rfm\|auto`    | Pick title; `auto` detects from ROMs           |
| `--update <path\|version>`  | File, directory, `.exe`, or version token (`210`, `2.6`) |
| `--dcs-mode bar4-patch\|io-handled` | Sound pipeline selector (default: `bar4-patch`) |
| `--headless`                | Run without opening a window                   |
| `--fullscreen`              | Open at full resolution                        |
| `--flipscreen`              | Rotate output 180°                             |
| `--bpp N`                   | Colour depth for the SDL window                |
| `--no-savedata`             | Skip NVRAM / SEEPROM load                      |
| `--config FILE`             | Load options from a YAML file                  |

See `docs/03-cli-reference.md` for the complete list.

## Documentation

Start with `docs/README.md` for the full index.
