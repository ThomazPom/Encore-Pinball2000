# 15 — ROM Loading

What this doc covers: how the QEMU machine loads Pinball 2000 chip ROMs, update flash, savedata, and DCS sample assets. The code is split between `qemu/p2k-rom.c`, which fills ROM-bank buffers, and `qemu/p2k-bar3-flash.c` / `qemu/p2k-dcs-audio.c`, which seed update flash and optional sample playback.

> [!WARNING]

## Machine-level load order

> [!IMPORTANT]
> Bank0 chips (`u100`/`u101`) are required. Missing bank0 aborts machine init. Extra banks and DCS ROM are best-effort.

`pinball2000_init()` loads ROM content before it maps ROM windows or registers the reset hook (`qemu/pinball2000.c:86-98`):

| # | Call | Required? | Result |
|---:|---|---|---|
| 1 | `p2k_load_bank0(s)` | Yes | Allocates 16 MiB bank0 and loads chips `u100/u101`. |
| 2 | `p2k_load_extra_banks(s)` | No | Best-effort banks 1-3 from `u102..u107`; absent banks map as `0xFF` later. |
| 3 | `p2k_load_dcs_rom(s)` | No | Best-effort 8 MiB DCS sound ROM from `u109/u110`. |
| 4 | `p2k_map_rom_windows(s)` | Requires bank0 | Creates option-ROM, PLX, BAR5, DCS ROM, and BIOS windows. |
| 5 | `p2k_install_bar3_flash(s)` | Later device init | Seeds/assembles BAR3 update flash. |
| 6 | `p2k_install_dcs_audio(s)` | Optional | Mounts pb2kslib sample container if audio is enabled. |

## Host file naming

> [!TIP]
> The chip loader tries two suffixes for each chip number: `.rom` then `.bin`. Place files at `<roms-dir>/<game>_u100.rom` or `.bin` for bank0.

The chip loader tries two suffixes for each chip number (`qemu/p2k-rom.c:20-28`):

```text
<roms-dir>/<game>_u100.rom
<roms-dir>/<game>_u100.bin
<roms-dir>/<game>/u100.rom
<roms-dir>/<game>/u100.bin
<roms-dir>/<game>_u101.rom
...
```

Examples for default machine properties:

```text
roms/swe1_u100.rom
roms/swe1_u101.rom
roms/swe1_u102.bin
roms/rfm_u100.rom
roms/rfm_u109.bin
```

The `game` string is a machine property and is mandatory (`qemu/pinball2000.c:55-59`, `qemu/pinball2000.c:191-193`). If `roms-dir` is unset, it defaults to `roms` (`qemu/pinball2000.c:60-62`, `qemu/pinball2000.c:194-197`).

## De-interleave algorithm

Each logical bank is 16 MiB (`P2K_BANK_SIZE`) and each physical chip contributes one 16-bit lane (`qemu/pinball2000.h:128-130`). The loader reads two bytes at a time from a chip and writes them at `bank + which_chip * 2`, then advances by four bytes (`qemu/p2k-rom.c:33-42`).

```c
ptr = bank + which_chip * 2;
while (fread(pair, 1, 2, fp) == 2) {
    ptr[0] = pair[0];
    ptr[1] = pair[1];
    ptr += 4;
}
```

| Logical bank | Chips | Function | Required? | Source |
|---|---|---|---|---|
| bank0 | `u100/u101` | `p2k_load_bank0()` | Yes; missing chip aborts machine init. | `qemu/p2k-rom.c:52-69` |
| bank1 | `u102/u103` | `p2k_try_load_bank()` via `p2k_load_extra_banks()` | No; absent bank pointer becomes `NULL`. | `qemu/p2k-rom.c:72-94` |
| bank2 | `u104/u105` | same | No | `qemu/p2k-rom.c:89-94` |
| bank3 | `u106/u107` | same | No | `qemu/p2k-rom.c:89-94` |
| DCS ROM | `u109/u110` | `p2k_load_dcs_rom()` into 8 MiB buffer | No; absent means silent/no DCS ROM window. | `qemu/p2k-rom.c:96-108` |

> ℹ️ The prompt's shorthand “u100..u113” comes from broader hardware notes, but HEAD only loads `u100..u107` for four game banks and `u109/u110` for DCS (`qemu/p2k-rom.c:64-108`).

## Mapping after load

ROM buffers are not directly executable until `p2k_map_rom_windows()` creates QEMU ROM MemoryRegions (`qemu/p2k-plx9054.c:112-177`). Important outcomes:

| Buffer | Mapped where | Source |
|---|---|---|
| First 32 KiB of bank0 | `0x000C0000` option-ROM overlay. | `qemu/p2k-plx9054.c:121-125` |
| bank0 | `0x08000000`, `0x14000000`, `0xFF000000` alias. | `qemu/p2k-plx9054.c:131-134`, `qemu/p2k-plx9054.c:159-176` |
| banks 1-3 | `0x08800000`, `0x09800000`, `0x0A800000`, plus BAR5 mirrors. | `qemu/p2k-plx9054.c:136-171` |
| DCS ROM | `0x0B800000` and `0x18000000`, if present. | `qemu/p2k-plx9054.c:149-156` |
| option ROM copy for execution | Reset hook copies bank0 bytes to RAM `0x80000`. | `qemu/p2k-boot.c:81-83` |

## BAR3 update flash

BAR3 is a 4 MiB Intel-style flash device at `0x12000000` (`qemu/p2k-bar3-flash.c:1-14`, `qemu/p2k-bar3-flash.c:26-28`). It starts erased to `0xFF`, then may be seeded from savedata or assembled from update bundle components (`qemu/p2k-bar3-flash.c:310-365`).

### Seed and update precedence

> [!IMPORTANT]
> Update selection order is: saved flash first, explicit `update=` property second, then auto-discovered bundle, then erased flash. Savedata always wins.

| Precedence | Source | Behavior | Source lines |
|---:|---|---|---|
| 1 | `savedata/<game>.flash` | If present, read up to 4 MiB into BAR3 and mark flash seeded. | `qemu/p2k-bar3-flash.c:315-328` |
| 2 | machine `update=` property | If non-empty, assemble that directory over the flash buffer. | `qemu/p2k-bar3-flash.c:330-333` |
| 3 | auto-discovered `updates/` bundle | If no saved flash and no explicit update, find newest matching update unless `P2K_NO_AUTO_UPDATE` is set. | `qemu/p2k-bar3-flash.c:334-354` |
| 4 | all `0xFF` | If no seed/update or auto-update disabled, leave flash erased. | `qemu/p2k-bar3-flash.c:355-359` |

### Update bundle layout

`assemble_update()` finds four files in one directory by suffix, reads them, then concatenates them into the 4 MiB flash buffer (`qemu/p2k-bar3-flash.c:171-219`):

| Flash offset | Component | Source |
|---:|---|---|
| `0x000000` | `*_bootdata.rom`, truncated to 32 KiB max | `qemu/p2k-bar3-flash.c:171-177`, `qemu/p2k-bar3-flash.c:200-203` |
| `0x008000` | `*_im_flsh0.rom` | `qemu/p2k-bar3-flash.c:203` |
| after `im_flsh0` | `*_game.rom` | `qemu/p2k-bar3-flash.c:204` |
| after game | `*_symbols.rom` | `qemu/p2k-bar3-flash.c:205` |

Auto-discovery maps game aliases to Williams game numbers (`swe1` → `50069`, `rfm` → `50070`) (`qemu/p2k-bar3-flash.c:221-230`) and scans `updates/pin2000_<game_num>_<version>_<date>_B_10000000/<game_num>/`, choosing the highest zero-padded version string (`qemu/p2k-bar3-flash.c:232-288`). It first tries `./updates`, then `<roms_dir>/../updates` (`qemu/p2k-bar3-flash.c:290-308`).

See [21-flash-bar3.md](21-flash-bar3.md) for flash command protocol details.

## BAR2 SRAM savedata

> [!WARNING]
> If `savedata/<game>.nvram2` is absent, the code warns that XINU resource lookups will likely fail. The SRAM is required for normal game operation.

BAR2 SRAM is seeded independently from `savedata/<game>.nvram2` when `p2k_install_plx_bars()` creates the SRAM MemoryRegion (`qemu/p2k-bars.c:59-75`, `qemu/p2k-bars.c:77-100`). If the file is absent, the code warns that XINU resource lookups will likely fail (`qemu/p2k-bars.c:62-68`).

| Savedata file | Target | Size used by code | Source |
|---|---|---:|---|
| `savedata/<game>.nvram2` | BAR2 SRAM at `0x11000000` | 128 KiB | `qemu/p2k-bars.c:35-37`, `qemu/p2k-bars.c:59-75` |
| `savedata/<game>.flash` | BAR3 update flash at `0x12000000` | 4 MiB | `qemu/p2k-bar3-flash.c:310-328` |

See [22-sram-bar2.md](22-sram-bar2.md) for BAR2 semantics.

## DCS sample assets

There are two DCS asset paths:

1. `p2k_load_dcs_rom()` loads raw DCS ROM chips `u109/u110` into an 8 MiB buffer used for DCS ROM windows (`qemu/p2k-rom.c:96-108`, `qemu/p2k-plx9054.c:149-156`).
2. `p2k-dcs-audio.c` optionally mounts a decoded sample container (`pb2kslib`) for host audio playback through QEMU audio (`qemu/p2k-dcs-audio.c:1-33`).

The pb2kslib container path resolution is intentionally simple: `$P2K_PB2KSLIB` if set, otherwise `<roms_dir>/<game>_sound.bin` (`qemu/p2k-dcs-audio.c:22-25`). Audio itself is off unless `P2K_DCS_AUDIO=1` is set, and `P2K_NO_DCS_AUDIO=1` forces it off (`qemu/p2k-dcs-audio.c:27-32`, `qemu/p2k-dcs-audio.c:824-834`). At install, it registers a QEMU audio card/voice, loads the container if found, hooks the shared DCS core callbacks, optionally preloads samples, and logs the mixer status (`qemu/p2k-dcs-audio.c:836-927`).

## See also

* [10-architecture.md](10-architecture.md) — where ROM loading fits in machine init.
* [13-memory-map.md](13-memory-map.md) — where loaded data appears in guest physical memory.
* [14-boot-recipe.md](14-boot-recipe.md) — how bank0 becomes executable at reset.
* [21-flash-bar3.md](21-flash-bar3.md) — BAR3 flash protocol.
* [22-sram-bar2.md](22-sram-bar2.md) — BAR2 savedata/resource behavior.
* [25-dcs-sound.md](25-dcs-sound.md) — DCS command and audio playback path.
