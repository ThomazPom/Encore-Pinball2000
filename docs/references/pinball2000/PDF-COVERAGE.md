# Preserved PDF coverage

This is the byte-level inventory of PDF documents retained by the Pinball 2000
knowledge archive. It exists to make omissions visible: PDFs embedded several
levels down in a captured web page must not disappear from the human index.

> [!IMPORTANT]
> **State on 2026-11-15:** 23 PDF files are preserved. The audit rendered every
> file, checked its page count and compared its SHA-256 identity with the local
> forensic/FlipperFiles corpus. The three additions from this pass are marked
> **recovered**. A retained PDF is evidence, not a guarantee that every claim or
> drawing inside it is correct.

## Factory manuals and schematics

| Document | Pages | Class | Preserved file | Qualification |
|---|---:|---:|---|---|
| Revenge From Mars operations manual, February 1999, raw scan (`16-50070.1-101`) | 120 | A | [PDF](sources/rfm-operations-manual-february-1999/raw/Bally%20-%20Revenge%20from%20Mars.pdf) | fidelity copy of the early edition |
| Revenge From Mars operations manual, February 1999, searchable OCR | 120 | A derivative | [PDF](sources/rfm-operations-manual-february-1999/raw/Bally_1999_Revenge_From_Mars_Operations_Manual_February_1999_OCR_searchable.pdf) | readable derivative kept beside the raw scan, not independent evidence |
| Revenge From Mars operations manual, April 1999 (`16-50070.2-101`) | 132 | A | [PDF](sources/rfm-operations-manual-april-1999/raw/1072932404-revenge-from-mars-pinbal-2000.pdf) | distinct later manual edition |
| Star Wars Episode I operations manual, July 1999 (`16-50069.1-101`) | 136 | A | [PDF](sources/swe1-operations-manual/raw/Williams_1999_Star_Wars_Episode_I_Manual.pdf) | production game manual |
| Pinball 2000 schematic manual (`16-10882`) | 15 | A | [PDF](sources/pinwiki-repair/assets/Bally_Pinball_2000_Schematic_Manual_16-10882_OCR_searchable.pdf) | shared cabinet, PRISM, original Williams audio-amplifier and power-driver schematics |
| Pinball 2000 safety manual (`16-10878`) | 229 | A | [PDF](sources/pinwiki-repair/assets/Pinball_2000_Safety_Manual_16-10878_Feburary_1999.pdf) | multilingual factory safety manual |

There is no separate game-specific SWE1 schematic manual in the audited corpus:
files advertised under that name contain the same shared `16-10882` platform
manual. They are therefore duplicates, not missing coverage.

## Repair and hardware references

| Document | Pages | Class | Preserved file | Qualification |
|---|---:|---:|---|---|
| Repairing Williams/Bally Pinball 2000, 2008 revision | 81 | C | [PDF](sources/pinball2000-repair-guide-2008/raw/PINBALL_%20EN%20Repairing%20Williams,%20Bally%20Pinball%202000,%20Revenge%20from%20Mars.pdf) | **recovered**; broad repair guide and later than the surviving 61-page 2003 mirror |
| Wells-Gardner K7300 series repair guide | 2 | C | [PDF](sources/wells-gardner-k7300-repair-guide/raw/wells-gardner-k7300-series-repair.pdf) | **recovered**; directly covers the P2K `19K7302`, common failures, parts and controls |
| Cyrix 586 GXM-AV motherboard manual | 11 | component manual | [PDF](sources/pinwiki-repair/assets/Cyrix-Motherboard-gxm-av.pdf) | board identification, jumpers and connectors |
| InformTech IT-586GXm+ motherboard manual | 25 | component manual | [PDF](sources/pinwiki-repair/assets/Cyrix-Motherboard-it-596-gxm.pdf) | board identification, setup and BIOS reference |
| Dataexpert MGX7520 motherboard manual | 57 | component manual | [PDF](sources/pinwiki-repair/assets/Dataexpert_MGX7520_A_motherboard_manual.pdf) | board variants, layout and configuration |
| GCT ST-MGXM motherboard manual | 27 | component manual | [PDF](sources/pinwiki-repair/assets/ST-MGXM_motherboard_manual.pdf) | board layout and setup |
| Duckscan CGM-1901CW capacitor list | 2 | C | [PDF](sources/pinwiki-repair/assets/DuckScanCap.pdf) | monitor-specific service aid |
| Big Guys replacement audio-amplifier schematic | 1 | C | [PDF](sources/pinwiki-repair/assets/Bigguys-pin2000-audio-amp-schematic.pdf) | complete analogue-input replacement for using the P2K speakers with a modern PC; not the factory board |
| RFM direct saucer-light modification drawing | 1 | C | [PDF](sources/pinwiki-repair/assets/RFM-RH-Direct-Saucer-Mod.pdf) | **recovered** after an invisible Unicode mark hid the reference; PinWiki explicitly warns that LED orientation in the drawing is wrong |

## Original monitor sheets

The historical monitor page distributed each original manual as three separate
one-page PDFs. Combined three-page copies found elsewhere add no content.

| Monitor | Sheet | Pages | Preserved file |
|---|---|---:|---|
| Ducksan CGM-1901CW | 1 | 1 | [PDF](sources/pinball2000de-monitor/assets/ducksan01.pdf) |
| Ducksan CGM-1901CW | 2 | 1 | [PDF](sources/pinball2000de-monitor/assets/ducksan02.pdf) |
| Ducksan CGM-1901CW | 3 | 1 | [PDF](sources/pinball2000de-monitor/assets/ducksan03.pdf) |
| Wells-Gardner 19K7302 | 1 | 1 | [PDF](sources/pinball2000de-monitor/assets/wellsg01.pdf) |
| Wells-Gardner 19K7302 | 2 | 1 | [PDF](sources/pinball2000de-monitor/assets/wellsg02.pdf) |
| Wells-Gardner 19K7302 | 3 | 1 | [PDF](sources/pinball2000de-monitor/assets/wellsg03.pdf) |

## Operation and update aids

| Document | Pages | Class | Preserved file | Qualification |
|---|---:|---:|---|---|
| CipherLab 1022 barcode-scanner manual | 23 | component manual | [PDF](sources/mypinballs-tournament-technical-setup/assets/manual.pdf) | required to reproduce the documented COM2 tournament-reader setup |
| Pinball 2000 Update note | 1 | C | [PDF](sources/pinball2000de-techinfo/assets/Pinball2000%20Update.pdf) | community serial-update procedure retained with its source page |

## Audited but not copied

| Candidate | Decision | Reason |
|---|---|---|
| 61-page 2003 Pinball 2000 repair guide | duplicate/superseded | the retained 81-page 2008 revision is materially later and broader |
| French translation of the 2008 repair guide | duplicate translation | improves language access but adds no technical evidence; the canonical English source is retained |
| 18-page PDF export of old PinWiki | duplicate/superseded | the self-contained PinWiki bundle is newer, searchable and retains its original assets |
| alternative RFM/SWE1 manual and schematic downloads | duplicate | page content matches a better-provenanced retained edition; filenames and compression differ |
| combined three-page Wells-Gardner manual | duplicate | same three sheets already preserved from the historical monitor page |
| updater UI instruction sheet and updater revision notes | reject here | documentation of one third-party desktop tool, not durable P2K hardware/protocol knowledge |
| NuCore user manual | out of scope | separate emulator ecosystem explicitly excluded from this archive |
| SWE1 marketing flyer and commercial manual listings | reject/index-only | no unique service or engineering knowledge |

> [!NOTE]
> Exhaustive means exhaustive for the qualified corpus and the multilingual web
> searches performed on 2026-11-15—not a claim that no unknown private factory
> document exists. The Strong Museum finding aid still points to uncopied
> internal Pinball 2000 records that require archive access.

[Knowledge archive](README.md) · [Source catalog](CATALOG.md) ·
[Preserved source bundles](sources/README.md) ·
[Preservation method](METHODOLOGY.md)
