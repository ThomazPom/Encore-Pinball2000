# Encore QEMU machine sources

This directory implements the `pinball2000` QEMU machine. The build script
copies these files into the pinned QEMU source tree and compiles a custom
`qemu-system-i386`.

> [!IMPORTANT]
> These files define the guest-visible machine. Verify device claims here
> before copying them into user documentation.

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

## Build

```sh
scripts/build-qemu.sh
```

## Upstream patch boundary

The files under `upstream-patches/` modify the pinned QEMU core. They are not
interchangeable machine sources: review and revalidate them whenever the QEMU
version changes.

| Patch | Why it exists | Maintenance status |
|---|---|---|
| `0001-i386-tcg-cyrix-mediagx-shim.patch` | Implements the gated MediaGX display-driver instructions missing from stock i386 TCG | Required CPU support. `CPU_WRITE` includes an empirically established scratchpad side effect in addition to the documented internal-register write. |
| `0002-p2k-observe-irq0-delivery.patch` | Observes clkint entry, interrupt acknowledgement, IRET and PIC EOI | Required for current timing control and diagnostics. It crosses several QEMU interrupt paths, so benchmark its overhead after an upgrade. |
| `0003-p2k-tcg-cflags-override.patch` | Gives the machine a callback at the TCG execution-loop boundary | Required by HOTLOOP. This is the most upgrade-sensitive patch because the callback is evaluated for every translated-block lookup. Its ordinary path returns the original flags. |
| `0004-p2k-pit-speed-target.patch` | Scales the i8254 channel divisor for `--speed-target` | Required for speed control in PIT-backed modes. The weak default leaves other machines unchanged. |

> [!IMPORTANT]
> Do not remove `0002` as “diagnostics only”: HOTLOOP's adaptive controller
> reads the observed clkint count. Do not remove `0003` while HOTLOOP is a
> supported clock source.

`0002` supplies the clkint-entry count used by default HOTLOOP, HOTLOOP with
`--with-pit`, and HOTLOOP `--speed-target` control. It also supplies the guest
IRQ observations reported by `-v` and `--bench`. Plain `--strict` execution
does not need the count for clock control, although `--strict --bench` and
`--strict -v` still use it for measurement.

## Deliberate compatibility boundaries

Not every file in this directory is a complete chip model:

- `p2k-pci.c` provides the fixed configuration-space topology the software
  uses to find already mapped devices. Replacing it with QEMU PCI devices is a
  large structural change and currently offers no cabinet or runtime benefit.
- `p2k-mem-detect.c` performs one signature-matched memory-size correction for
  affected software versions.
- `p2k-probe-cell-shim.c` performs a bounded base-ROM-only compatibility
  update; normal update boots do not activate it.
- `p2k-plx-regs.c` and the ISA, SuperIO and GX files model the behavior used by
  the software, not every feature of their physical chips.

These are valid cleanup targets only when a replacement solves a concrete
boot, cabinet or maintainability problem. A more elaborate device model is
not automatically a better implementation.

Details: [compatibility support](../docs/30-compatibility-support.md).

> [!TIP]
> Start with the [architecture](../docs/10-architecture.md), then use the
> [memory map](../docs/13-memory-map.md) and source table above to locate a
> device.
