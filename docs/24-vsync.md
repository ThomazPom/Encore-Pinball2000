# 24 — VSYNC

This doc covers the QEMU VSYNC ticker implemented by `qemu/p2k-vsync.c`.  The ticker keeps the guest-visible BAR2 and MediaGX timing latches fresh; it does not call guest interrupt handlers directly.

## What the guest polls

| Address | Name | Writer | Meaning |
|---:|---|---|---|
| `0x11000004` | BAR2 SRAM vsync flag | QEMU VSYNC ticker; guest clears | 32-bit little-endian “vsync seen” flag (`qemu/p2k-vsync.c:7-9`, `qemu/p2k-vsync.c:62-69`). |
| `0x40008354` | MediaGX `DC_TIMING2` | QEMU VSYNC ticker | Active scanline counter `0..240`, VBLANK pulse `241` (`qemu/p2k-vsync.c:10-14`, `qemu/p2k-vsync.c:40-47`). |

The BAR2 address comes from `P2K_BAR2_BASE = 0x11000000`; the display register is `P2K_GX_BASE + P2K_DC_TIMING2_OFF` (`qemu/p2k-vsync.c:40-43`).

## Timer cadence

| Constant | Value | Meaning |
|---|---:|---|
| `P2K_VSYNC_PERIOD_NS` | `17,500,000` ns | One frame period, approximately 57 Hz (`qemu/p2k-vsync.c:44`). |
| `P2K_SCANLINE_TICKS` | `30` | Number of subtick updates per frame (`qemu/p2k-vsync.c:45`). |
| `P2K_SUBTICK_NS` | period / 30 | QEMUTimer re-arm interval (`qemu/p2k-vsync.c:46`, `qemu/p2k-vsync.c:77-79`). |
| `p2k_scanline` | increments by 8 | Synthetic line counter used for `DC_TIMING2` (`qemu/p2k-vsync.c:48-49`, `qemu/p2k-vsync.c:62-75`). |

`p2k_install_vsync()` allocates a QEMU virtual-clock nanosecond timer and arms the first subtick (`qemu/p2k-vsync.c:81-90`).  The callback always re-arms itself on `QEMU_CLOCK_VIRTUAL` (`qemu/p2k-vsync.c:77-86`).

> [!NOTE]
> The VSYNC ticker runs at approximately 57 Hz (17.5 ms period) with 30 subticks per frame. Each subtick advances the scanline counter by 8 until reaching the VBLANK pulse at 241.

```
subtick:
  p2k_scanline += 8
  if p2k_scanline >= 241:
      BAR2[4..7] = 1
      DC_TIMING2 = 241
      p2k_scanline = 0
  else:
      DC_TIMING2 = p2k_scanline
```

## End-of-frame pulse

When the counter reaches 241, the ticker writes two dwords through `cpu_physical_memory_write()` (`qemu/p2k-vsync.c:51-60`, `qemu/p2k-vsync.c:62-69`):

1. `BAR2 + 4 = 1`
2. `GX_BASE + 0x8354 = 241`

> [!IMPORTANT]
> The VSYNC ticker must be installed *after* BAR2 SRAM and MediaGX display regions are mapped. Otherwise physical-memory writes to `0x11000004` and `0x40008354` will fail silently.

Using physical-memory writes preserves QEMU dirty tracking and the current MemoryRegion mapping (`qemu/p2k-vsync.c:28-30`, `qemu/p2k-vsync.c:51-60`).

Active-scan subticks write only the `DC_TIMING2` value and intentionally leave the BAR2 latch alone (`qemu/p2k-vsync.c:70-75`).  The code comment says the flag “stays 0 here,” but the function does not write zero; it relies on the guest to clear the BAR2 flag after observing it.

## Why approximately 57 Hz

The period is 17.5 ms, which is about 57 Hz (`qemu/p2k-vsync.c:16-18`, `qemu/p2k-vsync.c:44`).  This preserves the hardware-side timing fact: Pinball 2000 timing behaved best at an NTSC-ish half-rate cadence closer to 57 Hz than exact 60 Hz.  The QEMU implementation keeps only that cadence (`qemu/p2k-vsync.c:16-18`, `qemu/p2k-vsync.c:20-26`).

> [!WARNING]
> If the VSYNC period is changed significantly from 57 Hz, the guest display loop may desync. The 17.5 ms period is hardware-validated, not arbitrary.

## Relationship to BAR2 and display

BAR2 owns the SRAM location and all-ones sentinel behavior; VSYNC only writes the latch at offset 4.  See [22 — SRAM BAR2](22-sram-bar2.md).

MediaGX owns the 16 MiB aperture and the display renderer; VSYNC only walks `DC_TIMING2`.  See [23 — MediaGX and display](23-mediagx-and-display.md).


## Register write helper

`p2k_vsync_write_dword()` explicitly packs a 32-bit value into little-endian bytes before writing physical memory (`qemu/p2k-vsync.c:51-60`).

That keeps the BAR2 flag and `DC_TIMING2` value independent of host endianness.

The helper writes exactly four bytes each time (`qemu/p2k-vsync.c:51-60`).

## Active-scan values

During active scanout, the synthetic line counter advances by 8 (`qemu/p2k-vsync.c:62-65`).

The guest sees values below 241 until the end-of-frame pulse (`qemu/p2k-vsync.c:70-75`).

At or above 241, QEMU emits the special VBLANK value and resets the internal counter (`qemu/p2k-vsync.c:64-69`).

This preserves the old hardware-side convention that `241` means vertical blank for a 480-line doubled display.

## No interrupt injection here

The ticker does not inject a PIC interrupt and does not call any guest ISR.

It only updates two memory-mapped/polled locations.

Guest code is responsible for polling, clearing the BAR2 flag, and running its own display callbacks.

That is a deliberate QEMU simplification: polling-based notification rather than interrupt injection.

> [!NOTE]
> The ticker updates memory-mapped registers only. No PIC interrupts are injected. Guest code must poll `BAR2+4` and `DC_TIMING2` to detect frame boundaries.


## See also

- [12 — CPU and timers](12-cpu-and-timers.md)
- [13 — Memory map](13-memory-map.md)
- [22 — SRAM BAR2](22-sram-bar2.md)
- [23 — MediaGX and display](23-mediagx-and-display.md)
