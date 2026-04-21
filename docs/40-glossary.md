# 40 — Glossary

Terms, acronyms, and Encore-specific jargon used throughout the
documentation.

---

**A20 gate**
The address line 20 enable/disable mechanism inherited from the IBM PC.
The BIOS programs the A20 gate through the keyboard controller during
POST. Encore models this in `src/io.c`.

**Alt+K**
The key combination that toggles raw keyboard capture mode in the SDL
window. In capture mode every keystroke is injected as a PS/2 scancode
to the guest KBC. See [36-cli-keyboard-guide.md](36-cli-keyboard-guide.md).

**BAR (Base Address Register)**
PCI base address register. The MediaGX and PLX 9050 bridge each expose
several BARs that map MMIO windows into the guest physical address space.
BAR4 of the PLX 9050 bridges to the DCS audio hardware. See
[07-memory-map.md](07-memory-map.md).

**BT-** prefix
Internal bug-tracker numbering used during development (e.g. BT-74,
BT-94, BT-107, BT-130). Preserved in comments for git-bisect
traceability.

**clkruns**
XINU global flag that enables clock-driven process scheduling. Set to 1
by XINU's own `clkinit()`. Encore does not poke this flag externally;
the game sets it naturally during boot.

**CMOS / RTC**
The MC146818-compatible real-time clock and non-volatile RAM, accessed
via I/O ports `0x70`/`0x71`. Encore provides a functional CMOS stub in
`src/io.c`.

**DCS (Digital Compression System)**
Williams' proprietary audio compression system, used on Pinball 2000.
The DCS-2 board is a PCI device sitting behind the PLX 9050 bridge.
Encore emulates DCS command delivery via the BAR4 path. The sample
library is stored in `*_P2K.bin` as OGG Vorbis. See
[12-sound-pipeline.md](12-sound-pipeline.md).

**DC (Display Controller)**
The display controller subsystem of the Cyrix MediaGX. Its registers
live in the GX_BASE MMIO region at offset `0x8300`. Encore emulates
`DC_TIMING2` to deliver VBLANK pulses to the game. See
[11-display-pipeline.md](11-display-pipeline.md).

**EMS**
Extended memory state — a small block of game-specific persistent data
saved to `savedata/*.ems`. See [10-savedata.md](10-savedata.md).

**Encore**
This project. A clean-room Pinball 2000 emulator written in C. The name
is French for "again", chosen because it re-implements a platform that
was believed to be gone.

**exec_count**
Internal batch counter incremented once per `uc_emu_start` call
(200 000 instructions per batch). Used as a coarse "time since boot"
proxy for startup sequencing. See [24-fps-and-pacing.md](24-fps-and-pacing.md).

**game_id**
A 32-bit integer identifying the Pinball 2000 title. Stored at offset
`0x3C` inside the update binary (`0x803C` in guest physical space).
50069 = SWE1; 50070 = RFM. See [25-game-detection-auto.md](25-game-detection-auto.md).

**game_prefix**
Encore's short string identifier for the current title: `"swe1"` or
`"rfm"`. Drives ROM file search, savedata naming, and bundle filtering.

**GP (Graphics Processor)**
The graphics subsystem of the Cyrix MediaGX. Its registers live in the
GX_BASE MMIO region at offset `0x8200`. The GP blit engine transfers
data from system RAM into the framebuffer at `GX_BASE + 0x800000`.

**GX_BASE**
The base address of the MediaGX MMIO region, set during BIOS
initialisation. In practice always `0x40000000` for Pinball 2000.

**IDT (Interrupt Descriptor Table)**
The x86 table that maps interrupt vectors to handler addresses. Encore
watches IDT[0x20] (IRQ0 handler) to detect when XINU has installed its
clock interrupt handler. See [22-xinu-boot-sequence.md](22-xinu-boot-sequence.md).

**IRQ0**
The timer interrupt, delivered by the PIT channel 0 to the PIC at
vector `0x20`. The primary scheduling clock for XINU. See
[16-irq-pic.md](16-irq-pic.md).

**LPT (Line Printer / Parallel Port)**
On Pinball 2000, the parallel port is repurposed as the interface to the
driver board that controls coils, lamps, and the switch matrix.
Encore emulates the LPT protocol (`src/lpt_pass.c`). See
[18-lpt-emulation.md](18-lpt-emulation.md).

**MediaGX**
Cyrix MediaGX — the all-in-one x86 SoC used in Pinball 2000 heads.
Contains the CPU core, North Bridge, graphics processor, and display
controller in a single package.

**pb2kslib**
Informal name for the Pinball 2000 sound library container format
(`*_P2K.bin`). A fixed-header container with XOR-obfuscated index and
OGG blob sections. Detected by shape (magic bytes), not filename.
See [32-tools-sound-decoder.md](32-tools-sound-decoder.md).

**PCI**
Peripheral Component Interconnect. The MediaGX and PLX 9050 bridge are
PCI devices. Encore emulates PCI configuration space access at I/O ports
`0xCF8`/`0xCFC`. See [07-memory-map.md](07-memory-map.md).

**PIC (Programmable Interrupt Controller)**
Intel 8259A or compatible. Encore emulates a cascaded master/slave pair
in `src/io.c`. The master handles IRQ0–IRQ7; the slave handles IRQ8–
IRQ15. See [16-irq-pic.md](16-irq-pic.md).

**PIT (Programmable Interval Timer)**
Intel 8253/8254 or compatible. Channel 0 produces IRQ0 at the XINU
scheduler rate (~4003 Hz for divisor 298). Encore emulates wall-clock
PIT delivery in `src/cpu.c`. See [24-fps-and-pacing.md](24-fps-and-pacing.md).

**PLX 9050**
PCI bus bridge chip used to connect the DCS-2 audio board to the
MediaGX PCI bus. BAR4 of the PLX 9050 is the primary DCS command
window. See [07-memory-map.md](07-memory-map.md).

**PRISM**
Williams' name for the MediaGX-based Pinball 2000 video system (Pinball
Real-time Interactive Super-resolution Media). Encore configures the
PRISM GX registers during boot.

**prnull**
XINU null output device. Encore installs a minimal STI+HLT+JMP stub at
`0xFF0000` as a safe landing pad for stray `prnull` calls before XINU
is fully initialised. See [22-xinu-boot-sequence.md](22-xinu-boot-sequence.md).

**RFM**
Revenge From Mars — one of two Pinball 2000 titles. `game_id` = 50070.

**SEEPROM**
93C46-compatible serial EEPROM used to store coin settings and credit
count. Mapped to `savedata/*.seeprom`. See [10-savedata.md](10-savedata.md).

**SRAM**
Static RAM in BAR2 of the PLX 9050 bridge, used for save data. Encore
mirrors BAR2 SRAM writes to `g_emu.bar2_sram[]` and flushes them to
disk on clean exit.

**SWE1**
Star Wars Episode I — one of two Pinball 2000 titles. `game_id` = 50069.

**Unicorn Engine**
The CPU emulation backend used by Encore. A multi-architecture emulator
built on QEMU's TCG, used in `UC_MODE_32` for i386 guest emulation on
an x64 host. Version 2.x required.

**UART**
Universal Asynchronous Receiver/Transmitter. COM1 (`0x3F8`) is the XINU
serial console. Encore bridges it to stdout / `--serial-tcp`.

**VBLANK**
Vertical blanking interval. The signal that triggers frame output in the
MediaGX display controller. Encore synthesises VBLANK at ~57 Hz
(17.5 ms period) by pulsing `DC_TIMING2`. See
[17-vblank.md](17-vblank.md).

**XINA**
The XINU shell (interactive command interpreter). Visible on COM1 as
`XINU: V7` during boot; accepts single-letter commands.

**XINU**
A teaching operating system (eXperimental Internet Networking Utility)
adapted by Williams for the Pinball 2000 platform. Provides processes,
semaphores, a scheduler, and device drivers for the game threads.
Relevant startup details in [22-xinu-boot-sequence.md](22-xinu-boot-sequence.md).

**xinu_booted**
Encore flag set when the string `"XINU"` is detected on the virtual
UART. See `include/encore.h`.

**xinu_ready**
Encore flag set when `xinu_booted` is true AND the clock interrupt
handler (`clkint`) has appeared in IDT[0x20] AND 50 execution batches
have elapsed as a grace period. Timer injection is enabled once this
flag is set.
