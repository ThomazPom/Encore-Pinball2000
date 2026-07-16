# 01 — Overview

Encore boots the real Williams Pinball 2000 firmware inside QEMU. You drop ROM chips in `roms/`, run `make`, hit a key, and we load XINA in a custom `qemu-system-i386` machine and start drawing frames. It's not a rewrite, not a rules simulator, and not stock QEMU with a BIOS file—it's a purpose-built machine that teaches QEMU just enough about the Cyrix MediaGX, PLX bridge, PRISM flash, DCS-2 audio, and LPT driver board to keep the 1999 game binary happy.

> **Status:** Behaviour described here is based on emulator testing only. Real-cabinet validation is pending. Treat cabinet-facing modes as experimental until they have been exercised on physical Williams Pinball 2000 hardware.
>
> Games in scope: **Star Wars Episode I** (`swe1`) and **Revenge from Mars** (`rfm`).

## How you use it

```sh
scripts/build-qemu.sh
scripts/run-qemu.sh --game swe1
```

The build script grabs a pinned upstream QEMU release, copies our `qemu/pinball2000.c` and `qemu/p2k-*.c` files into QEMU's `hw/i386/`, patches the build system to recognize the new machine, and compiles `i386-softmmu`. The run script wraps that binary with sensible defaults—SDL display, auto-detected audio, the right ROM paths, and a virtual switch matrix you can drive with your keyboard.

> [!IMPORTANT]
> Stock QEMU does not know the `pinball2000` machine. If `scripts/run-qemu.sh` falls back to a system `qemu-system-i386`, it still needs a binary built with Encore's machine sources.

## Why QEMU

The first Encore prototype proved we could boot Williams ROMs—it used Unicorn to run the CPU, scanned for specific byte patterns to patch in compatibility fixes, and hand-rolled a PIT/PIC/IRQ loop to keep the game ticking. That worked, but as the hardware model grew it became clear we were reimplementing half of a PC emulator while debugging the other half. The CPU stepper, interrupt dispatcher, timer cadence, display refresh, console plumbing, audio backends—every one of those is already solved in QEMU, and solved better than we'd write them in a weekend.

So we pivoted. QEMU now owns the x86 CPU, the i8259 PIC, the i8254 PIT, the ISA bus, timer delivery, TCG translation, console devices, audio outputs, and display surfaces. Encore contributes one custom machine (`pinball2000`) and a handful of device files under `qemu/p2k-*.c` that model the Pinball 2000 board surfaces the game actually touches. The project goal shifted from "write an emulator" to "teach QEMU enough about this weird 1999 arcade board that the real firmware just works."

## The big picture

```text
┌────────────────────────────────────────────────────────────────────┐
│ scripts/run-qemu.sh                                                │
│ You run this → it validates your display/audio → resolves updates  │
│ → picks savedata directory → launches qemu-system-i386              │
└───────────────────────────────┬────────────────────────────────────┘
                                │
                                ▼
┌────────────────────────────────────────────────────────────────────┐
│ custom qemu-system-i386                                            │
│ built by scripts/build-qemu.sh from upstream QEMU + our qemu/*     │
└───────────────────────────────┬────────────────────────────────────┘
                                │ -M pinball2000,game=swe1,roms-dir=...
                                ▼
┌────────────────────────────────────────────────────────────────────┐
│ qemu/pinball2000.c                                                 │
│ MachineClass registration, RAM, CPU, ISA bus, PIC, PIT,            │
│ ROM loader, reset hook, device wiring                              │
└──────────────┬───────────────┬──────────────┬───────────────┬──────┘
               │               │              │               │
               ▼               ▼              ▼               ▼
       ROM / boot        PCI / PLX / BARs  display / vsync  sound / LPT
       p2k-rom.c         p2k-pci.c         p2k-display.c    p2k-dcs*.c
       p2k-boot.c        p2k-plx*.c        p2k-vsync.c      p2k-lpt-board.c
                         p2k-bar3-flash.c  p2k-gx.c
```

The split is intentionally boring: one concern per file. See [10-architecture.md](10-architecture.md) for the guided tour and [11-machine-init.md](11-machine-init.md) for the device-by-device init sequence.

> [!TIP]
> If you're debugging device initialization, start with [11-machine-init.md](11-machine-init.md) for the exact call sequence.

## What happens when you boot

We load the chip ROMs from `roms/`, QEMU creates a 16 MiB i386 machine, and our
`pinball2000` reset hook drops the CPU into protected mode at the PRISM option
ROM entry point. A real cabinet reaches that point through CPU reset, BIOS POST,
option-ROM discovery and a real-mode transition; Encore deliberately skips
those earlier stages and reconstructs their final CPU/GDT state. From the
protected-mode entry onward, XINA and the game firmware execute normally.

> [!IMPORTANT]
> Encore loads chip ROMs from the selected `roms/` directory, asks QEMU to create a 16 MiB i386 machine, and then uses the `pinball2000` reset recipe to enter the PRISM option ROM path the Williams software expects.

The detailed boot recipe is in [14-boot-recipe.md](14-boot-recipe.md); ROM and update loading rules are in [15-rom-loading.md](15-rom-loading.md). The important bit: you're running the actual Williams firmware—`swe1` or `rfm`—with updates auto-discovered or explicitly selected.

## Who this is for

| If you are... | Start here | Notes |
|---|---|---|
| A player or preservation tester | [02-quickstart.md](02-quickstart.md) | Build QEMU, run a game, learn the common flags. |
| A ROM/update archivist | [15-rom-loading.md](15-rom-loading.md) | Understand chip naming, update directories, and version tokens. |
| A QEMU contributor | [10-architecture.md](10-architecture.md) | Current device boundaries and where each implementation lives. |
| A cabinet owner | [04-troubleshooting.md](04-troubleshooting.md) | Cabinet-facing paths exist, but are not real-cabinet validated yet. |
| A debugger / reverse engineer | [30-symptom-patches.md](30-symptom-patches.md) | Current compatibility mechanisms and their exact activation scope. |

## What works today

| Area | Where we are |
|---|---|
| CPU and PC platform | QEMU's TCG i386, PIC, PIT, ISA bus—stock QEMU machinery. |
| Machine launch | `scripts/run-qemu.sh` wraps `-M pinball2000,game=<id>,roms-dir=<dir>`. |
| ROM loading | Bank0 chips (`u100`/`u101`) required; extra banks and DCS ROM best-effort. |
| Updates | Wrapper understands `auto`, `latest`, `none`, version tokens like `2.10`, and explicit bundle paths. |
| Display | QEMU display backends—SDL on desktop, `none` for headless. |
| Audio | DCS audio auto-selected, or you can force a QEMU backend or silence it. |
| Serial | COM1 goes to stdio, TCP, null, or stderr depending on your flags. |
| LPT driver board | Emulated by default; ppdev passthrough and cabinet-purist modes experimental. |
| Compatibility shims | A ROM-specific `mem_detect` rewrite and a base-ROM-only DCS probe-cell shim remain; neither is active device debt for normal genuine-2.x cabinet runs. |

> [!WARNING]
> Some compatibility code is explicit and bounded. The static PCI responder
> supplies the fixed topology the cabinet software expects; it is not roadmap
> work unless a concrete cabinet failure exposes missing behavior.

## What Encore is not

> [!NOTE]
> Encore is a custom QEMU machine, not a standalone emulator or a general-purpose ROM loader.

* **Not a stock PC.** We don't boot `bios.bin` like a normal PC—our reset hook drops the CPU straight into the PRISM option ROM path the Williams board expects.
* **Not a ROM distribution.** You bring your own ROMs and updates; we load them.
* **Not cabinet-certified yet.** The ppdev passthrough exists, but nobody's validated it on a real driver board. Desktop emulation is solid; cabinet claims need proof.

> [!CAUTION]
> Cabinet-facing modes exist but are not validated on physical Williams Pinball 2000 hardware. Do not connect to real cabinet hardware without explicit testing and verification.

## See also

* [↑ Documentation index](README.md)
* [02-quickstart.md](02-quickstart.md)
* [03-cli-reference.md](03-cli-reference.md)
* [10-architecture.md](10-architecture.md)
* [11-machine-init.md](11-machine-init.md)
* [14-boot-recipe.md](14-boot-recipe.md)
* [15-rom-loading.md](15-rom-loading.md)
* [20-plx-pci.md](20-plx-pci.md)
* [23-mediagx-and-display.md](23-mediagx-and-display.md)
* [25-dcs-sound.md](25-dcs-sound.md)
* [26-lpt-board.md](26-lpt-board.md)
