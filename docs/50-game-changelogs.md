# RFM and SWE1 game-code changelogs

This page answers two different questions without mixing them:

1. which update payloads are preserved in this checkout; and
2. which gameplay or system changes are actually supported by a release note.

The first answer comes from the repository. The second comes from the original
Williams/Bally revision histories or the community author's published notes.
An update filename, embedded build date or changed binary is **not** by itself a
changelog.

> [!IMPORTANT]
> These are game-code releases, not Encore releases. Encore can stage preserved
> update components for emulation, but it does not claim authorship, warranty or
> physical-machine compatibility for third-party game code.

For update loading and selection, see [ROM and update loading](15-rom-loading.md).
For what Encore has actually exercised, see
[Compatibility and support](30-compatibility-support.md) and the
[validation matrix](26-testing-validation-matrix.md).

## Evidence labels

The tables below use four deliberately narrow labels:

| Label | Meaning |
|---|---|
| **Published notes** | a release author or Williams/Bally support page describes the changes |
| **Secondary transcript** | a preserved community post reproduces revision text whose original publisher page is no longer in the current catalogue |
| **Preserved payload** | the corresponding extracted ROM components exist locally |
| **Installer preserved** | a source `.exe` exists under `updates/exe-sources/` |
| **No recovered notes** | bytes exist, but this checkout has no attributable change list |

“No recovered notes” does not mean “no changes.” It means that guessing from
strings, symbols or a binary diff would be reverse-engineering evidence rather
than a historical changelog.

## Sources of record

The maintained source links checked on 2026-09-26 are:

- [Williams/Bally RFM revision history](https://www.planetarypinball.com/mm5/Williams/tech/pin2000/software/rfm_history.html)
  for RFM 1.00 through 1.50;
- [Williams/Bally SWE1 revision history](https://www.planetarypinball.com/mm5/Williams/tech/pin2000/software/sw_history.html)
  for SWE1 1.20 through 1.40;
- [post-Williams 2003 revision-text transcript](https://pinside.com/pinball/forum/topic/swe1-worth-updating-from-13-to-15)
  for SWE1 1.50 and RFM 1.60, treated here as a secondary source;
- [myPinballs RFM update log](https://mypinballs.com/software/rfm/code_updates.jsp)
  for the unofficial 2.x line;
- [myPinballs SWE1 combined update notes](https://www.mypinballs.com/files/pin2k/starwars_updates_log.pdf)
  for SWE1 2.00 through 2.10.

> [!NOTE]
> The local outer-directory date records artifact provenance. Published notes
> remain authoritative for the described change set even when their displayed
> release date and the preserved artifact date differ.

## Preserved payload inventory

An Encore update needs four core components:

```text
*_bootdata.rom
*_im_flsh0.rom
*_game.rom
*_symbols.rom
```

Some packages also preserve `*_pubboot.rom` and `*_sf.rom`. The latter files
belong to the physical update and sound paths; they are not required by
`tools/build_update_bin.py` to assemble the four-part update image used by the
emulator.

The inventory below was generated from the tree rather than copied from an old
document.

### Star Wars Episode I (`50069`)

| Version | Outer artifact date | Components | Source EXE | Notes status |
|---:|---:|---|---|---|
| 1.30 | 1999-09-21 | core + PUB boot + sound flash | yes | Williams notes |
| 1.40 | 2000-07-25 | core + PUB boot + sound flash | yes | Williams notes |
| 1.50 | 2000-07-25 | core + PUB boot + sound flash | no | secondary transcript |
| 1.50 | 2003-09-22 | core + PUB boot + sound flash | yes | secondary transcript |
| 1.66 | 2022-04-03 | core only | no | no recovered notes |
| 2.00 | 2025-04-11 | core + PUB boot + sound flash | yes | myPinballs notes |
| 2.01 | 2025-05-01 | core + PUB boot + sound flash | yes | myPinballs notes |
| 2.10 | 2025-10-31 | core + PUB boot + sound flash | yes | myPinballs notes |

The two 1.50 directories have different provenance labels but their six current
ROM components are byte-for-byte identical. Keep both names when preserving
provenance; do not describe them as different builds without new byte evidence.

### Revenge From Mars (`50070`)

| Version | Outer artifact date | Components | Source EXE | Notes status |
|---:|---:|---|---|---|
| 1.20 | 1999-06-09 | core + PUB boot + sound flash | yes | Williams notes |
| 1.30 | 1999-11-24 | core + PUB boot + sound flash | yes | Williams notes |
| 1.40 | 2000-01-31 | core + PUB boot + sound flash | yes | Williams notes |
| 1.50 | 2000-07-25 | core + PUB boot + sound flash | yes | Williams notes |
| 1.60 | 2003-09-22 | core + PUB boot + sound flash | yes | secondary transcript |
| 1.80 | 2006-04-23 | core + PUB boot + sound flash | no | no recovered notes |
| 1.90 | 2018-03-29 | core + PUB boot | no | no recovered notes |
| 1.91 | 2018-05-30 | core + PUB boot + sound flash | no | no recovered notes |
| 1.95 | 2018-03-29 | core + PUB boot | no | no recovered notes |
| 2.00 | 2018-12-03 | core + PUB boot + sound flash | yes | myPinballs notes |
| 2.10 | 2019-04-11 | core + PUB boot + sound flash | yes | myPinballs notes |
| 2.20 | 2019-10-22 | core + PUB boot + sound flash | yes | myPinballs notes |
| 2.21 | 2020-04-05 | core + PUB boot + sound flash | yes | myPinballs notes |
| 2.22 | 2020-06-30 | core + PUB boot + sound flash | yes | myPinballs notes |
| 2.23 | 2021-04-08 | core + PUB boot + sound flash | yes | myPinballs notes |
| 2.24 | 2022-01-29 | core + PUB boot + sound flash | yes | myPinballs notes |
| 2.50 | 2022-12-16 | core + PUB boot + sound flash | yes | myPinballs notes |
| 2.60 | 2024-08-08 | core + PUB boot + sound flash | yes | myPinballs notes |

This is 26 extracted update paths in total: eight SWE1 and eighteen RFM. Twenty
have a matching source EXE in this checkout. A local ZIP or extracted directory
can preserve useful bytes without proving the history of the missing original
installer.

## Williams/Bally releases

The summaries in this section are paraphrases of the published revision
histories, not claims inferred from Encore behavior.

### Revenge From Mars

| Version | Published change summary |
|---:|---|
| 1.00 | completed Attack Mars; added three question-mark modes, Hypno-Beam multiball, match/initials presentation, pricing and replay bookkeeping, switch compensation and several service diagnostics; included stability work against lockups and resets |
| 1.10 | changed flipper timing to reduce heat, extended bookkeeping, revised regional pricing/reporting/replay behavior, and improved multiball lamps, Autopsy ejection and switch compensation |
| 1.20 | restored UK pricing support, corrected Canadian bonus pricing, expanded system information and timestamps, added saucer-light and attract options, and fixed message/pricing display cases |
| 1.30 | added Martian Champion, jet-bumper, ball-save and victory-lap rules; expanded family mode and ball-loop adjustments; fixed a multiball-start failure |
| 1.40 | fixed a Martian Bowling reset and Martian Happy Hour animation issue; made the Bonus Wave total page unconditional |
| 1.50 | updated the operating system and improved coin, bill and credit handling |

RFM 1.00 and 1.10 are described by the published history but are not preserved
as extracted update bundles here. The community transcript attributes RFM 1.60
to the final XINA 1.19 and a factory-reset fix for the country-setting mismatch
seen when the power-driver board is disconnected. Because the current
Williams/Bally catalogue stops at 1.50, that 1.60 statement remains labelled a
secondary transcript rather than silently promoted to primary evidence.

### Star Wars Episode I

| Version | Published change summary |
|---:|---|
| 1.20 | added translations, optional ball save, the GUNGAN/JARJAR bumper-spinner rule, status-report tables and attract instructions; corrected scoring, switch tolerance, replay, keyboard display and checksum cases |
| 1.30 | fixed power cycling when Japanese DIP settings are selected |
| 1.40 | updated the operating system and credit handling, and closed a tournament ramp-shot exploit |
| 1.50 | the secondary revision-text transcript attributes the final XINA 1.19 and the same disconnected-power-driver country-setting/factory-reset fix as RFM 1.60 |

SWE1 1.20 is described by the source history but is not preserved as an
extracted bundle here. No recovered note supports a separate change summary
for 1.66.

### Question Mark and community test scenes

`QuestionMarkScene` is real retained SWE1 game code, not merely a label found
in the community build. The preserved 1.50 symbol table already contains its
availability, start, end, intro, background, audio and audit objects, and the
game image contains `Question Mark Started`, `Question Mark Finished` and
`Scene - Question Mark` strings.

#### The 2016 tournament/community experiment

An exact historical updater identity,
`pin2000_50069_0200_02262016_B_10000000.exe`, is attested for a rewritten SWE1
1.30 test build exposing the otherwise unreachable “Questionmark Mission”. It
was numbered 2.00 but was not a chronological successor to the factory line.
A [contemporary owner discussion](https://pinside.com/pinball/forum/topic/p2k-swep1-question-mark-mission)
independently confirms the puzzle that motivated such a build: the stock game
recorded Question Mark started/ended statistics, yet owners could not identify
a normal way to play it.

That updater is not locally preserved. Its identity and reported purpose are
historical evidence, while its exact code changes and runtime presentation
remain unverified until the payload is recovered. It must not be confused with
the unrelated myPinballs 2.00 released in 2025.

#### The 2025 myPinballs re-exposure

The [SWE1 2.0 author's published notes](https://www.mypinballs.com/files/pin2k/starwars_updates_log.pdf)
then state: “Scene - Added sample modes in, 4 extra (for testing).” A comparison
of `scene_table_data` supplies the missing identity evidence:

| SWE1 build | Destroyer Droid | Hover Tank | Watto's Chance | Question Mark |
|---|---|---|---|---|
| 1.50 | `(0,1,0)` | `(0,1,0)` | `(0,1,0)` | `(0,4,2)` |
| community 2.0 | `(0,1,0)` | `(0,1,0)` | `(0,1,0)` | `(0,1,0)` |

The tuple labels are not preserved, so this document does not invent names for
the three fields. The relevant fact is the exact transition: community 2.0
reclassifies Question Mark to match the three other late sample scenes while
its release note announces four test modes. Taken together, these are strong
evidence that **Question Mark is the fourth test scene exposed by the 2025
community release**. This locally verifiable result independently supports the
reported purpose of the missing 2016 experiment; it does not prove the two
builds implemented the exposure identically.

The reproducibility anchors are `scene_table_data` at `0x002e02a0` in 1.50 and
`0x002de9b8` in community 2.0. Each table row is five little-endian 32-bit
values; the second value resolves through the matching symbol ROM to the
corresponding `jedi_scene_*` object. Addresses and interpretation are scoped to
those exact builds.

#### Observed gameplay and remaining boundary

The project exercised the retained scene in Encore by changing only
`Scenes::choose_next_scene()` in a temporary SWE1 2.10 image so every normal
mission draw returned the stock Question Mark scene at index 13. Selection and
activation otherwise followed the game's normal path.

The observed result was a minimal mystery award, not a developed mission:
activation produced a black screen, immediately added score and awarded a
`JEDI` letter, with no playable rules, visible scene or meaningful
presentation. The code also updates its start/finish audits, explaining why
the dormant scene remains visible in game statistics.

This is a retained project observation, originally documented on `main` by
commit `52d570b`; it is not merely an inference from symbol names. The temporary
patched image and raw capture were not retained, so a future reproduction
should additionally preserve input/output hashes, the patch, scripted inputs,
video/log evidence and clean-state policy. That reproducibility gap does not
erase the observed result, but the result characterises the scene inherited by
2.10—not necessarily the missing 2016 build byte for byte.

## Unofficial myPinballs releases

These releases are community game-code updates. “Latest” in Encore means the
highest locally installed version; it does not mean that Encore recommends the
release for every physical playfield configuration.

### Revenge From Mars 2.x

| Version | Published change themes |
|---:|---|
| 2.00 | introduced Quick-Shot, expanded circle-shot and champion awards, added attract effects, allowed scenes from the centre saucer, and revised ball-save/autolaunch behavior |
| 2.10 | expanded Quick-Shot and Capture Multiball, added add-a-ball paths, repaired Super Skill Shot, added shaker and real-knocker controls, and revised several mode awards |
| 2.20 | added Midnight Madness and party/flipper modes, expanded shaker and lamp effects, and added LED-oriented lamp-test options |
| 2.21 | added two saucer-related high-score records, corrected a shaker case, and added optional credit clearing at boot |
| 2.22 (internal 2.30) | added Power Drain, generalized multiball logic for larger troughs, corrected scene/hurry-up interactions, and expanded updater baud support |
| 2.23 (internal 2.40) | added Score War and new Stroke of Luck awards, repaired Payback Time and popper interactions, and added player-score reduction support |
| 2.24 (internal 2.42) | added scene backgrounds and bonus-wave shaker effects; its XINA update raised the future game-update size ceiling to 8 MiB |
| 2.50 | added Double Scoring, improved family-mode filtering and added shaker-power adjustment |
| 2.60 | added physical-lock and three-ball-lock behavior plus more shaker effects and mode content |

> [!WARNING]
> The author states that RFM 2.60 and later require the full four-opto hardware
> expansion. A successful emulator boot cannot certify a physical cabinet for
> that release. RFM 2.50 is the last locally preserved version before that
> stated hardware boundary.

### Star Wars Episode I 2.x

| Version | Published change themes |
|---:|---|
| 2.00 | added random awards, Double Scoring, shaker support, test scenes, Quick Multiball and Droid Hurry Up; revised Jar Jar options, C-3PO awards, autolaunch and multiball behavior |
| 2.01 | repaired attract persistence and Jar Jar graphics, expanded attract speech/effects and corrected the imported hurry-up scoring scale |
| 2.10 | repaired Multiball Champion and ball release, added a C-3PO champion, Midnight Madness, extensive shaker effects, new narration and revised autolaunch behavior |

The combined source note says the 2.00 line includes XINA 1.38 changes. Treat
that as part of the packaged community release, not as an Encore operating
system version.

## Known changelog gaps

The repository or surviving histories attest more versions than this checkout
can pair with primary change notes. The most useful open gaps are:

- SWE1 1.00, 1.10 and community/tournament builds 1.60 and 1.65;
- SWE1 1.66, whose four-component payload is preserved without notes;
- the 2016 community “2.00” Question Mark test build, whose exact updater and
  original accompanying note are not preserved here;
- RFM community/tournament builds 1.21, 1.70 and 1.80;
- preserved RFM 1.90, 1.91 and 1.95 payloads; and
- RFM 2.11, reported between 2.10 and 2.20 but absent from both the current
  local payload set and the author's continuous changelog.

Tournament documentation associates RFM 1.8 with scrolling tournament scores
and an 8 MiB memory requirement. That is useful compatibility evidence, but it
is not a complete 1.7-to-1.8 release note. See
[Tournament-server preservation](49-tournament-server.md) for the separated
protocol and historical evidence.

> [!TIP]
> A useful contribution for one of these gaps includes the original note or a
> stable archive URL, its author/publisher, the exact version and predecessor,
> and hashes for any matching recovered payload. A forum recollection should
> remain labelled secondary.

## Selecting a release in Encore

Use an explicit game and run without savedata when comparing clean update
behavior:

```bash
scripts/run-qemu.sh \
  --game rfm \
  --update 2.50 \
  --no-savedata
```

`latest` selects the highest version present for that game:

```bash
scripts/run-qemu.sh --game swe1 --update latest --no-savedata
```

On the inventory recorded above, that resolves to SWE1 2.10 or RFM 2.60.
Those values change if the installed bundle set changes.

For an audit where artifact provenance matters, pass the inner directory:

```bash
scripts/run-qemu.sh \
  --game swe1 \
  --update updates/pin2000_50069_0150_09222003_B_10000000/50069 \
  --no-savedata
```

> [!CAUTION]
> A numeric version is not a unique artifact identifier. This matters for
> SWE1 1.50, which has two outer paths even though their current component
> hashes match. Evidence reports should record the complete resolved path and
> component hashes.

Do not reuse persistent flash when the question is “what does this release do
from a clean state?” `--update none` alone does not erase an existing saved
flash; pair it with `--no-savedata` for a clean base-ROM comparison.

## What a changelog does not prove

A published feature does not prove that Encore emulates every device it uses.
Examples include:

- network tournament clients without a compatible tournament server;
- card-reader flows while COM2 has no receive backend;
- physical shaker, knocker, opto or trough expansions;
- installation through a physical PUB card; and
- a saved state migrated safely between unrelated game-code lines.

See [Tournament-server preservation](49-tournament-server.md) for the first two
boundaries and [Real LPT passthrough](46-real-lpt-passthrough.md) for the
physical I/O evidence boundary.

Similarly, a passing boot smoke is not a gameplay certification. The
compatibility page distinguishes preserved, structurally inspected,
smoke-validated and physically unverified combinations.

## Reproducing the local inventory

List extracted bundle identities:

```bash
find updates -mindepth 1 -maxdepth 1 -type d \
  -name 'pin2000_*' -printf '%f\n' | sort
```

Check whether every bundle has the four emulator components:

```bash
for bundle in updates/pin2000_*/*; do
  test -d "$bundle" || continue
  for part in bootdata im_flsh0 game symbols; do
    compgen -G "$bundle/*_${part}.rom" >/dev/null ||
      printf 'missing %-9s %s\n' "$part" "$bundle"
  done
done
```

Compare the two preserved SWE1 1.50 payloads by content rather than name:

```bash
sha256sum updates/pin2000_50069_0150_*/50069/*.rom
```

For release evidence, preserve together:

- the complete outer and inner bundle names;
- SHA-256 values for every component used;
- the exact game and update arguments;
- savedata policy;
- Encore commit and dirty-state flag;
- the cited upstream release-note snapshot or URL; and
- the raw log and pass/fail criteria.

That is sufficient to distinguish “the author documented this change,” “these
bytes are present,” and “Encore exercised these bytes.” None should substitute
for the other two.

---

[Documentation](README.md) · Previous: [Tournament preservation](49-tournament-server.md) · Related: [Community updates](47-community-updates.md)
