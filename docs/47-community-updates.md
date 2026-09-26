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

### Exact local inventory

| Family | Preserved outer identities |
|---|---|
| SWE1 | `0130_09211999`, `0140_07252000`, `0150_07252000`, `0150_09222003`, `0166_04032022`, `0200_04112025`, `0201_05012025`, `0210_10312025` |
| RFM | `0120_06091999`, `0130_11241999`, `0140_01312000`, `0150_07252000`, `0160_09222003`, `0180_04232006`, `0190_03292018`, `0191_05302018`, `0195_03292018`, `0200_12032018`, `0210_04112019`, `0220_10222019`, `0221_04052020`, `0222_06302020`, `0223_04082021`, `0224_01292022`, `0250_12162022`, `0260_08082024` |

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

### Historically attested gaps

The prior inventory also records versions for which this checkout has no
complete payload. They remain recovery leads, not supported local bundles:

| Family | Attested but not locally preserved |
|---|---|
| RFM | 0.70, 0.71, 0.84–0.87, 0.90, 1.00, 1.10, 1.21, 1.70, the November 2017 tournament/community 1.90, and 2.11 |
| SWE1 | 0.40, development 0.43, 1.00–1.20, tournament/community 1.60 and 1.65, and the 2016 tournament test labelled 2.00 |

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
