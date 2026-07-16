# 15 — ROM and update loading

Encore loads two different kinds of content:

- physical game and sound chip images from `roms/`;
- software-update components from `updates/`.

## Physical ROM chips

The chip loader accepts `.rom` or `.bin` files in either layout:

```text
roms/swe1_u100.rom
roms/swe1/u100.bin
```

RFM uses the `rfm` prefix. `--roms DIR` changes the ROM root.

| Chips | Contents | Required |
|---|---|---|
| `u100/u101` | Game bank 0 and boot image | Yes |
| `u102/u103` | Game bank 1 | No |
| `u104/u105` | Game bank 2 | No |
| `u106/u107` | Game bank 3 | No |
| `u109/u110` | DCS sound ROM | For `adsp` and `adsp-thread` sound |

Each pair contains two 16-bit lanes. `qemu/p2k-rom.c` reads two bytes from one
chip, places them in its lane, and advances four bytes in the combined bank.
Missing optional game banks still receive mapped ROM windows filled with `ff`.

The first 32 KiB of bank 0 is visible at `000c0000`. On reset, Encore also
copies it to RAM at `00080000`, where execution begins.

Details: [memory map](13-memory-map.md) and
[boot path](14-boot-recipe.md).

## Drop an update into `updates/`

Extract the update so it has this directory shape:

```text
updates/
└── pin2000_50069_0210_10312025_B_10000000/
    ├── gamelist.txt
    └── 50069/
        ├── pin2000_50069_0210_bootdata.rom
        ├── pin2000_50069_0210_im_flsh0.rom
        ├── pin2000_50069_0210_game.rom
        ├── pin2000_50069_0210_symbols.rom
        ├── pin2000_50069_0210_sf.rom       # optional, native DCS sound flash
        └── pin2000_50069_0210_pubboot.rom  # ignored by Encore
```

Use game number `50069` for SWE1 and `50070` for RFM. The outer directory must
start with:

```text
pin2000_<game-number>_<four-digit-version>_
```

The date and remaining suffix may vary. Inside it, the game-number directory
contains the update ROMs. Once this extracted directory is under `updates/`,
Encore can resolve it without copying individual files elsewhere.

> [!IMPORTANT]
> The loader reads extracted directories. It does not directly extract or load
> an update `.exe`, `.zip`, other archive, or a preassembled `update.bin`.

Encore searches both:

- `<Encore checkout>/updates`;
- `<parent of --roms DIR>/updates`.

The second location keeps a custom ROM collection and its updates together.

## The 4×4 update loader

Every load uses four required components:

| Component suffix | BAR3 placement |
|---|---|
| `*_bootdata.rom` | Offset `000000`; at most 32 KiB copied |
| `*_im_flsh0.rom` | Offset `008000` |
| `*_game.rom` | Immediately after `im_flsh0` |
| `*_symbols.rom` | Immediately after `game` |

`qemu/p2k-bar3-flash.c` finds files by suffix, so their prefix can vary. If
one of these four files is absent, the directory is ignored. If their assembled
size exceeds the 4 MiB BAR3 flash, the update is rejected.

The launcher provides four ways to select the directory:

| Selection | Behavior |
|---|---|
| no option / `--update auto` | With no saved flash, choose the highest installed version for the selected game |
| `--update latest` | Resolve and apply the highest installed version |
| `--update VERSION` | Resolve a matching installed version |
| `--update DIRECTORY` | Apply that inner game-number directory directly |

The same SWE1 2.10 version can be written as:

```sh
scripts/run-qemu.sh --game swe1 --update 0210
scripts/run-qemu.sh --game swe1 --update 210
scripts/run-qemu.sh --game swe1 --update 2.10
scripts/run-qemu.sh --game swe1 --update 2.1
```

For a directory outside the normal layout, point directly to the directory
holding the four component ROMs:

```sh
scripts/run-qemu.sh --game swe1 --update /data/my-update/50069
```

> [!TIP]
> After dropping a new extracted update into `updates/`, use `--update latest`
> or its version number. This applies it even when `savedata/<game>.flash`
> already exists.

`--update none` disables discovery and runs the base-ROM path.

## Flash and savedata precedence

At startup, BAR3 is erased to `ff`, then:

1. `savedata/<game>.flash` is loaded when present;
2. an explicitly selected update is assembled over it;
3. `auto` discovers an update only when saved flash was not loaded;
4. `--update none` disables discovery.

Use `--no-savedata` for an isolated run. A normal clean exit writes changed
BAR3 content back to `savedata/<game>.flash`.

Details: [savedata](09-savedata.md).

## Sound files associated with updates

`*_sf.rom` is not one of the four BAR3 components. Native DCS engines use it as
their separate 1 MiB sound-flash image. The lookup order is:

1. `--dcs-sound-flash PATH`;
2. `*_sf.rom` in the selected update directory;
3. `<roms>/<game>_28f800.rom`;
4. `<roms>/<game>/28f800.rom`.

The `pb2kslib` engine instead reads `<roms>/<game>_sound.bin`, or the file
selected by `--pb2kslib`.

Details: [DCS sound](25-dcs-sound.md).
