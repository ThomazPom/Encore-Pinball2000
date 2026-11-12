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
[general technical page](../sources/pinball2000de-techinfo/README.md) and the
[PRISM deep dive](../sources/pinball2000de-prism/README.md).

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
[Networking](network-and-tournament.md) ·
[All preserved sources](../sources/README.md)
