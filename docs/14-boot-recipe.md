# 14 — Boot and reset recipe

Encore does not begin at the PC reset vector and run a conventional BIOS POST.
The `pinball2000` machine constructs the board, stages the PRISM option ROM in
RAM, installs a small temporary GDT and places CPU0 directly at the protected-
mode PRISM entry point.

```text
base ROM chips u100/u101
        │ interleave into bank 0
        ├─ read-only ROM windows
        └─ first 32 KiB copied to RAM 0x00080000 on reset
                                      │
temporary GDT at 0x00088000 ──────────┤
                                      ▼
                         CS:EIP 0008:000801D9
                         ESP    0008B000, IF=0
                                      │
                                      ▼
                             PRISM → XINA → game
```

The recipe runs after every QEMU system reset, not just the first process
launch. ROM/update selection and persistent flash policy are described in
[ROM and update loading](15-rom-loading.md); the surrounding device order is
in [Architecture](10-architecture.md).

## Before reset execution

Machine construction prepares everything the first instruction may observe:

1. allocate 16 MiB of RAM and one TCG `486` CPU;
2. load mandatory bank-0 chips `u100` and `u101` into one 16 MiB interleaved
   image;
3. attempt optional game banks `u102` through `u107` and DCS chips `u109` and
   `u110`;
4. install the ROM, BIOS, PLX, savedata, update, device and diagnostic
   mappings;
5. register `p2k_post_reset()` after the device models, then let QEMU perform
   its normal reset sequence.

Bank 0 is mandatory: failure to load either lane aborts machine creation.
Missing later banks become erased `0xff` windows; missing DCS ROM disables that
source. These are asset-loading rules, not steps performed by the CPU.

The two physical 8 MiB bank-0 chips are read as 16-bit pairs. Chip 0 occupies
the first two bytes of each four-byte group and chip 1 the next two, producing
the linear 16 MiB image consumed by all bank-0 views.

## Reset-time steps

QEMU invokes registered reset callbacks after its device reset pass, so the
CPU reset cannot subsequently overwrite this final entry state.
`p2k_post_reset()` performs four narrow operations:

1. clear volatile guest-extension installation state;
2. copy bank 0 bytes `0x0000–0x7fff` into RAM at `0x00080000`;
3. write a 32-byte GDT at RAM `0x00088000`;
4. program CPU0 for the protected-mode entry below.

The copy at `0x00080000` is executable RAM. It is distinct from the read-only
option-ROM view at `0x000c0000`, even though both begin with the same 32 KiB of
bank 0.

> [!IMPORTANT]
> The high BIOS reset-vector mirror at `0xffff0000` is mapped but is not the
> initial execution path. This avoids claiming that Encore reproduces a full
> PC BIOS POST. PRISM may still call code in the low BIOS shadow later.

## Temporary GDT

The reset helper writes four descriptors and loads `GDTR` with base
`0x00088000`, limit `0x001f`:

| Selector | Descriptor | Initial use |
|---:|---|---|
| `0x0000` | null | architectural null selector |
| `0x0008` | flat 4 GiB, present 32-bit conforming readable code | initial `CS` |
| `0x0010` | flat 4 GiB, present 32-bit writable data | `DS`, `ES`, `SS`, `FS`, `GS` |
| `0x0018` | 16-bit code descriptor | PRISM transition support |

The GDT sits immediately after the staged 32 KiB option ROM. It is needed only
to establish the initial segment caches; XINA later installs its own tables,
so this address is not a permanent host-owned reservation.

## CPU0 entry state

After reset, Encore explicitly owns the following fields:

| State | Value | Meaning |
|---|---:|---|
| CPU model/count | one QEMU TCG `486` | machine-class limit and default |
| `CR0.PE` | 1 | protected mode enabled |
| `CR0.ET` | 1 | 387-compatible extension type |
| `CS` | `0x0008` | flat 32-bit code descriptor |
| `DS/ES/SS/FS/GS` | `0x0010` | flat 32-bit data descriptors |
| `EIP` | `0x000801d9` | PRISM protected-mode entry |
| `ESP` | `0x0008b000` | initial stack top |
| `EFLAGS` | `0x00000002` | reserved bit only; interrupts disabled |

The direct entry skips the option ROM's real-mode call pair around
`0x000801bf/0x000801c4`. Other general-purpose registers retain QEMU reset
state; documentation and new code must not invent values for fields the recipe
does not set.

Interrupts being disabled at the first instruction is intentional. PRISM and
XINA initialize their own interrupt tables, PIC/PIT state and eventually the
live IRQ0 handler before normal clock delivery begins. See
[CPU, PIT and IRQ0 timing](12-cpu-and-timers.md).

## ROM and BIOS views available at boot

The initial recipe depends on the staged RAM copy, while later guest code sees
several board windows:

| Address | Size | Access | Contents |
|---:|---:|---|---|
| `0x00080000` | 32 KiB | RAM | reset-time PRISM copy and initial execution |
| `0x000c0000` | 32 KiB | read-only | option-ROM view of bank 0 |
| `0x000f0000` | 64 KiB | read/write RAM shadow | `bios.bin`, or `0xff` if absent |
| `0x08000000` | 8 MiB effective | read-only | local-address compatibility view of bank 0; bank 1 overlays from `0x08800000` |
| `0x14000000` | 16 MiB | read-only | pristine BAR5 bank-0 mirror |
| `0xff000000` | 4 MiB | read-only | high bank-0 alias |
| `0xffff0000` | 64 KiB | read-only | high `bios.bin` mirror |

Additional game banks and DCS mirrors are catalogued in the
[memory map](13-memory-map.md). Overlapping low-memory ROM/shadow regions have
higher QEMU memory-region priority than the broad RAM alias.

The `0x08000000` value comes from the PLX local remap configuration; it is not
the card's CPU-visible PCI BAR. Encore keeps these local-address mirrors for
compatibility, while PCI BAR5 is the complete 64 MiB window beginning at
`0x14000000`.

The low BIOS shadow is writable because legacy guest code may patch it. If
`roms/bios.bin` is missing, both BIOS views are filled with `0xff` and QEMU
emits a warning; current game boot is expected to have the asset available.

## Reset versus persistence

A system reset restages PRISM and CPU entry state, but it is not a factory
reset. BAR2 NVRAM, BAR3 update flash and the PLX SEEPROM retain their device
state according to the selected savedata policy. The RTC also retains XINA's
relative year counter and accounts for off-time year boundaries. The read-only
base ROM images remain unchanged.

Likewise, the BT-131 eight-byte LAN identity at `0x000d0008` is a read-only
MMIO overlay installed during machine construction, not a per-reset write into
guest RAM.

> [!CAUTION]
> Do not diagnose saved game/update behavior from the CPU reset recipe alone.
> `--fresh`, `--no-savedata` and an existing BAR3 image decide what persistent
> state the guest sees; see [Persistent cabinet state](09-savedata.md).

## Verification checklist

For a boot-path change, verify all of the following rather than stopping at a
visible splash screen:

1. both mandatory bank-0 lanes loaded at their expected sizes;
2. reset-time `CS:EIP`, `ESP`, `EFLAGS`, `CR0` and `GDTR` match the table;
3. the first 32 KiB at `0x00080000` matches the start of interleaved bank 0;
4. a normal guest reaches XINA and remains running;
5. a QEMU system reset repeats the same entry recipe;
6. savedata/update behavior remains unchanged unless the change explicitly
   owns persistence.

Avoid reintroducing catch-all exception stubs or writable ROM views to hide a
bad jump. A real trap should stay visible until its CPU instruction, mapping or
entry-state cause is understood.

---

← [CPU and timers](12-cpu-and-timers.md) ·
[Documentation index](README.md) · [ROM and update loading](15-rom-loading.md)
