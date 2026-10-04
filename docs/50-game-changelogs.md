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

The tables below use deliberately narrow labels:

| Label | Meaning |
|---|---|
| **Published notes** | a release author or Williams/Bally support page describes the changes |
| **Archived release history** | a release-history file is preserved by an independent archive, while the original publisher page is no longer available |
| **Secondary transcript** | a forum or collector post reproduces revision text, but the original release file or publisher page has not been recovered |
| **Preserved payload** | the corresponding extracted ROM components exist locally |
| **Installer preserved** | a source `.exe` exists under `updates/exe-sources/` |
| **No recovered notes** | bytes exist, but this checkout has no attributable change list |

“No recovered notes” does not mean “no changes.” It means that guessing from
strings, symbols or a binary diff would be reverse-engineering evidence rather
than a historical changelog.

## Sources of record

The source links and archives checked through 2026-10-04 are:

- [Williams/Bally RFM revision history](https://www.planetarypinball.com/mm5/Williams/tech/pin2000/software/rfm_history.html)
  for RFM 1.00 through 1.50;
- [IPDB-preserved RFM revision history](https://www.ipdb.org/files/4446/Bally_1999_Revenge_From_Mars_ROM_Revision_History.txt)
  for RFM 1.00 through 1.60; this is the complete “Revenge From Mars -
  Revision History” text, including the dated 2003 addition absent from the
  current Planetary Pinball page;
- [collector-preserved extended RFM revision text](https://pinside.com/pinball/forum/topic/rfm-190-software-do-you-have-the-exe-update-or-can-extract-from-pub#post-9286049)
  for RFM 0.50 through 0.90 and the post-Williams 1.70--1.91 line; this remains
  secondary evidence because the linked German source does not expose the
  original release-note files;
- [Williams/Bally SWE1 revision history](https://www.planetarypinball.com/mm5/Williams/tech/pin2000/software/sw_history.html)
  for SWE1 1.20 through 1.40;
- [IPDB-preserved SWE1 revision history](https://www.ipdb.org/files/4458/Williams_1999_Star_Wars_Episode_I_ROM_Revision_History.txt)
  for SWE1 1.20 through 1.50; its complete “Star Wars Episode One - Revision
  History” text adds the dated 2003 release above the 1.20--1.40 sequence;
- [myPinballs RFM update log](https://mypinballs.com/software/rfm/code_updates.jsp)
  for the unofficial 2.x line;
- [myPinballs SWE1 combined update notes](https://www.mypinballs.com/files/pin2k/starwars_updates_log.pdf)
  for SWE1 2.00 through 2.10.

> [!NOTE]
> The local outer-directory date records artifact provenance. A release date
> printed in an attributable history answers a different question, so the two
> dates are retained even when they differ.

### Archived-source integrity anchors

The links above establish provenance, but this page does not depend on them
remaining online: the useful change assertions are retained below in
paraphrased form. The first two hashes identify the Williams-history text after
normalizing CRLF line endings to LF; the other two identify the author-source
snapshots used for the community summaries:

| Game | Archived filename | Covered releases | Normalized SHA-256 |
|---|---|---|---|
| RFM | `Bally_1999_Revenge_From_Mars_ROM_Revision_History.txt` | 1.00--1.60 | `179d4dd53589878985debccfd0c37e40109bc74670c39697a2792f9ae306c6f9` |
| SWE1 | `Williams_1999_Star_Wars_Episode_I_ROM_Revision_History.txt` | 1.20--1.50 | `f1c011ddac97da29b21c5ea8e7d0eb99c4aed67de644f46945c16fe03b51e9bf` |
| RFM | `code_updates.jsp`, captured 2026-10-02 | 2.00--2.60 | `2ee567612cdca8902c65885daa656673c54035481c0b876cc9772c75310a72b3` |
| SWE1 | `starwars_updates_log.pdf`, captured 2026-10-02 | 2.00--2.10 | `71bc740163d9b063009784183cc868c9a8f2e8c08b5a02c5f1a598c3b4626099` |

The Pinside page is a live thread whose full-page hash changes with replies and
site markup, so a page hash would be misleading. Its dated, version-specific
facts are instead embedded below, together with the post anchor and explicit
secondary-evidence label.

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
| 1.00 | 1999-07-17 | core + PUB boot + sound flash | yes | no recovered notes |
| 1.10 | 1999-09-14 | core + PUB boot + sound flash | yes | no recovered notes |
| 1.20 | 1999-09-16 | core + PUB boot + sound flash | yes | Williams notes |
| 1.30 | 1999-09-21 | core + PUB boot + sound flash | yes | Williams notes |
| 1.40 | 2000-07-25 | core + PUB boot + sound flash | yes | Williams notes |
| 1.50 | 2000-07-25 | not recovered | no | historical server listing only |
| 1.50 | 2003-09-22 | core + PUB boot + sound flash | yes | archived release history |
| 1.60 | 2013-02-01 | core + PUB boot + sound flash | yes | no recovered notes |
| 1.65 | 2018-02-21 | core + PUB boot + sound flash | yes | no recovered notes |
| 1.66 | 2022-04-03 | core only | no | no recovered notes |
| 2.00 | 2016-02-26 | core + PUB boot + sound flash | yes | Question Mark owner reports; no recovered notes |
| 2.00 | 2025-04-11 | core + PUB boot + sound flash | yes | myPinballs notes |
| 2.01 | 2025-05-01 | core + PUB boot + sound flash | yes | myPinballs notes |
| 2.10 | 2025-10-31 | core + PUB boot + sound flash | yes | myPinballs notes |

The two 1.50 directories have different provenance labels but their six current
ROM components are byte-for-byte identical. Keep both names when preserving
provenance; do not describe them as different builds without new byte evidence.

### Revenge From Mars (`50070`)

| Version | Outer artifact date | Components                    | Source EXE | Notes status         |
| ------: | ------------------: | ----------------------------- | ---------- | -------------------- |
|    0.70 |          1999-03-29 | core + sound flash            | yes        | secondary transcript |
|    0.71 |          1999-03-29 | core + sound flash            | yes        | no recovered notes   |
|    0.84 |          1999-04-06 | core + sound flash            | yes        | no recovered notes   |
|    0.85 |          1999-04-05 | core + sound flash            | yes        | no recovered notes   |
|    0.86 |          1999-04-06 | core + sound flash            | yes        | no recovered notes   |
|    0.87 |          1999-04-06 | core + sound flash            | yes        | no recovered notes   |
|    1.20 |          1999-06-09 | core + PUB boot + sound flash | yes        | Williams notes       |
|    1.21 |          2016-05-20 | core + PUB boot + sound flash | yes        | no recovered notes   |
|    1.30 |          1999-11-24 | core + PUB boot + sound flash | yes        | Williams notes       |
|    1.40 |          2000-01-31 | core + PUB boot + sound flash | yes        | Williams notes       |
|    1.50 |          2000-07-25 | core + PUB boot + sound flash | yes        | Williams notes       |
|    1.60 |          2003-09-22 | core + PUB boot + sound flash | yes        | archived release history |
|    1.70 |          2006-02-06 | core + PUB boot + sound flash | yes        | secondary transcript |
|    1.80 |          2006-04-23 | core + PUB boot + sound flash | no         | secondary transcript + binary evidence |
|    1.90 |          2017-11-22 | core + PUB boot + sound flash | yes        | disputed secondary transcript |
|    1.90 |          2018-03-29 | core + PUB boot               | no         | no recovered notes   |
|    1.91 |          2018-05-30 | core + PUB boot + sound flash | no         | secondary transcript |
|    1.95 |          2018-03-29 | core + PUB boot               | no         | no recovered notes   |
|    2.00 |          2018-12-03 | core + PUB boot + sound flash | yes        | myPinballs notes     |
|    2.10 |          2019-04-11 | core + PUB boot + sound flash | yes        | myPinballs notes     |
|    2.20 |          2019-10-22 | core + PUB boot + sound flash | yes        | myPinballs notes     |
|    2.21 |          2020-04-05 | core + PUB boot + sound flash | yes        | myPinballs notes     |
|    2.22 |          2020-06-30 | core + PUB boot + sound flash | yes        | myPinballs notes     |
|    2.23 |          2021-04-08 | core + PUB boot + sound flash | yes        | myPinballs notes     |
|    2.24 |          2022-01-29 | core + PUB boot + sound flash | yes        | myPinballs notes     |
|    2.50 |          2022-12-16 | core + PUB boot + sound flash | yes        | myPinballs notes     |
|    2.60 |          2024-08-08 | core + PUB boot + sound flash | yes        | myPinballs notes     |

This is 40 extracted update paths in total: thirteen SWE1 and twenty-seven RFM.
Thirty-five have a matching source EXE in this checkout. A local ZIP or
extracted directory can preserve useful bytes without proving the history of
the missing original installer.

## Attested builds outside the extracted inventory

The extracted inventory is intentionally not the complete historical version
list. The following builds are supported by a contemporary report or an
archived revision relationship, but are not runnable extracted updates in this
checkout. They remain recovery leads rather than inferred changelogs.

### Star Wars Episode I

| Version | Line | Surviving evidence |
|---:|---|---|
| 0.43 | Williams/Bally development | reported on a developer PRISM card; no distributed container or changelog recovered |

### Revenge From Mars

| Version | Line | Surviving evidence |
|---:|---|---|
| 0.50 | Williams/Bally pre-release | secondary revision transcript dates the Waukegan startup release to 1999-03-07 |
| 0.60 | Williams/Bally sample release | secondary revision transcript dates the sample release to 1999-03-16 |
| 0.80 | Williams/Bally revision-2 prototype | preserved `rfm_080.zip`, but not an extracted update path |
| 0.90 | Williams/Bally pre-release | named by the Williams 1.00 history as its predecessor; a secondary revision transcript survives, but no standalone updater is established |
| 2.11 | myPinballs intermediate | build dated 2019-05-10; updater and complete notes have not been recovered |

The dates attached to these names overlap and do not always sort numerically.
They establish identities, not a trustworthy publication order. The full
container-level provenance and the preserved Hemtoni bundles are recorded in
[Community updates](47-community-updates.md).

## Williams/Bally and post-Williams release histories

This section is a self-contained, item-by-item paraphrase of the published and
archived revision histories, not a set of claims inferred from Encore behavior.
It deliberately preserves the complete factual changelog while avoiding a
verbatim mirror of the source files.

### Revenge From Mars

#### Base ROM 0.1 — directly observed

The ordinary unsuffixed RFM base chips predate the extracted update inventory.
A disposable launch with no update or savedata, captured on the headless guest
console:

```bash
scripts/run-qemu.sh \
  --game rfm \
  --update none \
  --no-savedata \
  --headless \
  --audio none
```

reported the following through the guest console:

```text
system_version: 0.1
game_version:   0.1
Game(Bally - Revenge From Mars - 50070 - 0.1)
Type(PRODUCTION_BUILD, FREE_PLAY_ONLY)
```

This establishes that the preserved base build exposes free play only; it does
not establish exactly which later intermediate build first implemented credit
play. The separate RFM revision-2 base pair selected by `--update r2` identifies
as 0.80 and must not be conflated with this 0.1 production base.

> [!IMPORTANT]
> The 0.50--0.90 entries below come from a collector-preserved revision
> transcript, not from the IPDB/Williams file used for 1.00--1.60. They retain
> useful pre-release knowledge but carry the weaker **secondary transcript**
> label. In particular, its single `0.7` entry does not establish whether the
> surviving 0.70 and 0.71 filenames contained identical changes.

#### Version 0.50 — 1999-03-07 (secondary transcript)

- Identified as the RFM release used for the Waukegan startup.

#### Version 0.60 — 1999-03-16 (secondary transcript)

- Identified as the RFM release prepared for sample games.
- Corrected Shoot Again presentation that requested music without telling the
  updater that the effect had played with music.
- Improved error handling at scene completion and changed the audio track used
  for the scene-selection group-change sound.
- Reduced the frequency of the Joe Dillon tribute in attract mode.
- Corrected missing audits for Capture Multiball super jackpot, Question Mark
  starts, and Attack Mars starts and wins.
- Moved the Martian Aerobics ship into ball range at the end, suppressed its
  zero-value bonus page and reduced the ship-hit score.
- Fixed unsafe Abduction score-box access from award effects.
- Corrected Mother Ship totals and a ship that could remain at the top after a
  Shoot Again effect.
- Added an Attack Mars enable lamp effect and made its flasher pulse during the
  mode.
- Added device `force_game_over` hooks so a test-system game termination could
  leave mechanisms in a stable state.
- Reorganized several audit and adjustment priorities.

#### Version 0.70 — 1999-03-26 (secondary transcript labels it `0.7`)

- Prevented top-lane hits from starting timers while a recent jet event was
  active; shortened the retry period for multiball-device pre-kick sounds; and
  removed an erroneous scene-completion nonfatal.
- Reworked Attack Mars display depth, fixed a Final Frontier case that could
  end the mode when balls should have been returned, added its total page, and
  changed crosshair arrows and steering back to the ship's perspective.
- Added a Mars Kneads Women difficulty control for the number of static TVs.
- Made initials entry start on `A` instead of a space and removed the unwanted
  leading ` AA` result.
- Forced Alien Abduction items into a stable, tournament-friendly order,
  paused its countdown when its background effect was not running, and added
  redundant detection for its scoring shots.
- Fixed score-box hiding, added custom font-pointer support to text boxes,
  enabled movie blowoff in Mars Kneads Women and reorganized total-page display
  and lamp effects around `scene_completed_finish`.
- Removed shooter-lane lights from the upper-playfield lamp effect so the
  ball-shooter effect could appear again.
- Added an intentionally empty extra-ball-award effect hook because the
  awarding sites already handled those effects themselves.
- Made Mother Ship easier so players could reach its later, higher-scoring
  waves, and increased Drive In Demolition scoring.
- Added score boxes, switch compensation, health bars and preset-driven
  difficulty adjustments to Tower Struggle and Big-O-Beam.
- Removed `Start Attack Mars` from Stroke of Luck because the center shot starts
  it, and added `Start Capture Multiball` under ready/last-ball conditions.
- Added a still-incomplete valid-playfield check intended to suppress scene
  start/end effects when appropriate.
- Turned the coin-door illumination on continuously at power-up and cleaned up
  status-report diagnostics.
- Updated circle-shot and lock lamps immediately when Stroke of Luck granted
  their awards.
- Reworked Martian Attack suspension and restored its timer to five seconds
  after a kill when less time remained.
- Held Capture Multiball in its grace period when play dropped to one ball but
  the moving ship remained available for a super jackpot.

#### Version 0.80 — 1999-03-30 (secondary transcript)

- Added a spinning-coin impact sound to Secret Weapon and fixed a missing final
  move animation after some ending shots.
- Fixed a Happy Hour pre-emption case that could raise an oversized bitmap
  decompression nonfatal.
- Began a rewrite of Attack Mars stage two: the mechanism was present, but its
  new scoring was not yet complete.
- Added and corrected Attack Mars sounds, including the large-explosion drone
  overload sound.
- Removed unused Fireworks variables.
- Roughed in Skill Shot and Super Skill Shot, but deliberately left them out of
  that release because they had not been tested sufficiently.

#### Version 0.90 — 1999-04-07 (secondary transcript)

- Reworked lock-diverter jam-switch logic.
- Moved Shoot Again lamp blinking into a background autofire lamp effect.
- Added sound when circle shots lit Extra Ball.
- Cleaned up Martian Attack and made ten small-Martian kills during Martian
  Multiball award one saucer light.
- Added game-specific default replay values to the factory-overwrite preset.
- Added a Hypno-Beam Multiball difficulty preset.
- Moved the Skill Shot award display so it no longer overlapped the bonus box.
- Added Hypno-Beam Multiball, guaranteed as the third—or first subsequent—
  Stroke of Luck award.

#### Version 1.00 — 1999-05-05

- Low-level system work targeted lockups and resets; the release history
  characterizes the resulting build as exceptionally stable.
- Stroke of Luck could now award Hypno-Beam multiball.
- The fifth, question-mark round gained Martian Bowling, Martian Autopsy and
  Martian Tank.
- Attack Mars was completed.
- Flipper presses could cancel selected effects.
- Numerous difficulty adjustments were implemented.
- After completing a round, the introduction to a later round was suppressed
  while the ball remained live instead of being captured by the playfield post.
- Match gained sound and graphics, while Enter Initials gained sound.
- Broken-switch compensation for the flippers and Action button was improved.
- Attract and pricing messages became available and editable from test mode by
  either the cabinet test controls or a keyboard.
- `Score Award 2` gained a credit-award setting, and replay-score management
  gained automatic percentage adjustment.
- Service mode gained an `Empty Balls` test.
- Failure to detect the power-driver board now produced a diagnostic pointing
  to a disconnected board or fuse F108.
- Separate audit totals were maintained for credits awarded by high score,
  score award, match and special.
- The shell gained real-time `Switch Trace` output.
- The solenoid test was corrected to operate the flipper coils.
- The test system gained keyboard control, including its help map and flip
  function.
- Test reports began listing fuse values.
- The hourly-earnings chart was extended to the preceding seven days.

#### Version 1.10 — 1999-05-24

- Flipper-circuit timing was changed to reduce operating temperature.
- Hourly bookkeeping was extended to seven days.
- Pricing behavior changed for Norway, the Netherlands and Holland.
- Printed adjustments gained replay information and DIP-switch settings.
- Replay boost became available when score awards were configured to grant
  credits.
- Coin-door-open messages began showing the game name and version.
- Multiball start and multiball awards gained lamp effects.
- Martian Autopsy began ejecting items as a fan, starting from the center shot.
- Switch compensation was improved in several areas.

> [!NOTE]
> A long-running [RFM technical archive](https://www.pinball2000.de/rfm_techinfo.htm)
> reports that 1.00 overpowered flipper coils in 50 Hz countries and recommends
> 1.10 or later. The 1.10 history independently records cooler flipper timing;
> the agreement is strong contextual evidence, but the Williams note itself
> does not explicitly say that the timing change fixed the 50 Hz field failure.

#### Version 1.20 — 1999-06-09

- The United Kingdom country option, absent from 1.10, was restored.
- Canadian-dollar bonus pricing was corrected.
- The System Information page was reorganized and gained the game and serial
  numbers.
- Tilt and replay records gained timestamps.
- New adjustments controlled the number of defeated Martians required to award
  saucer lights.
- Attract-mode sounds and their adjustment were added.
- Attract and game-over screens began honoring the `Insert Coins` adjustment,
  allowing that phrase to stay hidden at swipe-card locations.
- A custom-message boundary bug that displayed garbage when a line used its
  final available character was fixed.
- Attract pricing changed to two lines per page, skipped empty lines
  intelligently and centered a page containing only one line.

#### Version 1.30 — 1999-11-24

- Martian Champion was added.
- A jet-bumper rule and a start-of-ball ball saver were added.
- Winning Attack Mars could start a victory-lap rule modeled on Attack From
  Mars.
- The ball could be allowed to loop when a scene had no action for that shot or
  when `LOCK` was lit. New adjustments controlled this behavior and defaulted
  it to off.
- A separate adjustment allowed loop shots during Bonus Wave sudden death.
- `Bonus Wave Ending` gained a new `Ramp Only` choice alongside the original
  `Ramps+Loops` behavior.
- Family Mode logic was expanded.
- Debouncing was added to the service-credit switch.
- A Mother Ship startup fault that could leave multiball balls unejected was
  fixed.

#### Version 1.40 — 2000-01-31

- A Martian Bowling fault that could reset the game was fixed.
- The Bonus Wave total page was made unconditional, including when the Jet Exit
  Post was disabled.
- Missing Martian Happy Hour animations were corrected.

#### Version 1.50 — 2000-07-31

- The operating system was updated to the then-current release.
- Coin, bill and credit handling was enhanced.

#### Version 1.60 — 2003-09-22

- Final XINA 1.19 fixed a possible factory reset after booting without the
  power-driver board: the last country DIP value stored in CMOS could otherwise
  disagree with the country value used for an open PDB cable.

RFM 1.00 and 1.10 are described by the published history but are not preserved
as extracted update bundles here. RFM 1.60 is not merely supported by the
previously cited forum transcript: the IPDB file preserves the complete RFM
revision history, retains the Williams copyright notice and adds a dated 1.60
entry above the same 1.00--1.50 history independently preserved elsewhere.
That makes it **archived release-history evidence**, not a current first-party
web publication and not a community changelog.

#### Version 1.70 — 2006-04-16 (secondary transcript)

- Packaged XINA 1.20.
- Added JTS tournament-system support.

The transcript date differs from the date encoded in the separately attested
`0170_02062006` updater name. Both values are retained; neither is silently
rewritten to make the chronology look cleaner.

#### Version 1.80 — 2006-04-23 (secondary transcript plus binary evidence)

- Packaged XINA 1.21.
- Required 8 MiB of system memory.

The memory statement is independently supported by the preserved binary's
`sizmem()` implementation and by the tournament-system technical setup. The
exact code evidence and the later return to 4 MiB are documented in
[Why RFM 1.80 needs 8 MiB](#why-rfm-180-needs-8-mib).

#### Version 1.90 — transcript date 2017-11-21 (identity disputed)

The secondary transcript attributes the following changes to a 1.90 build
using XINA 1.22:

- removed the 8 MiB requirement;
- retained English and German while removing Spanish and French to save ROM
  space;
- restored the Joe Dillon tributes;
- added spider and ant as the fifth and sixth Big-O-Beam animals;
- enabled Ball Saver and Loop Dead Scene by default; and
- replaced the blinking attract lamp show with revised choreography.

It also speculates about other unused animation and sound content; that phrase
is preserved here as an unresolved suggestion, not promoted into a confirmed
change.

> [!WARNING]
> The thread later distinguishes the November 2017 tournament-server PUB build
> from Hemtoni's locally preserved March 2018 `1.90`. It explicitly says their
> precise differences remain unclear. Therefore the list above belongs only to
> the **secondary transcript's dated 2017 identity** and must not be presented
> as the changelog of Encore's 2018 Hemtoni payload.

The PUB owner also reproduces separate XINA notes in which 1.22b fixes a TCP
port bug and translations and returns to the factory 4 MiB memory model. The
local Hemtoni 1.90 binary likewise reports 4 MiB, corroborating the memory
outcome only—not identity with the unavailable 2017 image.

#### Version 1.91 — transcript date 2018-05-31 (secondary transcript)

- Identifies XINA 1.22.
- Adds sounds, notably new attract-mode speech announcing the 3D presentation.

The preserved Hemtoni bundle carries a 2018-05-30 outer date. This one-day
difference is recorded rather than used to infer a second build. No recovered
revision text describes 1.95.

### Star Wars Episode I

#### Base ROM 0.40 — directly observed

The ordinary SWE1 base-chip set is preserved locally; 0.40 is not merely a
production-cabinet report. A fresh launch with update discovery disabled and
no savedata:

```bash
scripts/run-qemu.sh \
  --game swe1 \
  --update none \
  --no-savedata \
  --headless \
  --audio none
```

loaded `swe1_u100` through `u107` plus `u109`/`u110`, left the update flash
erased, and reported through the guest console:

```text
system_version: 1.12
game_version:   0.40
Game(Williams - Episode I - 50069 - 0.40)
Type(PRODUCTION_BUILD, ALLOW_SCORE_CREDIT)
```

This directly establishes a preserved production base with credit support. It
does not recover a change list for 0.40 or establish the contents of the
separately reported 0.43 developer image.

#### Version 1.20 — 1999-09-16

- Translations were added.
- An optional ball saver was introduced, disabled by default.
- The bumpers and spinner gained the G-U-N-G-A-N/J-A-R-J-A-R rule.
- Locked balls began ejecting immediately when a game ended.
- The status report gained high-score-to-date tables.
- Attract mode gained instructions.
- During initials entry, the Action buttons could move one line up or down.
- Various scoring, lamp and display-choreography issues were adjusted.
- Ramp/spinner combinations became tolerant of ramp or switch errors.
- A replay-boost bug was fixed.
- The display shown when a PC keyboard was attached was corrected.
- An adjustment was added to let slam tilt reset the game.
- An operating-system checksum-calculation bug affecting some ROMs was fixed.

#### Version 1.30 — 1999-09-21

- Power cycling caused by Japanese DIP-switch settings was fixed. The archived
  note says this update was unnecessary for a machine already on 1.20 unless it
  used those Japanese settings.

#### Version 1.40 — 2000-07-31

- The operating system was updated to the then-current release.
- Coin, bill and credit handling was enhanced.
- Tournament mode was fixed to prevent exploitation of ramp shots.

#### Version 1.50 — 2003-09-22

- Final XINA 1.19 fixed a possible factory reset after booting without the
  power-driver board: the last country DIP value stored in CMOS could otherwise
  disagree with the country value used for an open PDB cable.

> [!NOTE]
> A [cached historical server listing](https://pinside.com/pinball/forum/topic/rfm-190-software-do-you-have-the-exe-update-or-can-extract-from-pub#post-9286049)
> also shows `pin2000_50069_0150_07252000_B_10000000.exe` as a distinct 1.4M
> file alongside the September 2003 1.50. Its binary and release notes have not
> been recovered, so this page does not assign the 2003 change above to the July
> 2000 artifact. A former local July directory was removed because its bytes were
> only a mislabelled duplicate of the preserved 2003 payload.

SWE1 1.20 is now preserved as both its original updater and extracted payload.
As with RFM 1.60, the IPDB document promotes SWE1 1.50 from a forum-only
transcript to **archived release-history evidence**; it does not turn the
archive into a current first-party publisher page. No recovered note supports
a separate change summary for 1.60, 1.65 or 1.66.

### Question Mark and community test scenes

`QuestionMarkScene` is real retained SWE1 game code, not merely a label found
in the community build. The preserved 1.50 symbol table already contains its
availability, start, end, intro, background, audio and audit objects, and the
game image contains `Question Mark Started`, `Question Mark Finished` and
`Scene - Question Mark` strings.

#### The 2016 tournament/community experiment

The exact historical updater
`pin2000_50069_0200_02262016_B_10000000.exe` and its six ROM components are
now preserved. Owner reports describe it as a rewritten SWE1 1.30 test build
exposing the otherwise unreachable “Questionmark Mission”. It was numbered
2.00 but was not a chronological successor to the factory line.
A [contemporary owner discussion](https://pinside.com/pinball/forum/topic/p2k-swep1-question-mark-mission)
independently confirms the puzzle that motivated such a build: the stock game
recorded Question Mark started/ended statistics, yet owners could not identify
a normal way to play it.

The recovered bytes establish the build identity, but they do not by themselves
prove the reported purpose. Its exact code changes and runtime presentation
remain to be characterized. It must not be confused with the unrelated
myPinballs 2.00 released in 2025.

#### The 2025 myPinballs re-exposure

The [SWE1 2.0 author's published notes](https://www.mypinballs.com/files/pin2k/starwars_updates_log.pdf)
then state: “Scene - Added sample modes in, 4 extra (for testing).” A comparison
of `scene_table_data` supplies the missing identity evidence:

| SWE1 build | Destroyer Droid | Hover Tank | Watto's Chance | Question Mark |
|---|---|---|---|---|
| 1.50 | `(0,1,0)` | `(0,1,0)` | `(0,1,0)` | `(0,4,2)` |
| myPinballs 2.00 (2025) | `(0,1,0)` | `(0,1,0)` | `(0,1,0)` | `(0,1,0)` |

The tuple labels are not preserved, so this document does not invent names for
the three fields. The relevant fact is the exact transition: myPinballs 2.00
reclassifies Question Mark to match the three other late sample scenes while
its release note announces four test modes. Taken together, these are strong
evidence that **Question Mark is the fourth test scene exposed by the 2025
community release**. This locally verifiable result independently supports the
reported purpose of the now-preserved 2016 experiment; it does not prove the two
builds implemented the exposure identically.

The reproducibility anchors are `scene_table_data` at `0x002e02a0` in 1.50 and
`0x002de9b8` in myPinballs 2.00. Each table row is five little-endian 32-bit
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
2.10—not necessarily the now-preserved 2016 build byte for byte.

## Why RFM 1.80 needs 8 MiB

The surviving [myPinballs technical setup](https://mypinballs.com/tournament/core/techsetup.jsp)
states that RFM 1.5 or later can connect to its tournament system, but that the
scrolling attract-mode scores require 1.8. It explicitly requires at least
8 MiB for 1.8 and notes that most games shipped with only 4 MiB.

The preserved update programs explain why. Their symbol ROMs locate the same
ten-byte `sizmem(void)` function, and the matching game ROMs contain:

```text
55 89 e5 b8 00 04 00 00 c9 c3  -> return 0x400 (XINU uses 4 MiB)
55 89 e5 b8 00 08 00 00 c9 c3  -> return 0x800 (XINU uses 8 MiB)
```

| Preserved RFM build | `sizmem()` address | Returned ceiling |
|---:|---:|---:|
| 1.50 | `0x002629c0` | 4 MiB |
| 1.60 | `0x002629d8` | 4 MiB |
| **1.80** | `0x00262c8c` | **8 MiB** |
| 1.90 | `0x0024cc50` | 4 MiB |
| 1.91 | `0x00262624` | 4 MiB |
| 1.95 | `0x0024cb60` | 4 MiB |
| 2.00 | `0x0026628c` | 4 MiB |
| 2.10 | `0x00269804` | 4 MiB |
| 2.20 | `0x0026da24` | 4 MiB |
| 2.21 | `0x0026f530` | 4 MiB |
| 2.22 | `0x00271288` | 4 MiB |
| 2.23 | `0x00271d18` | 4 MiB |
| 2.24 | `0x00272534` | 4 MiB |
| 2.50 | `0x0027391c` | 4 MiB |
| 2.60 | `0x0027456c` | 4 MiB |

> [!IMPORTANT]
> This establishes a narrow but strong conclusion: 1.80 deliberately raised
> XINU's usable-memory ceiling from 4 to 8 MiB, and the value is back to 4 MiB
> in every preserved release from 1.90 onward. On an unexpanded 4 MiB
> cabinet, 1.80 therefore assumes memory the machine does not physically have;
> reports of malfunction on stock machines have a direct technical
> explanation. The binaries do not by themselves prove which symptom every
> owner saw or the authors' stated motive for reverting the value.

Encore exposes 16 MiB of physical RAM, so unmodified 1.80 has the 8 MiB it asks
for and can appear healthy. That is correct for an upgraded cabinet but can
mask 1.80's original stock-hardware incompatibility. Conversely, Encore's
optional 4→14 MiB signature patch does not match 1.80's distinct 8 MiB body;
it is neither needed nor silently applied to that build.

## Unofficial myPinballs releases

These releases are community game-code updates. “Latest” in Encore means the
highest locally installed version; it does not mean that Encore recommends the
release for every physical playfield configuration.

### Revenge From Mars 2.x

#### 2.00 — 2018-12-03

- Introduced the progressive Quick-Shot hurry-up with graphics and speech.
- Rebalanced Secret Weapon's speech and progression, added an optional round
  announcement and made the Martian regenerate after inactivity.
- Expanded Circle Shot thresholds with Martian bombs, ball-save restarts and
  Quick-Shot awards; added missile and Hypno-Beam champion records.
- Added attract lamp shows, champion pages and more flipper-button speech.
- Allowed scenes to start from the center saucer, corrected autolaunch timing
  and expanded Stroke of Luck awards.
- Optimized service graphics and flash use, and updated the packaged system to
  XINA 1.30.

#### 2.10 — 2019-04-11

- Refined Quick-Shot timing, display behavior, speech, lamps and shaker
  feedback.
- Expanded Capture Multiball and added add-a-ball paths through locks, bottom
  lanes and Stroke of Luck; revised initial ball counts and Bonus Wave sudden
  death.
- Improved missile-circle progression, repaired Super Skill Shot and corrected
  several lamp, speech and champion cases.
- Added configurable shaker and physical-knocker support with feedback across
  the major modes.
- Added an initial Payback Time implementation, revised several mode awards and
  updated the packaged system to XINA 1.31.

#### 2.11 — 2019-05-10

This intermediate build is historically attested, but neither its updater nor
a complete original change list has been recovered. Its version number alone
is not used to infer behavior.

#### 2.20 — 2019-10-22

- Added an attract clock/date display and Midnight Madness.
- Added party-mode infrastructure, including the drunk-flipper Happy Hour
  option.
- Expanded lamp and shaker effects and lowered the default Attack Mars
  champion score to 200 million.
- Added LED-oriented lamp-test controls and updated the packaged system to XINA
  1.32.

#### 2.21 — 2020-04-05

- Added attract high-score pages and per-player tracking for saucer lights
  collected and saucers destroyed.
- Fixed missing shaker feedback for some destroyed ships.
- Added optional credit clearing at boot, new color definitions and packaged
  XINA 1.33.

#### 2.22 / internal 2.30 — 2020-06-30

- Added Power Drain, which temporarily attacks the flippers during Martian
  Attack Multiball.
- Generalized Midnight Madness and Capture Multiball add-a-ball logic for
  different trough capacities.
- Fixed scene ball save, hurry-up cleanup/presentation and restoration of Happy
  Hour flippers around higher-priority modes.
- Recognized a six-ball trough, restored the original 1.50 sound file, added
  updater baud rates, freed flash space and packaged XINA 1.34.

#### 2.23 / internal 2.40 — 2021-04-08

- Added Score War, in which Stroke of Luck can reduce opponents' scores during
  multiplayer games.
- Expanded Stroke of Luck with random points, player-score changes, Martian
  bombs, bonus multiplier and improved Collect Bonus presentation.
- Fixed Happy Hour restoration, queued Power Drain, Stroke of Luck/Payback Time
  ball conflicts, Payback Time shot/lamp state and duplicate mode starts.
- Made Mothership adapt to trough capacity, added extensive speech and packaged
  XINA 1.35 score-reduction support.

#### 2.24 / internal 2.42 — 2022-01-29

- Added new presentation for ball save, Martian Attack, multiball and extra
  ball, plus Bonus Wave shaker feedback.
- Packaged XINA 1.36, raising the maximum future game-update size from 4 to
  8 MiB.

#### 2.50 — 2022-12-16

- Added Double Scoring as a Stroke of Luck award.
- Let upgraded troughs use smart bombs to add three balls to Capture Multiball.
- Tightened Family Mode speech filtering and added adjustable shaker intensity.

#### 2.60 — 2024-08-08

- Added system, multiball and right-lock support for the physical three-ball
  lock hardware.
- Expanded Mothership targets, added an adult Mothership option and added
  shaker feedback to Mothership, Autopsy, Tank, Bowling and Invaders.
- Equalized the four Mystery Mode probabilities and added diagnostic output for
  the Bowling fault.
- Added tournament-score attract content, revised tournament display flow and
  changed the default shaker power to 100.

> [!WARNING]
> The author states that RFM 2.60 and later require the full four-opto hardware
> expansion. A successful emulator boot cannot certify a physical cabinet for
> that release. RFM 2.50 is the last locally preserved version before that
> stated hardware boundary.

### Star Wars Episode I 2.x

#### 2.00 — 2025-04-11

- Expanded Random Awards with ball-save restart, opponent-score reduction,
  two million points, Double Scoring and Collect Bonus; introduced the Double
  Scoring and shaker modules.
- Added four sample scenes, Sub Escape start lamps and settings to reduce Jar
  Jar content or exclude Jar Jar Juggle.
- Expanded Podrace to eight checkpoints by default, made the C-3PO overlay
  translucent, reduced the build requirement from four to two, reordered its
  awards and added three million points plus a Multiball Champion.
- Added Watto captive-ball rules and Quick Multiball, which can stack with
  other modes and scores jackpots from captive-ball hits.
- Added R2-D2 to the sneaky lane and introduced Droid Hurry Up, qualified there
  and collected at the left drop target.
- Reworked autolaunch and ball-save control, temporarily made regular
  multiball four balls, and changed Jedi Multiball to 20 seconds of unlimited
  autolaunch followed by sudden death.
- Prevented the left saucer from locking a ball during Quick Multiball and
  aligned ball-save/autofire lamp behavior with RFM.
- Packaged XINA 1.38 changes for per-game physical-knocker driver IDs, delayed
  coin-door messages and SWE1 ball-save behavior without an autolauncher.

#### 2.01 — 2025-05-01

- Added the Searchlight attract effect and returned the other lamp effects to
  the attract loop more frequently.
- Fixed Jedi tables being reset at every startup.
- Updated the version/contact presentation, removed the Williams web address
  and added the myPinballs site and logo.
- Greatly expanded attract flipper-button speech and restored missing Jar Jar
  Juggle junk graphics.
- Added shaker feedback to Multiball, Battle Droids and R2-D2.
- Reduced Droid Hurry Up scoring by a factor of ten to match SWE1 rather than
  RFM's score scale.

#### 2.10 — 2025-10-31

- Fixed Multiball Champion and improved ball release at multiball start.
- Added a C-3PO Build Champion to the left loop and attract mode, plus basic
  C-3PO shaker feedback.
- Added shaker effects to Battle Droids, Federation Fighter, R2-D2, Queen's
  Fashion, Pod Race, Hangar Escape, Jar Jar Juggle, Sub Escape, Probe Droid,
  Skill Shot and the right target-bank magnet.
- Ported Midnight Madness from RFM and added its attract clock.
- Added the Bounty Hunter narrator and jibes to gameplay, outlanes, ball save
  and attract mode.
- Updated multi-device autolaunch handling and expanded attract-logo color
  transitions from three to sixteen.

The combined source note says the 2.00 line includes XINA 1.38 changes. Treat
that as part of the packaged community release, not as an Encore operating
system version.

## Known changelog gaps

The repository or surviving histories attest more versions than this checkout
can pair with primary change notes. The most useful open gaps are:

- SWE1 0.40 change notes, the separate 0.43 developer image, 1.00, 1.10 and
  community/tournament builds 1.60 and 1.65;
- SWE1 1.66, whose four-component payload is preserved without notes;
- the 2016 community “2.00” Question Mark test build, whose updater is now
  preserved but whose original accompanying note and exact code changes remain
  uncharacterized;
- RFM 1.21, primary identity-specific notes for the 1.70/1.80 tournament line,
  and any 1.80 gameplay changes beyond the established JTS/memory facts;
- definitive changelogs tying the distinct 2017 tournament 1.90 and 2018
  Hemtoni 1.90 bytes to their changes, plus complete notes for preserved 1.91
  and 1.95; and
- RFM 2.11, reported between 2.10 and 2.20 but absent from both the current
  local payload set and the author's continuous changelog.

The 1.80 memory requirement and its exact implementation are established
above, but they are not a complete 1.70-to-1.80 gameplay release note. See
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
> SWE1 1.50: a [cached historical server listing](https://pinside.com/pinball/forum/topic/rfm-190-software-do-you-have-the-exe-update-or-can-extract-from-pub#post-9286049)
> attests separate July 2000 and September 2003 filenames, but only the 2003
> binary is preserved. A former local July path contained a mislabelled copy of
> the 2003 payload and was removed. Evidence reports should record the complete
> resolved path and component hashes.

Do not reuse persistent flash when the question is “what does this release do
from a clean state?” `--update none` alone does not erase an existing saved
flash; pair it with `--no-savedata` for a clean base-ROM comparison.

## What a changelog does not prove

A published feature does not prove that Encore emulates every device it uses.
Examples include:

- network tournament clients without a validated end-to-end server and card
  reader path;
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

Hash the preserved SWE1 1.50 payload and its source container:

```bash
sha256sum updates/pin2000_50069_0150_09222003_B_10000000/50069/*.rom \
  updates/exe-sources/pin2000_50069_0150_09222003_B_10000000.exe
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
