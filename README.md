# Encore Pinball 2000 emulator

Encore runs the original Williams Pinball 2000 software on modern computers.
It supports **Star Wars Episode I** and **Revenge from Mars**, including their
graphics, sound, controls, updates and persistent game state.

<p align="center">
  <img src="docs/images/swe1-attract.png" alt="Star Wars Episode I running in Encore" width="45%">
  &nbsp;
  <img src="docs/images/rfm-attract.png" alt="Revenge from Mars running in Encore" width="45%">
</p>

## Why Encore?

Pinball 2000 depends on an unusual 1999 computer and pinball-hardware
combination. Those original computers are aging, difficult to replace and tied
to the rest of the cabinet.

Encore preserves the original games by providing the machine they expect
inside QEMU. It is not a remake or a simulation of the rules: the Williams game
software itself is running.

> [!IMPORTANT]
> Both released games boot to attract mode with graphics, DCS audio, desktop
> controls and savedata.

> [!WARNING]
> Real-cabinet support is implemented but still awaits physical validation.
> Desktop operation does not prove that a powered playfield is safe.

## Read next

- **[Cabinet installation](docs/01-cabinet-installation.md)**
- **[Quickstart](docs/02-quickstart.md)**
- **[Desktop controls](docs/41-cli-keyboard-guide.md)**
- **[Command-line reference](docs/03-cli-reference.md)**
- **[Troubleshooting](docs/04-troubleshooting.md)**
- **[Known limitations](docs/35-known-limitations.md)**
- **[Cabinet testing](docs/46-real-lpt-passthrough.md)**
- **[Release process](docs/47-release-process.md)**
- **[Documentation index](docs/README.md)**

Game ROMs and community update payloads are not supplied by Encore. Users must
provide material they are legally entitled to use.
