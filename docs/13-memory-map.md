# 13 — Memory map

This is the guest-visible address map installed by the current `pinball2000`
machine. All addresses below are guest physical addresses.

## Memory

| Range | Device or data |
|---|---|
| `00000000–00ffffff` | 16 MiB system RAM |
| `00080000–00087fff` | Executable copy of the first 32 KiB of bank 0 |
| `00088000–0008801f` | Flat boot GDT |
| `000c0000–000c7fff` | Read-only option-ROM shadow |
| `000d0008–000d000f` | LAN-ROM probe response |
| `000f0000–000fffff` | BIOS shadow |
| `08000000–0b7fffff` | PLX game-ROM banks 0–3 |
| `0b800000–0bffffff` | DCS `u109/u110` ROM, when present |
| `10000000–100000ff` | PLX registers and serial interface |
| `11000000–1101ffff` | 128 KiB battery-backed SRAM |
| `11020000–11ffffff` | Remaining BAR2 window; reads return all ones |
| `12000000–123fffff` | 4 MiB update flash |
| `13000000–13ffffff` | DCS frontend |
| `14000000–17ffffff` | Game-ROM bank mirrors |
| `18000000–187fffff` | DCS ROM mirror, when present |
| `40000000–407fffff` | MediaGX register backing |
| `40008100–4000820f` | MediaGX blitter overlay |
| `40800000–40bfffff` | Framebuffer alias to RAM at `00800000` |
| `40c00000–40ffffff` | Upper MediaGX register window |
| `ff000000–ff3fffff` | High alias of game-ROM bank 0 |
| `ffff0000–ffffffff` | BIOS reset-vector mirror |

Encore starts the CPU at the protected-mode entry rather than the BIOS reset
vector.

> [!NOTE]
> The BIOS mirror remains visible to guest software. CPU execution begins from
> the bank-0 copy at `00080000`.

## I/O ports

| Port | Device |
|---|---|
| `0020–0021`, `00a0–00a1` | i8259 interrupt controllers |
| `0022–0023` | MediaGX configuration registers |
| `002e–002f` | Super I/O configuration |
| `0040–0043` | i8254 timer |
| `0060`, `0064` | Keyboard controller |
| `0061` | System-control port B |
| `0070–0071` | CMOS and real-time clock |
| `0080` | POST byte |
| `00ea–00eb` | Companion-chip configuration |
| `0138–013f` | DCS UART frontend |
| `02f8–02ff` | COM2 probe surface |
| `0378–037a` | Driver-board parallel port |
| `03f8–03ff` | COM1 and XINA console |
| `0cf8–0cff` | PCI configuration responder |

The PCI responder exposes the fixed board topology expected by the cabinet
software: MediaGX host bridge at `0:0.0`, PRISM at `0:8.0`, PLX at `0:9.0`,
and the Cyrix ISA bridge at `0:18.0`.

## Implementation notes

- Optional game banks are filled with `ff` when their chip files are absent.
- Small device regions overlay the larger RAM or sentinel regions using QEMU
  `MemoryRegion` priority.
- BAR2 contains 128 KiB of SRAM even though an unused header constant still
  names a 256 KiB size. `qemu/p2k-bars.c` is authoritative.
- BAR3 implements array, status, ID, CFI and program operations rather than
  exposing its backing bytes as plain RAM.
- BAR4 and ports `0138–013f` are two frontends to the same DCS core.

Source: `qemu/pinball2000.c`, `qemu/p2k-*.c` and
`qemu/pinball2000.h`.

Details: [architecture](10-architecture.md),
[ROM loading](15-rom-loading.md), and [display](23-mediagx-and-display.md).
