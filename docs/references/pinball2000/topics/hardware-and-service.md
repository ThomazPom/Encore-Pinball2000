# Hardware and service

The original platform deliberately separated commodity PC hardware from the
Pinball 2000-specific PRISM and power-driver functions.

```text
MediaGX PC / CX5520
├── PCI: PRISM
│   ├── masked game ROMs
│   ├── game and sound flash
│   ├── battery-backed data
│   └── DCS-2 audio
├── parallel port: cabinet power-driver board
├── video: 640x240 low-resolution CRT
├── serial: software update and optional peripherals
└── optional ISA Ethernet: SMC EtherEZ family
```

This is a conceptual map, not a replacement for the factory schematic retained
inside [PinWiki repair](../sources/pinwiki-repair/README.md).

## What is strongly established

- Williams identified the CPU as a Cyrix MediaGX motherboard in Baby-AT form,
  with game ROM, flash, battery-backed memory and DCS-2 on PRISM.
- Repair sources consistently require the CX5520 companion chipset; visually
  similar CX5510/CX5530 boards do not reproduce the shipped system behavior.
- The cabinet power-driver board communicates through the PC parallel port and
  exposes diagnostic features intended to isolate cabinet faults.
- The shipped monitor is an unmodified 19-inch low-resolution arcade CRT. Its
  darkness, geometry and reflection are part of the presentation, not merely a
  generic VGA output device.

Start with the [factory FAQ](../sources/williams-faq/README.md), then use
[PinWiki](../sources/pinwiki-repair/README.md), the
[2008 repair guide](../sources/pinball2000-repair-guide-2008/README.md), the
[general technical page](../sources/pinball2000de-techinfo/README.md) and the
[PRISM deep dive](../sources/pinball2000de-prism/README.md).

For CRT work, the historical monitor page retains the original Ducksan and
Wells-Gardner sheets. The separate
[K7300 repair guide](../sources/wells-gardner-k7300-repair-guide/README.md)
adds `19K7302` chassis identifiers, common component failures, capacitor values
and remote-control-board settings. The complete document inventory and the
duplicate/rejection decisions are in [PDF coverage](../PDF-COVERAGE.md).

## What sits where on PRISM

The removable PRISM assembly is a sandwich, and the distinction between its
halves explains several otherwise surprising service outcomes:

- the socketed masked game-image ROMs and sound ROMs are changed for an
  RFM/SWE1 conversion;
- updateable game/sound flash remains associated with the main PRISM board;
- battery-backed PRISM data is a separate concern again;
- changing only the ROM daughterboard can therefore leave flash content from
  the previous game and requires the matching update to be applied.

First-hand testimony preserved in the
[PRISM deep dive](../sources/pinball2000de-prism/README.md) describes the game
flash as overlaying part of the larger masked-ROM address space. If flash is
invalid, the masked image supplies a bootable fallback from which recovery is
possible. The remembered `64 MiB` masked-image and `4 MiB` overlay figures are
useful architecture evidence, but remain testimony rather than a replacement
for measured address decoding.

> [!WARNING]
> The sandwich connector and socketed ROM pins are easy to damage. The
> preserved swap record specifically warns that paper ROM labels can obscure
> the true package notch; pin 1 must be established from the package and board,
> not label orientation.

## Power-driver board

The cabinet interface is not a collection of PC peripherals. The parallel-port
power-driver board switches coils and lamps, scans cabinet inputs and reports
diagnostic conditions. Preserved technical material describes MOSFET coil
drivers, two lamp matrices totaling 128 controlled lamps, driver fault
detection and power-loss sensing. Pinouts, fuse values and test procedures stay
in the factory manual and schematics rather than being retyped here.

The practical consequence for a modern controller is important: reproducing
the screen and buttons does not reproduce the electrical behavior or safety
boundaries of the original cabinet interface.

## Display signal and replacement limits

The original display path is low-resolution analogue RGB at arcade/CGA-like
timing, with negative sync and geometry arranged for the cabinet mirror. It is
not ordinary 640x480 VGA merely because it uses a familiar 15-pin connector.
The preserved [monitor information](../sources/pinball2000de-monitor/README.md)
identifies Ducksan `CGM-1901CW` and Wells-Gardner `19K7302` hardware and records
qualified field replacements:

- a K7300-family chassis is the closest documented service route;
- some K7200/K7203 substitutions require mounting changes and sync/pin moves;
- a successful field substitution for one revision is not a universal drop-in
  claim.

Use the factory drawing plus the exact chassis manual before moving conductors.
The [K7300 repair guide](../sources/wells-gardner-k7300-repair-guide/README.md)
is the retained component-level reference.

A community video-amplifier procedure adds two non-obvious checks. The
Wells-Gardner and Ducksan chassis do not use the sync inputs identically, and
the P2K video connector may not provide the `+5 V` on pin 9 expected by the
aftermarket amplifier. A board that produces no picture may therefore be
unpowered rather than incompatible. This remains a chassis-specific field
procedure; follow the illustrated [PinWiki video-amplifier section](../sources/pinwiki-repair/README.md#video-amplifier)
instead of copying a pin move from another monitor.

The 2008 repair guide also records two useful behavioral facts: the CRT
degausses on a true cold start, and an upside-down replacement display can be
corrected in XINA with `fb flip` (the source is unsure whether that command must
be repeated after reboot). Neither observation fixes left/right mirroring.

## Replacing the original AT power supply with ATX

The original PC supply is an ageing AT unit. PinWiki preserves two ways to use
a modern ATX supply while keeping cabinet power-up behavior:

1. use a 20-pin ATX-to-AT `P8/P9` adapter with provision for a power switch; or
2. adapt the ATX `PS_ON` control, normally the green wire, so it is pulled to
   ground when the computer is meant to run.

Permanently grounding `PS_ON` makes the ATX supply start whenever cabinet AC
is applied and the supply's rear switch is on. Routing `PS_ON` through the
original computer-case switch instead preserves the ability to restart only
the PC. This is electrical control of the supply, not a BIOS “restore after
power loss” setting. Both the [2008 repair guide](../sources/pinball2000-repair-guide-2008/README.md)
and PinWiki describe the active-low green-wire behavior. See the illustrated
[PinWiki ATX procedure](../sources/pinwiki-repair/README.md#installing-an-atx-power-supply).

> [!WARNING]
> The Pinball 2000 computer is normally fed approximately 120 V by the cabinet
> transformer even in a 230/240 V country. A supply removed for bench testing
> instead sees local mains. Verify the actual input and selector before every
> bench/cabinet transition; do not blindly retain the last voltage setting.

## Audio amplifier and a modern PC

The cabinet speakers are passive and still need a power amplifier. In the
original system, PRISM feeds the Williams amplifier through its `J1` game-audio
connector; the amplifier then drives the left backbox, right backbox and
cabinet speaker channels.

```text
original:   PRISM audio ── J1 ── Williams amplifier ── three speaker channels
modern PC:  line output ──────── replacement amplifier ── same speakers
```

Two preserved paths support a modern PC:

- the [Big Guys replacement schematic](../sources/pinwiki-repair/assets/Bigguys-pin2000-audio-amp-schematic.pdf)
  documents a complete analogue-input replacement amplifier;
- the original Williams amplifier has an unpopulated stereo-input footprint,
  `J4`. The community procedure in
  [PinWiki repair](../sources/pinwiki-repair/README.md#modifying-the-original-audio-amplifier)
  connects a 3.5 mm cable to `J4` and adds a `10 kΩ` resistor plus a
  `10 µF / 50 V` capacitor around the documented `J1` pin 1 and 5 V bias
  connection.

> [!CAUTION]
> The Williams schematic establishes the original amplifier circuit; the `J4`
> conversion is a community repair procedure, not a Williams modification
> bulletin. Confirm the board revision, capacitor polarity and connector
> orientation on the physical board before soldering.

## Service claims that need care

Clock-reduction advice, power-supply quality judgements and some compatible
motherboard lists are field experience rather than Williams specifications.
They are useful repair leads, but model/revision and measurement conditions
matter. The archive preserves the claim without upgrading it to a universal
rule.

> [!WARNING]
> PRISM separation, CRT servicing and live cabinet work can damage scarce
> hardware or expose dangerous voltages. The archive establishes provenance;
> it is not a substitute for safe service practice.

## Conversion and modularity

Williams designed the cabinet, removable logic box, wiring and playfield
connections around faster field conversion. The surviving SWE1 kit evidence
shows that the conversion supplied a playfield, art, shooter, manual, ROMs and a
PRISM-separation tool—not a second complete PRISM. See the
[platform overview](../sources/williams-platform-overview/README.md),
[kit contents](../sources/pinball2000de-swe1-conversion-kit/README.md) and
[swap procedure](../sources/pinball2000de-game-swap/README.md).

[Architecture](architecture-and-development.md) ·
[Operation and diagnostics](operation-and-diagnostics.md) ·
[Networking](network-and-tournament.md) ·
[All preserved sources](../sources/README.md)
