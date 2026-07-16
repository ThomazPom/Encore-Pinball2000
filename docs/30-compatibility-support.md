# 30 — Compatibility support

Encore contains three narrow mechanisms required by supported software. They
are working parts of the emulator, not items scheduled for removal.

## Fixed PCI topology

`qemu/p2k-pci.c` answers configuration accesses at ports `0cf8/0cfc` with the
MediaGX, PRISM, PLX and ISA-bridge identities and BAR addresses expected by the
game. The mapped Encore devices implement the behavior behind those addresses.

A generic QEMU PCI bus would add complexity without improving the cabinet. The
fixed responder should change only when a supported ROM or physical trace
requires PCI behavior it does not provide.

## Memory-size correction

Some updates contain a ten-byte `mem_detect` function that reports only 4 MiB:

```text
55 89 e5 b8 00 04 00 00 c9 c3
```

`qemu/p2k-mem-detect.c` searches only the bounded relocated-game region for
that exact function and changes the immediate value to 14 MiB. Other function
bodies are untouched; software already reporting the correct size is not
modified. Set `P2K_MEM_DETECT_PATCH=0` only to isolate this behavior during a
diagnostic run.

## Base-ROM DCS detection

The software started by `--update none` checks a relocated cell before using
the DCS board. `qemu/p2k-probe-cell-shim.c` supplies its expected staged values
during boot, allowing that software to initialize sound normally.

This mechanism is enabled only when automatic update loading is explicitly
disabled. Runs with a selected or discovered update never arm its scanner.

## Boundary

These mechanisms do not replace XINU scheduling, inject guest functions,
redirect fatal handlers, or write controls into guest RAM. MediaGX-specific
instructions are implemented in QEMU TCG. Adaptive HOTLOOP raises the emulated
IRQ0 input and leaves interrupt entry, acknowledgement and return to the guest.

New compatibility behavior should be added only for a reproduced software or
cabinet requirement, with a narrow activation condition and an isolation
switch suitable for testing.

## See also

- [10-architecture.md](10-architecture.md)
- [12-cpu-and-timers.md](12-cpu-and-timers.md)
- [14-boot-recipe.md](14-boot-recipe.md)
