# 23 — MediaGX and Display

This doc covers the Cyrix MediaGX memory aperture, framebuffer alias, QEMU console renderer, GP_BLT engine, and diagnostic graphics-list watcher implemented by `qemu/p2k-gx.c`, `qemu/p2k-display.c`, `qemu/p2k-gp-blt.c`, and `qemu/p2k-gfxlist-watch.c`.

## 16 MiB MediaGX aperture

The PCI stub exposes MediaGX at `0x40000000` (`qemu/p2k-pci.c:56`, `qemu/p2k-pci.c:73-82`).  `p2k-gx.c` maps that as a 16 MiB aperture split into registers, framebuffer alias, and more registers (`qemu/p2k-gx.c:1-23`, `qemu/p2k-gx.c:34-47`).

| Address range | Region | R/W | Meaning |
|---:|---|---|---|
| `0x40000000..0x407FFFFF` | `p2k.gx.regs1` | R/W RAM | Lower 8 MiB register RAM: GP block, DC registers, BC registers (`qemu/p2k-gx.c:34-40`, `qemu/p2k-gx.c:56-64`). |
| `0x40800000..0x40BFFFFF` | `p2k.gx.fb` | R/W alias | 4 MiB framebuffer alias into system RAM at physical `0x00800000` (`qemu/p2k-gx.c:37-46`, `qemu/p2k-gx.c:66-77`). |
| `0x40C00000..0x40FFFFFF` | `p2k.gx.regs2` | R/W RAM | Upper 4 MiB register RAM, including `DC_TIMING2` at base+`0x8354` as seen by the VSYNC ticker (`qemu/p2k-gx.c:18-22`, `qemu/p2k-gx.c:79-82`). |

`BC_DRAM_TOP` at offset `0x20000` is pre-seeded to `0x007FFFFF` so the guest sees 8 MiB installed (`qemu/p2k-gx.c:42-45`, `qemu/p2k-gx.c:61-64`).  The framebuffer alias requires QEMU RAM to cover `0x800000..0xBFFFFF`; otherwise the machine exits with an error (`qemu/p2k-gx.c:66-76`).

> [!IMPORTANT]
> The framebuffer alias at `0x40800000` maps directly to system RAM at physical `0x00800000`. If RAM doesn't cover this range, the machine will refuse to start.

```
MediaGX guest MMIO                      Backing storage
0x40000000 regs1  ------------------>   RAM MemoryRegion
0x40800000 fb     ------------------>   system RAM +0x800000
0x40C00000 regs2  ------------------>   RAM MemoryRegion
```

## Framebuffer format

The guest source is 640×240 RGB555 stored in 16-bit pixels (`qemu/p2k-display.c:1-15`, `qemu/p2k-display.c:32-40`).  The display path reads from physical `0x00800000 + DC_FB_ST_OFFSET`, not by trapping writes to `0x40800000` (`qemu/p2k-display.c:4-6`, `qemu/p2k-display.c:50-64`).

> [!NOTE]
> Framebuffer is 640×240 RGB555, line-doubled to 480 output lines. Boot pitch is 1280 bytes; game pitch is 2048 bytes. The renderer reads memory directly, not through write traps.

| Field | Value | Citation |
|---|---:|---|
| Visible source width | 640 pixels | `qemu/p2k-display.c:32-35` |
| Visible source height | 240 rows | `qemu/p2k-display.c:32-35` |
| QEMU console size | 640×480 | `qemu/p2k-display.c:32-35`, `qemu/p2k-display.c:168-169` |
| Boot pitch | 1280 bytes | `qemu/p2k-display.c:8-11`, `qemu/p2k-display.c:115-120` |
| Game pitch | 2048 bytes | `qemu/p2k-display.c:8-11`, `qemu/p2k-display.c:115-120` |
| Buffer size heuristic | `0x78000` | `qemu/p2k-display.c:40`, `qemu/p2k-display.c:115-119` |
| Start-offset register | `GX_BASE + 0x8310` | `qemu/p2k-display.c:37-39`, `qemu/p2k-display.c:112-114` |

The renderer doubles every source row to produce 480 output lines and flips Y by default (`qemu/p2k-display.c:13-15`, `qemu/p2k-display.c:85-94`, `qemu/p2k-display.c:128-155`).  F2 toggles the flip through the LPT input handler (`qemu/p2k-lpt-board.c:534-539`).

## QEMU console surface

`p2k_install_display()` creates a QEMU graphic console, resizes it to 640×480, then replaces the surface (`qemu/p2k-display.c:162-185`).

| Mode | Surface format | Conversion path |
|---|---|---|
| default | QEMU default ARGB8888 | `rgb555_to_argb()` expands 5-bit channels and writes two destination rows (`qemu/p2k-display.c:66-77`, `qemu/p2k-display.c:142-150`). |
| `P2K_DISPLAY_BPP=16` / `--bpp 16` | `PIXMAN_x1r5g5b5` | Source RGB555 is masked to `0x7FFF` and copied as native 16-bit pixels (`qemu/p2k-display.c:104-110`, `qemu/p2k-display.c:134-141`, `qemu/p2k-display.c:164-180`). |

The final update is a full-console refresh with `dpy_gfx_update_full()` (`qemu/p2k-display.c:154`).

## Display-controller register walk

The display code reads `DC_FB_ST_OFFSET` (`GX_BASE+0x8310`) every refresh to find the active framebuffer (`qemu/p2k-display.c:112-121`).  The VSYNC ticker writes `DC_TIMING2` (`GX_BASE+0x8354`) about 30 times per frame, cycling active scanline values and then writing 241 for VBLANK (`qemu/p2k-vsync.c:40-47`, `qemu/p2k-vsync.c:62-79`).

| Register | Address | Writer | Reader | Meaning |
|---|---:|---|---|---|
| `DC_FB_ST_OFFSET` | `0x40008310` | guest | display update | Start offset within the 4 MiB framebuffer alias (`qemu/p2k-display.c:37-40`, `qemu/p2k-display.c:112-126`). |
| `DC_TIMING2` | `0x40008354` | QEMU VSYNC ticker | guest | Active scanline `0..240`, VBLANK pulse `241` (`qemu/p2k-vsync.c:10-14`, `qemu/p2k-vsync.c:64-75`). |

The DC register storage itself is ordinary RAM from `p2k-gx.c`; reads and writes use `address_space_*` helpers rather than MediaGX-specific callbacks (`qemu/p2k-display.c:50-64`, `qemu/p2k-vsync.c:28-30`).

## GP_BLT engine overlay

`p2k-gp-blt.c` overlays a semantic MMIO block at `GX_BASE+0x8100`, priority 1, over the otherwise RAM-backed `regs1` area (`qemu/p2k-gp-blt.c:23-31`, `qemu/p2k-gp-blt.c:204-214`).  It is device emulation, not a temporary symptom patch (`qemu/p2k-gp-blt.c:1-5`).

> [!NOTE]
> GP_BLT engine executes immediately on every trigger write. `BLT_STATUS` always returns `0x300` (idle) because blits are synchronous and complete before the guest polls status.

| Offset from `0x40008100` | Name | R/W | Meaning |
|---:|---|---|---|
| `0x000` | `DST` | W/R shadow | Packed destination: x in low 16 bits, y in high 16 bits (`qemu/p2k-gp-blt.c:16-18`, `qemu/p2k-gp-blt.c:172-176`). |
| `0x004` | `WIDTH` | W/R shadow | Width in RGB555 pixels (`qemu/p2k-gp-blt.c:17`, `qemu/p2k-gp-blt.c:177-179`). |
| `0x008` | `SRC` | W/R shadow | Packed source: x/y (`qemu/p2k-gp-blt.c:18`, `qemu/p2k-gp-blt.c:180-183`). |
| `0x100` | `RASTER_MODE` | W/R shadow | Bit 12 enables transparent blit with key `0x7C1F` (`qemu/p2k-gp-blt.c:19`, `qemu/p2k-gp-blt.c:184-187`). |
| `0x108` | `BLT_TRIGGER` | W | Executes one row copy (`qemu/p2k-gp-blt.c:20`, `qemu/p2k-gp-blt.c:188-190`). |
| `0x10C` | `BLT_STATUS` | R | Returns `0x300` idle so the guest does not spin (`qemu/p2k-gp-blt.c:21`, `qemu/p2k-gp-blt.c:129-133`). |

The copy source and destination are offsets into physical framebuffer RAM at `0x00800000`, using a 2048-byte row stride (`qemu/p2k-gp-blt.c:47-50`, `qemu/p2k-gp-blt.c:75-99`).  Transparent blits read the destination row and skip source pixels equal to `0x7C1F` (`qemu/p2k-gp-blt.c:100-113`).

## Graphics-list watch

`p2k-gfxlist-watch.c` is diagnostic and opt-in.  It arms only when `P2K_GFXLIST_WATCH` is set (`qemu/p2k-gfxlist-watch.c:20-22`, `qemu/p2k-gfxlist-watch.c:111-120`).

| Watched address | Symbol / role | Meaning |
|---:|---|---|
| `0x00343e8c` | `_gfx_driver_list` | Two 12-byte entries; expected first entry points to `gfx_mediagx` (`qemu/p2k-gfxlist-watch.c:3-18`, `qemu/p2k-gfxlist-watch.c:31-38`). |
| `0x00343788` | `gfx_driver` | Current driver pointer (`qemu/p2k-gfxlist-watch.c:34-36`, `qemu/p2k-gfxlist-watch.c:70-79`). |
| `0x0034378c` | `screen` | Allegro screen pointer (`qemu/p2k-gfxlist-watch.c:35-37`). |
| `0x00343bb0` | `scrbuf` | Screen buffer pointer (`qemu/p2k-gfxlist-watch.c:36-37`). |
| `0x002ef5e8..0x002ef678` | GX globals | Framebuffer, width, height, pixel size, scan width, base pointer (`qemu/p2k-gfxlist-watch.c:39-55`). |

A QEMU virtual-clock timer samples after 50 ms and then every 100 ms (`qemu/p2k-gfxlist-watch.c:31-32`, `qemu/p2k-gfxlist-watch.c:107-119`).  Logs are transition-based after the first tick (`qemu/p2k-gfxlist-watch.c:65-105`).

## Display implementation notes

The QEMU build uses MemoryRegions, RAM aliases, `GraphicHwOps`, and QEMU surfaces.  The hardware facts that matter are the MediaGX base, the 0x800000 framebuffer mirror, RGB555 source pixels, 640×240-to-640×480 row doubling, and the `DC_TIMING2` VBLANK convention.

> [!NOTE]
> The display renderer reads memory directly from physical RAM via `address_space_read()`. No framebuffer caching exists; every QEMU display update regenerates the visible surface from guest RAM.


## Addressing and bounds checks

The renderer bounds-checks `DC_FB_ST_OFFSET` and falls back to zero when the offset is above `0x300000` (`qemu/p2k-display.c:123-126`).

This keeps scanout inside the 4 MiB framebuffer alias even if the guest temporarily programs a bogus start address.

Rows are fetched with `address_space_read()` from the physical framebuffer mirror (`qemu/p2k-display.c:60-64`, `qemu/p2k-display.c:128-133`).

The renderer does not cache framebuffer contents; every QEMU display update regenerates the visible surface from guest RAM (`qemu/p2k-display.c:79-83`, `qemu/p2k-display.c:96-155`).

## GP shadow semantics

The GP overlay keeps a dword shadow for reads of registers that are not otherwise semantic (`qemu/p2k-gp-blt.c:68-71`).

`p2k_gp_read()` returns the shadow truncated to the requested access size (`qemu/p2k-gp-blt.c:129-141`).

`p2k_gp_write()` merges byte and word writes into the shadow before decoding known registers (`qemu/p2k-gp-blt.c:144-160`).

That means guest read-modify-write patterns still behave like the RAM-backed register area the overlay replaced.

Unknown GP writes are not fatal; they are simply shadowed and ignored by the switch statement (`qemu/p2k-gp-blt.c:172-193`).

## Transparent blit key

Transparent mode is selected by raster-mode bit 12 (`qemu/p2k-gp-blt.c:184-187`).

The transparent color key is `0x7C1F` (`qemu/p2k-gp-blt.c:47-50`).

> [!IMPORTANT]
> Transparent blit key is hardcoded to `0x7C1F` (magenta). When transparent mode is enabled, source pixels matching this value are skipped. Non-transparent mode copies all pixels directly.

When transparent mode is off, the engine copies the row bytes directly (`qemu/p2k-gp-blt.c:93-99`).

When transparent mode is on, the engine reads the destination row, replaces only non-key source pixels, and writes the merged row back (`qemu/p2k-gp-blt.c:100-113`).

## Console ownership

The display device owns one QEMU console at index 0 (`qemu/p2k-display.c:42-48`, `qemu/p2k-display.c:162-169`).

The LPT screenshot helper later looks up that same console by index 0 and serializes the active surface (`qemu/p2k-lpt-board.c:388-447`).

This is why display and LPT key handling are cross-linked even though their guest hardware devices are separate.

## Practical display pipeline

Guest software writes MediaGX registers and framebuffer pixels.

`p2k-gx.c` makes those addresses exist.

`p2k-gp-blt.c` gives the hardware blitter side effects.

`p2k-vsync.c` maintains the scanline/VBLANK register.

`p2k-display.c` finally presents the current framebuffer through QEMU's UI.

Each file owns one layer, which avoids monolithic callback designs.

> [!WARNING]
> Display-controller register order matters. Initialize the MediaGX aperture and framebuffer alias before starting the VSYNC ticker or the guest will poll uninitialized memory.


## See also

- [13 — Memory map](13-memory-map.md)
- [20 — PLX / PCI configuration](20-plx-pci.md)
- [24 — VSYNC](24-vsync.md)
- [26 — LPT board](26-lpt-board.md)
- [31 — MediaGX gate](31-mediagx-gate.md)
