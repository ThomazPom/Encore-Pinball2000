# 30 — Compatibility support

Three current mechanisms supply behavior expected by supported software.

## Fixed PCI topology

`qemu/p2k-pci.c` answers configuration accesses at ports `0cf8/0cfc`. It
reports the MediaGX host bridge, PRISM, PLX and ISA bridge with the fixed BAR
addresses used by Encore's mapped devices.

## Optional memory-size override

Supported updates contain this `sizmem()` function, which returns 4 MiB:

```text
55 89 e5 b8 00 04 00 00 c9 c3
```

Encore normally leaves that native 4 MiB result unchanged. Set
`P2K_MEM_DETECT_PATCH=1` to make `qemu/p2k-mem-detect.c` search the bounded
relocated-game region for that exact function and change its immediate value to
14 MiB. A different function body does not match. The override remains
available for diagnosing an older update that wedges with the native limit.

> [!NOTE]
> The override is disabled by default. Current SWE1 update boots, including
> 2.10, reach the running game with the native 4 MiB result.

## Base-ROM DCS detection

Base-ROM boots use the same emulated DCS devices as update boots. No periodic
guest-memory patch or compatibility timer is required.

Details: [architecture](10-architecture.md) and
[boot path](14-boot-recipe.md).
