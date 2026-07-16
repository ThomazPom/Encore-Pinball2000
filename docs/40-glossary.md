# 40 — Glossary

Terms, acronyms, and QEMU-port-specific jargon used throughout the
documentation.

---

**A20 gate**
The address line 20 enable/disable mechanism inherited from the IBM PC.
The BIOS programs the A20 gate through the keyboard controller during
POST. Encore models this via standard QEMU i8042 keyboard controller emulation.

**BAR (Base Address Register)**
PCI base address register. The MediaGX and PLX 9054 bridge each expose
several BARs that map MMIO windows into the guest physical address space.
BAR4 of the PLX 9054 bridges to the DCS audio hardware. See
[docs/13-memory-map.md](13-memory-map.md).

> [!NOTE]
> Encore uses PLX 9054, not 9050. The PCI device IDs and BAR layout match real hardware.

**BT-** prefix
Internal bug-tracker numbering used during development (e.g. BT-74,
BT-94, BT-107, BT-130). Preserved in comments for git-bisect

**clkruns**
XINU global flag that enables clock-driven process scheduling. Set to 1
by XINU's own `clkinit()`. The emulator does not poke this flag externally;
the game sets it naturally during boot.

**CMOS / RTC**
The MC146818-compatible real-time clock and non-volatile RAM, accessed
via I/O ports `0x70`/`0x71`. QEMU provides a functional CMOS/RTC device
via its standard `hw/rtc/mc146818rtc.c` subsystem, configured in `qemu/pinball2000.c`.

**DCS (Digital Compression System)**
Williams' proprietary audio compression system, used on Pinball 2000.
The DCS-2 board is a PCI device sitting behind the PLX 9054 bridge.
Encore emulates DCS command delivery via BAR4 MMIO and plays back
OGG Vorbis samples extracted from `*_P2K.bin` containers. See
[docs/25-dcs-sound.md](25-dcs-sound.md).

**DC (Display Controller)**
The display controller subsystem of the Cyrix MediaGX. Its registers
live in the GX_BASE MMIO region at offset `0x8300`. Encore emulates
`DC_TIMING2` to deliver VBLANK pulses to the game. See
[docs/23-mediagx-and-display.md](23-mediagx-and-display.md) and [docs/24-vsync.md](24-vsync.md).

**EMS**
`savedata/*.ems`. It is not Pinball 2000 hardware and Encore intentionally ignores it;
see [docs/09-savedata.md](09-savedata.md).

**Encore**
This project. A clean-room Pinball 2000 emulator. The name
is French for "again", chosen because it re-implements a platform that
was believed to be gone. Encore is the second major implementation,

**game_id**
A 32-bit integer identifying the Pinball 2000 title. Stored at offset
`0x3C` inside the update binary (`0x803C` in guest physical space).
50069 = SWE1; 50070 = RFM. Auto-detected by the ROM loader from the flash image.

**game_prefix**
Short string identifier for the current title: `"swe1"` or
`"rfm"`. Drives ROM file search, savedata naming, and bundle filtering.
Passed via `--game` to `scripts/run-qemu.sh`.

**GP (Graphics Processor)**
The graphics subsystem of the Cyrix MediaGX. Its registers live in the
GX_BASE MMIO region at offset `0x8200`. The GP blit engine transfers
data from system RAM into the framebuffer at `GX_BASE + 0x800000`.
See [docs/23-mediagx-and-display.md](23-mediagx-and-display.md).

**GX_BASE**
The base address of the MediaGX MMIO region, set during BIOS
initialisation. In practice always `0x40000000` for Pinball 2000.
Encore maps this region via `qemu/p2k-mediagx.c`.

**IDT (Interrupt Descriptor Table)**
The x86 table that maps interrupt vectors to handler addresses. QEMU's i386 TCG
maintains the IDT as part of the CPU state. The boot process watches IDT[0x20]
(IRQ0 handler) to detect when XINU has installed its clock interrupt handler.
See [docs/12-cpu-and-timers.md](12-cpu-and-timers.md).

**IRQ0**
The timer interrupt, delivered by the PIT channel 0 to the PIC at
vector `0x20`. The primary scheduling clock for XINU. QEMU delivers this
via its standard i8254 PIT and i8259 PIC devices. See [docs/12-cpu-and-timers.md](12-cpu-and-timers.md).

**LPT (Line Printer / Parallel Port)**
On Pinball 2000, the parallel port is repurposed as the interface to the
driver board that controls coils, lamps, and the switch matrix.
Encore emulates the LPT protocol in `qemu/p2k-lpt-board.c`. See
[docs/26-lpt-board.md](26-lpt-board.md).

**MediaGX**
Cyrix MediaGX — the all-in-one x86 SoC used in Pinball 2000 heads.
Contains the CPU core, North Bridge, graphics processor, and display
controller in a single package. Encore uses QEMU's standard i486
CPU model with custom MediaGX-specific MMIO devices.

> [!TIP]
> The MediaGX is close enough to a 486 that QEMU's i486 TCG backend handles it without
> custom CPU emulation. The magic is in the MMIO peripherals, not the instruction set.

**pb2kslib**
Informal name for the Pinball 2000 sound library container format
(`*_P2K.bin`). A fixed-header container with XOR-obfuscated index and
OGG blob sections. Detected by shape (magic bytes), not filename.
Encore's DCS audio device parses this format at runtime.

**PCI**
Peripheral Component Interconnect. The MediaGX and PLX 9054 bridge are
PCI devices. QEMU emulates PCI configuration space access at I/O ports
`0xCF8`/`0xCFC` via its standard `hw/pci/` subsystem. The Pinball2000-specific
PCI devices are registered in `qemu/p2k-pci.c` and `qemu/pinball2000.c`.

**PIC (Programmable Interrupt Controller)**
Intel 8259A or compatible. QEMU emulates a cascaded master/slave pair
via `hw/intc/i8259.c`. The master handles IRQ0–IRQ7; the slave handles IRQ8–
IRQ15. The Pinball2000 machine wires these up in `qemu/pinball2000.c`.

**PIT (Programmable Interval Timer)**
Intel 8253/8254 or compatible. Channel 0 produces IRQ0 at the XINU
scheduler rate (~4003 Hz for divisor 298). QEMU provides this via
`hw/timer/i8254.c`. See [docs/12-cpu-and-timers.md](12-cpu-and-timers.md).

**PLX 9054**
PCI bus bridge chip used to connect the driver board and DCS-2 audio to the
MediaGX PCI bus. BAR2 exposes 128 KiB SRAM (audits/high scores), BAR3 exposes
4 MiB flash (update image), BAR4 is the DCS command window. Encore
implements these in `qemu/p2k-bars.c`, `qemu/p2k-bar3-flash.c`, and `qemu/p2k-dcs.c`.
See [docs/20-plx-pci.md](20-plx-pci.md).

**PRISM**
Williams' name for the MediaGX-based Pinball 2000 video system (Pinball
Real-time Interactive Super-resolution Media). Encore configures the
PRISM GX registers during machine init in `qemu/p2k-mediagx.c`.

**QEMU**
Quick Emulator. The open-source machine emulator and virtualizer that provides
the CPU, PCI, ISA, and peripheral device infrastructure for this port. The i386
TCG (Tiny Code Generator) backend translates guest x86 instructions to host code.
Version 9.0+ recommended.

**RFM**
Revenge From Mars — one of two Pinball 2000 titles. `game_id` = 50070.

**SEEPROM**
93C46-compatible serial EEPROM behind the PLX 9050/9054 CNTRL register
(bit-banged on bits 24–26). Stores cabinet config, dip switches, and the
PCI subsystem-id image. Modelled in `qemu/p2k-plx-regs.c`, seeded from
`savedata/<game>.see` (128 bytes, little-endian word order) and flushed
atomically on exit when an enabled WRITE, ERASE, ERAL, or WRAL changes content.
See [docs/09-savedata.md](09-savedata.md).

**SRAM**
Static RAM in BAR2 of the PLX 9054 bridge, used for save data (audits,
high scores, resource tables). Encore loads this from `savedata/<game>.nvram2`
at boot and flushes it atomically on exit via `qemu_add_exit_notifier`. Guest 
writes update the QEMU `MemoryRegion` in real-time. See 
[docs/22-sram-bar2.md](22-sram-bar2.md) and [docs/09-savedata.md](09-savedata.md).

**SWE1**
Star Wars Episode I — one of two Pinball 2000 titles. `game_id` = 50069.

**TCG (Tiny Code Generator)**
QEMU's JIT translation layer. Converts guest instructions (x86 in our case) to
host instructions (x86_64, ARM64, etc.) on the fly. Much faster than interpretation,
though still slower than native execution. Encore uses TCG in `qemu-system-i386`
mode for 32-bit x86 guests.

**UART**
Universal Asynchronous Receiver/Transmitter. COM1 (`0x3F8`) is the XINU
serial console. QEMU's standard `hw/char/serial.c` provides this; the Pinball2000
machine wires it to stderr. See [docs/27-isa-stubs.md](27-isa-stubs.md).

**VBLANK**
Vertical blanking interval. The signal that triggers frame output in the
MediaGX display controller. Encore synthesises VBLANK at ~57 Hz
(17.5 ms period) by pulsing `DC_TIMING2` via a QEMUTimer. See
[docs/24-vsync.md](24-vsync.md).

**XINA**
The XINU shell (interactive command interpreter). Visible on COM1 (stderr) as
`XINU: V7` during boot; accepts single-letter commands. Encore passes
this through unchanged from the ROM firmware. See
[docs/45-xina-shell-cookbook.md](45-xina-shell-cookbook.md) for tested shell
commands and captured output.

**XINU**
A teaching operating system (eXperimental Internet Networking Utility)
adapted by Williams for the Pinball 2000 platform. Provides processes,
semaphores, a scheduler, and device drivers for the game threads.
Relevant startup details in [docs/14-boot-recipe.md](14-boot-recipe.md).

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
