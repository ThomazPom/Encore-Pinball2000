# Encore — a Pinball 2000 QEMU Machine

Williams Pinball 2000 firmware running inside a purpose-built QEMU machine.

<p align="center">
  <img src="docs/images/swe1-attract.png" alt="Encore running Star Wars Episode I attract mode" width="45%">
  &nbsp;
  <img src="docs/images/rfm-attract.png" alt="Encore running Revenge from Mars attract mode" width="45%">
</p>

<p align="center">
  <a href="docs/02-quickstart.md"><strong>Quickstart</strong></a>
  &nbsp;|&nbsp;
  <a href="docs/README.md"><strong>Documentation</strong></a>
  &nbsp;|&nbsp;
  <a href="docs/04-troubleshooting.md">Troubleshooting</a>
  &nbsp;|&nbsp;
  <a href="docs/35-known-limitations.md">Known limitations</a>
  &nbsp;|&nbsp;
  <a href="docs/36-roadmap.md">Roadmap</a>
</p>

---

## Status

> [!IMPORTANT]
> **Encore boots both Star Wars Episode I and Revenge from Mars to attract mode
> with graphics, DCS audio, cabinet controls and persistent machine state.**
> Genuine Williams updates can be selected at launch, and the supported update
> and DCS-engine combinations are covered by a reproducible validation matrix.

> [!WARNING]
> **Physical-cabinet validation is still pending.** Linux parallel-port
> passthrough is implemented, but a working software path is not proof that
> switch polarity, output timing and electrical behavior are safe on a powered
> playfield. Read the [cabinet procedure](docs/46-real-lpt-passthrough.md)
> before connecting hardware.

> [!NOTE]
> Game ROMs and community update payloads are not supplied by Encore. The
> repository contains the preserved original Williams update set; users provide
> their own legally obtained chip ROMs and any additional update bundles.

---

## What is Encore?

Pinball 2000 replaced the usual dot-matrix display with a projected computer
image reflected into the playfield. Behind that image is a peculiar 1999 PC and
pinball hybrid: a Cyrix MediaGX processor, Williams PRISM/PLX hardware, DCS-2
sound, and a parallel-port connection to the cabinet driver board.

Encore models that machine inside QEMU and runs the original Williams i386
software. It is not a recreation of the game rules, a video capture, or a
modern rewrite. XINU, XINA and the game execute as guest code while Encore
provides the hardware surfaces they expect:

- protected-mode PRISM boot and ROM mapping;
- MediaGX instructions, framebuffer, blitter and VSync;
- PLX configuration, SRAM, update flash and serial EEPROM;
- DCS command transport with sample or native ADSP audio engines;
- emulated cabinet switches and Linux `ppdev` passthrough;
- serial console, savedata, diagnostics and repeatable benchmarks.

The result is the original software running on modern Linux with a normal QEMU
display and audio backend, while retaining a path toward a real cabinet.

## Start here

```sh
./scripts/build-qemu.sh
./scripts/run-qemu.sh --game swe1
```

That is deliberately only the shortest launch example. ROM naming,
dependencies, update selection, sound assets, controls and host setup belong in
the dedicated guides:

| Document | Purpose |
|---|---|
| [Quickstart](docs/02-quickstart.md) | Build dependencies, ROM placement and first boot. |
| [Command-line reference](docs/03-cli-reference.md) | Every supported launcher option. |
| [Troubleshooting](docs/04-troubleshooting.md) | Diagnose build, boot, display, audio and serial failures. |
| [Architecture](docs/10-architecture.md) | How the QEMU machine and devices fit together. |
| [Validation matrix](docs/26-testing-validation-matrix.md) | Reproduce compatibility results across games, updates and audio engines. |
| [Known limitations](docs/35-known-limitations.md) | Current, reproducible gaps. |
| [Roadmap](docs/36-roadmap.md) | The remaining path to physical-cabinet proof. |

> [!TIP]
> Run `./scripts/run-qemu.sh --bench` to measure guest-clock speed, IRQ0
> delivery, jitter and LPT cadence under the same graphical conditions as a
> normal session.

## What is in the repository?

| Path | Purpose |
|---|---|
| `qemu/` | Pinball 2000 machine and device implementation compiled into QEMU. |
| `scripts/` | Build, launch and benchmark entry points. |
| `docs/` | User guides, current architecture and validation evidence. |
| `tools/` | ROM, update, symbol and diagnostic utilities. |
| `roms/` | Local user-supplied chip ROMs; payloads are ignored by Git. |
| `updates/` | Preserved Williams updates and locally installed extra bundles. |
| `savedata/` | Persistent guest hardware state created at runtime. |

> [!IMPORTANT]
> Stock `qemu-system-i386` does not contain the `pinball2000` machine. Use
> `scripts/build-qemu.sh` and launch through `scripts/run-qemu.sh`.

## Project direction

Encore is aimed at a practical Pinball 2000 replacement computer: reliable
desktop play today and a carefully validated real-cabinet connection next.
Adaptive HOTLOOP timing, protected-mode direct boot, the fixed cabinet PCI
topology and narrowly gated firmware compatibility mechanisms are deliberate
parts of the current design—not a list of features awaiting removal.

The open work is empirical: test the physical driver board, measure the real
bus, verify every input and output class, and change code only when those tests
identify missing behavior.

## Credits and legal status

Williams and Bally created the Pinball 2000 platform and its two released
games. Encore builds on QEMU for x86 execution, host devices, display, audio and
debugging infrastructure. Community preservation work keeps machines, firmware
and hardware knowledge available decades later.

This checkout does not currently contain a project-level license file. QEMU and
third-party reference material retain their own licenses. Williams game ROMs,
update payloads and sound data remain copyrighted material; possessing an
Encore checkout does not grant rights to redistribute them.

---

← [Documentation index](docs/README.md)
