# Knowledge coverage ledger

This ledger prevents preserved knowledge from becoming a pile of files that is
technically searchable but practically invisible. Every accepted source bundle
appears exactly once below and has an explicit route into the readable topic
layer.

> [!IMPORTANT]
> “Covered” does not mean that every sentence was copied. It means the source
> was reviewed, its durable contribution was identified, and a reader can reach
> either the distilled conclusion or the primary detail from a topic guide.

## Status legend

| Status | Meaning |
|---|---|
| **distilled** | durable conclusions are stated in a topic guide, with the source linked |
| **routed** | specialized detail remains in the source, but a topic guide explains when to open it |
| **primary artifact** | the source is chiefly a manual, schematic or revision record; the guide routes to it rather than duplicating it |
| **qualified** | useful testimony or field observation is surfaced with its uncertainty intact |

## Source-to-topic matrix

| Preserved bundle | Type | Status | Durable contribution and visible route |
|---|---|---|---|
| [gerwiki-xina](sources/gerwiki-xina/README.md) | technical synthesis | distilled | XINA shell, commands, serial access and network services in [architecture](topics/architecture-and-development.md) and [networking](topics/network-and-tournament.md) |
| [mypinballs-rfm-code-updates](sources/mypinballs-rfm-code-updates/README.md) | community revision log | routed | post-Williams RFM releases are kept separate from factory history in [software updates](topics/software-updates.md) |
| [mypinballs-rfm-update-help](sources/mypinballs-rfm-update-help/README.md) | updater troubleshooting | routed | practical historical updater failures are routed from [software updates](topics/software-updates.md) |
| [mypinballs-tournament-game-setup](sources/mypinballs-tournament-game-setup/README.md) | operating instructions | distilled | tournament menus, addressing, COM2 barcode reader and barcode-free play in [networking](topics/network-and-tournament.md) |
| [mypinballs-tournament-technical-setup](sources/mypinballs-tournament-technical-setup/README.md) | deployment instructions | qualified | historical router, port and barcode-reader setup in [networking](topics/network-and-tournament.md), without promoting obsolete public settings |
| [pinball-news-rfm-online](sources/pinball-news-rfm-online/README.md) | contemporary report / direct account | distilled | demonstrated tournament scope and traffic model in [networking](topics/network-and-tournament.md) |
| [pinball2000-development-lore-part-1](sources/pinball2000-development-lore-part-1/README.md) | first-hand testimony | distilled | workstation, TFTP/ISA boot and development workflow in [architecture](topics/architecture-and-development.md) |
| [pinball2000-development-lore-part-2](sources/pinball2000-development-lore-part-2/README.md) | first-hand testimony | distilled | framebuffer, RGB555, geometry and memory constraints in [architecture](topics/architecture-and-development.md) |
| [pinball2000-development-lore-part-3](sources/pinball2000-development-lore-part-3/README.md) | first-hand testimony | distilled | transparency and compression trade-offs in [architecture](topics/architecture-and-development.md) |
| [pinball2000-development-lore-part-4](sources/pinball2000-development-lore-part-4/README.md) | first-hand testimony | distilled | pre-emptive/co-operative execution and allocation ownership in [architecture](topics/architecture-and-development.md) |
| [pinball2000-development-lore-part-5](sources/pinball2000-development-lore-part-5/README.md) | first-hand testimony | distilled | display effects, priority and sprite behavior in [architecture](topics/architecture-and-development.md) |
| [pinball2000-development-lore-part-6](sources/pinball2000-development-lore-part-6/README.md) | first-hand testimony | distilled | deliberate compatibility mode for byte-reproducible old builds in [architecture](topics/architecture-and-development.md) |
| [pinball2000-repair-guide-2008](sources/pinball2000-repair-guide-2008/README.md) | repair synthesis | distilled | captured invalid-RTC diagnostic reboot, ATX `PS_ON`, cold-start degauss and `fb flip` behavior in [operation](topics/operation-and-diagnostics.md) and [hardware](topics/hardware-and-service.md) |
| [pinball2000de-game-swap](sources/pinball2000de-game-swap/README.md) | field procedure | distilled | conversion order, update/ROM interaction and post-swap reset in [hardware](topics/hardware-and-service.md) and [software updates](topics/software-updates.md) |
| [pinball2000de-making-of](sources/pinball2000de-making-of/README.md) | first-person seminar transcript | distilled | Holopin concept and production design goals in [history](topics/prototypes-and-history.md) |
| [pinball2000de-misc](sources/pinball2000de-misc/README.md) | mixed archive | qualified | early Ethernet observations, serial-update leads, country variants and machine differences routed from [operation](topics/operation-and-diagnostics.md) and [history](topics/prototypes-and-history.md) |
| [pinball2000de-monitor](sources/pinball2000de-monitor/README.md) | specialist technical page | distilled | original CRT signal, chassis and qualified replacement paths in [hardware](topics/hardware-and-service.md) |
| [pinball2000de-playboy](sources/pinball2000de-playboy/README.md) | historical archive | routed | surviving proposed-game material in [history](topics/prototypes-and-history.md) |
| [pinball2000de-prism](sources/pinball2000de-prism/README.md) | technical synthesis / testimony | distilled | PRISM separation, masked-ROM daughterboard and flash overlay/fallback in [hardware](topics/hardware-and-service.md) and [software updates](topics/software-updates.md) |
| [pinball2000de-rfm-history](sources/pinball2000de-rfm-history/README.md) | production history | routed | RFM chronology and production context in [history](topics/prototypes-and-history.md) |
| [pinball2000de-rfm-misc](sources/pinball2000de-rfm-misc/README.md) | mixed game archive | qualified | version provenance, MEL evidence and gameplay observations routed from [history](topics/prototypes-and-history.md) and [operation](topics/operation-and-diagnostics.md) |
| [pinball2000de-rfm-prototypes](sources/pinball2000de-rfm-prototypes/README.md) | photographed observations | routed | observable RFM prototype differences in [history](topics/prototypes-and-history.md) |
| [pinball2000de-rfm-techinfo](sources/pinball2000de-rfm-techinfo/README.md) | field repair page | routed | RFM-specific mechanical, switch, opto and playfield failure leads in [operation and diagnostics](topics/operation-and-diagnostics.md) |
| [pinball2000de-swe1-conversion-kit](sources/pinball2000de-swe1-conversion-kit/README.md) | photographed kit record | distilled | conversion-kit contents in [hardware](topics/hardware-and-service.md) |
| [pinball2000de-swe1-misc](sources/pinball2000de-swe1-misc/README.md) | mixed game archive | qualified | SWE1 1.4, MEL and production observations routed from [history](topics/prototypes-and-history.md) and [operation](topics/operation-and-diagnostics.md) |
| [pinball2000de-swe1-prototypes](sources/pinball2000de-swe1-prototypes/README.md) | photographed observations | routed | observable SWE1 prototype differences in [history](topics/prototypes-and-history.md) |
| [pinball2000de-swe1-techinfo](sources/pinball2000de-swe1-techinfo/README.md) | field repair page | routed | SWE1-specific switch, opto and centre-shield failure leads in [operation and diagnostics](topics/operation-and-diagnostics.md) |
| [pinball2000de-techinfo](sources/pinball2000de-techinfo/README.md) | technical synthesis / testimony | distilled | chipset compatibility, PRISM layout and power-driver behavior in [hardware](topics/hardware-and-service.md) |
| [pinball2000de-troubleshooting](sources/pinball2000de-troubleshooting/README.md) | field repair page | distilled | safety-critical power/switch warnings and discriminating fault symptoms in [operation and diagnostics](topics/operation-and-diagnostics.md) |
| [pinball2000de-wizard-blocks](sources/pinball2000de-wizard-blocks/README.md) | historical archive | routed | surviving third-game evidence in [history](topics/prototypes-and-history.md) |
| [pinwiki-repair](sources/pinwiki-repair/README.md) | repair synthesis / document carrier | distilled | broad service map, factory schematics, ATX replacement/auto-power and original-amplifier conversion in [hardware](topics/hardware-and-service.md) and [operation](topics/operation-and-diagnostics.md) |
| [rfm-operations-manual-april-1999](sources/rfm-operations-manual-april-1999/README.md) | factory manual | primary artifact | later RFM operation, adjustment, diagnostic and wiring reference from [operation and diagnostics](topics/operation-and-diagnostics.md) |
| [rfm-operations-manual-february-1999](sources/rfm-operations-manual-february-1999/README.md) | factory manual | primary artifact | early RFM manual retained for revision comparison from [operation and diagnostics](topics/operation-and-diagnostics.md) |
| [swe1-operations-manual](sources/swe1-operations-manual/README.md) | factory manual | primary artifact | SWE1 operation, adjustment, diagnostics and wiring from [operation and diagnostics](topics/operation-and-diagnostics.md) |
| [wells-gardner-k7300-repair-guide](sources/wells-gardner-k7300-repair-guide/README.md) | chassis repair guide | primary artifact | P2K monitor-family repair data routed from [hardware](topics/hardware-and-service.md) |
| [williams-faq](sources/williams-faq/README.md) | factory FAQ | distilled | product architecture, display, PRISM, flash and service choices across [hardware](topics/hardware-and-service.md) and [architecture](topics/architecture-and-development.md) |
| [williams-platform-overview](sources/williams-platform-overview/README.md) | factory overview | distilled | modularity, diagnostics and field-update design across [hardware](topics/hardware-and-service.md) and [operation](topics/operation-and-diagnostics.md) |
| [williams-rfm-revision-history](sources/williams-rfm-revision-history/README.md) | factory revision log | primary artifact | official RFM changes through 1.50 in [software updates](topics/software-updates.md) |
| [williams-swe1-revision-history](sources/williams-swe1-revision-history/README.md) | factory revision log | primary artifact | official SWE1 changes through 1.40 in [software updates](topics/software-updates.md) |
| [williams-update-instructions](sources/williams-update-instructions/README.md) | factory procedure | distilled | serial update sequence and true completion condition in [software updates](topics/software-updates.md) |
| [wms-software-history](sources/wms-software-history/README.md) | first-hand testimony | distilled | PERC/APPLE/XINA lineage, toolchain and debugging in [architecture](topics/architecture-and-development.md) |

## What this ledger does not claim

- A field fix is not promoted to a factory-approved repair because it is useful.
- A remembered number is not promoted to a hardware specification without
  corroboration.
- Game-specific switch, coil, lamp and connector tables remain in the factory
  manuals; duplicating them would create a second, drifting manual.
- Raw captures remain the authority for exact wording, dates, pinouts and
  photographs.

## Deep extraction checkpoints

The largest synthesis pages need section-level review in addition to their row
above. The current promoted examples make that review visible:

| Broad source area | Promoted into topics | Detail deliberately left at source |
|---|---|---|
| PinWiki power and PC | ATX `PS_ON` auto-power, cabinet/bench voltage distinction, fan/foam obstruction, capacitor and mounting-post symptoms | illustrated rewiring sequence, component dimensions and supplier part numbers |
| PinWiki display | chassis identity, replacement limits, sync and video-amplifier `+5 V` caveat | chassis pin moves and component-level repair tables |
| PinWiki audio | original `J1` path, `J4` analogue-input conversion and replacement-amplifier route | solder-point photographs and the full amplifier schematic |
| PinWiki lamps/coils/switches | LED diagnostic/fade trade-off, loose coil-lug hazard and replacement-board false coin-door symptom | exact matrices, connectors, fuse values and individual part tables |
| 2008 repair guide | invalid-timestamp reboot on entering diagnostics, ATX `PS_ON`, cold-start degauss and `fb flip` | full board-level troubleshooting sequences and component tables |
| Pinball2000.de miscellaneous | country variants and early Ethernet/update observations | dated correspondence, peripheral experiments and collection trivia |

This table is expanded when another multi-section source yields a new promoted
fact; it is not a substitute for the source-to-topic matrix.

This matrix was reconciled against the bundle directories on `2026-11-15`.
Future preservation passes must add a row and a topic route for every accepted
bundle.

[Knowledge archive](README.md) · [Source index](sources/README.md) ·
[Preservation method](METHODOLOGY.md)
