# 33 — Genesis and Story

A chronological account of how Encore was built, what was discovered
along the way, and why the project exists in its current form.

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## Starting point

The project began as a single-developer investigation into whether the
Pinball 2000 i386 game binary could be run on modern hardware without
the original MediaGX machine. The only prior art was a closed-source,
32-bit QEMU 0.8.1-based emulator from 2007 — functional but impossible
to build or run cleanly on modern x64 Linux.

The initial approach was to study the game binary through disassembly
and identify the minimum viable hardware surface: which PCI devices,
which MMIO regions, which I/O ports the game actually touched on the
way to rendering a frame.

## v0.1 — First boot (Encore v0.1)

The first milestone was reaching the XINA boot prompt. `Encore v0.1`
established the core architecture: Unicorn Engine (`UC_MODE_32`), a
16 MB guest RAM region, chip-ROM de-interleave, PCI config space,
and a minimal UART stub so the serial console would produce output.

Commit `826c79f` — *"Encore v0.1: initial build — ROM auto-detect,
savedata, modular architecture"*.

## v0.2 — XINA boot, EEPROM, MMIO fix

MMIO callback ordering was wrong; the PRISM GX register map needed
the MMIO callback to be installed before the Unicorn engine started.
SEEPROM (93C46) initialisation was also needed to pass the BIOS
self-test. After those two fixes, XINA (the XINU shell) appeared on
the virtual COM1 console.

Commit `5aead9f` — *"Encore v0.2: MMIO callback fix + EEPROM verify + XINA boot"*.

## Graphics milestone

Getting pixels on screen required understanding the Graphics Processor
(GP) blit engine inside the MediaGX and the `DC_TIMING2` register that
gates frame output. The framebuffer lives at `GX_BASE + 0x800000`
(640×240 RGB555, 2048-byte row pitch). The emulator must increment
`DC_TIMING2` on each read to convince the game the display controller
is alive.

Commit `792e48e` — *"MILESTONE: Game(Williams - Episode I) banner reached"*.

## Performance work

The earliest correct build ran at 5–6 FPS — the emulated guest was
producing frames far slower than the target 57 Hz. A series of
optimisations brought this to full speed:

* Direct RAM-pointer reads bypassing Unicorn's `uc_mem_read` API
  doubled throughput.
* Wall-clock PIT delivery (rather than cycle-counting) matched the game
  scheduler's expected interrupt rate.
* Iteration-count based batch execution (200 000 instructions per
  `uc_emu_start` call) replaced the unreliable SIGALRM-stop approach.
* The `nulluser` idle-loop HLT patch (BT-74) eliminated 100 % CPU
  burn during XINU idle time.

By commit `3bf49d1` the emulator reached 50–56 FPS.

## LPT driver-board

Pinball 2000 uses a proprietary parallel-port driver board for switch
matrix, coil drivers, and lamp matrix. Reverse-engineering the LPT
protocol from the game binary and the original P2K driver source took
several weeks of tracing. The emulated switch matrix is now complete;
real cabinet passthrough via Linux `ppdev` (`--lpt-device`) was added
later.

## DCS audio — the polarity discovery

DCS audio was the last major subsystem to fall into place. The PCI
probe that the game uses to detect the DCS board lives at guest address
`0x1A2ABC`. The probe has **inverted polarity**: it returns 1 (device
present) when the probed memory cell is **not** `0xFFFF`. Simply
returning `0xFFFF` from the BAR4 read — the intuitive "device present"
signal — caused the probe to return 0 and the game to skip DCS init
entirely.

The fix was two-pronged:
1. **bar4-patch mode**: byte-patch the `CMP/JNE` in `dcs_mode_select`
   to force `dcs_mode=1` regardless of the probe result.
2. **io-handled mode**: scribble `0x0000` (not `0xFFFF`) into the probe
   cell so the natural probe returns 1.

The discovery that the DCS-detect cell and the watchdog health register
are the **same memory location** was a separate insight, documented in
[15-watchdog-scanner.md](15-watchdog-scanner.md).

## Minimisation pass (2026-04-21)

By April 2026 the codebase had accumulated roughly a dozen hardcoded
per-version patches. A systematic audit removed every patch that could
be replaced by a pattern scan or by letting the game initialise
naturally. The final tally: seven dropped patch blocks, four surviving
(all pattern-scanned or game-agnostic).

The regression matrix (all seven bundles) was run after each removal.
Nothing regressed. The resulting codebase is the current state of the
`master` branch.

## Current status

Six of the seven known update bundles boot to attract mode with full
graphics and DCS audio under emulation. RFM v1.2 (1999) reaches a pre-XINU crash and
is documented as a known limitation. The emulator is approximately
8 000 lines of C across twelve files.

## Cross-references

* Architecture: [05-architecture.md](05-architecture.md)
* DCS polarity: [14-dcs-probe-polarity.md](14-dcs-probe-polarity.md)
* Watchdog scanner: [15-watchdog-scanner.md](15-watchdog-scanner.md)
* Patching philosophy: [21-patching-philosophy.md](21-patching-philosophy.md)
* Known limitations: [38-known-limitations.md](38-known-limitations.md)

## Historical research notes

### Why a from-scratch Unicorn build (and not a binary translation of the original)

Before Encore was started, several attempts were made to take the
original 32-bit P2K runtime binary and re-host it on modern x86-64
Linux directly. The shortest path explored was static binary
translation via the Rev.ng toolchain. Five iterations of that
approach were attempted; all of them failed for different reasons
(symbol resolution, dynamic library bridging, calling-convention
mismatches between the i386 C ABI used by the binary and the
SysV AMD64 ABI of host libraries, and similar issues).

The conclusion from those experiments was that re-hosting the
original binary unmodified was not viable in any reasonable amount
of work, and that a clean-room implementation built on top of an
existing CPU emulator (Unicorn Engine) would be both faster to
deliver and easier to maintain.

### Binary-stripping observations

All shipped P2K game binaries are stripped of debugging symbols but
retain dynamic symbols (the export table needed by the dynamic
loader). This is enough to recover the call-graph for the major
subsystems through standard reverse-engineering tools, which is how
the function table in
[46-function-reference.md](46-function-reference.md) was produced.

The original runtime also linked an MP3 wrapper library exposing a
small API (initialise, load, play, stop, free) used for the early
boot music; Encore replaces this with SDL_mixer entirely.
