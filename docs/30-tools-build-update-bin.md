# 30 — tools/build_update_bin.py

`tools/build_update_bin.py` assembles a monolithic `update.bin` from a
directory of component ROM files produced by de-archiving a Williams
update installer.

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## Why this tool exists

Encore's `--update` flag accepts a pre-built `update.bin` directly. But
the files distributed by Williams come as `.exe` installers (renamed
ZIPs) that unpack into a directory of four separate `.rom` files.
`build_update_bin.py` concatenates them in the correct order and at the
correct offsets so the resulting binary matches what Williams' own
installer would produce.

## Bundle layout

```
0x000000  bootdata.rom       32 KB
0x008000  im_flsh0.rom      ~615 KB
0x09e1f4  game.rom          ~2.5 MB   (offset follows im_flsh0 size)
0x308ff4  symbols.rom       ~750 KB   (offset follows game.rom size)
```

The only fixed offset is `bootdata` at zero and `im_flsh0` at `0x8000`.
Subsequent components are packed with no gap. Exact offsets depend on
the sizes of `im_flsh0.rom` and `game.rom`, which vary between versions
and titles. The script prints the actual offsets for every run so you
can verify against a known-good dump.

## Component file names

The script searches recursively under the bundle directory for files
whose lowercase basename contains one of these keywords (preceded by
`_`):

| Keyword     | Component      |
|-------------|---------------|
| `bootdata`  | Bootstrap ROM  |
| `im_flsh0`  | BIOS / flash   |
| `game`      | Main game code |
| `symbols`   | Symbol table   |

Other files in the bundle (`pubboot`, `sf`) are ignored. The four
required files must all be present or the script exits with an error
listing what is missing.

## Usage

```sh
python3 tools/build_update_bin.py BUNDLE_DIR [OUTPUT_FILE]
```

* `BUNDLE_DIR` — path to the unpacked installer directory.
* `OUTPUT_FILE` — optional; defaults to `BUNDLE_DIR/update.bin`.

### Example

```sh
unzip pin2000_swe1_v21_installer.exe -d swe1_v21/
python3 tools/build_update_bin.py swe1_v21/
# → swe1_v21/update.bin (3 276 416 bytes = 0x31fdc0)
#   bootdata @ 0x000000  (32768 B)
#   im_flsh0 @ 0x008000  (630260 B)
#   game     @ 0x0a4ff4  (2621440 B)
#   symbols  @ 0x2a4ff4  (786432 B)
```

Then run Encore against it:

```sh
./build/encore --update swe1_v21/update.bin
```

## Relationship to the --update DIR path

Encore's built-in `--update DIR` handler (`src/rom.c`) performs the
same assembly on the fly using the same search logic. `build_update_bin.py`
is useful when you want a persistent `update.bin` on disk for sharing
or archival, or when debugging the layout itself.

## Cross-references

* Update loader: [09-update-loader.md](09-update-loader.md)
* ROM loading: [08-rom-loading-pipeline.md](08-rom-loading-pipeline.md)
* Symbol dump tool: [29-tools-sym-dump.md](29-tools-sym-dump.md)
