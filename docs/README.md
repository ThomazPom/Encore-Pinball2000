# Encore-Pinball2000 — Documentation

A clean-room, single-binary x86 emulator for the Williams **Pinball 2000**
pinball platform. The guest is an i386 protected-mode image running on top
of **Unicorn Engine**; video is driven by **SDL2** and audio by
**SDL2_mixer**. The project boots every dearchived SWE1 and RFM update
bundle with graphics and DCS sound, and can optionally drive a real
cabinet through the host parallel port.

## Status

> **Emulator-only validation.** All documented behaviour has been
> observed under emulation on x86-64 Linux. Real-cabinet validation
> against a physical Pinball 2000 machine is pending. If you own a
> cabinet, see [42-cabinet-testing-call.md](42-cabinet-testing-call.md)
> to learn how to help verify.

This tree contains 47 documents, grouped by subsystem. Read the first
three in order if you are new to the project; afterwards jump straight
to whatever interests you.

## Getting started

| # | Document | What you learn |
|---|---|---|
| [01](01-overview.md)  | What Encore is, goals, 10 000-foot architecture |
| [02](02-quickstart.md) | Build from source, run your first bundle |
| [03](03-cli-reference.md) | Every CLI flag with a worked example |
| [04](04-config-yaml.md) | The YAML-ish config file loader |

## Architecture

| # | Document |
|---|---|
| [05](05-architecture.md) | Subsystems, threads, data-flow |
| [06](06-cpu-emulation.md) | Unicorn host, i386 guest, IDT, IRET, paging |
| [07](07-memory-map.md) | Guest physical address space, BARs, MMIO |
| [08](08-rom-loading-pipeline.md) | Chip-ROM de-interleave, bank concat, auto-detect |
| [09](09-update-loader.md) | `--update FILE|DIR|.exe|VERSION`; bundle naming |
| [10](10-savedata.md) | NVRAM + SEEPROM persistence, `--no-savedata` |

## Display, sound, I/O

| # | Document |
|---|---|
| [11](11-display-pipeline.md) | BAR2 SRAM, VBLANK, frame-double, aspect |
| [12](12-sound-pipeline.md) | DCS audio, shape-detected sample container |
| [13](13-dcs-mode-duality.md) | `--dcs-mode bar4-patch` vs `io-handled` |
| [14](14-dcs-probe-polarity.md) | The inverted-probe discovery |
| [15](15-watchdog-scanner.md) | Pattern-scanned, UART-anchored, generic |
| [16](16-irq-pic.md) | PIC state machine, IRQ routing, EOI, timer |
| [17](17-vblank.md) | How VBLANK is delivered and why 57 Hz |
| [18](18-lpt-emulation.md) | Virtual matrix: switches, coils, lamps |
| [19](19-real-lpt-passthrough.md) | `--lpt-device /dev/parport0`, Alt+K capture |

## Symbols and patches

| # | Document |
|---|---|
| [20](20-symbols-rom.md) | `SYMBOL TABLE` format, `sym_lookup` |
| [21](21-patching-philosophy.md) | pattern-scan > sym-lookup > hardcoded |
| [22](22-xinu-boot-sequence.md) | clkruns, prnull, nulluser, ready-queue |
| [23](23-boot-scheduler-fix.md) | The scheduler-priming discovery |

## Operations

| # | Document |
|---|---|
| [24](24-fps-and-pacing.md) | Frame budget, exec-count batching |
| [25](25-game-detection-auto.md) | `game_id @ 0x803C`, LPT auto-detect |
| [26](26-testing-bundle-matrix.md) | Full regression matrix, all 7 bundles |
| [27](27-troubleshooting.md) | Common failures and fixes |
| [28](28-build-system.md) | Makefile targets, layout |

## Host tools

| # | Document |
|---|---|
| [29](29-tools-sym-dump.md) | `tools/sym_dump.py` |
| [30](30-tools-build-update-bin.md) | `tools/build_update_bin.py` |
| [31](31-tools-deinterleave.md) | Chip-ROM deinterleaver recipe |
| [32](32-tools-sound-decoder.md) | The driver disassembly, sample container |

## History and reference

| # | Document |
|---|---|
| [33](33-genesis-and-story.md) | How this project started, chronology |
| [34](34-nonfatal-vs-fatal.md) | Exception classification, panic-loop taming |
| [35](35-rfm-vs-swe1.md) | Per-title differences, chip-ROM revisions |
| [36](36-cli-keyboard-guide.md) | Live key-binding cheat-sheet |
| [37](37-distribution.md) | Folder layout after renaming, what to ship |
| [38](38-known-limitations.md) | What does not yet work |
| [39](39-future-work.md) | Roadmap and `maybe-fun` list |
| [40](40-glossary.md) | Terms and acronyms |
| [41](41-build-env-and-runtime.md) | Build, environment and runtime knobs |
| [42](42-cabinet-testing-call.md) | Real-cabinet testing call-to-action |
| [43](43-cabinet-hardware-primer.md) | Cabinet hardware primer for software contributors |
| [44](44-xina-os-deep-dive.md) | XINA OS deep dive |
| [45](45-official-update-manager.md) | Official update manager background |
| [46](46-function-reference.md) | Key function reference (game binary) |
| [47](47-community-updates.md) | Community updates (mypinballs.com) |
| [48](48-lpt-protocol-references.md) | Real PB2K LPT protocol — public references |
| [49](49-splash-screen.md) | Startup splash image (`--splash-screen`) |
| [50](50-cpu-clock-mismatch.md) | Speculative: CPU clock-rate vs guest-side iodelay timing |

## Conventions

* Every file reference is a repository-relative path (`src/io.c:443` means
  line 443 of `src/io.c` in this checkout).
* Guest physical addresses are always hex with `0x` prefix; host
  pointers are avoided in docs because they are meaningless across runs.
* Code excerpts are copied verbatim from the current checkout. When the
  code changes, update the doc in the same commit.

## Authorship

The entire project — code, documentation, tooling — is the work of a
single developer. There is no team, no corporate backer, no AI coauthor.
The commit history is the ground truth for attribution.
