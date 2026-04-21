# 01 — Overview

> *Encore* (French: "again") is a clean, second-generation Pinball 2000
> emulator. It does not share code with any prior project and is written
> from scratch in C against published hardware references, plus
> disassembly of the game's own i386 binaries.

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## What Encore is

Encore loads the exact ROM and update files that Williams shipped for a
Pinball 2000 head, boots them inside a self-contained Unicorn-driven i386
virtual machine, and renders the resulting 640×240 framebuffer to an SDL
window while pumping the game's DCS-2 command stream to an SDL_mixer
audio backend. It is a single 800 KB native ELF with no interpreter, no
Python runtime, no kernel module; copy the binary, point it at a ROM
folder, and run.

The goal is **authenticity through simplicity**:

* Boot the *real* game binary, not a reimplementation.
* Run exclusively from untouched ROM images — the flash update is
  assembled on disk the same way Williams' own service installer would
  have assembled it.
* Emulate only the hardware the game actually uses, nothing more.
  MediaGX config registers, the PLX 9050 bridge, the PRISM BAR0-BAR5
  window, DCS-2 PCI audio, the LPT driver-board protocol and the
  COM1 UART — everything else returns a safe default.

## Goals (in order of priority)

1. **Correct boot on every dearchived bundle.** Seven update bundles
   have been pulled from the wild — SWE1 v1.5 / v2.1 and RFM v1.2 /
   v1.6 / v1.8 / v2.5 / v2.6. Six of the seven reach attract mode with
   full graphics and DCS audio. The oldest (RFM v1.2, 1999) dies
   pre-XINU and is documented separately (see [26](26-testing-bundle-matrix.md)).
2. **Single binary, zero dynamic discovery.** No per-bundle JSON blob,
   no Python plug-ins, no per-game #ifdef. Every bundle-specific offset
   is either pattern-scanned at boot or looked up through the update's
   own `SYMBOL TABLE`.
3. **Real-cabinet compatible.** `--lpt-device /dev/parport0` forwards
   every LPT access to a physical Pinball 2000 driver board. The
   emulated switch matrix falls silent so the real playfield owns the
   I/O.
4. **Small, legible, hackable.** ≈ 8 000 lines of C across thirteen files.
   Any technically-minded contributor should be able to read the entire
   source in an afternoon.

## 10 000-foot architecture

```
┌────────────────────────────────────────────────────────────────┐
│                  main.c — CLI / config / wiring                │
│                                                                 │
│   parse_args() → rom_load_all() → cpu_init() → memory_init()   │
│   io_init() → bar_init() → display_init() → sound_init()       │
│   netcon_init() → lpt_passthrough_open() → cpu_run()           │
└────────┬────────────────┬────────────────┬────────────┬────────┘
         │                │                │            │
         ▼                ▼                ▼            ▼
   ┌───────────┐    ┌───────────┐   ┌──────────┐  ┌──────────┐
   │ Unicorn   │    │ SDL2      │   │ SDL2_    │  │ Linux    │
   │ (i386     │    │ window    │   │ mixer    │  │ ppdev    │
   │  guest)   │    │ + input   │   │ (audio)  │  │ (optional)│
   └─────┬─────┘    └───────────┘   └──────────┘  └──────────┘
         │
         │ MMIO / port hooks
         ▼
   ┌────────────────────────────────────────────────┐
   │ io.c    — PIC, PIT, CMOS, LPT, UART, DCS-UART  │
   │ bar.c   — PCI BAR0..BAR5 MMIO, SRAM, flash     │
   │ pci.c   — PCI config space (0xCF8 / 0xCFC)     │
   │ cpu.c   — patch scans, IRQ inject, HLT idle    │
   │ rom.c   — chip-ROM de-interleave, bundle load  │
   │ symbols.c — XINU SYMBOL TABLE index            │
   └────────────────────────────────────────────────┘
```

A single Unicorn thread owns guest execution. The host main loop runs
in batches of 200 000 instructions; between batches we tick the PIT,
drive VBLANK, drain the UART, refresh the SDL texture, and poll the TCP
bridges.

## Why not just extend a prior emulator?

Two reasons.

**Legal** — the pre-existing public emulator is closed-source and
carries a restrictive redistribution clause; building a new emulator
from published datasheets and disassembly of the game's *own* binary
keeps Encore's own license clean.

**Technical** — the prior emulator runs as a 32-bit binary with
patched QEMU 0.8.1 (2007). On modern x64 Linux it is a brittle stack of
multilib glibc, ALSA quirks and RTC-SIGIO races. Encore's 64-bit
Unicorn-based approach sidesteps every one of those and the whole
program compiles with stock Debian 12 packages.

## What Encore is *not*

* **Not a WPC / pre-Pinball-2000 emulator.** The DCS audio board
  pre-dates Pinball 2000 but the video, LPT driver-board protocol and
  XINU/XINA stack are specific to the 1999-2000 Williams platform.
* **Not a MAME contribution.** MAME has a different performance model
  (high-level timings, no UC/JIT) and a different licensing contract.
  Encore can coexist but it is its own program.
* **Not a ROM hacker's sandbox.** There is no debugger UI, no symbolic
  breakpoint panel, no graphical monitor. If you want to poke the game
  while it runs, use `--serial-tcp` and `nc`.

Read [02-quickstart.md](02-quickstart.md) next to get a bundle booting
on your machine in under five minutes.

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
