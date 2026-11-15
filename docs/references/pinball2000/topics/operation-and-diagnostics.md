# Operation and diagnostics

This guide is the route from a symptom to the best preserved evidence. It does
not replace the game-specific switch, lamp, coil, adjustment and wiring tables
in the factory manuals.

## Start with the right authority

| Need | Best first source |
|---|---|
| operate, adjust or diagnose RFM | [April 1999 RFM operations manual](../sources/rfm-operations-manual-april-1999/README.md) |
| compare an early RFM procedure or table | [February 1999 RFM operations manual](../sources/rfm-operations-manual-february-1999/README.md) |
| operate, adjust or diagnose SWE1 | [SWE1 operations manual](../sources/swe1-operations-manual/README.md) |
| broad symptom-based repair | [PinWiki repair](../sources/pinwiki-repair/README.md) and the [2008 repair guide](../sources/pinball2000-repair-guide-2008/README.md) |
| original schematics and theory | factory PDFs carried by [PinWiki repair](../sources/pinwiki-repair/README.md) |
| old field reports and unusual symptoms | [Pinball2000.de troubleshooting](../sources/pinball2000de-troubleshooting/README.md) |

The manuals are class A evidence, but known errata and later field discoveries
still matter. Community pages are diagnostic leads, not automatic permission
to modify a board.

## Entering diagnostics

The original cabinet exposes bookkeeping, adjustments and diagnostic tests
through the coin-door controls. PinWiki also preserves the keyboard route,
useful when servicing the PC section or a bench setup. Once inside, prefer the
built-in switch, lamp, coil, sound and display tests over guessing from game
play.

XINA itself has a larger engineering shell. Its commands can inspect processes,
memory, stacks, ROM/flash state, PDB, RTC and networking. Access and command
availability vary by software version; some commands can change persistent
state. See [architecture and development](architecture-and-development.md#observability-built-into-xina)
before treating the shell as an operator menu.

## Safety-critical distinctions

> [!WARNING]
> The CRT contains hazardous high voltage. The cabinet also has high-energy
> coil circuits. Power down before reseating boards, separating PRISM, moving
> connectors or probing a switch matrix unless a qualified live measurement
> specifically requires otherwise.

- A momentary contact between the roughly 50 V coil supply and switch wiring
  can damage much more than the touched switch; the preserved troubleshooting
  archive reports extensive logic damage from this mistake.
- The cabinet's internal PC supply may be fed by the machine transformer rather
  than the local wall voltage. A supply removed for bench testing must be set
  for the voltage actually applied on the bench. Field rewiring means the
  physical machine must be verified, not assumed.
- PRISM halves and socketed ROMs are orientation-sensitive. Paper labels are
  not a reliable pin-1 marker; use the package notch and board markings.

## Symptoms that discriminate between causes

| Symptom | Useful lead, not a blind prescription |
|---|---|
| balls recycle at every power-up or coin-door cycle | the RFM trough-eject opto has produced exactly this symptom; verify it in switch diagnostics |
| no sound after an RFM/SWE1 ROM swap | stale or mismatched sound flash can look like a hardware fault; apply the matching game-and-sound update |
| `U109`/sound-ROM complaint after a game swap | first rule out an update/ROM mismatch before replacing hardware |
| entering diagnostics/bookkeeping immediately reboots | the 2008 repair guide captured `Fatal: Got invalid time stamp!`; inspect the RTC through the `9600 8N1` serial shell, check the motherboard/PRISM batteries and correct the clock with XINA `time set` before blaming the coin-door controls |
| static or inter-channel audio leakage | a direct developer field report found crosstalk in tightly bundled PRISM-to-amplifier conductors; separating them is a reversible diagnostic test |
| PUB board does not take control during boot | a historical field fix reserves ISA memory block `C800` in BIOS; confirm the board and BIOS before changing it |
| game video absent on a replacement motherboard | a board that boots DOS is not necessarily P2K-compatible; the CX5520 companion chipset is the strongly supported baseline |

> [!CAUTION]
> One preserved page reports a speaker-connector pin-number error in a factory
> drawing, while another battery reference disagrees between `BR2325` and
> `BR2335`. These remain visible source conflicts. Inspect the actual board,
> connector orientation and battery marking before ordering or wiring parts.

## Preventive inspection with high diagnostic value

The broad [PinWiki repair source](../sources/pinwiki-repair/README.md) contains
several checks worth surfacing before a failure is misdiagnosed:

- the lid foam can detach and sag onto the CPU fan; loss of that fan can
  overheat the MediaGX system, so inspect clearance and fan rotation;
- five motherboard electrolytics are a documented source of erratic behavior
  and no-boot failures, particularly visibly bulged or leaking green Tayeh
  parts; replacement requires low-ESR parts of suitable physical size and care
  around the multilayer board;
- a mounting post beneath a replacement motherboard can touch a location where
  that board has no mounting hole and produce a permanent “coin door open”
  symptom; verify isolation before chasing the switch matrix;
- vibration can loosen uninsulated coil spade connectors, causing heat or a
  short to the adjacent lug; inspect the mechanical connection before blaming
  the driver MOSFET;
- LED playfield lamps draw too little current for the original burnt-lamp
  detector and may alter strobing and fade effects, so lamp-test failures after
  an LED conversion do not necessarily identify an open circuit.

These are discriminating observations, not a mandate to recap, trim, solder or
replace every surviving machine pre-emptively.

## Game-specific wear and failure routes

The long field pages are most valuable as symptom indexes:

- [RFM technical information](../sources/pinball2000de-rfm-techinfo/README.md)
  covers autofire alignment and bracket fatigue, skip-ramp binding, stand-up
  target shorts, trough/Stroke-of-Luck optos, shooter-lane wear and playfield
  cracking. Its repairs are owner reports and must be qualified per machine.
- [SWE1 technical information](../sources/pinball2000de-swe1-techinfo/README.md)
  covers C-3PO switch adjustment, a centre-target opto symptom and centre-shield
  binding after wear.
- [RFM notes](../sources/pinball2000de-rfm-misc/README.md) and
  [SWE1 notes](../sources/pinball2000de-swe1-misc/README.md) preserve version,
  production and gameplay observations that do not belong in a repair manual.
- The [miscellaneous platform archive](../sources/pinball2000de-misc/README.md)
  is the qualified route for country-specific cabinet differences, early
  Ethernet experiments, period serial-updater reports and scattered physical
  hardware observations that do not justify separate universal rules.

RFM 1.0 is specifically reported to overdrive flipper coils under 50 Hz
conditions; the preserved field page says to use 1.1 or later. That is a
version-specific operational warning, not evidence that every hot flipper coil
has the same cause.

## After changing games

A physical RFM/SWE1 conversion changes the playfield and masked/sound ROM set,
but flash and battery-backed settings can still describe the previous game.
The preserved swap procedure therefore says to disable update code before a
ROM change, load the matching game-and-sound update afterwards, and factory
reset settings that may otherwise be misinterpreted across games. Read the
[complete swap evidence](../sources/pinball2000de-game-swap/README.md) before
separating PRISM.

## Keep exact tables in one place

Part numbers, fuse ratings, connector pinouts and switch/lamp/coil matrices are
not reproduced here. Use the factory manual for the exact game and revision,
then check [PDF coverage](../PDF-COVERAGE.md) for the retained schematic and
service-document inventory. This avoids a convenient summary becoming a
second, stale wiring manual.

[Hardware and service](hardware-and-service.md) ·
[Software updates](software-updates.md) ·
[Knowledge coverage](../KNOWLEDGE-COVERAGE.md)
