# Encore — Pinball 2000 on QEMU

Encore runs the original Williams Pinball 2000 software in a custom QEMU i386
machine. It supports:

- **Star Wars Episode I** (`swe1`)
- **Revenge from Mars** (`rfm`)

Both games boot to attract mode with graphics, DCS audio, keyboard controls and
persistent machine state. Linux `ppdev` passthrough for a physical driver board
is implemented, but physical-cabinet validation is still pending.

<p align="center">
  <img src="docs/images/swe1-attract.png" alt="Star Wars Episode I running in Encore" width="45%">
  &nbsp;
  <img src="docs/images/rfm-attract.png" alt="Revenge from Mars running in Encore" width="45%">
</p>

## Quick start

Encore does not distribute game ROMs. Put your legally obtained chip dumps in
`roms/`, then build and run:

```sh
./scripts/build-qemu.sh
./scripts/run-qemu.sh --game swe1
```

The launcher selects the newest installed update by default. To select another
configuration:

```sh
./scripts/run-qemu.sh --game rfm --update latest
./scripts/run-qemu.sh --game swe1 --update 2.00 --dcs-engine adsp
./scripts/run-qemu.sh --game swe1 --update none
```

See the [quickstart](docs/02-quickstart.md) for dependencies and ROM naming, and
run `./scripts/run-qemu.sh --help` for every option.

## Controls

| Key | Action |
|---|---|
| `Space` / `S` | Start |
| `F10` / `C` | Insert credit |
| `F4` | Open or close coin door |
| `Down` / keypad `-` | Volume down |
| `Up` / keypad `+` | Volume up |
| `Right` | Begin service test |
| `F5` / `Enter` | Service enter |
| `F7` / `F8` | Left/right flipper |
| `F6` / `F9` | Left/right action button |
| `F2` | Flip display vertically |
| `F3` | Screenshot |
| `F12` | Dump emulated board state |
| `F1` | Quit |

These controls update the emulated cabinet switch matrix. They are not sent to
the guest as PC keyboard input.

## Useful commands

```sh
# Interactive XINA serial console
./scripts/run-qemu.sh --serial

# Measure steady-state guest speed, IRQ0 delivery, jitter and LPT cadence
./scripts/run-qemu.sh --bench

# Select an audio engine
./scripts/run-qemu.sh --dcs-engine pb2kslib
./scripts/run-qemu.sh --dcs-engine adsp
./scripts/run-qemu.sh --dcs-engine adsp-thread

# Deliberately change game-clock speed
./scripts/run-qemu.sh --speed-target 75
./scripts/run-qemu.sh --speed-target 120
```

The default timing mode is adaptive HOTLOOP at 100% guest speed. `--strict`
uses the natural i8254 path for diagnostics. `--with-pit` combines HOTLOOP and
the natural PIT and is retained for controlled testing.

## Savedata

Normal runs persist guest-visible hardware state under `savedata/`:

- `<game>.nvram2` — PRISM battery SRAM
- `<game>.flash` — update flash
- `<game>.see` — PLX serial EEPROM

Use `--no-savedata` for an isolated run that neither loads nor writes these
files. See [savedata documentation](docs/09-savedata.md).

## Updates and sound

Encore reads extracted update directories containing:

```text
*_bootdata.rom
*_im_flsh0.rom
*_game.rom
*_symbols.rom
```

The repository contains the preserved original Williams update set. Community
builds are not distributed; install them locally under `updates/` or pass their
extracted directory to `--update`. Sound ROMs and sound flash remain separate
from the BAR3 update image; see [ROM loading](docs/15-rom-loading.md) and
[DCS sound](docs/25-dcs-sound.md).

## Real cabinet support

The emulated LPT board is the default. A real Linux parallel port can be opened
with:

```sh
./scripts/run-qemu.sh \
  --lpt-device /dev/parport0 \
  --cabinet-purist \
  --lpt-trace /tmp/encore-lpt.log
```

`--cabinet-purist` refuses to run without a real port and disables desktop key
injection. Do not power a playfield until you have read the
[cabinet procedure](docs/46-real-lpt-passthrough.md). Physical validation is
the remaining gate; the software path existing does not certify electrical or
coil behavior.

## Repository layout

| Path | Purpose |
|---|---|
| `qemu/` | Pinball 2000 QEMU machine and device implementation |
| `scripts/` | Build, launch and benchmark wrappers |
| `docs/` | Current user and implementation documentation |
| `tools/` | ROM, update, symbol and diagnostic utilities |
| `roms/` | Local user-supplied chip ROMs; binary payloads are ignored by Git |
| `updates/` | Preserved Williams updates and locally installed additional bundles |
| `savedata/` | Runtime device state |

## Documentation

- [Quickstart](docs/02-quickstart.md)
- [Command-line reference](docs/03-cli-reference.md)
- [Troubleshooting](docs/04-troubleshooting.md)
- [Architecture](docs/10-architecture.md)
- [Validation matrix](docs/26-testing-validation-matrix.md)
- [Known limitations](docs/35-known-limitations.md)
- [Roadmap](docs/36-roadmap.md)
- [Documentation index](docs/README.md)

## License status

This checkout does not currently contain a project-level license file. QEMU and
third-party reference material retain their own licenses. Williams game ROMs,
update payloads and sound data remain copyrighted material; possessing an
Encore checkout does not grant rights to redistribute them.
