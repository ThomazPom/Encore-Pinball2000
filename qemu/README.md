# `qemu/` — Pinball 2000 custom QEMU machine

This directory holds the source for a custom QEMU **machine type** named
`pinball2000`.  When built into a patched `qemu-system-i386`, it exposes the
Williams Pinball 2000 board (Cyrix MediaGX + CS5530 + PLX9054 + DCS sound +
LPT driver board) so the unmodified game ROM image can run.

## Why a custom machine type, not stock QEMU + `-bios`

`roms/bios.bin` is **not** an entry point on this hardware in any practical
sense.  The Unicorn project never executed the BIOS — it built a flat
protected-mode GDT in memory, copied the PRISM option ROM (first 32 KiB of
game ROM bank 0 = chips `u100` + `u101` interleaved) to physical address
`0x80000`, and jumped CPU to `EIP=0x801D9` with `ESP=0x8B000` and `IF=0`.
That is the boot recipe to reproduce.

Stock `qemu-system-i386 -M isapc -bios roms/bios.bin` therefore proves
nothing about Pinball 2000.  It only proves QEMU TCG runs (which it does).
That smoke-test is available as `scripts/run-qemu.sh --tcg-only`.

## Files

| File              | Purpose                                                  | Status     |
|-------------------|----------------------------------------------------------|------------|
| `pinball2000.h`   | Address-map / register / boot-entry constants            | written    |
| `pinball2000.c`   | `MachineClass` skeleton; documents init order            | scaffold   |
| `plx9054.c`       | PLX 9054 PCI bridge (BAR0 ROM window, BAR2 SRAM, BAR4)   | not yet    |
| `cs5530.c`        | CS5530 south-bridge wiring (PIT/PIC reuse stock QEMU)    | not yet    |
| `dcs2.c`          | DCS2 sound device on port `0x13c` (byte-stream protocol) | not yet    |
| `lpt_board.c`     | BT-94/107 driver board on port `0x378`                   | not yet    |

## Building (planned)

The custom machine cannot be built as a runtime QEMU plugin — QEMU does not
support out-of-tree `MachineClass` registration.  The two practical options:

1. **Vendored QEMU fork** (recommended):
   - Add QEMU source as a git submodule under `qemu-src/`.
   - Symlink/copy these files into `qemu-src/hw/i386/`.
   - Add to `qemu-src/hw/i386/meson.build` and `Kconfig`.
   - `cd qemu-src && ./configure --target-list=i386-softmmu --disable-werror && make -j`.
   - Resulting `qemu-src/build/qemu-system-i386` accepts `-M pinball2000`.

2. **External patch series** kept against a pinned upstream tag.

The build harness (`scripts/build-qemu.sh`) is a stub today.  It will pin
QEMU 9.x or 10.x once the scaffold compiles cleanly.

## Parity roadmap

A useful boot needs, in order:

1. **CPU + RAM + entry recipe** — i486 (Cyrix MediaGX surrogate), 16 MiB RAM,
   bank0 option-ROM copy, GDT, PM jump to `0x801D9`.  *Then guest reaches the
   first executable instruction in PRISM.*
2. **PIC + PIT** (reuse stock QEMU `i8259` + `i8254`).  *Then IRQ0 timing
   works correctly without any of the Unicorn-era guards.*
3. **PLX9054 BAR2 SRAM + watchdog health register** (`P2K_BAR2_WATCHDOG_HEALTH`
   must read `0xFFFF`).  *Then the watchdog stops resetting the board.*
4. **PLX9054 BAR0 ROM window with bank switching**.  *Then PRISM finishes
   POST and locates the game image.*
5. **DCS2 sound device** (port `0x13c`, byte-stream `HIGH/LOW` pairing,
   reset reply `0xF0`).  *Then sound init handshake `0xACE1` succeeds.*
6. **LPT board device** (port `0x378`, idle opcode `0x00 → 0xF0`).  *Then
   XINU sees driver-board presence and lamps/coils tasks start.*
7. **Display path** — DC_TIMING2 / VSYNC at ~57 Hz hooked to a QEMU
   `qemu_console` framebuffer.  *Then attract mode renders.*

Every step has a corresponding piece of distilled knowledge in
`pinball2000.h` or `unicorn.old/src/`.  None of the Unicorn timing
heuristics (vticks, dynamic budget, IRR guards, LPT pacing) is needed
here — QEMU owns CPU/PIT/PIC semantics natively.
