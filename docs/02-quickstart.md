# 02 — Quickstart

## Run Encore now

From a Linux desktop, copy and paste:

```bash
git clone https://github.com/ThomazPom/Encore-Pinball2000.git
cd Encore-Pinball2000
./scripts/run-qemu.sh
```

That last command is the normal launcher—no setup command has to be run first.
On a fresh checkout it prepares what is missing, then opens the emulator.

The defaults deliberately do the useful thing:

- `auto` identifies a connected Pinball 2000 playfield, or selects SWE1 with
  the emulated driver board;
- the newest matching installed update is selected automatically;
- the direct SDL window and a usable audio backend are selected automatically;
- game state is kept under `savedata/`.

> [!NOTE]
> The first launch is longer. It may ask permission to install runtime
> packages, then offer to **build the matching custom QEMU** or **download the
> latest verified binary**. Choose download for the fastest start. Missing ROM
> and update trees are fetched automatically. Later launches go straight to
> the game.

> [!IMPORTANT]
> Encore uses its own `pinball2000` QEMU machine. Stock distribution QEMU
> cannot replace it; let the launcher build or download the custom executable.

## Play

With the emulated board:

| Key | Action |
|---|---|
| `C` or `F10` | coin |
| `S` or `Space` | Start |
| `F7`, `F8` | left/right flipper |
| `F4` | open/close coin door |
| `Up`, `Down` | volume |
| `F2` | flip display vertically |
| `F3` | screenshot |
| `F1` | quit cleanly |

The complete mapping is in [Desktop controls](41-cli-keyboard-guide.md).

## Already using a published archive?

The Linux x86-64 release already contains the custom QEMU binary:

```bash
tar -xzf encore-pinball2000-linux-x86_64.tar.gz
cd Encore-Pinball2000
./scripts/run-qemu.sh
```

Its first launch fetches the absent ROM/update trees, then starts normally.

## Optional: prepare without launching

To perform the same dependency, QEMU and asset preparation but stop just
before the emulator starts:

```bash
./scripts/run-qemu.sh --preflight
```

On Debian-family systems, preflight offers to install missing packages through
the available privilege helper. It can also acquire custom QEMU and the absent
asset trees. QEMU itself still runs as the invoking unprivileged user.

Preflight is useful for provisioning or diagnosis; it is not a mandatory first
step. It stops before update resolution, savedata loading and guest boot, so a
successful preflight is not a complete game test.

## Choose a game

The bare command uses `auto`. To choose explicitly:

```bash
# Star Wars Episode I
./scripts/run-qemu.sh --game swe1

# Revenge From Mars
./scripts/run-qemu.sh --game rfm
```

Without `--update`, Encore uses the newest matching local bundle and falls
back to base ROMs when none exists. Useful overrides are:

```bash
# Explicitly require the highest local SWE1 update
./scripts/run-qemu.sh --game swe1 --update latest

# Select a locally installed version
./scripts/run-qemu.sh --game swe1 --update 0166

# Unambiguous disposable base-ROM run
./scripts/run-qemu.sh --game swe1 --update none --no-savedata
```

An unavailable explicit version is an error; Encore never silently chooses a
different one. `--update none` alone does not erase a previously saved update,
which is why the base-ROM example also uses `--no-savedata`. Details are in
[ROMs and updates](15-rom-loading.md).

## Keep, ignore or reset state

Normal launches persist audits, settings, high scores and update flash in
`savedata/`.

```bash
# Ignore existing state and discard all changes from this run
./scripts/run-qemu.sh --game swe1 --no-savedata

# Ignore existing state once, then replace it on clean exit
./scripts/run-qemu.sh --game swe1 --fresh
```

> [!WARNING]
> `--fresh` is a reset, not a temporary session. Back up the profile first if
> it matters. Use `--no-savedata` when nothing should be read or written.

See [Persistent cabinet state](09-savedata.md) before copying or resetting a
profile.

## If the window does not open

Run from a graphical desktop or a local login VT. On a remote shell with no
display, use `--headless` only when a headless diagnostic is actually wanted.

The first useful checks are:

```bash
./scripts/run-qemu.sh --help
./scripts/run-qemu.sh --preflight
./scripts/run-qemu.sh --game swe1 -v 2>&1 | tee encore-run.log
```

Continue with [Troubleshooting](04-troubleshooting.md) if preparation succeeds
but the guest still fails. To turn a host into a boot-to-game appliance, use
[Cabinet installation](01-cabinet-installation.md).

---

← [Documentation index](README.md) · [Command-line reference](03-cli-reference.md)
· [Desktop controls](41-cli-keyboard-guide.md)
