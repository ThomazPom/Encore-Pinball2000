# Encore Pinball 2000 — QEMU board (clean experiment)

This branch is a from-scratch experiment to emulate the Williams **Pinball 2000**
hardware (Cyrix MediaGX + CS5530 + PLX9054 + DCS sound + LPT driver board) on top
of upstream **QEMU**, instead of the previous Unicorn-based custom emulator.

It is branched from `4b02a63` (the last commit before LPT bus pacing was
introduced into the Unicorn project) and is intentionally narrow.

## Why QEMU, not Unicorn

The Unicorn project lives in [`unicorn.old/`](unicorn.old/) for reference only.
It hit a hard ceiling: `uc_emu_start` API overhead caps useful PIT/IRQ0 wall-Hz
at ~3.3 kHz against a 4004 Hz hardware target, regardless of how the timing
loop is structured. Reaching real-board fidelity required reimplementing PIT,
PIC, IRQ injection, EOI semantics, ESP-based task-switch heuristics, IRR/ISR
guards, and a vticks budget — all of which QEMU already implements correctly
and natively.

The decision: stop fighting the API; let QEMU own the CPU loop, the i8254 PIT,
the i8259 PIC, and the event-loop. Only port Pinball 2000 *hardware* knowledge
into a custom QEMU machine type.

## Project layout

```
.
├── qemu/             # New custom QEMU machine "pinball2000" (out-of-tree source)
│   ├── pinball2000.c # MachineClass, CPU+chipset wiring
│   ├── plx9054.c     # PCI bridge / BAR0 ROM window / BAR2 SRAM (TODO)
│   ├── cs5530.c      # CS5530 south-bridge stubs (TODO)
│   ├── dcs2.c        # Williams DCS sound device on port 0x13c (TODO)
│   ├── lpt_board.c   # BT-94/107 driver board on parport (TODO)
│   ├── pinball2000.h # Address-map / register constants
│   └── README.md     # Hardware design notes for the board
├── scripts/
│   ├── run-qemu.sh   # Launch our qemu-system-i386 against a Pinball 2000 ROM set
│   └── build-qemu.sh # Build a patched qemu-system-i386 with our machine type
├── tools/            # ROM/update/sound asset utilities (architecture-independent)
├── roms/             # Game ROMs (bios.bin + per-game banks)
├── updates/          # Optional update bundles
├── savedata/         # Per-game NVRAM (gitignored)
└── unicorn.old/      # Archived old Unicorn project — REFERENCE ONLY
```

## Build & run (current state)

The custom machine type is **scaffolded, not yet buildable** — see
[`qemu/README.md`](qemu/README.md) for what is implemented and what is needed
before `qemu-system-i386 -M pinball2000 ...` will actually start.

`roms/bios.bin` is **not** the entry point: the Unicorn project never
executed it.  The real first instructions live in the PRISM option ROM
(first 32 KiB of game ROM bank 0) at `EIP=0x801D9` after a hand-built
protected-mode setup.  The QEMU machine reproduces this recipe in
`pinball2000_init()`.

For a sanity check that the host `qemu-system-i386` binary even runs TCG:

```sh
scripts/run-qemu.sh --tcg-only
```

This is **not** a Pinball 2000 boot — see `qemu/README.md`.

## Out of scope for this branch (intentionally)

- Unicorn batches, vticks accounting, dynamic budgets
- IRQ0 in-flight guards, direct IRR scheduling
- LPT bus pacing
- `bar4-patch` DCS fallback as default
- Wallclock-driven timing heuristics

If a Pinball 2000-specific bug genuinely requires one of these *under QEMU*,
it must be re-proven independently before being introduced.

## Status

Foundation only. See `qemu/README.md` for the parity roadmap.
