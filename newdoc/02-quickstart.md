# 02 — Quickstart

This guide starts Encore on a Linux desktop with emulated cabinet controls. It
uses an explicit game, an emulated LPT board and disposable savedata so the
first run cannot select real parallel-port hardware or change an existing game
state. For a machine that should boot directly into the game, use the
[Cabinet installation](01-cabinet-installation.md) workflow instead.

> [!IMPORTANT]
> Encore requires its custom `pinball2000` QEMU machine. A distribution's
> stock `qemu-system-i386` cannot run the game.

## Choose a starting point

### Published Linux release

The published x86-64 archive already contains the custom QEMU executable.
Extract it and enter its directory:

```sh
tar -xzf encore-pinball2000-linux-x86_64.tar.gz
cd Encore-Pinball2000
```

The release deliberately omits ROM, update and savedata directories. The
launcher downloads the complete offline ROM and update trees when those
directories are absent. See [ROM and update loading](15-rom-loading.md) for the
accepted layouts and selection rules.

### Source checkout

From a source checkout, the launcher looks for the custom QEMU executable in
the repository and in Encore's build and release caches. When none exists, an
interactive run offers two choices:

1. build the pinned QEMU version locally, matching the checkout;
2. download the latest verified release binary, which may lag behind the
   checkout.

The local build is also available directly:

```sh
scripts/build-qemu.sh
```

It checks its prerequisites before downloading or building anything. On a
Debian-family system, a failed prerequisite check prints the exact package
installation command.

## Prepare the first run

Run the complete preparation path without starting QEMU:

```sh
scripts/run-qemu.sh \
  --preflight \
  --game swe1 \
  --lpt-device emulated \
  --no-savedata
```

The preflight can offer to install missing runtime packages, acquire the
custom QEMU executable and fetch absent ROM/update trees. It stops immediately
before launching a compositor or QEMU.

> [!NOTE]
> Package installation or host-device preparation may request administrator
> authorization. QEMU itself normally runs as the invoking, unprivileged user.

## Start the game safely

Use the same explicit desktop profile without `--preflight`:

```sh
scripts/run-qemu.sh \
  --game swe1 \
  --lpt-device emulated \
  --no-savedata
```

`--no-savedata` runs from a fresh temporary directory and disables persistent
flash, NVRAM and serial-EEPROM writes. Removing the option enables the normal
`savedata/` directory.

The desktop defaults select the direct SDL framebuffer renderer when a
graphical session is available and choose a supported host audio backend. If
the launcher finds neither a graphical session nor a local login VT, it stops
with an instruction to use `--headless`.

> [!TIP]
> With the emulated board, press `C` for coin, `S` for Start, `F7` and `F8`
> for the flippers, and `F1` to quit. The complete mapping is in
> [Desktop controls](41-cli-keyboard-guide.md).

## Keep or reset game state

For an ordinary persistent SWE1 session:

```sh
scripts/run-qemu.sh --game swe1 --lpt-device emulated
```

To ignore existing savedata once and replace it with a newly initialized state
when the emulator exits normally:

```sh
scripts/run-qemu.sh --game swe1 --lpt-device emulated --fresh
```

> [!WARNING]
> `--fresh` is intentionally persistent: it ignores the old state for that
> boot, then saves the new state. Use `--no-savedata` for disposable testing.

See [Savedata](09-savedata.md) before moving, sharing or resetting cabinet
state.

## Select a game or update

Use an explicit game for predictable desktop runs:

```sh
# Star Wars Episode I
scripts/run-qemu.sh --game swe1 --lpt-device emulated

# Revenge From Mars
scripts/run-qemu.sh --game rfm --lpt-device emulated
```

Without `--game`, the default `auto` mode identifies a recognized physical
driver board or falls back to SWE1 with the emulated board. Without `--update`,
the machine discovers the newest matching update bundle available under
`updates/` and falls back to the base ROMs when none exists.

Useful explicit update selections are:

```sh
# Highest locally available update for the selected game
scripts/run-qemu.sh --game swe1 --update latest --lpt-device emulated

# Disposable base-ROM run, ignoring any installed saved update
scripts/run-qemu.sh --game swe1 --update none --no-savedata \
  --lpt-device emulated

# A locally available short version code
scripts/run-qemu.sh --game swe1 --update 0166 --lpt-device emulated
```

An unavailable explicit version is an error; it never silently selects a
different update.

`--update none` suppresses bundle staging and discovery but does not erase an
existing `<game>.flash` seed. Pair it with `--no-savedata`, as above, when the
test must start from base ROMs without modifying persistent state.

## Get help or diagnose preparation

The launcher is the authoritative inventory of current options:

```sh
scripts/run-qemu.sh --help
scripts/build-qemu.sh --help
```

Repeat the preflight with the exact host backend and device options of a
failing launch to validate its host-preparation path without starting the
emulator. If preparation succeeds but the guest still fails, rerun the same
launch with verbose diagnostics and capture its terminal output:

```sh
scripts/run-qemu.sh \
  --game swe1 \
  --lpt-device emulated \
  --no-savedata \
  -v 2>&1 | tee encore-run.log
```

For further diagnosis, continue with [Troubleshooting](04-troubleshooting.md).

---

← [Documentation index](README.md) · [Command-line reference](03-cli-reference.md)
· [Desktop controls](41-cli-keyboard-guide.md)
