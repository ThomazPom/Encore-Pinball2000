# Encore QEMU machine sources

This directory implements the `pinball2000` QEMU machine. The build script
copies these files into the pinned QEMU source tree and compiles a custom
`qemu-system-i386`.

> [!IMPORTANT]
> These files define the guest-visible machine. Change a device only against a
> reproduced software failure or a physical-cabinet trace.

## Source map

| Files | Responsibility |
|---|---|
| `pinball2000.c`, `p2k-internal.h` | Machine creation and shared interfaces |
| `p2k-rom.c`, `p2k-plx9054.c` | Chip loading and ROM windows |
| `p2k-boot.c` | Protected-mode reset state |
| `p2k-pci.c`, `p2k-plx-regs.c` | Fixed PCI topology and PLX registers |
| `p2k-bars.c`, `p2k-bar3-flash.c` | SRAM and update flash |
| `p2k-dcs*.c` | DCS protocol and audio engines |
| `p2k-lpt-board.c` | Emulated and real driver-board connection |
| `p2k-gx.c`, `p2k-gp-blt.c`, `p2k-display.c`, `p2k-vsync.c` | Graphics and display timing |
| `p2k-isa-stubs.c`, `p2k-superio.c`, `p2k-cyrix-ccr.c` | Board-specific I/O surfaces |
| `p2k-mem-detect.c`, `p2k-probe-cell-shim.c` | Narrow software compatibility support |
| `p2k-clkint-hotloop.c`, `p2k-timing-audit.c`, `p2k-diag.c` | Clock delivery and diagnostics |

> [!NOTE]
> The fixed PCI responder, memory-size correction and base-ROM DCS support are
> accepted implementation choices. They are described in
> [compatibility support](../docs/30-compatibility-support.md).

## Build

```sh
scripts/build-qemu.sh
```

> [!TIP]
> Start with the [architecture](../docs/10-architecture.md), then use the
> [memory map](../docs/13-memory-map.md) and source table above to locate a
> device.
