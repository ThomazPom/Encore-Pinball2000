# 10 — Savedata

Savedata in Encore is deliberately simple: four binary files, no
versioning, no JSON, no migrations. State in, state out; whatever the
game wrote to NVRAM is what we read back next run.

## The four files

Under `./savedata/`, files are named `<game_id_str>.<suffix>`:

| Suffix | Source of truth | Size | Role |
|---|---|---:|---|
| `.nvram2` | `g_emu.bar2_sram` | 128 KiB | BAR2 SRAM: audits, bookkeeping, high scores |
| `.see`    | `g_emu.seeprom`   | 128 B   | 93C46 SEEPROM: cabinet config, dip switches |
| `.ems`    | `g_emu.ems`       | 16 B    | EMS state: service-menu flags |
| `.flash`  | `g_emu.flash`     | 4 MiB   | BAR3 update flash: code image |

`game_id_str` is derived at ROM load time; typical values are `swe1_21`,
`swe1_14`, `rfm_26`, `rfm_15`. Multiple saves can coexist in the same
directory — Encore picks the one matching today's loaded update.

## Naming derivation

```
game_prefix = "swe1"
update_version = 2.1
game_id_str = "swe1_21"        // minor rounded to 1 digit
```

If the savedata directory already contains a `swe1_21.nvram2`, that ID
is reused verbatim. If only `swe1_14.nvram2` exists and the current
update is 2.1, we keep `swe1_21` (new save) and leave the 1.4 save
untouched. There is no migration — games rarely change their NVRAM
layout in a breaking way between updates, but when they do, Williams
bumped the header version inside the file and the game handles the
mismatch itself.

## Load path

`savedata_load()` is called by `rom_load_all()` after the chip ROMs
are loaded but before MMIO mapping. It:

1. `stat()`s each of the four expected files. Missing files are OK;
   they mean "use factory defaults".
2. Reads the bytes into the matching in-memory buffer.
3. For the `.flash` file only, also copies into the guest BAR3 region
   immediately (`uc_mem_write`).
4. Logs a summary:

```
[save] loaded swe1_21.nvram2 (131072 B)
[save] loaded swe1_21.see (128 B)
[save] swe1_21.ems missing — using defaults
[save] loaded swe1_21.flash (3991796 B)
```

If `--no-savedata` was passed, the entire function returns 0 before
opening anything:

```
[save] --no-savedata: skipping NVRAM/SEEPROM load
```

## Save path

`savedata_save()` is called by `cleanup_and_save()` on:

* a clean quit via F1 / SDL_QUIT;
* SIGINT (Ctrl-C) or SIGTERM (kill / system shutdown);
* `g_emu.running = false` from any explicit source.

Hard crashes (SIGSEGV, SIGKILL) skip the save — this is intentional.
We only want to persist state that came from a clean shutdown;
corrupted state from a panic should not survive into the next run.

Each file is written with `save_file()` which is a simple
fopen/fwrite/fclose. Writes are **not** atomic: we do not write to a
temp file and rename. The reasoning is that the save data is
game-level bookkeeping — if the host crashes mid-write, the game will
detect corruption and recreate the file. This matches real hardware,
where a mid-write power loss on the NVRAM has always meant "lose your
last session".

## `--no-savedata`

Pass this to:

* Run regression tests that must start from a factory state every time.
* Experiment with the service menu without committing changes to
  disk.
* Debug a save-related bug without risking trashing your good save.

When set, both load and save are skipped, so the existing savedata
files are entirely untouched.

## SEEPROM defaults

At cold boot (no `.see` on disk), `bar_seeprom_reinit()` fills
`g_emu.seeprom` with a known-good default set: coin-door DIPs cleared,
audits zeroed, volume at mid. The defaults are hard-coded in
`bar.c` and match what a new-from-factory cabinet would read on its
first power-up.

If you corrupt your `.see` file (wrong size, garbage data), the SEEPROM
handler inside the game will typically rewrite it with factory defaults
on the next reboot. To force this without deleting the file, change
the first four bytes to `0xFF 0xFF 0xFF 0xFF` — the game's
checksum check will fail and it will reinitialise.

## EMS (Emitter Management State)

16 bytes used by the service menu to store a handful of latched flags
(whether the game has ever booted, a first-boot-date timestamp, etc.).
If missing, the game initialises them on first boot; there is no
failure path.

## .flash — special care

The `.flash` file is a full 4 MiB update image. If you install a new
update with `--update 2.5` and the old `.flash` is from 2.1, Encore
loads the **new** update first and then overlays the **old** `.flash`
on top, silently downgrading you.

Two ways to avoid this:

1. `--no-savedata` — then the fresh update wins.
2. `rm ./savedata/<game_id>.flash` — the load becomes a no-op and the
   update survives.

A future release may auto-detect mismatching flash versions and refuse
to load the stale save; today the behaviour is deliberately dumb.

## Where the files live

`./savedata/` by default, overridable with `--savedata /path`. The
directory is created on first save if absent. Permissions: mode `0600`
on the files; they contain per-game bookkeeping that has no reason to
be world-readable.
