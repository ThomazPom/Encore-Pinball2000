# 09 — Persistent cabinet state

Encore keeps mutable cabinet state outside the ROM and update trees. Each game
owns four opaque device images under one savedata directory.

## Files and ownership

| File | Exact size | Emulated device | Typical guest-owned content |
|---|---:|---|---|
| `<game>.nvram2` | 196,608 bytes | 192 KiB battery-backed BAR2 SRAM | audits, high scores, adjustments and resource state |
| `<game>.flash` | 4,194,304 bytes | 4 MiB BAR3 update flash | installed update plus later guest flash writes |
| `<game>.see` | 128 bytes | PLX 93C46 SEEPROM | board configuration words |
| `<game>.rtc` | 80 bytes | MC146818 RTC plus save timestamp | clock registers and off-time year rollover accounting |

`<game>` is `swe1` or `rfm`. These are opaque device-state images, not
interchangeable configuration files. Do not edit, concatenate or truncate
them.

The default directory is `savedata/` at the repository root. Select a separate
one with:

```sh
scripts/run-qemu.sh --game swe1 --savedata /path/to/cabinet-state
```

The emulator creates the directory if needed and verifies that it can create
and remove a file there. If the requested directory is unusable, it warns and
falls back to `$XDG_DATA_HOME/encore/savedata`, or
`~/.local/share/encore/savedata` when `XDG_DATA_HOME` is unset. Startup stops
if neither location is usable.

> [!IMPORTANT]
> Read the launch output after changing `--savedata`. A permission error can
> move persistence to the fallback directory instead of the path you expected.

## Choose the state policy

| Mode | Reads existing files | Writes on exit | Intended use |
|---|---|---|---|
| ordinary launch | yes | yes | normal cabinet or desktop play |
| `--fresh` | no | yes, to the selected directory | intentionally replace a cabinet state |
| `--no-savedata` | no | no | disposable tests and reproducible diagnostics |

An ordinary launch uses any files already present. Missing devices start from
their reset contents and are persisted only according to the write rules
below.

`--fresh` and `--no-savedata` are mutually exclusive.

> [!WARNING]
> `--fresh` is a reset, not a temporary profile. It ignores all four existing
> seeds, then replaces them with newly initialized images when Encore exits.
> Back up the directory first if its audits, scores or adjustments matter.

In this mode BAR2 starts empty, BAR3 starts erased before the selected update
is installed, SEEPROM starts from Encore's built-in board defaults, and RTC
starts from the current host date and time.

`--no-savedata` exports `P2K_NO_SAVEDATA=1`, uses an empty throwaway working
directory as a second isolation layer, discards every device change and removes
that working directory on normal launcher exit. The selected savedata files are
neither read nor written.

For a normal disposable run:

```sh
scripts/run-qemu.sh --game swe1 --no-savedata
```

## Updates also live in savedata

The active update is represented by `<game>.flash`, so changing update
selection can change persistent state:

- the default `--update auto` compares saved BAR3 with the newest matching
  local bundle and installs the bundle in memory when they differ;
- an explicit update does the same comparison with the selected bundle;
- a successfully installed bundle is saved to `<game>.flash` on exit;
- `--update none` suppresses staging and auto-discovery, but still loads an
  existing `<game>.flash` seed.

Therefore this command does **not** guarantee base ROMs when saved flash exists:

```sh
scripts/run-qemu.sh --game swe1 --update none
```

Use read-only disposable state for an unambiguous base-ROM test:

```sh
scripts/run-qemu.sh --game swe1 --update none --no-savedata
```

> [!CAUTION]
> Combining `--fresh` with `--update none` replaces the existing update flash
> as well as NVRAM, SEEPROM and RTC state. Prefer `--no-savedata` when the goal
> is only to compare base ROM behavior.

See [ROM and update loading](15-rom-loading.md) for bundle selection and flash
layout.

## Back up, restore or separate profiles

Exit Encore before copying state. A complete backup keeps the four devices
together:

```sh
cp -a savedata savedata.backup
```

Restore only while the emulator is stopped:

```sh
cp -a savedata.backup/. savedata/
```

For independent profiles, keep separate directories rather than repeatedly
copying individual files:

```sh
scripts/run-qemu.sh --game swe1 --savedata "$HOME/p2k-state/cabinet-a"
scripts/run-qemu.sh --game swe1 --savedata "$HOME/p2k-state/testing" --fresh
```

Do not run two emulator instances for the same game against the same savedata
directory. There is no cross-process state lock; each process can finish with
its own full device image, so the last successful rename wins.

## When files are written

Device images are flushed by QEMU's exit notifiers:

- BAR2 NVRAM is written as a complete 192 KiB image;
- BAR3 flash is skipped when it was neither changed nor replaced;
- SEEPROM is skipped unless a guest write changed it, except that `--fresh`
  deliberately persists its built-in defaults.
- RTC is written as 64 register bytes plus the host save time. At the next
  launch Encore refreshes wall-clock fields and adds only calendar-year
  boundaries crossed while powered off to XINA's RTC year counter.

Every save is written to a sibling `.tmp` file and renamed over the destination
only after the expected byte count was written. This protects the previous
image from a partial final write, but it cannot preserve guest changes that
were never flushed.

Use `F1` or the window close action for a normal shutdown. After a host crash,
power loss or forced kill, inspect the savedata directory before assuming the
last session was recorded. A leftover `.tmp` file is not an automatically
selected seed.

Run with `-v` to see which images were seeded, ignored, installed, saved or
skipped:

```sh
scripts/run-qemu.sh --game swe1 -v
```

The cabinet uninstaller deliberately leaves project files, ROMs and savedata
untouched.

## Environment equivalents

Advanced integrations may set `P2K_NO_SAVEDATA=1` or
`P2K_FRESH_SAVEDATA=1`. The launcher treats non-empty values other than `0` as
enabled and applies the same mutual-exclusion check. Prefer the command-line
options in manual use because their intent is visible in shell history.

---

← [Desktop controls](41-cli-keyboard-guide.md) ·
[Documentation index](README.md) · [Troubleshooting](04-troubleshooting.md)
