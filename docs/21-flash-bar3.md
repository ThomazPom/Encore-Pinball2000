# 21 — Flash BAR3

This doc covers the BAR3 update-flash device implemented by `qemu/p2k-bar3-flash.c`, plus the ROM-loading context from `qemu/p2k-rom.c`.  The device is an Intel 28F320J3-style command-state flash window at guest physical `0x12000000`.

> [!IMPORTANT]
> BAR3 persistence is now safe — saved images respect erase/program semantics.

## Address, size, and geometry

| Address | Name | R/W | Size | Meaning |
|---:|---|---|---:|---|
| `0x12000000` | PRISM BAR3 | R/W command MMIO | 4 MiB | Intel 28F320 update flash window. |

The BAR3 MemoryRegion is installed as `p2k.bar3-flash` with 1-, 2-, 3-, or 4-byte unaligned little-endian accesses accepted. Offsets are masked to the 4 MiB window with `P2K_BAR3_MASK`.

Erase blocks are modeled as **128 KiB** (`P2K_BAR3_ERASE_BLOCK = 0x20000`). This follows the device ID/CFI already exposed by the emulator: Intel manufacturer `0x89`, 28F320J3 device `0x16`, 32 blocks, and CFI block-size bytes `0x0200 * 256 = 0x20000`. Some PRISM notes describe 64 KiB blocks for similar parts, but the emulated 28F320J3 geometry is internally 128 KiB.

## Backing state

| Field | Initial value | Meaning |
|---|---:|---|
| `s_flash` | allocated 4 MiB | Backing array for read-array mode and programmed bytes. |
| `s_flash_loaded` | 4 MiB snapshot | Startup image after savedata/update seeding; used to suppress no-op saves. |
| `s_cmd` | `0xFF` | Current command selector. |
| `s_cmd_act` | `false` | `false` means read-array mode; `true` means status/ID/CFI/operation mode. |
| `s_status` | `0x80` | Intel status register; bit 7 ready, bit 4 program error. |
| `s_dirty` | `false` | Set only when program or erase changes a byte in `s_flash`. |

Reads in array mode return `s_flash[off]`. Reads in command mode are decoded by `s_cmd`.

## Command protocol

| Command byte | Intel operation | After this change | R/W behavior |
|---:|---|---|---|
| `0xFF` | Read Array / reset | Faithful | Leaves command-active mode; subsequent reads return backing flash bytes. |
| `0x70` | Read Status Register | Faithful | Enters status mode; reads return `s_status`. |
| `0x50` | Clear Status Register | Faithful | Clears error bits by setting status to ready (`0x80`) and stays in status mode. |
| `0x90` | Read Identifier | Faithful enough for boot | Reads word offset 0 as Intel `0x89`, word offset 1 as 28F320J3 `0x16`; other offsets return 0. |
| `0x98` | CFI Query | Faithful enough for boot | Returns the small CFI table used by firmware, including `QRY`, Intel primary table pointer, 4 MiB size, 32 blocks, and 128 KiB erase-block geometry. |
| `0x20` | Block Erase setup | Faithful | Arms erase state. No bytes change until confirm. |
| `0xD0` after `0x20` | Block Erase confirm | Faithful | Fills the addressed 128 KiB erase block with `0xFF`, sets dirty only if any byte changed, then returns ready status. |
| `0xD0` after `0x60` or other state | Lock/unlock confirm or resume | Stubbed (status-only) | Acknowledges with ready status; lock bits and suspend/resume timing are not modeled. |
| `0x40` | Program setup | Faithful | Arms single program state. The next write is data, even if its low byte equals another command. |
| `0x10` | Alternate Program setup | Faithful | Same as `0x40`. |
| data after `0x40`/`0x10` | Program data | Faithful | Programs each access byte as `old & data`: flash can only clear bits from 1 to 0. If data attempts 0 to 1 without erase, the stored bit remains 0 and status bit 4 is set. Dirty is set only when the resulting byte differs. |
| `0x60` | Lock-bit setup | Stubbed (status-only) | Enters lock command state. Confirm commands are acknowledged but no lock state is stored. |
| `0x01` after `0x60` | Lock block confirm | Stubbed (status-only) | Acknowledges ready status. |
| `0x2F` after `0x60` | Lock-down confirm | Stubbed (status-only) | Acknowledges ready status. |
| `0xE8` | Write Buffer Program | Not implemented | Reads in this mode return status if selected internally, but writes do not arm buffered programming. |
| `0xB0` | Program/Erase Suspend | Not implemented | Treated as an unrecognized write. Operations are synchronous, so there is currently nothing to suspend. |
| `0x30` | Program/Erase Resume | Not implemented | Treated as an unrecognized write. |

The model is synchronous: erase and program complete during the confirming/data write and immediately expose ready status. That is enough for the guest wait loops while preserving the physical erase-before-program invariant.

```
Read array:            s_cmd_act = false
write 0x70          -> status mode, reads return s_status
write 0x20          -> erase setup
write 0xD0 at addr  -> memset erase-block(addr) to 0xFF, status ready
write 0x40/0x10     -> program setup
write data at addr  -> s_flash[addr+i] &= data[i], status ready/error
write 0x50          -> status ready, error bits clear
write 0xFF          -> read-array mode
```

## Initial seed and update assembly

At install time the flash buffer is allocated and filled with `0xFF`. The game name defaults to `swe1` if unset.

Seed order:

1. Try `savedata/<game>.flash` and read up to 4 MiB into `s_flash` unless savedata is disabled.
2. If an explicit machine `update=` path exists, assemble that update bundle over the buffer.
3. If no saved flash was present and auto-update is not disabled, discover the newest matching bundle under `updates/` or beside the ROM directory.
4. If auto-update is disabled, leave all `0xFF` for the supported base/museum path.
5. Copy the seeded buffer into `s_flash_loaded` for the no-op save guard.

Update assembly mirrors the Williams bundle layout:

| Flash offset | Source file suffix | Rule |
|---:|---|---|
| `0x000000..0x007FFF` | `*_bootdata.rom` | Copied first, truncated to 32 KiB. |
| `0x008000..` | `*_im_flsh0.rom` | Copied after bootdata if it fits. |
| next | `*_game.rom` | Copied after `im_flsh0`. |
| next | `*_symbols.rom` | Copied last. |

The helper requires all four files in the selected directory; otherwise the update is ignored. Game aliases map to Williams game numbers `swe1 → 50069` and `rfm → 50070` for auto-discovery.

> [!WARNING]
> Partial update bundles are rejected. All four files (`*_bootdata.rom`, `*_im_flsh0.rom`, `*_game.rom`, `*_symbols.rom`) must be present or the update will not assemble.

## Persistence

BAR3 flash is seeded from `savedata/<game>.flash` at boot (if present) and written back atomically on exit via a `qemu_add_exit_notifier` callback. Previously corrupted saved images are accepted as raw 4 MiB input; normal erase/program rules apply to future guest writes.

The flush is skipped when:

1. savedata is disabled,
2. no guest operation changed flash, or
3. `s_dirty` is true but `s_flash` is bit-identical to `s_flash_loaded`.

The on-disk format remains a raw 4 MiB image. The atomic write uses `.tmp` + `rename` to avoid partial writes.

> [!TIP]
> To install an update permanently, boot with `--update <version>` once. The game will program the flash, and on exit the update is saved to `savedata/<game>.flash`. Future runs auto-load from savedata without needing `--update` again.

See [docs/09-savedata.md](09-savedata.md) for the full persistence story.

## Read granularity

`p2k_bar3_read()` assembles multi-byte reads little-endian by calling `flash_read_byte()` once per byte. This lets command modes such as Read ID and CFI work correctly even when the guest issues word or dword reads.

Command decode is byte-oriented from the low byte of a write. During program mode, however, every byte in the write access is programmed with the bit-clear-only rule.

## Failure modes the model avoids

The file comment records two observed failures when BAR3 behaves like blank RAM: status reads return `0xFF`, which the game treats as error bits, and resource walking can fall into the “Retrieve Resource” loop.

Returning ready status for command sequences remains a boot requirement. Modeling real erase/program behavior additionally prevents savedata from accumulating physically impossible bytes after update installation or bookkeeping writes.

## See also

- [13 — Memory map](13-memory-map.md)
- [15 — ROM loading](15-rom-loading.md)
- [20 — PLX / PCI configuration](20-plx-pci.md)
- [22 — SRAM BAR2](22-sram-bar2.md)
- [30 — Symptom patches](30-symptom-patches.md)
