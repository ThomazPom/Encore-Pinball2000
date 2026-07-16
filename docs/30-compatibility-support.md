# 30 — Compatibility support

Three current mechanisms supply behavior expected by supported software.

## Fixed PCI topology

`qemu/p2k-pci.c` answers configuration accesses at ports `0cf8/0cfc`. It
reports the MediaGX host bridge, PRISM, PLX and ISA bridge with the fixed BAR
addresses used by Encore's mapped devices.

## Memory-size correction

Some updates contain this `mem_detect` function, which returns 4 MiB:

```text
55 89 e5 b8 00 04 00 00 c9 c3
```

`qemu/p2k-mem-detect.c` searches the bounded relocated-game region for that
exact function and changes its immediate value to 14 MiB. A different function
body does not match. `P2K_MEM_DETECT_PATCH=0` disables the correction for a
diagnostic run.

## Base-ROM DCS detection

The base software checks a relocated cell before initializing DCS.
`qemu/p2k-probe-cell-shim.c` supplies the staged values used by that check. The
helper activates when `--update none` sets `P2K_NO_AUTO_UPDATE=1`.

> [!NOTE]
> Runs with a selected or automatically discovered update do not activate the
> base-ROM probe-cell helper.

Details: [architecture](10-architecture.md) and
[boot path](14-boot-recipe.md).
