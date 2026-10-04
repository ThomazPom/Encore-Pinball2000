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

The current tree contains 40 extracted update paths:

- thirteen SWE1 paths, covering 1.00–1.40, the preserved 2003-labelled 1.50, 1.60,
  1.65, 1.66, two distinct 2.00 identities, 2.01 and 2.10;
- twenty-seven RFM paths, covering the recovered 0.70–0.87 pre-release set,
  selected 1.20–1.80 releases, two distinct 1.90 identities, 1.91, 1.95 and
  the locally present 2.00–2.60 releases.

Thirty-five matching classic `.exe` containers are preserved under
`updates/exe-sources/`. Six additional ZIP sources preserve prototype or
community material. An extracted directory can be useful even when the
original installer is missing, but its provenance status must say so.

The repository does not publish all 40 paths. Williams/Bally,
post-Williams and independently preserved community material may be tracked;
the myPinballs paths and matching downloaded installers are explicitly ignored
by `.gitignore`. They are supported local test inputs, not repository payloads.

### Recovery batch — 2026-10-04

Fifteen previously filename-only containers were recovered on 2026-10-04.
They are ZIP-compatible `.exe` archives and were copied unchanged into
`updates/exe-sources/`, then unpacked verbatim into matching outer directories
under `updates/`.

> [!NOTE]
> No source or personal attribution is recorded for this recovery. The stable
> evidence is the exact filename and container SHA-256 below.

| Game | Version / identity | Original container | SHA-256 |
|---|---:|---|---|
| SWE1 | 1.00 | `pin2000_50069_0100_07171999_B_10000000.exe` | `3b8b70040e782ee482914f9d6acebc8001fec3e90831c9113bba2f918e4636b2` |
| SWE1 | 1.10 | `pin2000_50069_0110_09141999_B_10000000.exe` | `6aa385aea140eb4cccac651ccc6191eef6e13fb2d04d2db7297444bb4990a9f4` |
| SWE1 | 1.20 | `pin2000_50069_0120_09161999_B_10000000.exe` | `3ac8cda7be8cc47c28c65e7f8e15452625e159772466caefa6d78f400774cda2` |
| SWE1 | 1.60 | `pin2000_50069_0160_02012013_B_10000000.exe` | `7cc1eed57d948ed26299435e80758c136253e2584148c27800f1a9ea46087ce1` |
| SWE1 | 1.65 | `pin2000_50069_0165_02212018_B_10000000.exe` | `8c25257d0258f9e7adfd79f3b66968593129b89d8aa2bb0ab2d3e87116b4dcd6` |
| SWE1 | 2.00 (2016) | `pin2000_50069_0200_02262016_B_10000000.exe` | `0f3ee00e83085b70eb0417b13ab8f8bb1a6bf2df547d4478b0454be4a77cdd72` |
| RFM | 0.70 | `pin2000_50070_0070_03291999_B_10000000.exe` | `7821277e7d9d71d9c260f5b4d56102ec92c54725bb1b99d33d2f1f3f643fd5af` |
| RFM | 0.71 | `pin2000_50070_0071_03291999_B_10000000.exe` | `bf5866760a421e3945be60f40dfa513461bc8907313e497c168400a590e4844f` |
| RFM | 0.84 | `pin2000_50070_0084_04061999_B_10000000.exe` | `f52fe437f80b8149d61585bd6120145d284962da5b14aad8f2687eb42fdc126f` |
| RFM | 0.85 | `pin2000_50070_0085_04051999_B_10000000.exe` | `034ba00eced0f5b2aa167c9fb68e89516a2c4db0c3c932b5823dc2f1fe1203fe` |
| RFM | 0.86 | `pin2000_50070_0086_04061999_B_10000000.exe` | `5c89a5cb18b1ebc2a1d5852ad5bdc87f7746ad7550e06370202e5e7621921068` |
| RFM | 0.87 | `pin2000_50070_0087_04061999_B_10000000.exe` | `bf6b7cbeec7b9aefc9fec3aa58d86773219931ca3b0b2da29b807a094dbe00df` |
| RFM | 1.21 | `pin2000_50070_0121_05202016_B_10000000.exe` | `d407ad47ada578f2120784c62229fce10789254c5311f6ff09f030e19f9e14d6` |
| RFM | 1.70 | `pin2000_50070_0170_02062006_B_10000000.exe` | `9f898918083136808b82215e99a4e5c1ffbf57687a70f9cbf5c6712000a16f99` |
| RFM | 1.90 (2017) | `pin2000_50070_0190_11222017_B_10000000.exe` | `96cd8f0700e4546ff694bd8bc69752ea9414f935d8e78aa932b4abeb6c97e186` |

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

| Game |     Version / identity | Publisher or line                          | Source container                                                                | What survives here                                                                                                          |
| ---- | ---------------------: | ------------------------------------------ | ------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| RFM  | 0.1, ordinary base ROM | Williams/Bally production base             | `rfm_u100.rom` + `rfm_u101.rom`                                                 | **Preserved and live-verified:** `PRODUCTION_BUILD`, `FREE_PLAY_ONLY`; no update flash                                      |
| RFM  |                   0.50 | Williams/Bally pre-release                 | no separate container established                                               | **Attested only:** secondary revision transcript describes the 1999-03-07 Waukegan startup release                          |
| RFM  |                   0.60 | Williams/Bally sample release              | no separate container established                                               | **Attested only:** secondary revision transcript describes the 1999-03-16 sample release                                    |
| RFM  |                   0.70 | Williams/Bally pre-release                 | `pin2000_50070_0070_03291999_B_10000000.exe`                                    | **Preserved:** EXE + core extraction + sound flash; no PUB boot                                                             |
| RFM  |                   0.71 | Williams/Bally pre-release                 | `pin2000_50070_0071_03291999_B_10000000.exe`                                    | **Preserved:** EXE + core extraction + sound flash; no PUB boot                                                             |
| RFM  |                   0.80 | Williams/Bally prototype, revision-2 board | `rfm_080.zip`                                                                   | **Preserved ZIP + secondary revision transcript:** prototype ROM material; not one of the 40 extracted update paths         |
| RFM  |                   0.84 | Williams/Bally pre-release                 | `pin2000_50070_0084_04061999_B_10000000.exe`                                    | **Preserved:** EXE + core extraction + sound flash; no PUB boot                                                             |
| RFM  |                   0.85 | Williams/Bally pre-release                 | `pin2000_50070_0085_04051999_B_10000000.exe`                                    | **Preserved:** EXE + core extraction + sound flash; no PUB boot                                                             |
| RFM  |                   0.86 | Williams/Bally pre-release                 | `pin2000_50070_0086_04061999_B_10000000.exe`                                    | **Preserved:** EXE + core extraction + sound flash; no PUB boot                                                             |
| RFM  |                   0.87 | Williams/Bally pre-release                 | `pin2000_50070_0087_04061999_B_10000000.exe`                                    | **Preserved:** EXE + core extraction + sound flash; no PUB boot                                                             |
| RFM  |                   0.90 | Williams/Bally pre-release                 | no separate container established                                               | **Attested + secondary revision transcript:** named by Williams as the predecessor of 1.00                                  |
| RFM  |                   1.00 | Williams/Bally                             | no separate container established                                               | **Attested only:** published revision history                                                                               |
| RFM  |                   1.10 | Williams/Bally                             | no separate container established                                               | **Attested only:** published notes and installed version reports                                                            |
| RFM  |                   1.20 | Williams/Bally                             | `pin2000_50070_0120_06091999_B_10000000.exe`                                    | **Preserved:** EXE + extraction                                                                                             |
| RFM  |                   1.30 | Williams/Bally                             | `pin2000_50070_0130_11241999_B_10000000.exe`                                    | **Preserved:** EXE + extraction                                                                                             |
| RFM  |                   1.40 | Williams/Bally                             | `pin2000_50070_0140_01312000_B_10000000.exe`                                    | **Preserved:** EXE + extraction                                                                                             |
| RFM  |                   1.50 | Williams/Bally                             | `pin2000_50070_0150_07252000_B_10000000.exe`                                    | **Preserved:** EXE + extraction                                                                                             |
| RFM  |                   1.60 | post-Williams maintenance                  | `pin2000_50070_0160_09222003_B_10000000.exe`                                    | **Preserved:** EXE + extraction                                                                                             |
| RFM  |                   1.21 | tournament/community                       | `pin2000_50070_0121_05202016_B_10000000.exe`                                    | **Preserved:** EXE + six-component extraction; no recovered notes                                                           |
| RFM  |                   1.70 | tournament/community                       | `pin2000_50070_0170_02062006_B_10000000.exe`                                    | **Preserved:** EXE + six-component extraction + secondary revision transcript                                               |
| RFM  |                   1.80 | tournament/community                       | `rfm_180.zip`; historical EXE name `pin2000_50070_0180_04232006_B_10000000.exe` | **Preserved ZIP + extraction + secondary revision transcript:** original EXE not recovered                                  |
| RFM  |    1.90, November 2017 | tournament/community PUB build             | `pin2000_50070_0190_11222017_B_10000000.exe`                                    | **Preserved:** EXE + six-component extraction + disputed secondary transcript; distinct from Hemtoni 1.90                   |
| RFM  |       1.90, March 2018 | Hemtoni                                    | `rfm_190.zip`                                                                   | **Preserved ZIP + reconstructed extraction:** `0190_03292018`; shared `im_flsh0` supplied separately                        |
| RFM  |         1.91, May 2018 | Hemtoni                                    | `rfm_191.zip`                                                                   | **Preserved ZIP + reconstructed extraction + secondary transcript:** `0191_05302018`; shared `im_flsh0` supplied separately |
| RFM  |       1.95, March 2018 | Hemtoni                                    | `rfm_195.zip`                                                                   | **Preserved ZIP + reconstructed extraction:** `0195_03292018`; shared `im_flsh0` supplied separately                        |
| RFM  |                   2.00 | myPinballs                                 | `pin2000_50070_0200_12032018_B_10000000.exe`                                    | **Preserved locally:** ignored EXE + extraction                                                                             |
| RFM  |                   2.10 | myPinballs                                 | `pin2000_50070_0210_04112019_B_10000000.exe`                                    | **Preserved locally:** ignored EXE + extraction                                                                             |
| RFM  |                   2.11 | myPinballs intermediate build              | original name not recovered; build dated 2019-05-10                             | **Attested only:** no updater or complete notes recovered                                                                   |
| RFM  |                   2.20 | myPinballs                                 | `pin2000_50070_0220_10222019_B_10000000.exe`                                    | **Preserved locally:** ignored EXE + extraction                                                                             |
| RFM  |                   2.21 | myPinballs                                 | `pin2000_50070_0221_04052020_B_10000000.exe`                                    | **Preserved locally:** ignored EXE + extraction                                                                             |
| RFM  |                   2.22 | myPinballs                                 | `pin2000_50070_0222_06302020_B_10000000.exe`                                    | **Preserved locally:** ignored EXE + extraction                                                                             |
| RFM  |                   2.23 | myPinballs                                 | `pin2000_50070_0223_04082021_B_10000000.exe`                                    | **Preserved locally:** ignored EXE + extraction                                                                             |
| RFM  |                   2.24 | myPinballs                                 | `pin2000_50070_0224_01292022_B_10000000.exe`                                    | **Preserved locally:** ignored EXE + extraction                                                                             |
| RFM  |                   2.50 | myPinballs                                 | `pin2000_50070_0250_12162022_B_10000000.exe`                                    | **Preserved locally:** ignored EXE + extraction                                                                             |
| RFM  |                   2.60 | myPinballs                                 | `pin2000_50070_0260_08082024_B_10000000.exe`                                    | **Preserved locally:** ignored EXE + extraction                                                                             |
| SWE1 |                   0.40 | Williams/Bally production base             | `swe1_u100`–`u107` + `u109`/`u110` ROM set                                      | **Preserved and live-verified:** XINA 1.12, game 0.40, `PRODUCTION_BUILD`, `ALLOW_SCORE_CREDIT`; no update flash            |
| SWE1 |                   0.43 | Williams/Bally development                 | no distributed container established                                            | **Attested only:** developer PRISM-card report                                                                              |
| SWE1 |                   1.00 | Williams/Bally                             | `pin2000_50069_0100_07171999_B_10000000.exe`                                    | **Preserved:** EXE + six-component extraction; no recovered notes                                                           |
| SWE1 |                   1.10 | Williams/Bally                             | `pin2000_50069_0110_09141999_B_10000000.exe`                                    | **Preserved:** EXE + six-component extraction; no recovered notes                                                           |
| SWE1 |                   1.20 | Williams/Bally                             | `pin2000_50069_0120_09161999_B_10000000.exe`                                    | **Preserved:** EXE + six-component extraction + published notes                                                             |
| SWE1 |                   1.30 | Williams/Bally                             | `pin2000_50069_0130_09211999_B_10000000.exe`                                    | **Preserved:** EXE + extraction                                                                                             |
| SWE1 |                   1.40 | Williams/Bally                             | `pin2000_50069_0140_07252000_B_10000000.exe`                                    | **Preserved:** EXE + extraction                                                                                             |
| SWE1 |       1.50, July 2000 | Williams/Bally                             | `pin2000_50069_0150_07252000_B_10000000.exe`                                    | **Attested only:** distinct 1.4M entry in a [cached historical server listing](https://pinside.com/pinball/forum/topic/rfm-190-software-do-you-have-the-exe-update-or-can-extract-from-pub#post-9286049); binary not recovered |
| SWE1 |   1.50, September 2003 | post-Williams maintenance                  | `pin2000_50069_0150_09222003_B_10000000.exe`                                    | **Preserved:** EXE + extraction                                                                                             |
| SWE1 |                   1.60 | tournament/community                       | `pin2000_50069_0160_02012013_B_10000000.exe`                                    | **Preserved:** EXE + six-component extraction; no recovered notes                                                           |
| SWE1 |                   1.65 | tournament/community                       | `pin2000_50069_0165_02212018_B_10000000.exe`                                    | **Preserved:** EXE + six-component extraction; no recovered notes                                                           |
| SWE1 |                   1.66 | Hemtoni                                    | `swep1_166.zip`                                                                 | **Preserved ZIP + extraction:** `0166_04032022`                                                                             |
| SWE1 |    2.00, February 2016 | tournament/community Question Mark test    | `pin2000_50069_0200_02262016_B_10000000.exe`                                    | **Preserved:** EXE + six-component extraction; distinct from the 2025 release                                               |
| SWE1 |       2.00, April 2025 | myPinballs                                 | `pin2000_50069_0200_04112025_B_10000000.exe`                                    | **Preserved locally:** ignored EXE + extraction                                                                             |
| SWE1 |                   2.01 | myPinballs                                 | `pin2000_50069_0201_05012025_B_10000000.exe`                                    | **Preserved locally:** ignored EXE + extraction                                                                             |
| SWE1 |                   2.10 | myPinballs                                 | `pin2000_50069_0210_10312025_B_10000000.exe`                                    | **Preserved locally:** ignored EXE + extraction                                                                             |

The RFM 0.1 row is deliberately present even though it is not an update. A
disposable base-ROM run with `--game rfm --update none --no-savedata` loaded
the unsuffixed U100/U101 pair and reported `system_version: 0.1`,
`game_version: 0.1` and `Type(PRODUCTION_BUILD, FREE_PLAY_ONLY)`. The separate
`--update r2` path instead selects `rfm_u100r2`/`rfm_u101r2` and identifies as
RFM 0.80 revision 2.

The cached historical listing attests two distinct SWE1 1.50 container names:
July 2000 and September 2003. The former local July directory was removed after
verification showed that it merely duplicated the preserved 2003 payload under
the older name. The actual July 2000 binary remains missing; its contents must
not be inferred from the 2003 hashes.

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

The two preserved RFM 1.90 identities must not be collapsed. The
`pin2000_50070_0190_11222017_B_10000000.exe` payload and the March 2018
`rfm_190.zip` payload have distinct game, boot and symbol bytes; evidence or
change claims for one must not be applied to the other. Likewise, SWE1's 2016
test labelled 2.00 is not the myPinballs 2.00 release from 2025.

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
