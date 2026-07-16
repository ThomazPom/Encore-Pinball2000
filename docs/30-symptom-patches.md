# 30 — Current compatibility mechanisms

Encore currently has three deliberate compatibility mechanisms. They are part
of the working emulator and are not automatic cleanup tasks.

| Mechanism | Active when | Purpose | Effect on normal cabinet use | Maintenance rule |
|---|---|---|---|---|
| Fixed PCI responder | Every run | Reports the MediaGX, PRISM/PLX and CS5530 PCI identity and fixed BAR layout expected by the firmware. | Required for boot; no known negative effect. | Keep it unless a supported ROM or cabinet trace demonstrates missing PCI behavior. |
| `mem_detect` correction | Only when the running firmware contains the exact `mov eax,0x400` signature | Changes the firmware's hard-coded 4 MiB result to the established 14 MiB usable value so XINU has enough memory. | Genuine SWE1 2.00 already reports 16 MiB and is not modified. Affected updates receive one byte correction. | Accepted permanent firmware compatibility. Keep the exact signature and one-byte scope. |
| Base-ROM DCS probe cell | Only with `--update none` | Makes the old museum/base software recognize DCS and send sound commands. | Genuine update boots never arm it. | Accepted permanent museum compatibility. Do not broaden its gate or reuse its scanner. |

## Fixed PCI responder

`qemu/p2k-pci.c` handles PCI configuration accesses on `0xCF8/0xCFC`.
It returns the fixed devices and BAR addresses used by the Pinball 2000 board.
The game then communicates with Encore's mapped PLX, flash, graphics and DCS
regions.

There is no current reason to replace this with a general-purpose QEMU PCI
topology. Such a rewrite would not improve gameplay, sound, timing or LPT
cabinet communication. Revisit it only if an actual ROM or cabinet test asks
for PCI behavior that is missing.

## Firmware `mem_detect` correction

Some firmware releases contain this function:

```text
55 89 E5 B8 00 04 00 00 C9 C3
               ^
               0x400 = 4 MiB
```

`qemu/p2k-mem-detect.c` looks for that exact ten-byte function in the relocated
running image and changes the immediate value from `0x400` to `0xE00`, meaning
14 MiB usable memory. Without it, affected releases can fail during XINU heap
initialization.

The scanner does not modify firmware with a different function. Genuine SWE1
2.00 contains `0x1000` and therefore runs unchanged. After a match—or after the
bounded scan window expires—the scanner stops.

`P2K_MEM_DETECT_PATCH=0` disables the correction for diagnostics. It is not the
recommended runtime setting for affected firmware.

Detailed signatures and per-version observations are in
[33-mem-detect.md](33-mem-detect.md).

## Museum/base-ROM DCS probe cell

`qemu/p2k-probe-cell-shim.c` exists solely for `--update none`. The old base
software checks a relocated guest cell before deciding that a DCS board is
present. Encore maintains the value expected during early boot, then changes it
when XINU installs its `clkint` handler. The game consequently follows its own
DCS initialization path and sends sound commands.

The wrapper sets `P2K_NO_AUTO_UPDATE=1` for `--update none`; that condition
**enables** this compatibility mechanism. Normal update boots do not scan or
write the probe cell.

This behavior is intentionally kept so the museum/base version has sound. See
[34-probe-cell-shim.md](34-probe-cell-shim.md) for its exact gate and staged
values.

## What is not patched

Current Encore does not:

- rewrite `nulluser` or `prnull`;
- inject scheduler entries or modify XINU timer variables;
- redirect guest Fatal handlers;
- patch DCS detection when a genuine update is selected;
- write keyboard controls into guest RAM;
- patch guest code for MediaGX instructions—those instructions are implemented
  in QEMU TCG.

Adaptive HOTLOOP is also not a guest-code patch. It raises QEMU's IRQ0 line;
the i8259, guest IDT handler, EOI and IRET path remain active.

## Project rule

Do not add a new compatibility mechanism merely because it makes a failing boot
progress. First identify the precise firmware or cabinet behavior it supplies.
Any new mechanism must have:

- a narrow activation condition;
- an observable reason;
- a way to disable or isolate it for testing;
- a clear statement of whether genuine-update cabinet runs use it.

The three mechanisms above are accepted. They should not be rewritten for
architectural style alone.

## See also

- [14-boot-recipe.md](14-boot-recipe.md)
- [20-plx-pci.md](20-plx-pci.md)
- [33-mem-detect.md](33-mem-detect.md)
- [34-probe-cell-shim.md](34-probe-cell-shim.md)
- [36-roadmap.md](36-roadmap.md)

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
