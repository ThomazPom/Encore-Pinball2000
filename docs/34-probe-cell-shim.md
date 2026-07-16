# 34 — DCS probe-cell shim for `--update none`

What this doc covers: the compatibility shim that keeps SWE1 base/no-update boots on the natural BAR4 DCS path by maintaining the DCS probe cell until XINU installs `clkint`.

> [!IMPORTANT]
> This is accepted permanent museum/base-ROM compatibility. It is intentionally
> gated to `--update none`, where it enables sound on the old base software.
> Genuine update boots never arm it. It is not cabinet-roadmap work.

## Status

Keep the shim. Its narrow gate and exact purpose are the contract: preserve
sound for `--update none` without affecting genuine-update operation.

## Gate

> [!TIP]
> `P2K_NO_AUTO_UPDATE=1` is the gate that **enables** this shim; the wrapper sets
> it for `--update none`. Normal `--update auto` boots bypass it.

The installer returns immediately unless `P2K_NO_AUTO_UPDATE` is set
(`qemu/p2k-probe-cell-shim.c:305-311`). The header says the wrapper sets that
for `--update none`, and that normal update boots get no scan, no scribble, and
no timer (`qemu/p2k-probe-cell-shim.c:9-17`).

## Scan and cell discovery

The shim scans `0x00100000..0x00400000` for the literal string
`pci_watchdog_bone(): the watchdog has expired` (`qemu/p2k-probe-cell-shim.c:18-27`,
`qemu/p2k-probe-cell-shim.c:76-79`, `qemu/p2k-probe-cell-shim.c:139-153`). It
then walks nearby code to find:

```text
81 3D <addr32> FF FF 00 00    ; cmp dword [probe_cell], 0x0000FFFF
```

The source follows the string push, surrounding call, optional nested call, and
CMP pattern (`qemu/p2k-probe-cell-shim.c:156-239`).

## Staged values

> [!NOTE]
> **Two-phase polarity flip:** Pre-XINU, the cell holds `0x0000FFFF` (sentinel expected by early boot). Post-XINU, it flips to `0x00000000` so `dcs_probe()` sees `cell != 0xFFFF`, returns DCS present, and the game uses BAR4 for audio.

| Phase | Value | Reason |
|---|---:|---|
| Pre-XINU | `0x0000FFFF` | Early boot expects the sentinel (`qemu/p2k-probe-cell-shim.c:28-31`, `qemu/p2k-probe-cell-shim.c:80-84`). |
| Post-XINU | `0x00000000` | `dcs_probe()` sees `cell != 0xFFFF`, returns present, and the game uses BAR4 (`qemu/p2k-probe-cell-shim.c:33-38`, `qemu/p2k-probe-cell-shim.c:85-95`). |

The write happens every 50 ms once the cell is known (`qemu/p2k-probe-cell-shim.c:241-298`).

## `clkint` phase flip

> [!IMPORTANT]
> **Phase detection is IDT-based, not time-based.** The shim caches the first non-zero IDT[0x20] handler (BIOS panic stub), then flips when the live handler changes (XINU's `clkint`). This avoids the `ed03826` false-flip lesson: never gate on elapsed virtual time when you can gate on guest-observable kernel state.

The phase flip is not elapsed-time based. The shim caches the first non-zero
IDT[0x20] handler as the BIOS panic-stub value, then flips as soon as the live
handler differs (`qemu/p2k-probe-cell-shim.c:263-283`). This is the `ed03826`
lesson: gate on XINU's `clkint` install, not virtual time.

The transition is from the panic-stub handler to XINU `clkint`; it does not use
UART text or an elapsed-time delay.

## Why it exists

Without the staged flip, the no-update base image can leave the cell at `0xFFFF`
forever; DCS then appears absent and audio never starts.
With the flip, SWE1 base emits the same BAR4 ACE1-wrapped stream as the update
boot, including the boot dong and sample commands.

## Scope and tradeoff

> [!WARNING]
> **This is a deliberate host write into guest data.** The probe cell is game
> BSS. The tradeoff is accepted for museum/base-ROM sound because the code is
> inactive on genuine-update boots.

The file admits the cell is game BSS that real PCI watchdog/DCS hardware would
update (`qemu/p2k-probe-cell-shim.c:40-48`). It is well-gated, but it is still a
host write into guest data.

## Maintenance rule

Do not broaden the gate, reuse the scanner for other patches, or make normal
update boots depend on it. Otherwise leave the accepted behavior alone.

## See also

- [docs/25-dcs-sound.md](25-dcs-sound.md)
- [docs/30-symptom-patches.md](30-symptom-patches.md)
- [docs/36-roadmap.md](36-roadmap.md)
