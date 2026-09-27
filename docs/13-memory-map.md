# 13 — Memory and I/O map

Encore exposes three address domains that must not be confused:

- x86 physical memory, including RAM, ROM and PCI-style MMIO;
- x86 I/O ports addressed by `in`/`out` instructions;
- PLX local-bus addresses preserved as compatibility mirrors.

The tables below describe the effective QEMU `FlatView`, after overlaps and
priorities have been resolved. They are therefore more precise than startup
messages that report the size originally requested for each region.

> [!IMPORTANT]
> `0x08000000` is the PLX `LAS3BA` local remap value, not the CPU-visible PCI
> BAR. The real CPU-facing 64 MiB ROM window is BAR5 at `0x14000000`. Encore
> additionally exposes the local addresses directly for compatibility.

The [boot recipe](14-boot-recipe.md) explains the initial RAM copy and CPU
entry. ROM contents and persistence belong to [ROM and update loading](15-rom-loading.md)
and [Persistent cabinet state](09-savedata.md).

## x86 physical address space

### RAM and low-memory overlays

The machine has exactly 16 MiB of RAM at `0x00000000–0x00ffffff`. Three
read-only or writable board views overlay portions of that RAM with QEMU
priority 1:

> [!IMPORTANT]
> Physical backing and guest allocation policy are different limits. Most
> preserved builds make XINU's `sizmem()` report 4 MiB; RFM 1.80 deliberately
> reports 8 MiB. Encore still backs the full 16 MiB in both cases. This lets
> 1.80 use its expected memory here, but can hide the incompatibility that its
> 8 MiB policy had with stock 4 MiB cabinets. See
> [Game changelogs](50-game-changelogs.md#why-rfm-180-needs-8-mib).

| Effective range | Size | Access | Owner and purpose |
|---:|---:|---|---|
| `0x00000000–0x000bffff` | 768 KiB | RAM | ordinary guest RAM, including the reset copy at `0x00080000` and GDT at `0x00088000` |
| `0x000c0000–0x000c7fff` | 32 KiB | read-only | PRISM option-ROM view of bank 0 |
| `0x000c8000–0x000d0007` | 32 KiB + 8 B | RAM | underlying guest RAM |
| `0x000d0008–0x000d000f` | 8 B | read-only MMIO | BT-131 LAN identity shadow: MAC, board ID and checksum |
| `0x000d0010–0x000effff` | 128 KiB − 16 B | RAM | underlying guest RAM |
| `0x000f0000–0x000fffff` | 64 KiB | read/write shadow | low `bios.bin` view; filled with `0xff` when absent |
| `0x00100000–0x00ffffff` | 15 MiB | RAM | extended guest RAM; framebuffer backing occupies `0x00800000–0x00bfffff` |

The LAN identity overlay exists even when networking is disabled. Enabling
the SMC8416 network device adds a higher-priority shared-memory region over a
larger part of the same D segment; see [Optional D-segment devices](#optional-d-segment-devices).

### PLX local-address compatibility views

Encore installs the PLX local remap and chip-select addresses directly into
the x86 physical map. Their effective layout is one continuous 64 MiB span:

| Effective range | Size | Access | Contents |
|---:|---:|---|---|
| `0x08000000–0x087fffff` | 8 MiB | read-only | first half of bank 0, before CS0 begins |
| `0x08800000–0x097fffff` | 16 MiB | read-only | bank 1, chips `u102/u103` |
| `0x09800000–0x0a7fffff` | 16 MiB | read-only | bank 2, chips `u104/u105` |
| `0x0a800000–0x0b7fffff` | 16 MiB | read-only | bank 3, chips `u106/u107` |
| `0x0b800000–0x0bffffff` | 8 MiB | read-only | DCS sound ROM, chips `u109/u110`, when available |

The bank-0 region is created from a 16 MiB image, but bank 1 begins only
8 MiB later at `0x08800000` with the same priority. QEMU consequently exposes
only bank0's first 8 MiB in this compatibility view. This is observable in
`info mtree -f`; the complete bank remains available through BAR5.

The PLX SEEPROM values explain the two views: `LAS3RR=0x0c000008` describes a
64 MiB PCI memory window, `LAS3BA=0x08000001` is its local remap base, and
`CS0BASE` through `CS3BASE` select local addresses beginning at
`0x08800000`, `0x09800000`, `0x0a800000` and `0x0b800000`.

### Fixed PRISM PCI windows

The guest discovers these addresses through the emulated PCI configuration
ports. Configuration writes, including BAR-size probes, are accepted but do
not relocate the fixed map.

| Effective range | Size | Access | PCI identity | Purpose |
|---:|---:|---|---|---|
| `0x10000000–0x100000ff` | 256 B | MMIO | BAR0 | PLX registers and 93C46 SEEPROM bit-bang interface |
| `0x11000000–0x1102ffff` | 192 KiB | RAM | BAR2 | persistent board SRAM/NVRAM |
| `0x11030000–0x11ffffff` | 15.8125 MiB | MMIO sentinel | remainder of BAR2 | reads as `0xffffffff`; writes are ignored |
| `0x12000000–0x123fffff` | 4 MiB | flash MMIO | BAR3 | update flash with Intel command semantics |
| `0x13000000–0x13ffffff` | 16 MiB | MMIO | BAR4 | DCS command/status transport |
| `0x14000000–0x14ffffff` | 16 MiB | read-only | BAR5 + `0x0000000` | complete pristine bank 0 |
| `0x15000000–0x15ffffff` | 16 MiB | read-only | BAR5 + `0x1000000` | bank 1 |
| `0x16000000–0x16ffffff` | 16 MiB | read-only | BAR5 + `0x2000000` | bank 2 |
| `0x17000000–0x17ffffff` | 16 MiB | read-only | BAR5 + `0x3000000` | bank 3 |
| `0x18000000–0x187fffff` | 8 MiB | read-only | expansion ROM BAR | DCS sound-ROM mirror, when available |

Missing optional banks 1–3 are still represented by `0xff`-filled 16 MiB
windows. The DCS chip-select and expansion-ROM views are installed only when
the DCS pair loaded successfully.

> [!NOTE]
> BAR2 is a 16 MiB PCI window but contains only 192 KiB of persistent SRAM.
> The sentinel is deliberate device behavior, not additional saved memory.

### MediaGX and high aliases

| Effective range | Size | Access | Purpose |
|---:|---:|---|---|
| `0x40000000–0x407fffff` | 8 MiB | mostly RAM-like MMIO backing | MediaGX GP/DC/BC register space |
| `0x40008100–0x4000820f` | 272 B | priority-1 MMIO overlay | semantic graphics-pipeline/blitter registers |
| `0x40800000–0x40bfffff` | 4 MiB | RAM alias | mirror of guest RAM `0x00800000–0x00bfffff` used as framebuffer |
| `0x40c00000–0x40ffffff` | 4 MiB | RAM-like MMIO backing | upper MediaGX register space |
| `0xff000000–0xff3fffff` | 4 MiB | read-only | high bank-0 alias |
| `0xffff0000–0xffffffff` | 64 KiB | read-only | high `bios.bin` reset mirror |

Only the GP block has semantic MMIO handlers inside the first MediaGX window;
the remaining register storage is plain host-backed memory seeded with the
values the guest expects. The framebuffer alias and low RAM are the same
bytes, not copied surfaces. See [MediaGX and display](23-mediagx-and-display.md).

## Optional D-segment devices

Two opt-in devices decode the same legacy address range and therefore cannot
be active together through the supported launcher:

| Mode | Physical range | Priority | I/O ports | IRQ |
|---|---:|---:|---:|---:|
| SMC8416 network | `0x000d0000–0x000d1fff` shared memory | 2 | `0x300–0x31f` by default | 7 by default |
| experimental PUB card | `0x000d0000–0x000d3fff` banked aperture; registers at `0x000d4000–0x000d4003` | 10 | none | none |

Both override normal RAM and the eight-byte LAN identity where their ranges
intersect. `run-qemu.sh` rejects `--pub-card` combined with any network mode
before starting QEMU. Bypassing the launcher can create an ambiguous machine
in which the PUB aperture wins because of its higher priority.

The network I/O base and IRQ are QOM properties of `p2k-smc8416`; the launcher
uses the defaults above. Network behavior is described in
[Networking](48-network.md).

## x86 I/O-port map

These are the live ports in the default machine. Ranges not listed remain in
QEMU's unassigned I/O container.

| Ports | Owner | Purpose |
|---:|---|---|
| `0x0020–0x0021` | i8259 master PIC | interrupt arbitration and EOI |
| `0x0022–0x0023` | Cyrix CCR | MediaGX indexed configuration |
| `0x002e–0x002f` | Super I/O | configuration index/data |
| `0x0040–0x0043` | i8254 PIT | channel programming; channel 0 drives IRQ0 |
| `0x0060`, `0x0064` | i8042 routing | PS/2 data/status when connected; absent-device overlays otherwise |
| `0x0061` | board stub | PIT/speaker control status |
| `0x0070–0x0071` | mc146818 RTC | CMOS index/data; RTC interrupt is IRQ8 |
| `0x0080` | POST latch | diagnostic POST writes |
| `0x00a0–0x00a1` | i8259 slave PIC | secondary interrupt controller |
| `0x00ea–0x00eb` | Cx5530 stub | companion-chip indexed registers |
| `0x0138–0x013f` | DCS UART | original sound-board byte transport |
| `0x02f8–0x02ff` | COM2 | guest UART model |
| `0x0300–0x031f` | optional SMC8416 | ASIC at `0x300–0x30f`, DP8390 at `0x310–0x31f` |
| `0x0370–0x0371` | PC97338 | Super-I/O indexed register face |
| `0x0378–0x037a` | LPT board | default driver-board data/status/control |
| `0x03f8–0x03ff` | COM1 | XINA console and host chardev |
| `0x04d0–0x04d1` | ELCR | PIC edge/level control |
| `0x0cf8–0x0cfb` | PCI address latch | configuration mechanism 1 address |
| `0x0cfc–0x0cff` | PCI data window | fixed configuration responses |

The LPT board may instead occupy `0x278–0x27a` or `0x3bc–0x3be`; those are the
other addresses XINA probes. A custom address is accepted for investigation
but may not be discovered by the guest. Disabling the LPT device removes the
range entirely. See [LPT board](26-lpt-board.md).

The i8042 model always exists, but the input router models the keyboard as
physically unplugged in cabinet modes. It enables or disables the priority-1
absent-device overlays at `0x60/0x64` as the keyboard connection changes.

BAR4 and the DCS UART at `0x138–0x13f` are two transports into the same DCS
core. They do not represent two independently running sound boards.

## PCI configuration face

Configuration mechanism 1 exposes four fixed identities on bus 0:

| Device | Identity | Important reported resources |
|---:|---|---|
| 0 | Cyrix MediaGX host bridge `1078:0001` | GX base `0x40000000` |
| 8 | WMS PRISM display device `146e:0001` | BAR0/BAR2/BAR3/BAR4/BAR5 and ROM BAR from the table above |
| 9 | raw PLX shadow `10b5:9050` | BAR0 `0x10000000` |
| 18 | Cyrix Cx5520 ISA bridge `1078:0002` | no MMIO window |

The device-9 face is a compatibility identity sharing BAR0, not a second PLX
instance. PCI configuration writes are swallowed; there is no dynamic BAR
relocation or general-purpose PCI bus model behind these responses.

## Inspecting the effective map

Start a read-only machine paused and attach a QEMU monitor:

```bash
scripts/run-qemu.sh --game swe1 --update none --display none \
  --audio none --no-savedata --monitor stdio -- -S
```

At the `(qemu)` prompt:

```text
info mtree -f
xp /16bx 0x08000000
xp /16bx 0x08800000
xp /16bx 0x14000000
xp /16bx 0x14800000
```

`info mtree -f` is essential when regions overlap: the non-flat tree shows
what was registered, while the flat view shows what the CPU can actually
reach. The four reads above distinguish the local bank0/bank1 boundary from
the two halves of complete BAR5 bank0.

For any mapping change, verify at least:

1. default, network and PUB FlatViews separately;
2. every overlap's priority and effective boundary;
3. PCI-reported BAR values against the installed MMIO regions;
4. framebuffer writes through both the low-RAM and GX aliases;
5. a normal guest boot, not only a paused monitor inspection.

---

← [ROM and update loading](15-rom-loading.md) ·
[Documentation index](README.md) · [MediaGX and display](23-mediagx-and-display.md)
