# Encore - a Pinball 2000 Emulator

Encore is a clean-room x86 emulator for the Williams/Bally **Pinball
2000** platform, used by **Star Wars Episode I** and **Revenge from
Mars**.

It runs the original Pinball 2000 game software in an i386 virtual
machine, renders the game display through SDL2, drives the DCS audio
path, and includes an experimental real-cabinet LPT passthrough path
for the Power Driver Board.

<p align="center">
  <img src="docs/images/swe1-attract.png" alt="Encore running Star Wars Episode I attract mode" width="45%">
  &nbsp;
  <img src="docs/images/rfm-attract.png" alt="Encore running Revenge from Mars attract mode" width="45%">
</p>

<p align="center">
  <a href="docs/README.md"><strong>Documentation Index</strong></a>
  &nbsp;|&nbsp;
  <a href="docs/02-quickstart.md"><strong>Quickstart</strong></a>
  &nbsp;|&nbsp;
  <a href="docs/03-cli-reference.md">CLI Reference</a>
  &nbsp;|&nbsp;
  <a href="docs/42-cabinet-testing-call.md">Real Hardware Testing</a>
</p>

---

## Status

> [!WARNING]
> Real-cabinet support is still under active validation. Emulator-only
> behaviour and real driver-board behaviour are not the same thing:
> cabinet timing, LPT electrical details, switch reads, lamps, coils,
> and DCS hardware all need real-machine confirmation.

The emulator can boot and exercise Pinball 2000 software under Linux,
but anything involving a physical cabinet should be treated as
experimental until verified on hardware. Start with:

| Need | Read |
|---|---|
| Build and first run | [docs/02-quickstart.md](docs/02-quickstart.md) |
| Every command-line flag | [docs/03-cli-reference.md](docs/03-cli-reference.md) |
| Real LPT setup | [docs/19-real-lpt-passthrough.md](docs/19-real-lpt-passthrough.md) |
| Public PDB/LPT protocol notes | [docs/48-lpt-protocol-references.md](docs/48-lpt-protocol-references.md) |
| Cabinet test reports | [docs/42-cabinet-testing-call.md](docs/42-cabinet-testing-call.md) |
| Known limitations | [docs/38-known-limitations.md](docs/38-known-limitations.md) |

---

## Quick Start

> [!IMPORTANT]
> **Use the maintained quickstart: [docs/02-quickstart.md](docs/02-quickstart.md)**
>
> The root README intentionally avoids install commands, dependency
> versions, and cabinet setup recipes. Those details change quickly and
> belong in the quickstart and subsystem docs.

For day-to-day usage after the first boot, use the
[CLI reference](docs/03-cli-reference.md).

---

## What Is In The Repo

| Path | Purpose |
|---|---|
| `src/` | Emulator implementation |
| `include/` | Shared C declarations and constants |
| `tools/` | Helper scripts for ROMs, updates, symbols, and sound assets |
| `docs/` | Maintained documentation and subsystem notes |
| `roms/` | Local ROM assets used by the development checkout |
| `updates/` | Local update bundles used by the development checkout |
| `savedata/` | Runtime NVRAM/SEEPROM/EMS state, created while running |
| `build/` | Build output, created by `make` |

Redistribution rules for ROM and update assets depend on the rights
holders. If you package Encore for others, review the licensing section
below and remove assets you are not allowed to ship.

---

## Documentation Map

The documentation index is the best entry point:

**[docs/README.md](docs/README.md)**

Common next reads:

| Topic | Document |
|---|---|
| Project overview | [docs/01-overview.md](docs/01-overview.md) |
| Architecture | [docs/05-architecture.md](docs/05-architecture.md) |
| CPU emulation | [docs/06-cpu-emulation.md](docs/06-cpu-emulation.md) |
| ROM and update loading | [docs/08-rom-loading-pipeline.md](docs/08-rom-loading-pipeline.md), [docs/09-update-loader.md](docs/09-update-loader.md) |
| DCS audio | [docs/12-sound-pipeline.md](docs/12-sound-pipeline.md), [docs/13-dcs-mode-duality.md](docs/13-dcs-mode-duality.md) |
| LPT emulation and passthrough | [docs/18-lpt-emulation.md](docs/18-lpt-emulation.md), [docs/19-real-lpt-passthrough.md](docs/19-real-lpt-passthrough.md) |
| Troubleshooting | [docs/27-troubleshooting.md](docs/27-troubleshooting.md) |
| Build/runtime environment | [docs/28-build-system.md](docs/28-build-system.md), [docs/41-build-env-and-runtime.md](docs/41-build-env-and-runtime.md) |
| Community updates | [docs/47-community-updates.md](docs/47-community-updates.md) |

---

## Reports And Testing

Real hardware reports are especially useful. If you can test a cabinet,
Power Driver Board, parallel-port card, DCS path, or update bundle, use
the report template in
[docs/42-cabinet-testing-call.md](docs/42-cabinet-testing-call.md).

For normal bug reports, include:

```text
Game / update:
Command line:
Host OS:
What worked:
What failed:
Relevant log output:
```

---

## Licensing And Credits

The Encore source code in `src/`, `include/`, `tools/`, and `docs/` is
original project work intended for free release.

ROMs, update packages, game software, artwork, sound data, trademarks,
and Pinball 2000 platform assets belong to their respective rights
holders. Remove those assets from any redistribution that does not have
permission to include them.

The XINA operating system inside the Pinball 2000 software is derived
from Comer's XINU; see
[docs/44-xina-os-deep-dive.md](docs/44-xina-os-deep-dive.md) for
project notes and context.
