# 14 — Boot path

Encore does not run a PC BIOS POST. It reconstructs the protected-mode state
that the PRISM option ROM expects after real-machine discovery, then executes
the Williams loader and game normally from that point.

## Reset sequence

`qemu/p2k-boot.c` performs the reset recipe:

1. Copy the first 32 KiB of bank0 to guest RAM at `0x80000`.
2. Write a flat temporary GDT at `0x88000`.
3. Load protected-mode segment descriptors and control registers.
4. Initialize the stack and general registers required by the PRISM entry.
5. Set EIP to `0x801d9`.
6. Resume QEMU's normal CPU execution.

The entry deliberately skips BIOS reset, option-ROM enumeration and the
real-mode transition. Adding those stages is not required for Encore's goal.

## What happens next

The guest validates update boot data when present, starts XINU/XINA, initializes
the display, DCS and cabinet services, then reaches the serial prompt and attract
mode. QEMU owns subsequent instruction execution and interrupt delivery.

## Related compatibility behavior

- MediaGX-specific instructions are decoded by Encore's gated TCG helpers.
- Firmware with the 4 MiB `mem_detect` constant receives the signature-gated
  one-byte correction before heap initialization.
- Only `--update none` enables base-ROM DCS probe-cell maintenance.

See [10 — Architecture](10-architecture.md), [13 — Memory map](13-memory-map.md),
[15 — ROM loading](15-rom-loading.md), and
[30 — Compatibility mechanisms](30-compatibility-support.md).

---

← [Documentation index](README.md) · [Project README](../README.md)
