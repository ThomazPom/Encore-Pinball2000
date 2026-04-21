# 09 — Update Loader

`--update` is the single most useful flag for quickly trying multiple
bundles without reshuffling folders. This doc explains every form it
accepts and how the resolver turns each form into a 4 MB flash image.

Entry points: `src/main.c` `resolve_update_token()`,
`apply_option("update", …)`; ROM side: `src/rom.c` `rom_load_update_flash`,
`assemble_update_from_dir`.

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## The four accepted forms

### Form 1 — a pre-built `update.bin`

Pass any file path. No name constraint.

```sh
./build/encore --game swe1 --update /some/where/update.bin
```

Encore reads the first 4 MB (or fewer) and uses it verbatim. This is
the cheapest path — zero assembly, zero concatenation.

### Form 2 — a bundle directory

A directory containing the four standard ROM files:

```
pin2000_50069_0210_…/
  bootdata.rom        or   50069_bootdata.rom
  im_flsh0.rom        or   50069_im_flsh0.rom
  game.rom            or   50069_game.rom
  symbols.rom         or   50069_symbols.rom
```

Encore walks the directory, picks the four files (matching on the
suffix after the last underscore) and concatenates them at these
offsets:

```
0x000000   bootdata.rom
0x008000   im_flsh0.rom
0x008000 + sizeof(im_flsh0)   game.rom
… + sizeof(game)              symbols.rom
```

This is identical to what `tools/build_update_bin.py` produces, and
the two paths round-trip. See [30-tools-build-update-bin.md](30-tools-build-update-bin.md).

### Form 3 — a Williams installer `.exe`

The service installer Williams shipped is a renamed ZIP. Encore
detects it by running `unzip -l` silently; if that succeeds, the file
is extracted to a temp directory and Form 2 runs on the result.

```sh
./build/encore --update ./downloads/swe1_update_210.exe
```

### Form 4 — a version token

`--update 210`, `--update 2.1`, `--update 1.80`, `--update 180`.
Encore normalises the input to a 4-digit `vvvv` and searches:

```
./updates/   ../updates/   updates/
```

for any directory or file whose name begins with
`pin2000_<gid>_<vvvv>_`, where `<gid>` is `50069` for SWE1 and
`50070` for RFM (inferred from `--game`, or from the bundle itself on
a match regardless of prefix).

Normalisation rules:

| Input | Interpretation | `vvvv` |
|---|---|---:|
| `210` | integer, treat as already vvvv | 0210 |
| `2.1` | major.minor_short (1-digit minor) | 0210 |
| `180` | integer | 0180 |
| `1.80`| major.minor_long (2-digit minor) | 0180 |
| `1.8` | major.minor_short | 0180 |

The single-digit vs two-digit minor heuristic counts trailing digit
characters after the dot.

## Auto-inference of `--game`

When `--update` resolves to a file, Encore opens it, seeks to
`0x803C` (offset within the `game.rom` region, which always starts at
flash `+ 0x08000` of the bundle… modulo the im_flsh0 size), and reads
a `u32` `game_id`:

```
50069 → SWE1
50070 → RFM
```

and overrides `g_emu.game_prefix` accordingly, with a log line. This
catches the common mistake of `--game swe1 --update rfm_2.6`
before it can boot the wrong chip ROMs against the wrong update flash.

When the bundle is a directory, we first parse the directory name
(`pin2000_50069_…`) for the `gid` and then open the assembled image
for the `0x3C` read, in that order.

## The assembly code path

```c
static int assemble_update_from_dir(const char *dir,
                                    uint8_t *flash,
                                    size_t flash_size) {
    // find bootdata / im_flsh0 / game / symbols in dir
    // memcpy each into flash[] at the known offsets
    // return total assembled size
}
```

Total size typically lands around 3.8 MB. The remaining bytes stay
at `0xFF` (erased flash). The guest's flash driver treats `0xFF`
regions as uninitialised.

## Installer-ZIP path

```c
/* assemble_update_from_dir — zip branch */
int rc = system("unzip -tq /path/to/X.exe");
if (rc == 0) {
    mkdtemp under ./savedata/
    unzip -q X.exe -d /tmp_inside_savedata/
    recursive scan for *.rom
    … then same as Form 2 …
}
```

Notice we do **not** use `/tmp`. This is deliberate: the project runs
in sandboxes where `/tmp` is rejected (see the project's top-level
security rule). Extraction happens under `./savedata/<uuid>/` and is
cleaned up on exit.

## A note on `.flash` savedata

If a `.flash` savedata file exists from a previous run, it is loaded
*after* the update assembly, so savedata wins. This mirrors the real
cabinet: the service installer writes into flash and that write
persists even across ROM updates, unless the user explicitly re-flashes.

Pass `--no-savedata` if you want the update to take effect on every
run regardless of what's in savedata.

## Error surface

* "Could not resolve `2.1`" → no directory named `pin2000_50069_0210_*`
  under any search root. Fix: download the bundle or pass a full
  path.
* "Missing bundle files" → the directory is present but one of the
  four component ROMs is missing or incorrectly named. Fix: rename
  the ROMs or run `tools/build_update_bin.py` which is stricter.
* "Assembled size > FLASH_SIZE" → the components add up to more than
  4 MB. Only seen on unusual bundles; usually a signal that one of the
  files was duplicated.

## The `latest` token

`--update latest` scans `./updates/` and selects the bundle with the
highest 4-digit version field for the active game. Combine with
`--game swe1` or `--game rfm` to pin the title; without `--game`, the
highest version across all bundled titles wins (and `game_prefix` is
inferred from the resolved bundle name).

Examples:

```sh
./build/encore --game swe1 --update latest      # → pin2000_50069_0210_*
./build/encore --game rfm  --update latest      # → pin2000_50070_0260_*
./build/encore --update latest                  # newest bundle, any game
```
