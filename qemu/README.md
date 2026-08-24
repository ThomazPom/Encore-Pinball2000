# Encore QEMU machine sources

This directory implements the `pinball2000` QEMU machine. The build script
copies these files into the pinned QEMU source tree and compiles a custom
`qemu-system-i386`.

> [!IMPORTANT]
> These files define the guest-visible machine. Verify device claims here
> before copying them into user documentation.

Development rules, hook placement and validation requirements:
[development guidelines](../docs/05-development-guidelines.md).

## Source map

| Files | Responsibility |
|---|---|
| `pinball2000.c`, `p2k-internal.h` | Machine creation and shared interfaces |
| `p2k-rom.c`, `p2k-plx9054.c` | Chip loading and ROM windows |
| `p2k-boot.c` | Protected-mode reset state |
| `p2k-pci.c`, `p2k-plx-regs.c` | Fixed PCI topology and PLX registers |
| `p2k-bars.c`, `p2k-bar3-flash.c` | SRAM and update flash |
| `p2k-dcs*.c` | DCS protocol and audio engines |
| `p2k-lpt-board.c`, `p2k-switch-keymap.c` | Emulated/real driver-board connection and configurable desktop switches |
| `p2k-gx.c`, `p2k-gp-blt.c`, `p2k-display.c`, `p2k-video-capture.c`, `p2k-vsync.c` | Graphics, compressed video capture and display timing |
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

Encore defaults to the validated QEMU 10.0.8 source release. Each directory is
one logical patch family. Variant filenames declare their source-compatible
release or inclusive range:

```text
qemu-10.0.8.patch
qemu-10.0.0_to_10.0.8.patch
```

`upstream-patches/series` defines family order without encoding it in directory
names. The builder requires one and only one declared variant from every family
to apply with zero context fuzz. A missing, ambiguous or rejected family stops
the build before compilation.

| Patch | Why it exists | Maintenance status |
|---|---|---|
| `mediagx-instructions` | Implements the gated MediaGX display-driver instructions missing from stock i386 TCG | Required CPU support. `CPU_WRITE` includes an empirically established scratchpad side effect in addition to the documented internal-register write. |
| `irq-observation` | Observes clkint entry, interrupt acknowledgement, IRET and PIC EOI | Required for current timing control and diagnostics. It crosses several QEMU interrupt paths, so benchmark its overhead after an upgrade. |
| `tcg-cflags-override` | Gives the machine a callback at the TCG execution-loop boundary | Used by `--legacy-hotloop` and TB-boundary diagnostics. Default host-timer HOTLOOP returns before using its legacy delivery path. This is the most upgrade-sensitive family. |
| `pit-speed-target` | Scales the i8254 channel divisor for `--speed-target` | Required for speed control in PIT-backed modes. The weak default leaves other machines unchanged. |

> [!IMPORTANT]
> Do not remove `irq-observation` as “diagnostics only”: HOTLOOP's adaptive
> controller reads the observed clkint count.

`irq-observation` supplies the clkint-entry count used by default HOTLOOP, HOTLOOP with
`--with-pit`, and HOTLOOP `--speed-target` control. It also supplies the guest
IRQ observations reported by `-v` and `--bench`. Plain `--strict` execution
does not need the count for clock control, although `--strict --bench` and
`--strict -v` still use it for measurement.

## Validate a QEMU release range

Source-test every stable release in an inclusive range against pristine QEMU
trees:

```sh
scripts/internal/qemu-patch-series.py validate \
  --from 10.0.0 --to 10.0.8
```

Add `--update-names` only after the complete requested range passes. It renames
each selected variant to the tested compatibility range. The validator checks
patch application, not compilation or boot behavior; promote a release into
`KNOWN_GOOD_VERS` only after those later checks also pass.

Compatibility ranges cover stable releases only. Release candidates require
an explicitly RC-named variant and a validation run with `--include-rc`.

The current patch bodies pass zero-fuzz source validation across every
published stable QEMU 10.x release from 10.0.0 through 10.2.4. This is source
compatibility, not a claim that those releases have all been compiled or
boot-tested; 10.0.8 remains the runtime-validated default.

## Deliberate compatibility boundaries

Not every file in this directory is a complete chip model:

- `p2k-pci.c` provides the fixed configuration-space topology the software
  uses to find already mapped devices. Replacing it with QEMU PCI devices is a
  large structural change and currently offers no cabinet or runtime benefit.
- `p2k-mem-detect.c` retains an opt-in, signature-matched XINU memory-size
  override (`P2K_MEM_DETECT_PATCH=1`) for diagnosing older updates.
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
