# Encore Pinball 2000 — Documentation

This directory is the long-form story. It tells you how we
built the machine, why each hardware detail matters, what still needs
cabinet testing, and where the project is heading. If you want the quickstart,
see [02-quickstart.md](02-quickstart.md). If you want to understand the
journey, keep reading.

> [!IMPORTANT]
> **All behaviour documented here is based on QEMU emulator testing only.**
> Real-cabinet validation is pending. If you own a Pinball 2000 cabinet,
> see [29-cabinet-testing-call.md](29-cabinet-testing-call.md) to help verify
> that what works in emulation also works on wire.

---

## Getting started

| Document | What you learn |
|---|---|
| [01 — Overview](01-overview.md) | What Encore is, why it exists, and the 10 000-foot view of how it brings Pinball 2000 firmware to life on modern hardware. |
| [02 — Quickstart](02-quickstart.md) | How to build from source and run your first game — the shortest path from clone to attract mode. |
| [03 — CLI Reference](03-cli-reference.md) | Every command-line flag, option shorthand, and environment variable the launcher understands. |
| [04 — Troubleshooting](04-troubleshooting.md) | Common boot failures, cryptic error messages, and how to fix them — the "it didn't work, now what?" guide. |

> [!TIP]
> New to the project? Read 01, 02, and 04 in that order. Document 03 is for later when you're customizing runs.

---

## The story

| Document | What you learn |
|---|---|
| [05 — Genesis and Story](05-genesis-and-story.md) | How Encore was built — the chronological account of first boot, the graphics milestone, DCS audio polarity, and the minimisation pass that removed every patch that could be replaced with clean device behaviour. |
| [08 — RFM vs SWE1](08-rfm-vs-swe1.md) | Why Revenge from Mars and Star Wars Episode I behave differently under emulation, which ROM revisions matter, and what "r2 chips" means. |
| [39 — Community Updates](39-community-updates.md) | Jim Askey's mypinballs.com firmware — the unofficial patches that fixed bugs, added lighting effects, and kept Pinball 2000 alive after Williams stopped shipping updates in 2003. |
| [38 — Official Update Manager](38-official-update-manager.md) | How the original Williams `.exe` self-extractors worked, what they installed, and why we preserve them under version control. |
| [29 — Cabinet Testing Call](29-cabinet-testing-call.md) | Why we need your help if you own real hardware — the gap between "boots in QEMU" and "drives a 500-pound pinball cabinet without catching fire." |

> [!NOTE]
> Document 05 is the heart of the repo. If you want to understand why the codebase is structured the way it is, start there.

---

## How it boots

| Document | What you learn |
|---|---|
| [10 — Architecture](10-architecture.md) | The subsystems, the thread model, the data flow — how the machine is structured and why. |
| [11 — Machine Init](11-machine-init.md) | What QEMU does during `machine->init()` to prepare the guest environment before the first instruction executes. |
| [12 — CPU and Timers](12-cpu-and-timers.md) | How the i386 TCG engine runs guest code, how the PIT delivers IRQ0, and why timing is hard. |
| [47 — Recommended Configuration and Runtime Matrix](47-recommended-configuration.md) | **The config to actually run** — both games across graphical/headless and HOTLOOP-only/strict/PIT-combo modes, with intended use, measured coverage, LPT expectations, and cabinet caveats. |
| [13 — Memory Map](13-memory-map.md) | Where the guest address space lives — RAM, ROM, BARs, MMIO regions — and why those addresses are not arbitrary. |
| [14 — Boot Recipe](14-boot-recipe.md) | The step-by-step sequence from power-on to XINA shell to attract mode, with every IRQ and MMIO access that makes it happen. |
| [15 — ROM Loading](15-rom-loading.md) | How the chip ROMs are de-interleaved, concatenated, and mapped into guest memory — the pipeline that turns four binary blobs into a bootable image. |
| [06 — XINA and Serial Console](06-xina-os-deep-dive.md) | How to open the guest console, inspect XINU processes, and run clock diagnostics. |

> [!IMPORTANT]
> Documents 14 and 15 are essential if you're trying to understand why the machine boots at all. The boot recipe walks you through every visible step; the ROM loader explains why we have four chip files instead of one.

---

## Hardware emulation

| Document | What you learn |
|---|---|
| [20 — PLX / PCI Configuration](20-plx-pci.md) | How the PLX 9054 PCI-to-local bridge is emulated, why CF8/CFC config-space ports exist, and what BAR0/BAR1 do. |
| [21 — Flash BAR3](21-flash-bar3.md) | The Intel 28F320 flash chip protocol, how update bundles are written to guest memory, and why CFI queries matter. |
| [22 — SRAM BAR2](22-sram-bar2.md) | The 128 KiB battery-backed SRAM that stores audits, high scores, and XINU resource tables — and why losing it breaks the boot. |
| [23 — MediaGX and Display](23-mediagx-and-display.md) | The Cyrix MediaGX Graphics Processor, the blit engine, the framebuffer format (RGB555 at 640×240), and how we convince SDL2 to render it. |
| [24 — VSYNC](24-vsync.md) | Why the game expects 57 Hz, what the `DC_TIMING2` register does, and how we deliver frame timing without drifting. |
| [25 — DCS Sound](25-dcs-sound.md) | The DCS audio coprocessor, the shape-detected sample container, the BAR4 MMIO interface, and how we decode and play WAV chunks on the host. |
| [26 — LPT Board](26-lpt-board.md) | The parallel-port driver board protocol — switch matrix reads, coil fires, lamp writes — and how the emulated matrix responds to keypresses. |
| [27 — ISA Stubs](27-isa-stubs.md) | The minimal ISA device stubs (PIT, PIC, UART, RTC) that keep the BIOS and XINU happy without modelling every chip on the original motherboard. |
| [28 — Cabinet Hardware Primer](28-cabinet-hardware-primer.md) | What the real Pinball 2000 hardware looks like — the driver board, the power supply, the MediaGX CPU card — so you understand what we're emulating. |
| [31 — MediaGX TCG Extension Gate](31-mediagx-gate.md) | The MediaGX CPU instructions required by the Williams display driver and how Encore confines them to its machine type. |
| [33 — Memory Detect](33-mem-detect.md) | The signature-gated compatibility rewrite for firmware that hard-codes 4 MiB; genuine SWE1 2.00 needs no change. |
| [34 — Probe Cell Shim](34-probe-cell-shim.md) | Accepted, strictly gated DCS probe-cell compatibility that gives `--update none` museum/base software sound. |

Documents 33 and 34 describe narrowly gated, accepted compatibility mechanisms:
the firmware memory-size correction and base-ROM DCS probe maintenance.

---

## Investigations and quirks

| Document | What you learn |
|---|---|
| [16 — Watchdog Scanner](16-watchdog-scanner.md) | Current scanner scope and the base-ROM probe-cell mechanism it supports. |
| [37 — Power Driver Board Protocol](37-power-driver-board-protocol.md) | The LPT electrical details, the handshake timing, and the community reverse-engineering notes that taught us how the driver board talks to the CPU. |

> [!NOTE]
> Document 17 is one of the most satisfying stories in the repo. If you like debugging tales, read it.

---

## Compatibility mechanisms

| Document | What you learn |
|---|---|
| [30 — Current Compatibility Mechanisms](30-symptom-patches.md) | The fixed PCI topology, memory-size correction and base-ROM DCS probe support used by current Encore. |

New compatibility behavior must have a concrete current failure, narrow activation
scope and regression coverage. Existing accepted mechanisms are not removal work.

---

## Operations and persistence

| Document | What you learn |
|---|---|
| [26 — Validation Matrix](26-testing-validation-matrix.md) | The reproducible SWE1/RFM base/latest × three-DCS-engine regression matrix and its latest retained result. |
| [09 — Savedata](09-savedata.md) | The four guest-visible persistent devices, their files, and read-only test mode. |
| [41 — CLI Keyboard Guide](41-cli-keyboard-guide.md) | The SDL2 keyboard shortcuts for Start button, menu navigation, service mode, and debug toggles — the desktop user's control panel. |
| [45 — XINA Shell Cookbook](45-xina-shell-cookbook.md) | The COM1 `%` prompt field guide: tested XINA command output plus cabinet-readiness ideas for audits, burn-in, pricing, DCS, networking, and telemetry. |
| [42 — Williams Symbol Tables](42-symbols-rom.md) | How to inspect update symbol tables offline and what Encore currently does with them. |
| [43 — Build System](43-build-system.md) | How `scripts/build-qemu.sh` compiles upstream QEMU with our machine files, what the Makefile does, and where build artifacts land. |
| [46 — Real LPT Passthrough](46-real-lpt-passthrough.md) | The implemented Linux `ppdev` path, its tracing/purist controls, and the physical-cabinet tests still required. |

> [!TIP]

---

## Roadmap and reference

| Document | What you learn |
|---|---|
| [35 — Known Limitations](35-known-limitations.md) | The current, reproducible limitations of Encore. |
| [36 — Roadmap](36-roadmap.md) | Where the project is going — the short-term validation tasks, the mid-term symptom-patch removals, and the long-term cabinet proof gate. |
| [40 — Glossary](40-glossary.md) | The jargon decoder — what "BAR", "PLX", "DCS", "XINA", "GP", "TCG", and fifty other acronyms mean in the context of this project. |

> [!IMPORTANT]
> **Read document 35 before filing a bug.** If your issue is listed there, we already know. If it's not listed, please do file it.

---

## Reference materials

The `references/` subdirectory contains:

* **`PinballDiag/`** — Diagnostic ROM source code fragments and protocol notes from the original Williams team, invaluable for understanding the LPT handshake and switch matrix.
* **`web_archive/`** — Archived forum posts, mailing list threads, and community reverse-engineering notes that predate the project. These are cited throughout the docs when explaining "how we knew that."

> [!NOTE]
> Reference materials are historical and may predate the current QEMU-based implementation. Cross-check dates and context before assuming they apply to current behaviour.

---

← [Back to project README](../README.md)
