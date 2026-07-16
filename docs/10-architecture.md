# 10 — Architecture

Encore is a custom QEMU i386 machine. QEMU owns x86 execution, TCG, timers,
interrupt controllers, display backends, audio backends and host input. Encore
adds the Pinball 2000 hardware surfaces used by Williams software.

```text
scripts/run-qemu.sh
        │
        ▼
custom qemu-system-i386
        │
        ▼
pinball2000 machine
  ├─ ROM and protected-mode boot
  ├─ fixed PCI/PLX/PRISM topology
  ├─ BAR2 SRAM and BAR3 update flash
  ├─ MediaGX registers, blitter and display
  ├─ DCS protocol and audio engine
  ├─ LPT cabinet board
  └─ ISA probes, RTC and COM1
```

## Machine startup

`qemu/pinball2000.c` creates 16 MiB RAM, an i386 CPU, ISA bus, i8259 PIC and
i8254 PIT, then installs Encore devices. The reset callback in `qemu/p2k-boot.c`
copies the PRISM option-ROM entry code, constructs the required protected-mode
state and begins execution at the Williams entry point.

Encore deliberately skips BIOS POST and real-mode option-ROM discovery. That is
the project boot design, not unfinished work.

## Device ownership

| Area | Current owner |
|---|---|
| ROM banks and DCS chips | `p2k-rom.c`, `p2k-plx9054.c` |
| PCI configuration and PLX registers | `p2k-pci.c`, `p2k-plx-regs.c` |
| SRAM and update flash | `p2k-bars.c`, `p2k-bar3-flash.c` |
| Display and MediaGX | `p2k-gx.c`, `p2k-gp-blt.c`, `p2k-display.c`, `p2k-vsync.c` |
| DCS protocol and playback | `p2k-dcs*.c`, `p2k-adsp2105-core.c` |
| Cabinet I/O | `p2k-lpt-board.c` |
| COM1, RTC and probe ports | `p2k-isa-stubs.c`, `p2k-superio.c` |
| Timing diagnostics and HOTLOOP | `p2k-timing-audit.c`, `p2k-clkint-hotloop.c` |

## Important boundaries

- Lamp/output state and switch-input state are separate in the LPT device.
- BAR4 and I/O-port DCS frontends share one protocol core.
- Audio engines consume the same DCS commands but render content differently.
- Savedata files represent guest-visible hardware, not whole-machine snapshots.
- Adaptive HOTLOOP raises IRQ0 through QEMU's PIC path; it does not patch XINU
  scheduler state.

## Accepted compatibility behavior

The fixed PCI responder is the intended cabinet topology. A signature-gated
memory correction supports firmware that reports only 4 MiB. `--update none`
enables a narrowly gated DCS probe-cell mechanism so base software can use
sound. These are current design choices; see
[30 — Compatibility mechanisms](30-compatibility-support.md).

For exact module ownership see [`qemu/README.md`](../qemu/README.md). For guest
addresses see [13 — Memory map](13-memory-map.md).

---

← [Documentation index](README.md) · [Project README](../README.md)
