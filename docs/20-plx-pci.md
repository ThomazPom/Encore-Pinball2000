# 20 — PLX / PCI Configuration

This doc covers the QEMU-side PCI configuration-space stub and the PLX 9050/9054 resource windows used by the Pinball 2000 PRISM card.  It is grounded in `qemu/p2k-pci.c`, `qemu/p2k-plx9054.c`, and `qemu/p2k-plx-regs.c` at HEAD, directly from the QEMU C implementations.

> [!WARNING]
> `qemu/p2k-pci.c` is explicitly a temporary symptom patch.  See [30 — Symptom patches](30-symptom-patches.md) and the verbatim sunset criterion below.

## Temporary status: `p2k-pci.c`

`p2k-pci.c` starts with a `STATUS: TEMPORARY SYMPTOM PATCH` block (see `qemu/p2k-pci.c:1-21`).

Verbatim removal condition:

> Removal condition: delete this file in favour of a real
>   - i440fx-style PCI host bridge (or a custom Cyrix MediaGX host bridge
>     class), and
>   - real PCIDevice classes for PLX9054, MediaGX video, CS5530, and
>     PRISM (PLX9050)
> once each individual BAR consumer (p2k-plx9054.c, p2k-gx.c,
> p2k-bar3-flash.c, p2k-dcs.c) has been ported to PCIDevice subclasses.
> At that point QEMU's pci_default_read_config() handles cf8/cfc for free.

The reason is structural: the machine does not instantiate a QEMU PCI host bridge.  Instead, PRISM's PCI probe is satisfied by static config dwords returned from the legacy x86 config ports (`qemu/p2k-pci.c:23-33`).

> [!NOTE]
> This is a symptom patch, not a proper PCI bus implementation. The config-space reads return fixed addresses; BAR-size probes are intentionally ignored to maintain stable memory windows.

## Legacy PCI config I/O ports

| Port | Name | R/W | Meaning |
|---:|---|---|---|
| `0xCF8` | PCI config address | R/W | Latched config selector; byte/word writes update the corresponding bytes (`qemu/p2k-pci.c:134-158`). |
| `0xCFC` | PCI config data | R/W | Reads decode bus/dev/fn/reg from the `0xCF8` latch; writes are swallowed so BAR-size probes keep fixed addresses (`qemu/p2k-pci.c:168-192`). |

The two handlers are installed as I/O memory regions over `get_system_io()` at `0xCF8` and `0xCFC` (`qemu/p2k-pci.c:202-218`).  Access sizes from 1 to 4 bytes are accepted (`qemu/p2k-pci.c:160-166`, `qemu/p2k-pci.c:194-200`).

```
outl 0xCF8, 0x8000_0000 | bus<<16 | dev<<11 | fn<<8 | reg
inl  0xCFC  -> static dword from p2k_pci_cfg_read()
outl 0xCFC, 0xFFFF_FFFF -> ignored BAR-size probe
```

## Devices exposed by the stub

| Bus | Dev.fn | Identity | Key config dwords |
|---:|---:|---|---|
| 0 | `0.0` | Cyrix MediaGX host bridge | Vendor/device `0x1078:0x0001`; class `0x060000`; BAR-like fields at `0x10`/`0x14` return `0x40000000` (`qemu/p2k-pci.c:73-84`). |
| 0 | `8.0` | WMS PRISM / PLX wrapper | Vendor/device `0x146E:0x0001`; class display; BAR0/2/3/4/5 and ROMBAR are returned from config regs (`qemu/p2k-pci.c:86-102`). |
| 0 | `9.0` | Raw PLX 9050 face | Vendor/device `0x10B5:0x9050`; same physical BAR0; exists so `pci_probe()` also finds the unskinned PLX (`qemu/p2k-pci.c:104-119`). |
| 0 | `18.0` | Cyrix Cx5520 ISA bridge | Vendor/device `0x1078:0x0002`; class `0x060100` (`qemu/p2k-pci.c:121-127`). |

Unknown bus/function/device reads return `0xFFFFFFFF` (`qemu/p2k-pci.c:66-71`, `qemu/p2k-pci.c:129-131`).

> [!NOTE]
> All BARs are mapped at fixed addresses. BAR0 (`0x10000000`) hosts the PLX register file and 93C46 SEEPROM. BAR2–BAR5 span `0x11000000–0x14000000` with SRAM, flash, DCS, and ROM mirrors respectively.

## Guest-visible PRISM / PLX addresses

| Config register | Address | Owner in this tree | Meaning |
|---:|---:|---|---|
| MediaGX `0x10/0x14` | `0x40000000` | `p2k-gx.c` | 16 MiB Cyrix MediaGX MMIO aperture (`qemu/p2k-pci.c:56`, `qemu/p2k-gx.c:34-40`). |
| PRISM BAR0 `0x10` | `0x10000000` | `p2k-plx-regs.c` | PLX register file + 93C46 SEEPROM model (`qemu/p2k-pci.c:57`, `qemu/p2k-plx-regs.c:59-60`). |
| PRISM BAR2 `0x18` | `0x11000000` | `p2k-bars.c` | Battery-backed SRAM plus upper-window all-ones sentinel (`qemu/p2k-pci.c:58`, `qemu/p2k-bars.c:35-38`). |
| PRISM BAR3 `0x1C` | `0x12000000` | `p2k-bar3-flash.c` | Intel 28F320 update flash (`qemu/p2k-pci.c:59`, `qemu/p2k-bar3-flash.c:26-28`). |
| PRISM BAR4 `0x20` | `0x13000000` | `p2k-dcs.c` | DCS-2 BAR4 MMIO view (`qemu/p2k-pci.c:60`, `qemu/p2k-dcs.c:25-26`). |
| PRISM BAR5 `0x24` | `0x14000000` | `p2k-plx9054.c` | Read-only ROM-bank mirrors (`qemu/p2k-pci.c:61`, `qemu/p2k-plx9054.c:159-171`). |
| PRISM ROMBAR `0x30` | `0x18000000` | `p2k-plx9054.c` | DCS sound ROM mirror when DCS ROM chips are present (`qemu/p2k-pci.c:62`, `qemu/p2k-plx9054.c:149-157`). |

> [!IMPORTANT]
> The code comments in `p2k-plx9054.c` still call this a PLX9054 / ROM-window map, but the actual BAR0 region at `0x10000000` is the PLX register file installed by `p2k-plx-regs.c` (`qemu/p2k-plx9054.c:1-23`, `qemu/p2k-plx-regs.c:420-432`).

## ROM and local-bus windows from `p2k-plx9054.c`

| Address | Name | R/W | Size | Meaning |
|---:|---|---|---:|---|
| `0x000C0000` | `p2k.optrom` | R | 32 KiB | First 32 KiB of bank0 as PRISM option ROM (`qemu/p2k-plx9054.c:121-125`). |
| `0x000F0000` | `p2k.bios-shadow` | R/W | 64 KiB | BIOS shadow loaded from `roms/bios.bin`, writable because legacy code may patch it (`qemu/p2k-plx9054.c:71-99`, `qemu/p2k-plx9054.c:127-129`). |
| `0xFFFF0000` | `p2k.bios-reset` | R | 64 KiB | High reset-vector mirror of BIOS bytes (`qemu/p2k-plx9054.c:100-109`). |
| `0x08000000` | `p2k.plx-bank0` | R | 16 MiB | Full game ROM bank0 (`qemu/p2k-plx9054.c:131-134`). |
| `0x08800000` | `p2k.plx-bank1` | R | 16 MiB | Optional bank1, 0xFF-filled if absent (`qemu/p2k-plx9054.c:136-141`). |
| `0x09800000` | `p2k.plx-bank2` | R | 16 MiB | Optional bank2 (`qemu/p2k-plx9054.c:142-144`). |
| `0x0A800000` | `p2k.plx-bank3` | R | 16 MiB | Optional bank3 (`qemu/p2k-plx9054.c:145-147`). |
| `0x0B800000` | `p2k.dcs-cs3` | R | 8 MiB | DCS sound ROM at PLX CS3 when u109/u110 load (`qemu/p2k-plx9054.c:149-153`). |
| `0x14000000` | `p2k.bar5-bank0` | R | 16 MiB | Pristine bank0 mirror used by checksum paths (`qemu/p2k-plx9054.c:159-162`). |
| `0x15000000` | `p2k.bar5-bank1` | R | 16 MiB | BAR5 bank1 mirror (`qemu/p2k-plx9054.c:163-165`). |
| `0x16000000` | `p2k.bar5-bank2` | R | 16 MiB | BAR5 bank2 mirror (`qemu/p2k-plx9054.c:166-168`). |
| `0x17000000` | `p2k.bar5-bank3` | R | 16 MiB | BAR5 bank3 mirror (`qemu/p2k-plx9054.c:169-171`). |
| `0x18000000` | `p2k.dcs-rombar` | R | 8 MiB | DCS ROMBAR mirror when DCS ROM exists (`qemu/p2k-plx9054.c:154-156`). |
| `0xFF000000` | `p2k.bank0-alias` | R | 4 MiB | BT-108 high alias of bank0 (`qemu/p2k-plx9054.c:173-176`). |

Each ROM window is a QEMU ROM region filled with `0xFF` before copying the source bytes (`qemu/p2k-plx9054.c:49-68`).  Overlapping low mappings use priority 1 so they beat the 0..16 MiB RAM alias (`qemu/p2k-plx9054.c:62-66`).

> [!NOTE]
> ROM regions are read-only. The priority system ensures option ROM and BIOS shadow overlay low RAM. BIOS shadow at `0xF0000` is intentionally writable for legacy patching compatibility.

## PLX BAR0 register file

`p2k-plx-regs.c` installs a 256-byte MMIO window at `0x10000000`. It is a 64-dword backing store with semantic handling for the PLX status/control registers.

| Offset | Name | R/W | Meaning |
|---:|---|---|---|
| `0x00..0xFC` | PLX dwords | R/W | Stored in `s_plx_regs[64]`; byte/word/dword access is merged into the dword slot. |
| `0x4C` | INTCSR | R | Returned with bit 2 and bit 5 forced set: bit 2 avoids PRISM watchdog fatal, bit 5 reports PLX ready. |
| `0x50` | CNTRL | R/W | 93C46 control bits plus DCS serial cohabitation; bit 28 is forced present and bit 27 reflects EEPROM/DCS data-out. |

> [!IMPORTANT]
> INTCSR must return bit 2 set or PRISM's watchdog triggers a NonFatal. The forced bits keep both the PLX status check and the DCS watchdog-clear path functional.

## 93C46 SEEPROM model

PRISM bit-bangs the PLX CNTRL register at offset `0x50` to verify and update a 93C46 image. The model uses the standard PLX bit assignments:

| CNTRL bit | Signal | Direction | Meaning |
|---:|---|---|---|
| 24 | EESK | host → EEPROM | Serial clock. |
| 25 | EECS | host → EEPROM | Chip select. |
| 26 | EEDI | host → EEPROM | Data in. |
| 27 | EEDO | EEPROM → host | Data out, ORed with DCS serial readiness. |
| 28 | EEPRESENT | device → host | Always forced to 1. |

The EEPROM array is 64 16-bit words; unassigned words start as `0xFFFF`. Words 0..47 are preloaded with the PRISM image that passes `plx_ee_verify()`, including PLX IDs, range-register pairs, base-address pairs, CS0..CS3 bases, and CNTRL default `0x40789242`.

Opcode coverage is now the full 93C46 write command set used by this board:

| Opcode | Command | Behaviour |
|---:|---|---|
| `10` | READ | Emits a dummy zero bit, then streams 16 data bits MSB-first and auto-increments for sequential reads. |
| `01` | WRITE | Collects one 16-bit word and programs the addressed word only when the EWEN latch is set. |
| `11` | ERASE | Sets the addressed word to `0xFFFF` only when the EWEN latch is set. |
| `00 00xxxx` | EWDS | Clears the write-enable latch. This is also the reset/default state. |
| `00 01xxxx` | WRAL | Collects one 16-bit word and writes it to all 64 words only when the EWEN latch is set. |
| `00 10xxxx` | ERAL | Sets all 64 words to `0xFFFF` only when the EWEN latch is set. |
| `00 11xxxx` | EWEN | Sets the write-enable latch. |

> [!NOTE]
> The 93C46 is bit-banged through CNTRL register bits 24-27. EECS (bit 25) must be high to activate the protocol; EESK (bit 24) clocks data; EEDI/EEDO (bits 26/27) are data in/out.

> [!IMPORTANT]
> WRITE, ERASE, ERAL, and WRAL silently no-op until the guest sends EWEN. EWDS is the power-on/default state, matching the datasheet. READ is always allowed.

> [!TIP]
> SEEPROM contents are seeded from `savedata/<game>.see` (128 bytes, little-endian word order) at machine init and flushed atomically on exit only when an enabled write/erase command actually changes content. Disable writeback with `P2K_NO_SAVEDATA=1`. See [09 — Savedata](09-savedata.md).

## DCS serial sharing on CNTRL

The DCS chip and EEPROM both observe CNTRL bits 24..26, and reads of bit 27 OR the EEPROM `EEDO` with DCS audio-ready/ack (`qemu/p2k-plx-regs.c:34-38`, `qemu/p2k-plx-regs.c:206-218`).  This is hardware-side coexistence, not an emulation workaround.

The DCS serial detector watches enable, clock, and data transitions (`qemu/p2k-plx-regs.c:286-333`).  Command class 0, subtype 3 sets `audio_ready=1`, which makes later CNTRL reads expose bit 27 and lets the game stay on the BAR4 command path (`qemu/p2k-plx-regs.c:241-262`, `qemu/p2k-plx-regs.c:335-348`).

> [!IMPORTANT]
> CNTRL bit 27 is ORed between EEPROM data-out and DCS audio-ready. If DCS never asserts ready, PRISM falls back to legacy UART mode at I/O port `0x13C`.

## What is deliberately not here

There is no QEMU `PCIDevice`, no PCI bus enumeration, no BAR relocation, no MSI/INTx routing, and no `pci_set_irq()` path while `p2k-pci.c` remains in place (`qemu/p2k-pci.c:5-11`).  The actual device surfaces are separately mapped memory and I/O regions.

BAR2 SRAM is documented in [22 — SRAM BAR2](22-sram-bar2.md).  BAR3 flash is documented in [21 — Flash BAR3](21-flash-bar3.md).  BAR4 DCS is documented in [25 — DCS sound](25-dcs-sound.md).  MediaGX display is documented in [23 — MediaGX and display](23-mediagx-and-display.md).


## Byte-lane and fixed-address behavior

The config-space read path decodes byte and word reads by combining the `0xCF8` low bits with the `0xCFC` window offset (`qemu/p2k-pci.c:170-184`).

This matters for BIOS-style probes that read vendor ID as a word and class code as individual bytes.

Writes to `0xCFC` are intentionally not stored (`qemu/p2k-pci.c:187-192`).

> [!WARNING]
> BAR writes are silently ignored. PCI resource reallocation will not work. If guest firmware attempts to move BARs, the device windows will stop responding.

That means the guest can perform normal PCI BAR-size probes, but it will always read the fixed addresses listed above.

The stub therefore models discovery, not a programmable PCI fabric.

## Installation order expectations

The PCI stub only reports addresses.

The actual memory windows must be installed by the machine initialization path before the guest touches them.

`p2k-plx9054.c` owns ROM-like regions (`qemu/p2k-plx9054.c:112-177`).

`p2k-plx-regs.c` owns the BAR0 register file (`qemu/p2k-plx-regs.c:420-432`).

`p2k-bars.c`, `p2k-bar3-flash.c`, and `p2k-dcs.c` own BAR2, BAR3, and BAR4 respectively.

The static config table and the separate MemoryRegions must agree; otherwise PRISM discovers an address that does not decode.


## See also

- [13 — Memory map](13-memory-map.md)
- [21 — Flash BAR3](21-flash-bar3.md)
- [22 — SRAM BAR2](22-sram-bar2.md)
- [23 — MediaGX and display](23-mediagx-and-display.md)
- [25 — DCS sound](25-dcs-sound.md)
- [30 — Symptom patches](30-symptom-patches.md)
