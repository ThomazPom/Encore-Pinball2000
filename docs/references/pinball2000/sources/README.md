# Preserved source bundles

This directory is the evidence layer of the Pinball 2000 knowledge archive.
Each link opens a readable derivative; the same bundle retains the raw capture,
its provenance and byte-level checksums.

> [!TIP]
> Start with the [topic guides](../README.md#topic-guides) when looking for an
> answer. Come here when you need the original wording, drawings, photographs
> or provenance behind it.

## Factory and contemporary Williams material

| Source | Class | What it contributes |
|---|---:|---|
| [Platform overview](williams-platform-overview/README.md) | A | Williams' contemporary statement of modularity, service, diagnostics and update methods |
| [Pinball 2000 FAQ](williams-faq/README.md) | A | cabinet, display, PRISM, flash, playfield and product-design choices |
| [Serial update instructions](williams-update-instructions/README.md) | A | null-modem wiring, updater sequence and the real completion criterion after reboot |
| [RFM revision history](williams-rfm-revision-history/README.md) | A | factory change log through 1.50 |
| [SWE1 revision history](williams-swe1-revision-history/README.md) | A | factory change log through 1.40 |
| [RFM operations manual, February 1999](rfm-operations-manual-february-1999/README.md) | A | early manual in raw-scan and searchable forms |
| [RFM operations manual, April 1999](rfm-operations-manual-april-1999/README.md) | A | later `16-50070.2-101` manual edition |
| [SWE1 operations manual](swe1-operations-manual/README.md) | A | operation, adjustments, diagnostics and game-specific wiring |

The platform schematic and safety manuals are retained as referenced artifacts
inside [PinWiki repair](pinwiki-repair/README.md), avoiding duplicate copies.

## Architecture and first-hand development history

| Source | Class | What it contributes |
|---|---:|---|
| [Williams software history](wms-software-history/README.md) | B | direct accounts of PERC, APPLE and XINA; C++, XINU, TFTP, GDB, MediaGX, compression and debugging |
| [Development lore, part 1](pinball2000-development-lore-part-1/README.md) | B | prototype, development workstation and TFTP/ISA boot workflow |
| [Part 2](pinball2000-development-lore-part-2/README.md) | B | framebuffer, RGB555, 640x240, ROM/RAM constraints and DisplayManager |
| [Part 3](pinball2000-development-lore-part-3/README.md) | B | transparency and image-compression trade-offs |
| [Part 4](pinball2000-development-lore-part-4/README.md) | B | pre-emptive/co-operative threading and allocation ownership |
| [Part 5](pinball2000-development-lore-part-5/README.md) | B | display effects, priorities, thresholds and sprites |
| [Part 6](pinball2000-development-lore-part-6/README.md) | B | deterministic-build compatibility incident and development culture |
| [George Gomez making-of transcript](pinball2000de-making-of/README.md) | B/C | product genesis, Holopin and the platform's design goals |

Parts 1–6 are static mirrors of first-person posts whose original Reddit URLs
remain linked in every bundle. Later parts are catalogued but not mirrored here
because no similarly clean, independent static source was found.

## Original hardware, repair and operation

| Source | Class | What it contributes |
|---|---:|---|
| [PinWiki repair](pinwiki-repair/README.md) | C | the broadest repair synthesis, board photographs, manuals, compatible motherboards and diagnostics |
| [General technical information](pinball2000de-techinfo/README.md) | C/B | platform overview plus quoted developer details |
| [PRISM information](pinball2000de-prism/README.md) | C/B | board separation, ROM/flash organization and first-hand failure discussion |
| [Troubleshooting](pinball2000de-troubleshooting/README.md) | C | boot, board, video and field-repair observations |
| [Monitor information](pinball2000de-monitor/README.md) | C | original CRT behavior, geometry and replacement considerations |
| [RFM/SWE1 game swap](pinball2000de-game-swap/README.md) | C | physical conversion sequence and failure modes |
| [RFM technical information](pinball2000de-rfm-techinfo/README.md) | C | game-specific switches, lamps, coils and known issues |
| [SWE1 technical information](pinball2000de-swe1-techinfo/README.md) | C | game-specific technical notes |
| [SWE1 conversion kit](pinball2000de-swe1-conversion-kit/README.md) | C | documented kit contents and conversion context |

## Networking, tournament play and community updates

| Source | Class | What it contributes |
|---|---:|---|
| [GerWiki XINA/P2K](gerwiki-xina/README.md) | C/D | XINA commands, Ethernet, telnet/HTTP and historical network notes |
| [Tournament technical setup](mypinballs-tournament-technical-setup/README.md) | C | router, ports, RAM requirement and RS-232 barcode-reader setup |
| [Tournament game setup](mypinballs-tournament-game-setup/README.md) | C | in-game tournament, addressing, pricing and COM2 settings |
| [RFM goes online](pinball-news-rfm-online/README.md) | C/B | contemporary report and direct account of the Internet tournament protocol's operational scope |
| [RFM community update log](mypinballs-rfm-code-updates/README.md) | C | community releases and their stated changes |
| [RFM update help](mypinballs-rfm-update-help/README.md) | C | practical updater troubleshooting |

> [!WARNING]
> Old server addresses, credentials and router instructions are historical
> evidence, not safe current defaults. Encore's current network behavior is
> documented in the main project documentation.

## Games, prototypes and production history

| Source | Class | What it contributes |
|---|---:|---|
| [RFM history](pinball2000de-rfm-history/README.md) | C | chronology and production context |
| [RFM prototypes](pinball2000de-rfm-prototypes/README.md) | C | observable prototype/production differences |
| [RFM notes and FAQ](pinball2000de-rfm-misc/README.md) | C/D | field observations and contemporary correspondence |
| [SWE1 prototypes](pinball2000de-swe1-prototypes/README.md) | C | observable prototype/production differences |
| [SWE1 notes and FAQ](pinball2000de-swe1-misc/README.md) | C/D | field observations and production notes |
| [Wizard Blocks](pinball2000de-wizard-blocks/README.md) | C | surviving material about the third planned platform game |
| [Playboy](pinball2000de-playboy/README.md) | C | surviving material about another planned game |
| [Miscellaneous platform archive](pinball2000de-misc/README.md) | C/D | developer quotations, Ethernet experiments and scattered platform evidence |

## Known capture gaps

Four images referenced by old pages could not be recovered from the local
corpus or live-web searches. Their exact URLs remain in each manifest:

- `autoplunger.jpg` and `inside.jpg` in the game-swap page;
- `p2k.gif` in two miscellaneous pages;
- `swe1_plastics2.jpg` in the SWE1 miscellaneous page.

No text is missing from those bundles. The gaps are visible rather than being
silently replaced by guessed media.

[Knowledge archive](../README.md) · [Source catalog](../CATALOG.md) ·
[Preservation method](../METHODOLOGY.md)
