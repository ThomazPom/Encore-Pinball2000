# RFM and SWE1 game-code changelogs

This document consolidates the surviving release notes for both commercial
Pinball 2000 games. It deliberately separates three kinds of evidence:

- **Published notes** — a revision history issued by Williams/Bally or by the
  update's author.
- **Secondary report** — a dated owner/developer report, not an original
  changelog.
- **Version only** — the build or archive is known, but no trustworthy change
  notes have yet been recovered.

Dates and archive provenance are inventoried separately in
[Update provenance and redistribution boundary](47-community-updates.md).
The entries below describe changes *from the preceding version named by the
original publisher*; they are not claims that every intermediate build was
publicly released.

## Star Wars Episode I

### Williams/Bally and post-Williams releases

#### 1.20 — 16 September 1999 (published notes; changes from 1.00)

- Added translations and an optional ball saver, disabled by default.
- Added the G-U-N-G-A-N / J-A-R-J-A-R bumper-and-spinner rule.
- Ejected locked balls immediately at game end.
- Added high-score tables to the status report and instructions to attract
  mode.
- Let the action buttons move one line at a time during initials entry.
- Improved scoring, lamp and display choreography and made ramp/spinner combos
  tolerate switch errors.
- Fixed Replay Boost, the display seen with a PC keyboard attached, and an OS
  ROM-checksum calculation bug.
- Added an adjustment allowing slam tilt to reset the game.

#### 1.30 — 21 September 1999 (published notes; changes from 1.20)

- Fixed power cycling with Japanese DIP-switch settings. Williams stated that
  this update was unnecessary for 1.20 machines not using those settings.

#### 1.40 — 31 July 2000 (published notes; changes from 1.30)

- Updated the operating system.
- Improved coin/bill handling and the credit system.
- Closed a ramp-shot exploit in tournament mode.

#### 1.50 — 22 September 2003 (published notes; changes from 1.40)

- Incorporated final XINA 1.19.
- Fixed a factory-reset condition when booting with the power-driver board
  disconnected: the remembered country DIP setting could disagree with the
  open-cable value.

### Builds without recovered release notes

| Version | Line | What is known |
|---:|---|---|
| 0.40 | Williams/Bally pre-release | Installed on surviving production machines; no changelog recovered. |
| 0.43 | Williams/Bally development | Reported on a developer flash PRISM card; no changelog recovered. |
| 1.00 | Williams/Bally | Archive name/date are known; no 1.00 release notes recovered. |
| 1.10 | Williams/Bally | Archive name/date are known; no release notes recovered. |
| 1.60 | community/tournament-server | Historical updater filename only; the archived tournament instructions do not describe its changes. |
| 1.65 | community/tournament-server | Historical updater filename only; the archived tournament instructions do not describe its changes. |
| 1.66 | Hemtoni | Preserved ROM bundle; no accompanying notes in the ZIP. |
| 2.00 | community/tournament-server | Rewritten 1.30 test build exposing the otherwise missing “Questionmark Mission”, normally visible only in the game-menu statistics. It is not a chronological continuation of the regular SWE1 release line; this experimental purpose explains the apparently anomalous 2.00 number and February 2016 date. The updater itself is missing. |
| 2.00 | myPinballs | Preserved updater; published notes summarised below. |
| 2.01 | myPinballs | Preserved updater; published notes summarised below. |
| 2.10 | myPinballs | Preserved updater; published notes summarised below. |

#### Questionmark Mission experiment

The dormant `QuestionMarkScene` was exercised in Encore by changing only
`Scenes::choose_next_scene()` in a temporary copy of SWE1 2.10 so that every
normal mission draw returned the stock Question Mark scene (index 13). Mission
selection and activation otherwise followed the game's normal path.

The observed result was a minimal mystery award rather than a developed
mission: activation produced a black screen and added score, with no playable
rules, visible scene or meaningful presentation. The recovered code also
increments its start/finish audits and can award a `JEDI` letter. This explains
why the scene survived in statistics despite being inaccessible in ordinary
play.

This experiment characterises the dormant scene retained by the later 2.10
image. The missing community 2.00 updater was described as a rewritten 1.30
made to test that scene, but without its binary we cannot claim that its exact
presentation or behaviour was byte-for-byte identical.

### myPinballs releases

The following summaries are condensed from the author's combined Star Wars
update log.

#### 2.00 — 11 April 2025 (published notes)

- Expanded Random Awards with ball-save restart, opponent-score reduction,
  two million points, Double Scoring and Collect Bonus; introduced the Double
  Scoring and shaker modules.
- Added four sample scenes, Sub Escape start lamps, controls to reduce Jar Jar
  content or exclude Jar Jar Juggle, and made the C-3PO overlay translucent.
- Expanded Podrace to eight checkpoints by default, reduced the C-3PO build
  requirement from four to two, reordered its awards and added a three-million
  award plus Multiball Champion.
- Added Watto captive-ball rules and Quick Multiball. Quick Multiball can stack
  with other modes and scores jackpots from captive-ball hits.
- Added R2-D2 to the sneaky lane and introduced Droid Hurry Up, qualified at
  that lane and collected at the left drop target.
- Reworked autolaunch and ball-save control, temporarily made regular
  multiball four balls, and changed Jedi Multiball to 20 seconds of unlimited
  autolaunch followed by sudden death.
- Prevented the left saucer locking a ball during Quick Multiball and aligned
  ball-save/autofire lamps with RFM.
- Updated to XINA 1.38, including per-game real-knocker driver IDs, a delayed
  coin-door-message fix and a SWE1 ball-save correction for its lack of an
  autolauncher.

#### 2.01 — 1 May 2025 (published notes)

- Added the Searchlight attract lamp effect and returned the other attract
  effects more frequently.
- Fixed Jedi tables being reset at every startup.
- Updated the version/contact presentation, removed the Williams web address,
  and added the myPinballs site and logo.
- Greatly expanded attract flipper-button speech and fixed missing Jar Jar
  Juggle junk graphics.
- Added shaker feedback to Multiball, Battle Droids and R2-D2.
- Reduced Droid Hurry Up scoring by a factor of ten to match SWE1's scoring
  scale rather than RFM's.

#### 2.10 — 31 October 2025 (published notes)

- Fixed Multiball Champion and improved ball release at multiball start.
- Added a C-3PO Build Champion to the left loop and attract mode, plus basic
  C-3PO shaker feedback.
- Added shaker effects to Battle Droids, Federation Fighter, R2-D2, Queen's
  Fashion, Pod Race, Hangar Escape, Jar Jar Juggle, Sub Escape, Probe Droid,
  Skill Shot and the right target-bank magnet.
- Ported Midnight Madness from the RFM codebase and added its attract clock.
- Added the Bounty Hunter narrator/jibes to gameplay, outlanes, ball save and
  attract mode.
- Updated multidevice autolaunch handling and expanded the attract-logo colour
  transitions from three to sixteen.

## Revenge From Mars

### Williams/Bally and post-Williams releases

#### Pre-release builds (versions attested; notes missing)

| Version | Evidence |
|---:|---|
| 0.70 | Historical Williams update filename dated 29 March 1999. |
| 0.71 | Historical Williams update filename dated 29 March 1999. |
| 0.80 | Preserved prototype ROM ZIP for revision-2 hardware; no accompanying release notes. |
| 0.84 | Historical Williams update filename dated 6 April 1999. |
| 0.85 | Historical Williams update filename dated 5 April 1999. |
| 0.86 | Historical Williams update filename dated 6 April 1999. |
| 0.87 | Historical Williams update filename dated 6 April 1999. |

These filenames establish builds, not a reliable publication sequence: their
embedded dates overlap and do not sort numerically. No behavioural changelog
has been recovered for them.

#### 0.90 (version attested; notes missing)

Williams' own 1.00 history names 0.90 as its predecessor, and surviving owners
have reported machines still running it. No 0.90 release notes or standalone
updater have been recovered.

#### 1.00 — 5 May 1999 (published notes; changes from 0.90)

- Added system stability work intended to eliminate lockups and resets.
- Added the Martian Bowling, Martian Autopsy and Martian Tank question-mark
  modes, completed Attack Mars, and made Stroke of Luck award Hypno-Beam
  multiball.
- Added many difficulty adjustments, effect cancellation via flipper buttons,
  and protection against starting the next round introduction while a loose
  ball remained in play.
- Added match/initials sound and graphics, attract/pricing messages, automatic
  replay-score percentage management, and credit bookkeeping by award type.
- Improved broken-switch compensation for flipper/action buttons.
- Added Empty Balls and Switch Trace diagnostics, corrected flipper operation
  in solenoid test, displayed fuse values, and enabled keyboard-driven test
  mode including display flipping.
- Added the explicit power-driver-board/F108 disconnected warning.

#### 1.10 — 24 May 1999 (published notes; changes from 1.00)

- Changed flipper timing to reduce heat and extended hourly bookkeeping to
  seven days.
- Revised pricing for Norway and the Netherlands, report output, replay boost,
  and coin-door version identification.
- Added multiball lamp effects, changed Autopsy item ejection to fan outward
  from the centre, and improved switch compensation.

A long-running RFM technical archive independently warns that 1.00 drove the
flipper coils too hard on 50 Hz installations and recommends 1.1 or later. The
published 1.10 notes describe changed flipper timing to reduce heat, which is
consistent with that field report, but Williams' notes do not explicitly tie
the two statements together.

#### 1.20 — 9 June 1999 (published notes; changes from 1.10)

- Restored the United Kingdom country choice and corrected Canadian dollar
  bonus pricing.
- Reorganised System Information and added game/serial information plus tilt
  and replay timestamps.
- Added configurable saucer-light awards for Martians killed and configurable
  attract sounds.
- Made attract/game-over text obey the Insert Coins adjustment for swipe-card
  locations.
- Fixed full-line custom messages and improved the two-line pricing display.

#### 1.30 — 24 November 1999 (published notes; changes from 1.20)

- Added Martian Champion, a jet-bumper rule, start-of-ball save, and an
  Attack-Mars victory lap.
- Added optional ball looping when a shot has no effect or LOCK is lit.
- Added a Bonus Wave sudden-death choice between ramps plus loops and ramps
  only.
- Expanded Family Mode and debounced the service-credit switch.
- Fixed Mothership multiball occasionally starting without ejecting balls.

#### 1.40 — 31 January 2000 (published notes; changes from 1.30)

- Fixed a Martian Bowling reset.
- Always displayed the Bonus Wave total even with the Jet Exit Post disabled.
- Restored missing Martian Happy Hour animations.

#### 1.50 — 31 July 2000 (published notes; changes from 1.40)

- Updated the operating system.
- Improved coin/bill handling and the credit system.

#### 1.60 — 22 September 2003 (published notes; changes from 1.50)

- Incorporated final XINA 1.19 and the same disconnected-power-driver
  country-setting/factory-reset correction as SWE1 1.50.

### Community and Hemtoni builds

No original change notes have been recovered for these versions. They remain
listed so that absence of documentation is not mistaken for absence of the
build.

| Version | Line | What is known |
|---:|---|---|
| 1.21 | community/tournament-server | Historical updater filename only; no release notes found on the archived server. |
| 1.70 | community/tournament-server | Pinballworld required RFM 1.7 or newer for its full tournament setup, but published no per-release notes. |
| 1.80 | community/tournament-server | Extracted update preserved. The myPinballs tournament documentation says 1.8 adds scrolling tournament scores in attract mode and requires at least 8 MiB RAM. Pinballworld likewise distributed/recommended it and warned about extra RAM. |
| 1.90 | community/tournament-server | November 2017 custom/PUB build reported; additional RAM reportedly required. One owner relayed, without primary confirmation, that it contained extra animations and call-outs absent from 1.5/1.6/1.8. |
| 1.90 | Hemtoni | March 2018 ROM bundle preserved; distinct from the November 2017 build. |
| 1.91 | Hemtoni | May 2018 ROM bundle preserved. |
| 1.95 | Hemtoni | March 2018 ROM bundle preserved; no notes included. |

### myPinballs releases

The following summaries are condensed from the author's continuous update log.

#### 2.00 — 3 December 2018 (published notes)

- Introduced Quick-Shot hurry-up with progressive scoring, graphics and
  speech.
- Reworked Secret Weapon balance, speech and progression, including a
  regenerating Martian and completion requirement for the saucer light.
- Expanded Circle Shot awards with a missile champion, Martian bombs, ball
  save and Quick-Shot thresholds; added a Hypno-Beam champion.
- Added attract lamp shows/champion pages and more flipper-button speech.
- Allowed scenes from the centre saucer, fixed ball autolaunch timing, and
  expanded Stroke of Luck awards.
- Optimised service graphics/flash use and moved to XINA 1.30.

#### 2.10 — 11 April 2019 (published notes)

- Refined Quick-Shot timing, presentation, speech and shaker feedback.
- Added multiball add-a-ball support through locks, bottom lanes and Stroke of
  Luck; adjusted initial ball counts and Bonus Wave sudden death.
- Improved missile-circle progression, fixed Super Skill Shot and corrected
  several lamp/speech issues.
- Added shaker and real-knocker support with adjustments and effects across
  most major modes.
- Added a first Payback Time implementation, more champion fixes/balancing,
  and XINA 1.31.

#### 2.11 — 10 May 2019 (version attested; notes missing)

The build date places this intermediate release between RFM 2.10 and 2.20.
Its updater and original change notes have not been recovered. The present
myPinballs continuous changelog omits it, so no behavioural changes are
inferred from the version number alone.

#### 2.20 — 22 October 2019 (published notes)

- Added clock/date attract display and Midnight Madness.
- Added party-mode infrastructure including drunk flippers in Happy Hour.
- Added more lamp and shaker effects and lowered the default Attack Mars
  champion score to 200 million.
- Added LED-oriented lamp-test adjustments and moved to XINA 1.32.

#### 2.21 — 5 April 2020 (published notes)

- Added attract high-score pages and per-player tracking for saucer lights and
  saucers destroyed.
- Fixed missing shaker feedback for some destroyed ships.
- Added an option to clear credits at boot plus new colour definitions in
  XINA 1.33.

#### 2.22 / internal 2.30 — 30 June 2020 (published notes)

- Added Power Drain, which temporarily attacks the flippers during Martian
  Attack Multiball.
- Made Midnight Madness and add-a-ball logic support different trough sizes.
- Fixed scene ball save, hurry-up cleanup/presentation and Happy Hour flipper
  restoration around higher-priority modes.
- Recognised a six-ball trough, restored the original 1.5 sound file, expanded
  updater baud rates, freed flash space and moved to XINA 1.34.

#### 2.23 / internal 2.40 — 8 April 2021 (published notes)

- Added Score War party mode, cross-player score-changing awards, Martian
  bombs, bonus multiplier and improved collect-bonus behaviour to Stroke of
  Luck.
- Fixed Happy Hour recovery, queued Power Drain, Stroke of Luck/Payback Time
  ball conflicts, Payback Time state/shot/lamp errors, and duplicate starts.
- Made Mothership adapt to trough capacity and added substantial speech.
- Added score-reduction support in XINA 1.35.

#### 2.24 / internal 2.42 — 29 January 2022 (published notes)

- Added new presentation for ball save, Martian Attack, multiball and extra
  ball, plus Bonus Wave shaker feedback.
- Updated XINA 1.36 to permit game updates up to 8 MiB rather than 4 MiB.

#### 2.50 — 16 December 2022 (published notes)

- Added Double Scoring through Stroke of Luck.
- Allowed upgraded troughs to add three balls to Capture Multiball via smart
  bombs.
- Tightened Family Mode filtering in several modes and added adjustable shaker
  intensity.

#### 2.60 — 8 August 2024 (published notes)

- Requires the four-opto expansion board introduced for the physical-lock
  upgrade.
- Added support for physically locking three balls and revised right-lock
  handling.
- Added shaker effects across more modes, expanded Mothership targets and an
  adult Mothership option.
- Equalised the four Mystery Mode probabilities, added tournament-score
  attract content, revised tournament display flow, and made shaker power 100
  by default.

## Evidence gaps and contribution rule

Do not infer a changelog from a newer version's mere existence, file size,
symbols, or build date. Binary comparison can establish that code differs, but
not why. A newly recovered note should identify its author/page/archive and,
where possible, the exact version it compares against.

The largest current gaps are:

- SWE1 1.00 and 1.10;
- every community/tournament-server build for both games;
- Hemtoni's SWE1 1.66 and RFM 1.90/1.91/1.95;
- myPinballs RFM 2.11.

The archived Pinballworld tournament-server instructions are evidence for
compatibility, not a substitute changelog. They state that RFM 1.5 and SWE1
1.4 already worked with limited functionality, recommend RFM 1.7 or newer and
SWE1 1.5, and warn that RFM 1.8 needs additional RAM on an original computer.
They do not enumerate what changed in RFM 1.7/1.8 or the other server-indexed
builds.

The separate myPinballs Tournament System documentation supplies a more exact
1.8 boundary: its client connects with RFM 1.5 or later, while 1.8 is required
for tournament scores to scroll during attract mode. That page specifies at
least 8 MiB RAM. This describes the observable purpose of 1.8, but still is not
a complete source-level list of every change between 1.7 and 1.8.

## Research coverage

This consolidation was checked against more than the surviving publisher
pages. The search included:

- Williams/Bally and mirrored WMS revision histories;
- the RFM and SWE1 operation manuals and the 61-page Pinball 2000 repair guide;
- the Pinball2000.de collector, technical and game-swap pages;
- IPDB-linked revision-history material;
- the archived Pinballworld server pages and their captured URL inventory;
- current and archived myPinballs game/tournament documentation;
- Pinside, Museum of the Game, Pinballinfo, MAACA, Pinball Revolution,
  FlipperFrance, Aussie Arcade and ArcadeControls discussions;
- exact searches for every known community updater filename and version.

Manuals generally document how the flash update mechanism works, not what a
particular game revision changed. Forum observations are retained only when
they add a testable version-specific fact, and remain labelled secondary. Exact
filename searches for the community SWE1 1.60/1.65/2.00 and RFM
1.21/1.70/1.80/1.90 updaters produced no original release notes. This negative
result is recorded so that a future search can concentrate on newly recovered
archives, attachments and private collections rather than silently repeating
the same crawl.

## Sources

- [Williams/Bally RFM revision history](https://www.planetarypinball.com/mm5/Williams/tech/pin2000/software/rfm_history.html)
- [Williams/Bally SWE1 revision history](https://www.planetarypinball.com/mm5/Williams/tech/pin2000/software/sw_history.html)
- [Williams/Bally Pinball 2000 software catalogue](https://www.planetarypinball.com/mm5/Williams/tech/pin2000/software.html)
- [Post-Williams SWE1 1.50 and RFM 1.60 revision text](https://pinside.com/pinball/forum/topic/swe1-worth-updating-from-13-to-15)
- [Wizboy's description of the community SWE1 2.00 test build](https://pinside.com/pinball/forum/topic/pin2k-swep1-version-04-is-this-a-legit-version#post-18)
- [myPinballs RFM update log](https://www.mypinballs.com/software/rfm/code_updates.jsp)
- [Archived myPinballs home page advertising RFM 2.10 in October 2019](https://web.archive.org/web/20191030091013/https://mypinballs.com/)
- [myPinballs SWE1 2.00/2.01/2.10 combined update log (PDF)](https://www.mypinballs.com/files/pin2k/starwars_updates_log.pdf)
- [Archived Pinballworld tournament-server installation instructions](https://web.archive.org/web/20161223170430/http://pinball-tournament.servegame.com/all.html)
- [myPinballs Tournament System technical setup and RFM 1.8 requirements](https://mypinballs.com/tournament/core/techsetup.jsp)
- [Pinball2000.de RFM technical notes](https://www.pinball2000.de/rfm_techinfo.htm)
- [RFM 1.8 update and RAM discussion](https://pinside.com/pinball/forum/topic/rfm-update-code-issue)
- [RFM 1.6/1.8 archive discussion](https://pinside.com/pinball/forum/topic/rfm-software-update-problem)
- [RFM 1.90 PUB-card report](https://pinside.com/pinball/forum/topic/pintastic-2018-sturbridge-massachusetts-buy-sell-trade-and-beyond/page/6)
- [Pinball 2000 repair guide (PDF)](https://www.coinopstuff.com/manuals/pinball-2000-repair-guide.pdf)
- [Preserved version/archive inventory](47-community-updates.md)

---

← [Documentation index](README.md)
