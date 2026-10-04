# Update provenance and redistribution boundary

Encore preserves several generations of Pinball 2000 game-code updates. This
page records what is actually present, which source container survives, and
where provenance remains incomplete. It is not a licence grant and it is not a
substitute for the [game-code changelogs](50-game-changelogs.md).

> [!IMPORTANT]
> A tracked payload is evidence of preservation, not proof that Encore owns or
> may relicense it. The repository still lacks a project-level licence and a
> complete asset provenance/notice inventory. Resolve those before describing
> a release archive as generally redistributable.

## Identities

Pinball 2000 update names use the game number and a four-digit version:

| Game | Short name | Game number |
|---|---|---:|
| Star Wars Episode I | SWE1 | `50069` |
| Revenge From Mars | RFM | `50070` |

Other numbers are not aliases. In particular, `50072` identifies **Wizard
Blocks**, not RFM or SWE1. A historical tournament-server listing or PUB-card
catalogue can contain several games and utilities; never classify an artifact
from version/date alone.

## Current preserved set

The current tree contains 26 extracted update paths:

- eight SWE1 paths, covering 1.30, 1.40, two labelled 1.50 paths, 1.66, 2.00,
  2.01 and 2.10;
- eighteen RFM paths, covering 1.20–1.60, 1.80, 1.90, 1.91, 1.95 and
  2.00–2.60 with the locally present intermediate releases.

Twenty matching classic `.exe` containers are preserved under
`updates/exe-sources/`. Six additional ZIP sources preserve prototype or
community material. An extracted directory can be useful even when the
original installer is missing, but its provenance status must say so.

The repository does not publish all 26 paths. Williams/Bally,
post-Williams and independently preserved community material may be tracked;
the myPinballs paths and matching downloaded installers are explicitly ignored
by `.gitignore`. They are supported local test inputs, not repository payloads.

Every accepted extracted bundle currently has the four emulator components:

```text
*_bootdata.rom
*_im_flsh0.rom
*_game.rom
*_symbols.rom
```

`*_pubboot.rom` and `*_sf.rom` are present only where the source set preserves
them. Their absence does not make the four-part Encore update image incomplete;
it does limit claims about recreating the original physical update package or
native sound-flash path.

### Version-by-version map

This is the human-readable index: one row means one distinct historical or
locally preserved identity. Rows are grouped by release line rather than
pretending that every version number belongs to one continuous chronology.

> [!TIP]
> **Preserved** means that usable source bytes exist locally. **Extracted
> only** means that Encore has the components but not their original container.
> **Attested only** is a recovery lead, not a runnable local update.

| Game |     Version / identity | Publisher or line                          | Source container                                                                | What survives here                                                                                                                    |
| ---- | ---------------------: | ------------------------------------------ | ------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| RFM  | 0.1, ordinary base ROM | Williams/Bally production base             | `rfm_u100.rom` + `rfm_u101.rom`                                                 | **Preserved and live-verified:** `PRODUCTION_BUILD`, `FREE_PLAY_ONLY`; no update flash                                                |
| RFM  |                   0.50 | Williams/Bally pre-release                 | no separate container established                                               | **Attested only:** secondary revision transcript describes the 1999-03-07 Waukegan startup release                                    |
| RFM  |                   0.60 | Williams/Bally sample release              | no separate container established                                               | **Attested only:** secondary revision transcript describes the 1999-03-16 sample release                                              |
| RFM  |                   0.70 | Williams/Bally pre-release                 | `pin2000_50070_0070_03291999_B_10000000.exe`                                    | **Attested only:** historical filename                                                                                                |
| RFM  |                   0.71 | Williams/Bally pre-release                 | `pin2000_50070_0071_03291999_B_10000000.exe`                                    | **Attested only:** historical filename                                                                                                |
| RFM  |                   0.80 | Williams/Bally prototype, revision-2 board | `rfm_080.zip`                                                                   | **Preserved ZIP + secondary revision transcript:** prototype ROM material; not one of the 26 extracted update paths                   |
| RFM  |                   0.84 | Williams/Bally pre-release                 | `pin2000_50070_0084_04061999_B_10000000.exe`                                    | **Attested only:** historical filename                                                                                                |
| RFM  |                   0.85 | Williams/Bally pre-release                 | `pin2000_50070_0085_04051999_B_10000000.exe`                                    | **Attested only:** historical filename                                                                                                |
| RFM  |                   0.86 | Williams/Bally pre-release                 | `pin2000_50070_0086_04061999_B_10000000.exe`                                    | **Attested only:** historical filename                                                                                                |
| RFM  |                   0.87 | Williams/Bally pre-release                 | `pin2000_50070_0087_04061999_B_10000000.exe`                                    | **Attested only:** historical filename                                                                                                |
| RFM  |                   0.90 | Williams/Bally pre-release                 | no separate container established                                               | **Attested + secondary revision transcript:** named by Williams as the predecessor of 1.00                                            |
| RFM  |                   1.00 | Williams/Bally                             | no separate container established                                               | **Attested only:** published revision history                                                                                         |
| RFM  |                   1.10 | Williams/Bally                             | no separate container established                                               | **Attested only:** published notes and installed version reports                                                                      |
| RFM  |                   1.20 | Williams/Bally                             | `pin2000_50070_0120_06091999_B_10000000.exe`                                    | **Preserved:** EXE + extraction                                                                                                       |
| RFM  |                   1.30 | Williams/Bally                             | `pin2000_50070_0130_11241999_B_10000000.exe`                                    | **Preserved:** EXE + extraction                                                                                                       |
| RFM  |                   1.40 | Williams/Bally                             | `pin2000_50070_0140_01312000_B_10000000.exe`                                    | **Preserved:** EXE + extraction                                                                                                       |
| RFM  |                   1.50 | Williams/Bally                             | `pin2000_50070_0150_07252000_B_10000000.exe`                                    | **Preserved:** EXE + extraction                                                                                                       |
| RFM  |                   1.60 | post-Williams maintenance                  | `pin2000_50070_0160_09222003_B_10000000.exe`                                    | **Preserved:** EXE + extraction                                                                                                       |
| RFM  |                   1.21 | tournament/community                       | `pin2000_50070_0121_05202016_B_10000000.exe`                                    | **Attested only:** historical filename                                                                                                |
| RFM  |                   1.70 | tournament/community                       | `pin2000_50070_0170_02062006_B_10000000.exe`                                    | **Attested + secondary revision transcript:** the transcript dates its JTS change to 2006-04-16                                       |
| RFM  |                   1.80 | tournament/community                       | `rfm_180.zip`; historical EXE name `pin2000_50070_0180_04232006_B_10000000.exe` | **Preserved ZIP + extraction + secondary revision transcript:** original EXE not recovered                                            |
| RFM  |    1.90, November 2017 | tournament/community PUB build             | `pin2000_50070_0190_11222017_B_10000000.exe`                                    | **Attested + disputed secondary transcript:** historical filename and owner reports; do not apply its claimed changes to Hemtoni 1.90 |
| RFM  |       1.90, March 2018 | Hemtoni                                    | `rfm_190.zip`                                                                   | **Preserved ZIP + reconstructed extraction:** `0190_03292018`; shared `im_flsh0` supplied separately                                  |
| RFM  |         1.91, May 2018 | Hemtoni                                    | `rfm_191.zip`                                                                   | **Preserved ZIP + reconstructed extraction + secondary transcript:** `0191_05302018`; shared `im_flsh0` supplied separately           |
| RFM  |       1.95, March 2018 | Hemtoni                                    | `rfm_195.zip`                                                                   | **Preserved ZIP + reconstructed extraction:** `0195_03292018`; shared `im_flsh0` supplied separately                                  |
| RFM  |                   2.00 | myPinballs                                 | `pin2000_50070_0200_12032018_B_10000000.exe`                                    | **Preserved locally:** ignored EXE + extraction                                                                                       |
| RFM  |                   2.10 | myPinballs                                 | `pin2000_50070_0210_04112019_B_10000000.exe`                                    | **Preserved locally:** ignored EXE + extraction                                                                                       |
| RFM  |                   2.11 | myPinballs intermediate build              | original name not recovered; build dated 2019-05-10                             | **Attested only:** no updater or complete notes recovered                                                                             |
| RFM  |                   2.20 | myPinballs                                 | `pin2000_50070_0220_10222019_B_10000000.exe`                                    | **Preserved locally:** ignored EXE + extraction                                                                                       |
| RFM  |                   2.21 | myPinballs                                 | `pin2000_50070_0221_04052020_B_10000000.exe`                                    | **Preserved locally:** ignored EXE + extraction                                                                                       |
| RFM  |                   2.22 | myPinballs                                 | `pin2000_50070_0222_06302020_B_10000000.exe`                                    | **Preserved locally:** ignored EXE + extraction                                                                                       |
| RFM  |                   2.23 | myPinballs                                 | `pin2000_50070_0223_04082021_B_10000000.exe`                                    | **Preserved locally:** ignored EXE + extraction                                                                                       |
| RFM  |                   2.24 | myPinballs                                 | `pin2000_50070_0224_01292022_B_10000000.exe`                                    | **Preserved locally:** ignored EXE + extraction                                                                                       |
| RFM  |                   2.50 | myPinballs                                 | `pin2000_50070_0250_12162022_B_10000000.exe`                                    | **Preserved locally:** ignored EXE + extraction                                                                                       |
| RFM  |                   2.60 | myPinballs                                 | `pin2000_50070_0260_08082024_B_10000000.exe`                                    | **Preserved locally:** ignored EXE + extraction                                                                                       |
| SWE1 |                   0.40 | Williams/Bally production base             | `swe1_u100`–`u107` + `u109`/`u110` ROM set                                      | **Preserved and live-verified:** XINA 1.12, game 0.40, `PRODUCTION_BUILD`, `ALLOW_SCORE_CREDIT`; no update flash                      |
| SWE1 |                   0.43 | Williams/Bally development                 | no distributed container established                                            | **Attested only:** developer PRISM-card report                                                                                        |
| SWE1 |                   1.00 | Williams/Bally                             | `pin2000_50069_0100_07171999_B_10000000.exe`                                    | **Attested only:** historical filename                                                                                                |
| SWE1 |                   1.10 | Williams/Bally                             | `pin2000_50069_0110_09141999_B_10000000.exe`                                    | **Attested only:** historical filename                                                                                                |
| SWE1 |                   1.20 | Williams/Bally                             | `pin2000_50069_0120_09161999_B_10000000.exe`                                    | **Attested only:** historical filename; published notes survive                                                                       |
| SWE1 |                   1.30 | Williams/Bally                             | `pin2000_50069_0130_09211999_B_10000000.exe`                                    | **Preserved:** EXE + extraction                                                                                                       |
| SWE1 |                   1.40 | Williams/Bally                             | `pin2000_50069_0140_07252000_B_10000000.exe`                                    | **Preserved:** EXE + extraction                                                                                                       |
| SWE1 |  1.50, July 2000 label | Williams/Bally                             | original container not recovered                                                | **Extracted only:** six components, byte-identical to the preserved 2003-labelled set                                                 |
| SWE1 |   1.50, September 2003 | post-Williams maintenance                  | `pin2000_50069_0150_09222003_B_10000000.exe`                                    | **Preserved:** EXE + extraction                                                                                                       |
| SWE1 |                   1.60 | tournament/community                       | `pin2000_50069_0160_02012013_B_10000000.exe`                                    | **Attested only:** historical filename                                                                                                |
| SWE1 |                   1.65 | tournament/community                       | `pin2000_50069_0165_02212018_B_10000000.exe`                                    | **Attested only:** historical filename                                                                                                |
| SWE1 |                   1.66 | Hemtoni                                    | `swep1_166.zip`                                                                 | **Preserved ZIP + extraction:** `0166_04032022`                                                                                       |
| SWE1 |    2.00, February 2016 | tournament/community Question Mark test    | `pin2000_50069_0200_02262016_B_10000000.exe`                                    | **Attested only:** distinct from the 2025 release; updater missing                                                                    |
| SWE1 |       2.00, April 2025 | myPinballs                                 | `pin2000_50069_0200_04112025_B_10000000.exe`                                    | **Preserved locally:** ignored EXE + extraction                                                                                       |
| SWE1 |                   2.01 | myPinballs                                 | `pin2000_50069_0201_05012025_B_10000000.exe`                                    | **Preserved locally:** ignored EXE + extraction                                                                                       |
| SWE1 |                   2.10 | myPinballs                                 | `pin2000_50069_0210_10312025_B_10000000.exe`                                    | **Preserved locally:** ignored EXE + extraction                                                                                       |

The RFM 0.1 row is deliberately present even though it is not an update. A
disposable base-ROM run with `--game rfm --update none --no-savedata` loaded
the unsuffixed U100/U101 pair and reported `system_version: 0.1`,
`game_version: 0.1` and `Type(PRODUCTION_BUILD, FREE_PLAY_ONLY)`. The separate
`--update r2` path instead selects `rfm_u100r2`/`rfm_u101r2` and identifies as
RFM 0.80 revision 2.

The two SWE1 1.50 outer paths currently contain the same six component bytes.
Keep both provenance labels, but do not call them different builds unless a
future recovery produces different hashes.

## Provenance classes

Use a class rather than flattening everything into “official” or “community”:

| Class | Meaning |
|---|---|
| Williams/Bally | contemporary publisher update or revision material |
| post-Williams maintenance | later XINA/game maintenance distributed after the pinball division closed |
| tournament/community | builds associated with surviving tournament or enthusiast work, sometimes only historically attested |
| myPinballs | the author's continuing unofficial 2.x update lines |
| extracted-only | component bytes survive but their original installer or complete note set does not |
| filename-only | a dated/versioned artifact is historically attested but no local payload is preserved |

These classes describe evidence, not quality. Compatibility is tracked
separately in [Compatibility and support](30-compatibility-support.md), and
gameplay changes are attributed in the changelog page.

## Dates are not enough

The date embedded in an outer directory often comes from source metadata or
the extraction process. It can differ from a public announcement date, and two
version lines can overlap.

Therefore:

- use the complete outer directory and component hashes for an evidence run;
- use an author's release note for the described change set;
- never infer chronology solely by sorting directory dates;
- never infer authorship from a `gamelist.txt`; and
- keep internal version labels distinct from public release numbers.

RFM 2.11 is historically attested between 2.10 and 2.20, but its updater and
original notes are not in the current local set. Absence from the current
continuous changelog is not proof that the build never existed.

### Identities that must stay distinct

The **Attested only** rows in the version map are recovery leads, not supported
local bundles. Several similarly numbered rows also describe genuinely
different historical identities rather than duplicate spelling.

The two RFM 1.90 identities must not be collapsed. The locally recovered
`rfm_190.zip` carries a March 2018 boot timestamp; it is not proof of the
separately attested November 2017 PUB-card build. Likewise, SWE1's 2016 test
labelled 2.00 is not the myPinballs 2.00 release from 2025.

The recovered RFM 1.90, 1.91 and 1.95 ZIPs omit `im_flsh0`. Their assembled
local directories use the byte-identical image found in the preserved RFM
1.40 through 1.80 trees; the recovered boot, game and symbol files remain the
version-specific evidence. This reconstruction must be stated whenever those
directories are exported or cited.

RFM 1.80 is canonically named `0180_04232006` from its boot/game timestamps.
Do not recreate the former, byte-identical `0180_09222003` duplicate: that date
belongs to RFM 1.60.

RFM 1.80 also differs materially from its neighbours: its `sizmem()` reports
8 MiB, while 1.50, 1.60 and every preserved RFM build surveyed from 1.90
through 2.60 report 4 MiB. This matches the author's hardware warning and is
not merely a filename or changelog inference. See
[Why RFM 1.80 needs 8 MiB](50-game-changelogs.md#why-rfm-180-needs-8-mib).

## Safe extraction and preservation

Classic PUB installers can be inspected with the repository extractor:

```bash
python3 tools/extract-pub-update.py /path/to/update.exe \
  --output /tmp/pin2000-update-review
```

The extractor:

- requires one consistent game/version set;
- requires the four core components;
- writes into a new destination unless `--force` is explicit; and
- prints sizes and SHA-256 values for the result.

Do not use `--force` on a preservation directory. Extract into a temporary
review location, compare hashes and metadata, then add a deliberately named
outer directory only after provenance review.

To assemble the emulator's four-part flash image without modifying the source
bundle:

```bash
python3 tools/build_update_bin.py \
  /path/to/outer/50069 \
  /tmp/update.bin
```

This is an emulator/research artifact. It is not a physical PUB-card image.

## Contribution record

A newly recovered update should arrive with:

- original filename and container hash;
- recovery source and permission/provenance note;
- acquisition date;
- game number, version and component inventory;
- SHA-256 for every extracted component;
- extractor/tool version or command;
- any original release notes kept separately from inference; and
- explicit redistribution status.

> [!CAUTION]
> Do not commit credentials, private archive links, owner savedata or a full
> machine dump with an update contribution. Live RAM and serial logs can
> contain operator or tournament data.

When an old and a newly recovered container produce identical components, keep
the source-container hashes and provenance records even if the extracted tree
does not need another byte-identical copy.

## What releases may claim

Until licensing/provenance work is complete, a release process must distinguish:

- emulator source and its notices;
- custom QEMU patches/build products;
- game/base/update assets;
- documentation and measurements; and
- user-created savedata or crash evidence.

A convenient first-run acquisition mechanism does not erase those boundaries.
See [Release process](47-release-process.md) for archive checks and the
[roadmap](36-roadmap.md) for the outstanding licence/provenance gate.

---

[ROM/update loading](15-rom-loading.md) · [Game-code changelogs](50-game-changelogs.md) · [Documentation](README.md)
