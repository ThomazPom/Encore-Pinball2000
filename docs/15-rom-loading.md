# 15 — ROMs, updates and software selection

Encore starts from immutable game ROM chips and exposes a separate writable
4 MiB update flash. The launcher chooses assets and policy; the machine loads
physical-chip images, resolves or assembles an update into BAR3, and lets the
original guest decide how to use it.

```text
roms/<game>_u100 + u101 ──interleave──► base bank 0 ─► PRISM boot
updates/.../<game-id>/
  bootdata + im_flsh0 + game + symbols ─► BAR3 update flash
savedata/<game>.flash ──────────────────► persistent BAR3 seed
```

Base ROMs and update flash are different authorities. Selecting no bundle does
not automatically discard an already persisted update. See
[Persistent cabinet state](09-savedata.md) before changing a cabinet profile.

## Asset acquisition boundary

The launcher expects a ROM tree and an update tree. If either directory is
absent, `scripts/internal/fetch-assets-if-missing.sh` shallow-clones the
configured asset repository and installs the complete missing tree through a
temporary sibling. `P2K_ASSETS_REPO` can override that repository.

Existing directories are never inspected, repaired or refreshed. A partial
`roms/` or `updates/` directory therefore remains partial and can fail only
when a specific chip or bundle is selected. Recovery for that case is in
[Troubleshooting](04-troubleshooting.md).

`--roms DIR` changes the chip root. Update lookup still considers the
repository `updates/` directory and the sibling `updates/` next to that ROM
root.

## Base game ROM chips

The machine accepts flat or per-game names:

```text
<roms-dir>/<game>_u<NN><revision>.rom
<roms-dir>/<game>_u<NN><revision>.bin
<roms-dir>/<game>/u<NN><revision>.rom
<roms-dir>/<game>/u<NN><revision>.bin
```

The first matching `.rom` or `.bin` file is used. `game` is normally `swe1`
or `rfm`.

| Chips | Linear bank | Requirement | Result when absent |
|---|---:|---|---|
| `u100` + `u101` | bank 0, 16 MiB | mandatory | machine creation fails |
| `u102` + `u103` | bank 1, 16 MiB | optional | mapped as erased `0xff` |
| `u104` + `u105` | bank 2, 16 MiB | optional | mapped as erased `0xff` |
| `u106` + `u107` | bank 3, 16 MiB | optional | mapped as erased `0xff` |
| `u109` + `u110` | DCS bank, 8 MiB | optional to machine boot | original DCS ROM source unavailable |

Each game chip pair is interleaved two bytes from the first chip, then two
bytes from the second. Game chips are 8 MiB each; DCS chips are 4 MiB each.
Bank 0 supplies the 32 KiB PRISM reset copy and every immutable bank-0 mirror
described in the [boot recipe](14-boot-recipe.md).

The `rom-revision` machine property suffixes only `u100/u101`. The supported
launcher spelling is `--update r2`, which selects RFM when `--game auto` is in
use, rejects another explicit game, loads `rfm_u100r2`/`rfm_u101r2`, and
suppresses update discovery. It is the RFM 0.80 revision-2 base-ROM path, not
an update-flash version.

The ordinary unsuffixed `rfm_u100`/`rfm_u101` pair is a different base-ROM
identity. With an erased update flash it reports RFM system/game version 0.1
and `Type(PRODUCTION_BUILD, FREE_PLAY_ONLY)`. Use
`--update none --no-savedata` to reproduce that path without an older
persistent BAR3 image silently supplying an update.

## Update bundle format

SWE1 uses game number `50069`; RFM uses `50070`. A normal bundle has an outer
directory whose name includes the zero-padded version and an inner game-number
directory:

```text
updates/pin2000_50069_0210_<date>_B_10000000/50069/
  pin2000_50069_0210_bootdata.rom
  pin2000_50069_0210_im_flsh0.rom
  pin2000_50069_0210_game.rom
  pin2000_50069_0210_symbols.rom
```

BAR3 needs those four suffixes. `_pubboot.rom`, `_sf.rom` and `gamelist.txt`
belong to complete distribution/PUB and sound workflows but are not copied
into the 4 MiB game flash.

> [!IMPORTANT]
> The emulator consumes extracted bundle directories. It does not directly
> load a classic installer `.exe`, a `.zip`, another archive or a preassembled
> `update.bin`. Use the preservation tools below to extract or inspect those
> sources first.

The machine assembles BAR3 as follows:

| Offset | Component |
|---:|---|
| `0x000000` | `bootdata`, copied up to 32 KiB |
| `0x008000` | `im_flsh0` |
| next byte | `game` |
| next byte | `symbols` |
| remaining bytes | erased `0xff` |

The combined result must fit in 4 MiB. Missing/unreadable components or an
oversized result reject the assembly. When replacement of a valid saved seed
fails, the in-memory old image is restored; without a seed, BAR3 remains
erased and guest boot may not progress.

The complete supplied boot-data page identifies an installed bundle, not just
its version words at offsets `0x40/0x44`. If that page matches the saved BAR3,
the machine keeps the existing image. Otherwise it assembles the selected
bundle over a freshly erased buffer so bytes from an older update cannot leak
into the new one.

## `--update` selection

| Specification | Launcher/machine behavior |
|---|---|
| `auto` or omitted | machine chooses the highest four-digit matching version found locally |
| `latest` | launcher resolves the highest matching version before QEMU starts; failure is fatal |
| `0210`, `210`, `2.10`, `2.1` | launcher resolves that exact normalized version; failure is fatal |
| inner bundle directory | launcher passes its absolute path to the machine |
| `none` | suppress explicit staging and machine auto-discovery |
| `r2` | select revision-suffixed RFM base ROMs and suppress update discovery |

An explicit inner path ending in `/50069` or `/50070` also resolves an
otherwise automatic game to SWE1 or RFM.

Automatic machine discovery scans `./updates` first, then the `updates`
sibling of `roms-dir`. It compares only four-character version fields from
matching outer directory names and selects the lexicographically highest
zero-padded value. The chosen inner game directory is then validated during
assembly; discovery does not fall back to an older bundle if the highest one
is malformed.

> [!CAUTION]
> `--update none` means “do not discover or stage a bundle.” In an ordinary
> run it still loads `savedata/<game>.flash`. Use
> `--update none --no-savedata` for a disposable, unambiguous base-ROM run.

At normal startup the precedence is:

1. initialize BAR3 erased;
2. unless fresh/read-only policy forbids it, load the persistent
   `<game>.flash` seed;
3. compare and install an explicit bundle, or the newest auto-discovered
   bundle, when enabled;
4. expose BAR3 to the guest and persist later changes on clean exit according
   to savedata policy.

`--fresh` skips the old seed but can save the newly selected update back to the
same profile. `--no-savedata` skips the seed and discards all BAR3 changes.

## Emulated update flash

BAR3 is an Intel-compatible 28F320-style 4 MiB flash at `0x12000000`, not a
read-only concatenated file. The model supplies array, status, ID and CFI read
modes plus program and 128 KiB erase-block behavior. Programming can clear
bits from 1 to 0; attempts to set a programmed 0 back to 1 raise the program-
error status until the guest erases the block.

Both single-value programming (`0x40`/`0x10`) and Intel write-buffer
programming are supported. The buffered sequence consists of `0xE8`, an x16
word count minus one, up to 16 data words (32 bytes), then `0xD0` to commit.
Data remains staged until that confirmation. The complete transfer must stay
inside one 128 KiB erase block. An invalid count, payload exceeding its
declared length or leaving the selected block, or confirmation in another
block reports command-sequence error status. Buffered commits obey the same
1-to-0 programming rule as individual writes.

That protocol matters during XINU resource discovery. Returning ordinary
array bytes for status commands can be interpreted as flash errors even when
the assembled payload itself is correct.

BAR3 writes set a dirty flag. On clean QEMU exit, a changed 4 MiB image is
written to a sibling temporary path and atomically renamed. An unchanged image
is not rewritten. The persistence details and concurrency limits are in
[Persistent cabinet state](09-savedata.md).

> [!NOTE]
> The supported persistent image size is exactly 4 MiB. Current BAR3 and BAR2
> seed readers do not yet reject every short malformed file as strictly as the
> SEEPROM reader; treat truncated savedata as corrupt rather than as a
> supported partial image.

## Sound payload association

The live ADSP engines can use the selected bundle's `_sf.rom`, but it is not
part of BAR3. The file must be exactly 1 MiB. Lookup order is:

1. `--dcs-sound-flash PATH`;
2. `_sf.rom` in the resolved update directory;
3. `<roms-dir>/<game>_28f800.rom`;
4. `<roms-dir>/<game>/28f800.rom`.

The plain extracted-sample engine instead uses the validated path from
`--pb2kslib`, then `<roms-dir>/<game>_sound.bin`; it performs no directory
walk. These files are independent of BAR3.

Automatic discovery retains the resolved bundle path so the DCS device sees
the matching sound payload. Sound-engine details belong in
[DCS sound](25-dcs-sound.md).

## PUB card is a separate interface

`--pub-card DIR` does not install that bundle into BAR3. It adds the
experimental read-only Prism Update Board windows used by XINA's `pub ...
dump` command. The model exposes a 4 MiB assembled game bank and, when present,
the same 1 MiB `_sf.rom` through both original sound addressing views.

The PUB aperture and optional SMC8416 shared-memory window both decode guest
address `0x000d0000`. The launcher therefore rejects every combination of
`--pub-card` with a network mode instead of silently letting one board shadow
the other.

This separation allows preservation work to inspect a programmed card without
changing normal update selection or persistent flash.

## Preservation tools

| Tool | Purpose |
|---|---|
| `tools/extract-pub-update.py` | extract and validate a complete ROM set from a classic InstallShield PUB `.exe` |
| `tools/dump-pub-card.py` | capture a physical or emulated PUB through XINA serial commands, with checkpoints |
| `tools/build_update_bin.py` | create a contiguous diagnostic update image from a bundle |
| `tools/extract_rom_strings.py` | reconstruct a linear chip-pair bank and inspect strings |
| `tools/deinterleave_rebuild.sh` | legacy, SWE1-only working-directory helper that reconstructs four interleaved banks |
| `tools/analyze_rom_files.py` | legacy offline signature/entropy survey for a caller-supplied ROM directory |

The installer extractor downloads one pinned, checksum-verified revision of
`idecomp` into the user cache, requires all six distribution components and
refuses to replace an output directory without `--force`.

The PUB dumper accepts a TCP XINA console or a physical serial device. It
derives component sizes from the card header, refuses implausible metadata,
can resume checkpointed raw banks, and optionally verifies that the `sound1`
and `sound8` views are identical before creating `_sf.rom`.

`build_update_bin.py` is an analysis/export helper; the emulator assembles the
same four game components directly and does not require `update.bin`.

The last two utilities are research leftovers, not runtime dependencies.
`deinterleave_rebuild.sh` uses fixed SWE1 filenames and writes `bank0.bin`
through `bank3.bin` plus `swe1_rebuilt.bin` in the current directory; run it
only in a disposable copy. `analyze_rom_files.py` has a historical default
path that does not match this repository layout, so always pass the intended
ROM directory explicitly. Its entropy and magic-byte labels are triage hints,
not format proof.

## Reproducible checks

When changing ROM or update code, record:

1. exact game, base-chip revision, update selector and savedata mode;
2. resolved bundle directory and component sizes/hashes;
3. whether BAR3 was erased, seeded, retained or replaced;
4. whether the run reached XINA and normal game code;
5. whether exit wrote a new 4 MiB flash image;
6. sound payload source when testing a live ADSP engine.

Do not infer the active update from the command line alone: a saved flash,
explicit bundle and automatic discovery can all affect the final image. The
machine log is the authoritative record for that run.

---

← [Boot recipe](14-boot-recipe.md) ·
[Documentation index](README.md) · [Memory map](13-memory-map.md)
