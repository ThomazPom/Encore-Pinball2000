# 15 — ROM and update loading

Encore loads physical chip images, update flash, saved state and audio assets
through separate paths. This page describes the current rules.

## Game ROM chips

The loader accepts either layout:

```text
roms/swe1_u100.rom
roms/swe1/u100.rom
```

The `.bin` suffix is also accepted. Change the root with `--roms` and select
the game with `--game swe1` or `--game rfm`.

| Chips | Purpose | Required |
|---|---|---|
| `u100/u101` | Game bank 0 and boot image | Yes |
| `u102/u103` | Game bank 1 | No |
| `u104/u105` | Game bank 2 | No |
| `u106/u107` | Game bank 3 | No |
| `u109/u110` | DCS sound ROM | Required by native ADSP audio |

Each chip supplies one 16-bit lane. `qemu/p2k-rom.c` reads two bytes from each
chip and interleaves the pairs into a 32-bit-wide bank. Missing optional game
banks read as `ff`.

Bank 0 is mapped into the PLX ROM window and its first 32 KiB appears at
`000c0000`. The reset hook also copies those 32 KiB to RAM at `00080000`, where
execution begins. Details: [memory map](13-memory-map.md).

## Update bundles

> [!IMPORTANT]
> An explicitly selected update is applied even when saved flash exists. Use
> `--no-savedata` when the test must begin from an uncontaminated flash image.

An update bundle is the inner game-number directory containing:

```text
*_bootdata.rom
*_im_flsh0.rom
*_game.rom
*_symbols.rom
```

The components are assembled into the 4 MiB BAR3 flash in that order:
boot data at offset `000000`, system image at `008000`, followed by game code
and symbols.

Select an update with:

```sh
scripts/run-qemu.sh --update 0200
scripts/run-qemu.sh --update latest
scripts/run-qemu.sh --update /absolute/path/to/50069
scripts/run-qemu.sh --update none
```

At startup the flash buffer is erased, then processed as follows:

1. Existing `savedata/<game>.flash` is loaded unless `--no-savedata` is used.
2. An explicitly selected update is applied over that buffer.
3. Without saved flash or an explicit selection, the newest matching bundle
   under `updates/` is applied automatically.
4. `--update none` disables automatic selection.

Therefore an explicit update selection overrides the update content loaded
from saved flash.

## Saved state

| File | Contents |
|---|---|
| `savedata/<game>.flash` | 4 MiB BAR3 update flash |
| `savedata/<game>.nvram2` | 128 KiB BAR2 SRAM |
| `savedata/<game>.see` | 128-byte PLX serial EEPROM |

Both are loaded independently. The machine saves changes on a clean exit;
`--no-savedata` skips loading and discards writes.
Details: [savedata](09-savedata.md).

## Sound assets

The audio engine determines which additional asset it needs:

| Engine | Asset |
|---|---|
| `pb2kslib` | `<roms>/<game>_sound.bin`, or `--pb2kslib PATH` |
| `adsp`, `adsp-thread` | `u109/u110` plus the matching sound-flash image |

For a native ADSP engine, the launcher resolves sound flash from the selected
update and ROM locations. Override it with `--dcs-sound-flash PATH`.

Details: [DCS sound](25-dcs-sound.md).
