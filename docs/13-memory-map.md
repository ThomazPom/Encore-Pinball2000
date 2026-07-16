# 13 — Memory Map

What this doc covers: the guest physical memory and I/O address map installed by the QEMU `pinball2000` machine. The table is derived from `qemu/pinball2000.h` constants and every `MemoryRegion` / I/O registration in `qemu/*.c`.

QEMU `MemoryRegion` priority matters because Encore overlays small device or ROM
regions on larger RAM and sentinel regions. This page documents the current
machine exactly as installed by `qemu/pinball2000.c` and `qemu/p2k-*.c`.

## System memory map

| Guest physical range | Owner / backing | Notes | Source |
|---|---|---|---|
| `0x00000000-0x00FFFFFF` | Low RAM alias `p2k.ram-alias` | QEMU machine RAM, 16 MiB by default. | `qemu/pinball2000.c:64-70`, `qemu/pinball2000.h:137` |
| `0x00080000-0x00087FFF` | Post-reset PRISM option-ROM copy in RAM | First 32 KiB of bank0 copied here on reset; CPU jumps into it at `0x801D9`. | `qemu/p2k-boot.c:81-83`, `qemu/pinball2000.h:131-134` |
| `0x00088000-0x0008801F` | Temporary flat GDT in RAM | Current QEMU ground truth. This is **not** `0x1000`; see warning below. | `qemu/p2k-boot.c:21-44`, `qemu/p2k-boot.c:54-60`, `qemu/p2k-boot.c:104-105` |

> [!WARNING]
> The current boot GDT is at `0x88000`, not `0x1000`. The historical prototype used `0x1000` but that location exposed a wild-jump-into-GDT bug once compatibility patches were removed.
| `0x000C0000-0x000C7FFF` | `p2k.optrom` ROM overlay | Read-only option-ROM shadow, first 32 KiB of bank0. | `qemu/p2k-plx9054.c:33`, `qemu/p2k-plx9054.c:121-125` |
| `0x000D0008-0x000D000F` | `p2k.nic-dseg-shadow` MMIO overlay | 8-byte read-only SMC8216T LAN-ROM shadow over RAM. | `qemu/p2k-nic-dseg.c:41-42`, `qemu/p2k-nic-dseg.c:84-88` |
| `0x000F0000-0x000FFFFF` | `p2k.bios-shadow` RAM overlay | 64 KiB BIOS shadow loaded from `roms/bios.bin` or `0xFF`. | `qemu/p2k-plx9054.c:34-35`, `qemu/p2k-plx9054.c:71-98` |
| `0x08000000-0x08FFFFFF` | `p2k.plx-bank0` ROM | Full 16 MiB bank0 at PLX LAS3BA base. | `qemu/p2k-plx9054.c:36`, `qemu/p2k-plx9054.c:131-134` |
| `0x08800000-0x097FFFFF` | `p2k.plx-bank1` ROM / `0xFF` fill | Full 16 MiB optional bank1 from `u102/u103`; overlaps bank0 by address because bases are 8 MiB apart. | `qemu/p2k-plx9054.c:37`, `qemu/p2k-plx9054.c:136-141` |
| `0x09800000-0x0A7FFFFF` | `p2k.plx-bank2` ROM / `0xFF` fill | Full 16 MiB optional bank2 from `u104/u105`. | `qemu/p2k-plx9054.c:38`, `qemu/p2k-plx9054.c:142-144` |
| `0x0A800000-0x0B7FFFFF` | `p2k.plx-bank3` ROM / `0xFF` fill | Full 16 MiB optional bank3 from `u106/u107`. | `qemu/p2k-plx9054.c:39`, `qemu/p2k-plx9054.c:145-147` |
| `0x0B800000-0x0BFFFFFF` | `p2k.dcs-cs3` ROM | 8 MiB DCS sound ROM at PLX CS3 if `u109/u110` are present. | `qemu/p2k-plx9054.c:40`, `qemu/p2k-plx9054.c:149-156` |
| `0x10000000-0x100000FF` | `p2k.plx-regs` MMIO | PLX BAR0 register file, SEEPROM, DCS serial bit-bang. | `qemu/p2k-plx-regs.c:59-60`, `qemu/p2k-plx-regs.c:420-429` |
| `0x11000000-0x11FFFFFF` | `p2k.bar2-sentinel` MMIO | Whole 16 MiB BAR2 window returns all ones above SRAM. | `qemu/p2k-bars.c:35-38`, `qemu/p2k-bars.c:81-87` |
| `0x11000000-0x1101FFFF` | `p2k.bar2-sram` RAM overlay | 128 KiB battery-backed SRAM seeded from `savedata/<game>.nvram2`. | `qemu/p2k-bars.c:35-37`, `qemu/p2k-bars.c:59-75`, `qemu/p2k-bars.c:88-96` |
| `0x12000000-0x123FFFFF` | `p2k.bar3-flash` MMIO | 4 MiB Intel 28F320-style update flash. | `qemu/p2k-bar3-flash.c:26-28`, `qemu/p2k-bar3-flash.c:310-365` |
| `0x13000000-0x13FFFFFF` | `p2k.bar4-dcs` MMIO | 16 MiB DCS audio board frontend. | `qemu/p2k-dcs.c:25-27`, `qemu/p2k-dcs.c:95-109` |
| `0x14000000-0x14FFFFFF` | `p2k.bar5-bank0` ROM | Pristine 16 MiB mirror of bank0. | `qemu/p2k-plx9054.c:41`, `qemu/p2k-plx9054.c:159-162` |
| `0x15000000-0x15FFFFFF` | `p2k.bar5-bank1` ROM / `0xFF` fill | Optional bank1 BAR5 mirror. | `qemu/p2k-plx9054.c:42`, `qemu/p2k-plx9054.c:163-165` |
| `0x16000000-0x16FFFFFF` | `p2k.bar5-bank2` ROM / `0xFF` fill | Optional bank2 BAR5 mirror. | `qemu/p2k-plx9054.c:43`, `qemu/p2k-plx9054.c:166-168` |
| `0x17000000-0x17FFFFFF` | `p2k.bar5-bank3` ROM / `0xFF` fill | Optional bank3 BAR5 mirror. | `qemu/p2k-plx9054.c:44`, `qemu/p2k-plx9054.c:169-171` |
| `0x18000000-0x187FFFFF` | `p2k.dcs-rombar` ROM | 8 MiB DCS ROM BAR mirror if sound ROM present. | `qemu/p2k-plx9054.c:45`, `qemu/p2k-plx9054.c:149-156` |
| `0x40000000-0x407FFFFF` | `p2k.gx.regs1` RAM | MediaGX GP/DC/BC register backing; seeds `BC_DRAM_TOP`. | `qemu/p2k-gx.c:34-43`, `qemu/p2k-gx.c:56-65` |
| `0x40008100-0x4000820F` | `p2k.gx.gp` MMIO overlay | MediaGX GP BLT engine overlay on regs1. | `qemu/p2k-gp-blt.c:43-58`, `qemu/p2k-gp-blt.c:204-215` |
| `0x40800000-0x40BFFFFF` | `p2k.gx.fb` RAM alias | MediaGX framebuffer alias to physical RAM `0x00800000`. | `qemu/p2k-gx.c:37-45`, `qemu/p2k-gx.c:66-77` |
| `0x40C00000-0x40FFFFFF` | `p2k.gx.regs2` RAM | Upper MediaGX register window. | `qemu/p2k-gx.c:39-40`, `qemu/p2k-gx.c:79-82` |
| `0xFF000000-0xFF3FFFFF` | `p2k.bank0-alias` ROM | 4 MiB high alias of bank0. | `qemu/p2k-plx9054.c:46-47`, `qemu/p2k-plx9054.c:173-176` |
| `0xFFFF0000-0xFFFFFFFF` | `p2k.bios-reset` ROM | 64 KiB high reset-vector mirror of BIOS shadow; not the normal boot path. | `qemu/p2k-plx9054.c:100-109` |

> [!IMPORTANT]
> The QEMU machine does not use the high reset vector at `0xFFFF0000`. The boot recipe writes protected-mode CPU state directly. The high reset mirror exists for compatibility only.


> ⚠️ `pinball2000.h` still contains `P2K_BAR2_SIZE = 0x00040000` (256 KiB) (`qemu/pinball2000.h:67-69`), but the QEMU BAR2 implementation maps 128 KiB SRAM plus a 16 MiB sentinel (`qemu/p2k-bars.c:35-38`, `qemu/p2k-bars.c:88-96`). Device docs should treat the implementation as ground truth and call out the constant drift if it matters.

## I/O port map

| I/O range | Owner | Behavior | Source |
|---|---|---|---|
| `0x0020/0x0021` | QEMU i8259 master | PIC command/data. | `qemu/pinball2000.h:72-78`, `qemu/pinball2000.c:80-82` |
| `0x0022/0x0023` | `p2k.cyrix.ccr` | Cyrix MediaGX CCR index/data. | `qemu/p2k-cyrix-ccr.c:1-10`, `qemu/p2k-cyrix-ccr.c:130-141` |
| `0x002E/0x002F` | `p2k.superio` | Winbond W83977EF SuperIO config ID. | `qemu/p2k-superio.c:1-14`, `qemu/p2k-superio.c:100-107` |
| `0x0040-0x0043` | QEMU i8254 | PIT channels and mode register. | `qemu/pinball2000.h:75-76`, `qemu/pinball2000.c:84` |
| `0x0060` | `p2k.i8042-data` | Keyboard-controller data. | `qemu/p2k-isa-stubs.c:35-86`, `qemu/p2k-isa-stubs.c:518` |
| `0x0061` | `p2k.port61` | System-control port B toggle in ISA stubs. | `qemu/p2k-isa-stubs.c:88-112`, `qemu/p2k-isa-stubs.c:519` |
| `0x0064` | `p2k.i8042-status` | Keyboard-controller status/command. | `qemu/p2k-isa-stubs.c:35-86`, `qemu/p2k-isa-stubs.c:520` |
| `0x0070/0x0071` | `p2k.cmos` | CMOS index/data with live BCD time. | `qemu/p2k-isa-stubs.c:114-162`, `qemu/p2k-isa-stubs.c:521` |
| `0x0080` | `p2k.post` | POST diagnostic byte. | `qemu/p2k-isa-stubs.c:164-184`, `qemu/p2k-isa-stubs.c:522` |
| `0x00A0/0x00A1` | QEMU i8259 slave | PIC command/data. | `qemu/pinball2000.h:77-78`, `qemu/pinball2000.c:80-82` |
| `0x00EA/0x00EB` | `p2k.cc5530` | Cyrix CC5530 config ID/revision. | `qemu/p2k-superio.c:10-14`, `qemu/p2k-superio.c:109-112` |
| `0x0138-0x013F` | `p2k-dcs-uart` | DCS UART/16550 overlay; command word at `0x13C`, flag at `0x13E`. | `qemu/p2k-dcs-uart.c:1-18`, `qemu/p2k-dcs-uart.c:240-254` |
| `0x02F8-0x02FF` | `p2k.com2` | COM2 16550-compatible probe surface and internal loopback. | `qemu/p2k-isa-stubs.c` |
| `0x0378-0x037A` | `p2k.lpt-board` | Parallel driver-board DATA/STATUS/CTRL. | `qemu/p2k-lpt-board.c:1-30`, `qemu/p2k-lpt-board.c:616-651` |
| `0x03F8-0x03FF` | `p2k.com1` | COM1 16550 stub plus chardev bridge and IRQ4. | `qemu/p2k-isa-stubs.c:186-203`, `qemu/p2k-isa-stubs.c:498-523` |
| `0x0CF8-0x0CFB` | `p2k.pci-addr` | Temporary PCI config address latch. | `qemu/p2k-pci.c:134-166`, `qemu/p2k-pci.c:202-210` |
| `0x0CFC-0x0CFF` | `p2k.pci-data` | Temporary PCI config data window. | `qemu/p2k-pci.c:168-200`, `qemu/p2k-pci.c:211-214` |

## PCI-visible identities

The CF8/CFC responder exposes the board identities PRISM scans for, but it is not a real QEMU PCI bus yet (`qemu/p2k-pci.c:1-31`).

| Bus/dev/fn | Device | Vendor:Device | Class/rev | Source |
|---|---|---|---|---|
| `0:0.0` | Cyrix MediaGX host bridge | `1078:0001` | `060000:00` | `qemu/p2k-pci.c:73-84` |
| `0:8.0` | WMS PRISM | `146E:0001` | `030000:02` | `qemu/p2k-pci.c:86-102` |
| `0:9.0` | PLX 9050 raw face | `10B5:9050` | `068000:01` | `qemu/p2k-pci.c:104-119` |
| `0:18.0` | Cyrix Cx5520 ISA bridge | `1078:0002` | `060100:00` | `qemu/p2k-pci.c:121-127` |


## Detailed region notes

### Low RAM and boot overlays

* The 16 MiB RAM alias is created from QEMU's machine RAM, not a custom malloc (`qemu/pinball2000.c:67-70`).
* The reset hook writes the option-ROM execution copy into RAM at `0x80000` on every reset (`qemu/p2k-boot.c:81-83`).
* The option-ROM shadow at `0xC0000` is a separate read-only ROM overlay with priority over RAM (`qemu/p2k-plx9054.c:49-67`, `qemu/p2k-plx9054.c:121-125`).
* The BIOS shadow at `0xF0000` is writable RAM initialized from `roms/bios.bin` if present (`qemu/p2k-plx9054.c:71-98`).
* The high reset mirror at `0xFFFF0000` is ROM and exists for compatibility, not normal execution (`qemu/p2k-plx9054.c:100-109`).

### PLX and PRISM windows

| Window group | Notes |
|---|---|
| PLX banks | `p2k_map_rom()` fills missing optional banks with `0xFF` before copying any source bytes (`qemu/p2k-plx9054.c:49-67`, `qemu/p2k-plx9054.c:136-147`). |
| BAR5 mirrors | Bank mirrors are mapped separately from PLX bank windows to preserve the addresses PRISM probes (`qemu/p2k-plx9054.c:159-171`). |
| Bank0 high alias | The 4 MiB alias at `0xFF000000` is a BT-108 compatibility mirror (`qemu/p2k-plx9054.c:173-176`). |
| DCS ROM | DCS ROM windows are only created if `s->dcs_rom` was loaded (`qemu/p2k-plx9054.c:149-157`). |

### BAR2 SRAM layout

| Offset/range | Behavior | Source |
|---|---|---|
| `+0x00000-0x1FFFF` | RAM-backed SRAM loaded from `.nvram2`. | `qemu/p2k-bars.c:59-96` |
| `+0x00004` | VSYNC writes dword `1` at end of frame. | `qemu/p2k-vsync.c:62-69` |
| `+0x20000-0xFFFFFF` | Sentinel MMIO returns all ones and drops writes. | `qemu/p2k-bars.c:39-57`, `qemu/p2k-bars.c:81-87` |

### BAR3 flash commands

The BAR3 window is MMIO, not direct RAM. Reads dispatch through command mode: array reads return `s_flash`, `0x70` returns status, `0x90` returns Intel/device ID, and `0x98` returns CFI bytes (`qemu/p2k-bar3-flash.c:35-78`). Writes implement reset/read-status/read-ID/CFI/program command transitions and update the backing flash buffer for program commands (`qemu/p2k-bar3-flash.c:89-125`).

### BAR4 and DCS UART

BAR4 MMIO and the `0x138-0x13F` UART overlay are two frontends over one DCS core. BAR4 offset 0 carries echo/response/command by access size and offset 2 carries the flag byte (`qemu/p2k-dcs.c:28-77`). The UART view uses word access at port `0x13C` for commands/responses and port `0x13E` for the flag byte, while byte accesses emulate enough 16550 state for probes (`qemu/p2k-dcs-uart.c:86-145`).

### MediaGX display registers

| Address | Role | Source |
|---|---|---|
| `0x40008100` | GP packed destination register. | `qemu/p2k-gp-blt.c:52-58`, `qemu/p2k-gp-blt.c:172-190` |
| `0x40008104` | GP width in pixels. | `qemu/p2k-gp-blt.c:52-58`, `qemu/p2k-gp-blt.c:177-179` |
| `0x40008108` | GP packed source register. | `qemu/p2k-gp-blt.c:52-58`, `qemu/p2k-gp-blt.c:180-183` |
| `0x40008200` | GP raster mode, including transparent-blit bit. | `qemu/p2k-gp-blt.c:56`, `qemu/p2k-gp-blt.c:184-187` |
| `0x40008208` | GP trigger; executes one row copy. | `qemu/p2k-gp-blt.c:57`, `qemu/p2k-gp-blt.c:188-190` |
| `0x4000820C` | GP status; returns idle `0x300`. | `qemu/p2k-gp-blt.c:58`, `qemu/p2k-gp-blt.c:129-133` |
| `0x40008310` | DC framebuffer start offset read by display. | `qemu/p2k-display.c:37-40`, `qemu/p2k-display.c:112-121` |
| `0x40008354` | DC_TIMING2 written by VSYNC. | `qemu/p2k-vsync.c:40-47`, `qemu/p2k-vsync.c:62-78` |

## I/O detail notes

* COM1 is both a stubbed UART and a QEMU chardev bridge; it binds to `serial_hd(0)` if one exists (`qemu/p2k-isa-stubs.c:498-516`).
* COM1 can pulse IRQ4 for RX-data and THR-empty events (`qemu/p2k-isa-stubs.c:266-285`, `qemu/p2k-isa-stubs.c:424-445`).
* SuperIO and CC5530 stubs are separate from the broader ISA stubs (`qemu/p2k-superio.c:1-14`).
* The LPT driver-board may optionally pass through to a Linux parport, but the default path is an emulated edge-detect state machine (`qemu/p2k-lpt-board.c:72-148`, `qemu/p2k-lpt-board.c:271-325`).
* The PCI config responder returns the fixed device topology expected by the
  cabinet software (`qemu/p2k-pci.c`). It is the current implementation, not a
  placeholder awaiting a generic PC chipset.

## Known map wrinkles

| Wrinkle | Why it is documented |
|---|---|
| Boot GDT | Encore places its temporary flat GDT at `0x88000` (`qemu/p2k-boot.c:21-44`). |
| BAR2 constants | The active implementation maps 128 KiB SRAM plus the remaining all-ones window (`qemu/p2k-bars.c`). |
| PLX bank overlaps | The implemented bank bases are 8 MiB apart while each mapped bank is 16 MiB (`qemu/p2k-plx9054.c:36-40`, `qemu/p2k-plx9054.c:131-147`). |
| PCI is not a QEMU PCIBus | CF8/CFC returns static IDs and BAR addresses without PCIDevice subclasses (`qemu/p2k-pci.c:23-31`). |

## Addressing convention

All ranges in this document are guest physical addresses. Because paging is not enabled by the reset recipe, early guest linear addresses match these physical addresses until the guest changes CPU state (`qemu/p2k-boot.c:100-122`). I/O ports are listed separately because QEMU dispatches them through `get_system_io()` rather than the memory address space.

## See also

* [10-architecture.md](10-architecture.md) — subsystem ownership.
* [11-machine-init.md](11-machine-init.md) — install order for these regions.
* [14-boot-recipe.md](14-boot-recipe.md) — option ROM copy and GDT use.
* [20-plx-pci.md](20-plx-pci.md), [21-flash-bar3.md](21-flash-bar3.md), [22-sram-bar2.md](22-sram-bar2.md), [23-mediagx-and-display.md](23-mediagx-and-display.md), [25-dcs-sound.md](25-dcs-sound.md), [26-lpt-board.md](26-lpt-board.md), [27-isa-stubs.md](27-isa-stubs.md) — device docs.
* [30-symptom-patches.md](30-symptom-patches.md) — accepted compatibility mechanisms.
