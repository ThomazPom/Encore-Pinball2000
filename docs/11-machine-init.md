# 11 — Machine Init

What this doc covers: the exact `pinball2000` MachineClass lifecycle and the ordered calls made by `pinball2000_init()`. This is a source-line ledger, not a design proposal; if it disagrees with intuition from historical documentation, the C code wins.

> [!NOTE]
> This is a source-line ledger of device initialization order. The exact sequence matters because later devices depend on earlier ones (e.g., display requires GX base, UART IRQ requires PIC).

## Registration path

| Step | Source | Meaning |
|---:|---|---|
| 1 | `qemu/pinball2000.c:206-211` | `TypeInfo` declares `pinball2000` as a subtype of `TYPE_X86_MACHINE` with instance storage `Pinball2000MachineState`. |
| 2 | `qemu/pinball2000.c:213-218` | QEMU `type_init()` registers the type during process startup. |
| 3 | `qemu/pinball2000.c:175-204` | `pinball2000_class_init()` fills `MachineClass`: description, `init`, family, CPU, RAM, and properties. |
| 4 | `qemu/pinball2000.c:179-180` | `mc->init = pinball2000_init`; this is the function QEMU runs for `-M pinball2000`. |
| 5 | `qemu/pinball2000.c:191-203` | Machine properties are `game`, `roms-dir`, and `update`. |

## MachineClass settings

| Setting | Value | Source |
|---|---|---|
| Description | `Williams Pinball 2000 (Cyrix MediaGX + CS5530 + PLX9054)` | `qemu/pinball2000.c:179` |
| Family | `pinball2000_i386` | `qemu/pinball2000.c:181` |
| Max CPUs | `1` | `qemu/pinball2000.c:182` |
| Default CPU type | `486` | `qemu/pinball2000.c:183` |
| Default RAM | `P2K_RAM_SIZE` = 16 MiB | `qemu/pinball2000.c:184`, `qemu/pinball2000.h:137` |
| Default RAM id | `p2k.ram` | `qemu/pinball2000.c:185` |
| No floppy/CD/parallel | QEMU default devices suppressed | `qemu/pinball2000.c:186-188` |

> [!IMPORTANT]
> There is no `pc_basic_device_init()` call. This is not a QEMU `pc` machine with extra devices; it builds only the pieces Pinball 2000 currently needs.

## Init order ledger

| # | Call / action | One-line description | Source |
|---:|---|---|---|
| 1 | Read machine state | Cast `MachineState` to `Pinball2000MachineState` and `X86MachineState`; cache system memory. | `qemu/pinball2000.c:46-53` |
| 2 | Validate `game=` | Refuse to boot without `-M pinball2000,game=<id>`. | `qemu/pinball2000.c:55-59` |
| 3 | Default `roms-dir` | If unset, use host directory `roms`. | `qemu/pinball2000.c:60-62` |
| 4 | Size x86 RAM | Set below-4G RAM to `machine->ram_size`, above-4G to zero. | `qemu/pinball2000.c:64-66` |
| 5 | Alias RAM at 0 | Map QEMU's auto-allocated `machine->ram` at guest physical `0x00000000`. | `qemu/pinball2000.c:67-70` |
| 6 | Create CPU0 | Call `x86_cpus_init(x86ms, CPU_VERSION_LATEST)`. | `qemu/pinball2000.c:72-74` |
| 7 | Create ISA bus | Build an ISA bus over system memory and I/O spaces. | `qemu/pinball2000.c:75-80` |
| 8 | Create PIC | Instantiate i8259 and connect it to CPU INTR. | `qemu/pinball2000.c:81` |
| 9 | Register ISA IRQs | Route ISA device IRQ lines to the i8259 inputs. | `qemu/pinball2000.c:82` |
| 10 | Create PIT | Instantiate QEMU i8254 at I/O `0x40`, IRQ0. | `qemu/pinball2000.c:84` |
| 11 | Load bank0 | De-interleave required chips `u100/u101` into `s->bank0`. | `qemu/pinball2000.c:86-89`, `qemu/p2k-rom.c:64-69` |
| 12 | Load optional banks | Best-effort load banks 1-3 from `u102..u107`. | `qemu/pinball2000.c:90-92`, `qemu/p2k-rom.c:89-94` |
| 13 | Load DCS ROM | Best-effort load DCS sound ROM from `u109/u110`. | `qemu/pinball2000.c:90-92`, `qemu/p2k-rom.c:96-108` |
| 14 | Map ROM windows | Map option ROM, BIOS shadows, PLX banks, BAR5 mirrors, high alias. | `qemu/pinball2000.c:94-98`, `qemu/p2k-plx9054.c:112-177` |
| 15 | Enable MediaGX opcodes | Gate patched TCG MediaGX helpers on for this machine only. | `qemu/pinball2000.c:99-102`, `qemu/p2k-mediagx-gate.c:127-137` |
| 16 | Install ISA stubs | Keyboard, port 0x61, CMOS, POST, COM1. | `qemu/pinball2000.c:104`, `qemu/p2k-isa-stubs.c:477-528` |
| 17 | Wire UART IRQ | Give COM1 stub the i8259 IRQ4 line. | `qemu/pinball2000.c:105-107`, `qemu/p2k-isa-stubs.c:272-285` |
| 18 | Install SuperIO/CC5530 | Ports `0x2E/0x2F` and `0xEA/0xEB`. | `qemu/pinball2000.c:108`, `qemu/p2k-superio.c:100-121` |
| 19 | Install Cyrix CCR | Ports `0x22/0x23` for MediaGX configuration. | `qemu/pinball2000.c:109`, `qemu/p2k-cyrix-ccr.c:130-141` |
| 20 | Install PCI stub | Temporary CF8/CFC config-space responder. | `qemu/pinball2000.c:110`, `qemu/p2k-pci.c:202-218` |
| 21 | Install BAR2 | BAR2 SRAM and all-ones sentinel. | `qemu/pinball2000.c:111`, `qemu/p2k-bars.c:77-100` |
| 22 | Install PLX regs | BAR0 register file, SEEPROM, DCS serial detect. | `qemu/pinball2000.c:112`, `qemu/p2k-plx-regs.c:420-433` |
| 23 | Install BAR3 flash | Update flash, savedata seed, optional update assembly. | `qemu/pinball2000.c:113`, `qemu/p2k-bar3-flash.c:310-365` |
| 24 | Install DCS BAR4 | 16 MiB DCS MMIO frontend. | `qemu/pinball2000.c:114`, `qemu/p2k-dcs.c:95-109` |
| 25 | Install DCS UART | I/O `0x138-0x13F` DCS/16550 overlay. | `qemu/pinball2000.c:115`, `qemu/p2k-dcs-uart.c:240-254` |
| 26 | Install DCS audio | Optional QEMU audio mixer/sample player. | `qemu/pinball2000.c:116`, `qemu/p2k-dcs-audio.c:824-927` |
| 27 | Install LPT board | Driver-board protocol on `0x378-0x37A`. | `qemu/pinball2000.c:117`, `qemu/p2k-lpt-board.c:616-651` |
| 28 | Install GX regions | MediaGX 16 MiB MMIO shape and framebuffer alias. | `qemu/pinball2000.c:118`, `qemu/p2k-gx.c:47-83` |
| 29 | Install GP BLT | Overlay the MediaGX GP BLT block. | `qemu/pinball2000.c:119`, `qemu/p2k-gp-blt.c:204-215` |
| 30 | Install display | Create QEMU console and 640×480 surface. | `qemu/pinball2000.c:120`, `qemu/p2k-display.c:162-185` |
| 31 | Install VSYNC | Virtual-clock display timing ticker. | `qemu/pinball2000.c:121`, `qemu/p2k-vsync.c:81-90` |
| 32 | Install mem-detect patch | Runtime `mem_detect()` prologue rewrite timer. | `qemu/pinball2000.c:122`, `qemu/p2k-mem-detect.c:114-124` |
| 33 | Install diag sampler | Optional `P2K_DIAG=1` sampler. | `qemu/pinball2000.c:123`, `qemu/p2k-diag.c:402-420` |
| 34 | Install timing audit | Default-on timing/PIT/PIC/IDT observer. | `qemu/pinball2000.c:124`, `qemu/p2k-timing-audit.c:194-213` |
| 35 | Install NIC shadow | Read-only D-segment LAN ROM overlay. | `qemu/pinball2000.c:125`, `qemu/p2k-nic-dseg.c:72-97` |
| 36 | Install gfx watcher | Optional `_gfx_driver_list` diagnostic. | `qemu/pinball2000.c:126`, `qemu/p2k-gfxlist-watch.c:111-121` |
| 37 | Install probe-cell shim | Strictly gated no-update compatibility scribble. | `qemu/pinball2000.c:127`, `qemu/p2k-probe-cell-shim.c:305-324` |
| 38 | Register reset hook | Arrange `p2k_post_reset()` after every system reset. | `qemu/pinball2000.c:129-130` |
| 39 | Ready banner | Log game and RAM MiB. | `qemu/pinball2000.c:132-133` |

## Reset is a second phase

Machine init maps devices and registers a reset hook; it does not itself write `EIP`. The actual boot recipe runs later from `p2k_post_reset()` because QEMU reset handlers run after device reset, so the CPU register writes are not clobbered by x86 reset (`qemu/p2k-boot.c:9-12`, `qemu/p2k-boot.c:72-127`).


## Dependency ordering notes

> [!WARNING]
> The init order is not arbitrary. RAM must exist before CPU reset writes the GDT. GX base must exist before display/GP/VSYNC install. Changing the order breaks boot.

| Dependency | Why it must come first | Current order source |
|---|---|---|
| RAM before CPU reset recipe | The reset hook later copies option ROM bytes and writes a GDT into RAM. | RAM alias is created before reset registration (`qemu/pinball2000.c:64-70`, `qemu/pinball2000.c:130-131`). |
| ISA/PIC before UART IRQ hookup | COM1 needs an i8259 input line for IRQ4. | PIC array exists before `p2k_isa_set_uart_irq(i8259[4])` (`qemu/pinball2000.c:80-107`). |
| Bank0 before ROM windows | ROM MemoryRegions copy from `s->bank0`. | `p2k_load_bank0()` precedes `p2k_map_rom_windows()` (`qemu/pinball2000.c:86-98`). |
| GX base before GP overlay/display/vsync | GP and display assume the GX register/FB backing regions exist. | `p2k_install_gx_stub()` precedes GP, display, VSYNC (`qemu/pinball2000.c:118-122`). |
| DCS core reset before frontends | BAR4 and UART both call the shared core reset, which is idempotent. | BAR4 then UART install order (`qemu/pinball2000.c:114-115`, `qemu/p2k-dcs-core.c:182-189`). |
| BAR3 before timing/diag | The guest will see flash before observers begin sampling. | BAR3 install precedes diag/audit (`qemu/pinball2000.c:113-125`). |

## Property table

| Property | Required? | Default | Consumer |
|---|---|---|---|
| `game` | Yes | none | ROM chip filenames, savedata filenames, update auto-discovery (`qemu/pinball2000.c:55-59`, `qemu/p2k-rom.c:24-28`). |
| `roms-dir` | No | `roms` | Chip ROMs, optional BIOS, update root fallback, pb2kslib fallback (`qemu/pinball2000.c:60-62`, `qemu/p2k-plx9054.c:71-84`, `qemu/p2k-bar3-flash.c:290-308`). |
| `update` | No | unset | Explicit BAR3 update directory (`qemu/pinball2000.c:163-173`, `qemu/p2k-bar3-flash.c:330-333`). |

> [!IMPORTANT]
> The `game` property is deliberately mandatory because the current loader has no code path that auto-detects a game by scanning the ROM directory.

## See also

* [10-architecture.md](10-architecture.md) — system-level overview.
* [12-cpu-and-timers.md](12-cpu-and-timers.md) — CPU/PIT/PIC details.
* [13-memory-map.md](13-memory-map.md) — regions installed by the ledger above.
* [14-boot-recipe.md](14-boot-recipe.md) — reset hook contents.
* [30-symptom-patches.md](30-symptom-patches.md) — temporary bridge status.
