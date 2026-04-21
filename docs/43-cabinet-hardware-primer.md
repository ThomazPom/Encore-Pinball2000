# 43 — Cabinet Hardware Primer

This document is aimed at software contributors who have never opened a
Pinball 2000 cabinet. Understanding the physical hardware helps explain
why the emulator emulates exactly what it does.

It is **not** a service guide. For electrical specifications, fuse
charts and safety procedures always consult the original Williams
documentation (see the final section).

## 1. Pinball 2000 (P2K) system board overview

The P2K head contains an embedded PC built around a highly integrated
Cyrix processor and chipset. The motherboard is a generic
off-the-shelf design — Williams did not manufacture custom main
boards — but only specific board models are compatible with the rest
of the platform.

* **CPU.** Cyrix MediaGX (`GXm-266GP` or `GXm-233GP`, 2.9 V core).
  This is not a standard PC CPU: graphics, memory controller and
  PCI bridge are on-die. It exposes a `GX_BASE` MMIO region at a
  configurable physical address that contains the graphics pipeline,
  display-controller register file and an internal register window.
* **South bridge.** Cyrix CX5520. Only the CX5520 works with P2K
  software — visually similar chips (CX5510, CX5530) are
  incompatible. The CX5520 hosts ISA, IDE, parallel port and serial
  ports.
* **Compatible motherboards.** The board must combine a CX5520, an
  available PCI slot for the PRISM card and the correct power
  connector. Known-good boards include the Cyrix 586-GXM-AV, the
  InformTech 586-GXM+, the Global Circuit Technology ST-MGXm and the
  Dataexpert MGX7520 (Baby-AT form factor only — the Slim variant
  has no PCI slots).
* **RAM.** Standard SDRAM DIMMs, 16–256 MB. The game typically uses
  32–64 MB.
* **Storage.** None. There is no hard disk. The PRISM card's ROM
  bank 0 contains an option ROM that hooks `INT 0x19` and loads the
  game directly.
* **BIOS.** Award BIOS on most known boards.

## 2. The PRISM card

PRISM is a custom PCI expansion card that occupies a single slot on
the host motherboard. It carries the masked game ROMs, a PLX 9050
local-bus bridge, the DCS-2 audio interface, and a 93C46 SEEPROM.

* **ROM capacity.** Up to 64 MB in a 16-bit-wide configuration: eight
  ROM chips arranged as two interleaved banks of four.
* **PCI identity.** Vendor `0x146E`, device `0x0001`, class
  `0x030000` (display), revision `2`.
* **Six PCI BARs.**
  * BAR0 — PLX 9050 register file.
  * BAR2 — DCS-2 interface plus on-card SRAM.
  * BAR3 — flash update region.
  * BAR4 — DCS audio command port.
  * BAR5 — ROM bank 0.
  * Expansion-ROM BAR — option ROM / DCS ROM bank 4.
* **PLX 9050.** The local-bus bridge handles ROM bank chip-select
  addressing through `CS0BASE`–`CS3BASE` registers and SEEPROM
  bit-banging through the `CNTRL` register.
* **Battery.** A coin-cell battery (BR2325) sits between the two
  PRISM boards (the ROM card and the audio card) and maintains
  SEEPROM state when the cabinet is unpowered. This battery is
  different from the motherboard CR2032 and is less commonly stocked,
  but is readily available online.

## 3. Power and supply rails

The cabinet head uses a standard AT power supply (or an ATX supply
with a small adapter). The supply rails (+5 V, +12 V) power the
motherboard and the PRISM card.

The game's driver board is **not** powered from the PC supply. The
cabinet's main transformer feeds the driver board, which generates:

* +50 V DC for coil and solenoid drive.
* +18 V AC for the lamp matrices.
* +20 V AC for the flashers.

Always consult the Williams service documentation for fuse values and
exact voltage tolerances.

A common failure mode is the AT power supply ageing out. A modern
ATX supply can be substituted with a wiring adapter or a small
modification to bring up the power-on pin.

## 4. Display path

The MediaGX contains an integrated VGA-compatible display controller
producing 640×240 output at approximately 57 Hz. The horizontal scan
rate is non-standard for VGA — it is 15 kHz CGA-class — and feeds a
19-inch arcade CRT mounted face-down in the head. The image is
reflected off a half-silvered mirror onto the playfield glass: this
is the "Pepper's Ghost" effect that makes the on-screen video appear
superimposed on the physical playfield.

The framebuffer lives in the MediaGX's integrated graphics memory
(there is no separate VRAM chip). Row stride is 2048 bytes; visible
width is 640 pixels; visible height is 240 rows (row-doubled to 480
on the display).

Encore reproduces this by row-doubling each 640×240 frame into a
640×480 SDL window. On a real cabinet, the 15 kHz monitor is the
limiting factor — a standard PC monitor at 31 kHz cannot display the
original signal.

## 5. Sound path — DCS-2

DCS (Digital Compression System, version 2) is a separate ADSP-2105
DSP subsystem mounted on the PRISM card. It has its own ROM bank
(accessed via PLX `CS3`) and communicates with the main CPU through
BAR4 MMIO and a serial protocol.

The main CPU writes command bytes to the BAR4 register window. The
DCS-2 DSP decodes them, looks up the requested sample in its ROM and
outputs PCM audio through the cabinet's stereo amplifier and
subwoofer.

In Encore, ADSP emulation is bypassed entirely. The emulator
intercepts the BAR4 command writes, looks up the sample ID in a
pre-extracted container file and plays the corresponding WAV/OGG
through SDL_mixer.

The DCS-2 board also participates in the boot handshake. If the
handshake fails, the game produces no audio and may report a DCS
fault on the XINA console.

## 6. Switch matrix and coil drive — LPT port

The playfield is connected to the main PC through a proprietary
driver board attached to the standard DB-25 parallel port. The LPT
port serves triple duty: switch-matrix scanning (read), lamp-matrix
drive (write) and coil/solenoid activation (write).

The driver board uses MOSFETs (20N10L) rather than the bipolar
transistors found in earlier Williams platforms. Coils fire at 50 V;
lamps run at 18 V; flashers at 20 V.

The switch matrix is scanned by cycling row-select signals on LPT
output pins and reading column sense lines on the input port. Timing
is deterministic and is driven by the game's interrupt handler.

The emulator virtualises the entire LPT interface. In `--lpt-device`
mode, reads and writes are forwarded to a real parallel port through
the Linux `ppdev` interface. See
[19-real-lpt-passthrough.md](19-real-lpt-passthrough.md).

## 7. Optional hardware

* **Network card.** An SMC8416T 10 Mbps ISA Ethernet card can be
  installed for tournament play, bookkeeping and internet
  high-score submission via the mypinballs.com service. Only the
  SMC8416T (and its BT/BTA variants) is recognised by the firmware.
* **Barcode reader.** An RS-232 serial barcode reader can be
  installed for tournament player login.

These are stubbed in Encore; network and barcode features are not
currently implemented.

## 8. Common failure modes

* **Motherboard capacitors.** The most widespread failure. Five
  electrolytic capacitors on the motherboard power-supply circuit
  bulge and leak (typically 1000 µF / 6.3 V TAYEH parts). Standard
  fix: replace with 1000 µF / 10 V low-ESR types (e.g.
  Nichicon UHE0J102MPD).
* **AT power supply.** Ageing supplies cause erratic behaviour or
  failure to boot. ATX replacements need a "power-on" pin
  modification.
* **CPU fan.** Fan and the foam strip above it both degrade with
  heat; the foam sags onto the blades and stops the fan. Trim the
  foam and replace the fan proactively.
* **PRISM battery.** A dead BR2325 between the two PRISM boards
  causes SEEPROM data loss and configuration reset on every
  power-on.
* **Capacitor leakage near ROM traces.** Leaking motherboard caps
  can damage nearby PCB traces, producing intermittent ROM-access
  errors.
* **LPT and driver-board connectors.** Vibration loosens spade
  connectors on coil lugs; loose connections arc and damage MOSFETs.

## 9. Finding original documentation

* The Williams Pinball 2000 Schematic Manual (document number
  16-10882, February 1999) is archived at the Internet Pinball
  Machine Database (IPDB) and covers circuit-level detail for both
  RFM and SWE1.
* The Pinball 2000 Safety Manual (16-10878) covers high-voltage
  safety procedures.
* Game-specific operator manuals contain switch and lamp matrices,
  fuse charts and playfield part numbers.
* Community resources include IPDB and various collector forums —
  search for "Pinball 2000 repair" for current community guides.

This primer is not a service guide. Always consult the original
Williams documentation for electrical specifications and safety
information.

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
