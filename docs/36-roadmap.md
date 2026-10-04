# 36 — Roadmap

Encore's next work should reduce uncertainty and operational risk before it
adds emulator modes. The default path already boots both games, uses natural
PIT/PIC delivery, renders through direct SDL, executes original DCS firmware
and can run with an emulated or physical driver board. The highest-value gaps
are now evidence, release governance and data safety.

This is a prioritized engineering backlog, not a promise of dates. Current
constraints and workarounds remain authoritative in
[Known limitations](35-known-limitations.md).

## Decision rules

Use these rules when adding or promoting roadmap work:

1. Preserve one obvious normal path. A new experiment does not become another
   permanent delivery mode without a measured user benefit.
2. Define the claim and success criteria before changing timing, sound,
   display or physical I/O.
3. Prefer removal, validation and observability over a second mechanism that
   masks the first one's failure.
4. Never compensate host delay by inventing guest IRQs or unbounded catch-up.
5. Physical-cabinet claims require physical evidence; emulator fixtures are
   not substitutes.
6. Keep historical experiments in Git, not in the public CLI or active
   documentation.

> [!IMPORTANT]
> “Builds”, “boots” and “supported” are different milestones. Every item below
> has an explicit completion condition so a partial result cannot silently be
> promoted.

## Priority 0 — release governance

### Establish a project license and provenance record

The repository has no project-level license. Before treating the source tree
as a formal redistributable project:

- choose and add the project license;
- inventory imported/adapted code and preserve its notices;
- separate game/update assets from emulator-source licensing terms;
- document contribution terms and provenance expectations;
- verify that release archives carry the required notices.

**Complete when:** a root license and provenance/notice inventory cover every
distributed source category, and a release-archive test confirms their
presence.

### Finish the documentation cutover

Continue the current code-first refresh one page at a time, then:

- perform the repository-wide orphan feature sweep;
- rebuild the documentation index and top-level README last;
- validate every link and command against the final tree;
- replace old pages only after their individual accepted hashes exist;
- remove the temporary `newdoc/` staging boundary and local worklog only after
  the cutover is reviewed.

**Complete when:** no live user/operator/maintainer feature is unowned, no old
exploration claim survives without current proof, and the final documentation
set passes link, command and diff checks.

### Produce one reproducible release evidence bundle

The current short compatibility smoke is useful but not release-wide proof.
Create a retained bundle containing:

- the default 24-cell game/base/latest × six-engine matrix at normal duration;
- normal desktop/audio acceptance runs for SWE1 and RFM;
- clean default benchmarks with IRQ depth and IStack margin;
- QEMU/build/host identity, raw logs, JSON/Markdown reports and asset inventory;
- explicit statements for physical hardware and combinations not tested.

The 162-cell all-update matrix is a separate, broader claim and should not be
silently substituted with a short run.

**Complete when:** another maintainer can reproduce the commands, relate every
summary row to raw evidence and state exactly what the bundle proves. Use the
artifact contract in [Testing and validation](26-testing-validation-matrix.md).

## Priority 1 — protect user data and repeatability

### Make savedata concurrency fail safely

Current profiles have no cross-process lock and writers share predictable
temporary names. Harden the complete three-file profile as one operational
unit:

- take a per-game/profile lock before QEMU mutates state;
- reject a second writer with a clear path/PID diagnostic;
- use collision-free temporary files;
- strictly validate BAR2 and BAR3 seed lengths, matching the SEEPROM policy;
- define file/directory synchronization expectations for power-loss durability;
- add interrupted-write, stale-lock and concurrent-launch tests.

Do not attempt to merge two cabinets' audits, scores or flash writes.

**Complete when:** simultaneous writers cannot start, malformed seeds fail or
recover deterministically, and fault-injection tests preserve the last accepted
profile. Preserve the user-facing guarantees in
[Persistent cabinet state](09-savedata.md).

### Add an asset manifest/audit path

First-run acquisition intentionally preserves an existing tree, even when it
is partial. Add a non-destructive verifier that can report:

- required base chips and exact sizes per game;
- update component presence, bundle identity and assembled-size validity;
- original DCS ROM/sound-flash compatibility;
- duplicate version identities and selected `latest` result;
- checksums when a maintained manifest exists.

Repair must remain explicit: never overwrite local preservation work merely
because a checksum differs.

**Complete when:** preflight can distinguish absent, complete, partial and
locally modified assets before a guest launch, with a documented opt-in
reacquisition procedure. Keep selection semantics aligned with
[ROMs and updates](15-rom-loading.md).

### Keep both admitted QEMU versions buildable

QEMU 10.0.8 is the release pin and 10.2.4 is a known-good source alternative.
CI currently builds only 10.0.8. Add a scheduled or matrix job that at least:

- applies every patch family with zero fuzz;
- builds the complete graft on both admitted versions;
- verifies machine/device registration;
- runs one ROM-backed smoke per version;
- refuses to retain a version in `KNOWN_GOOD_VERS` after regression.

Do not move the release pin merely because a newer version compiles.

**Complete when:** both versions are continuously checked and promotion of a
new release requires recorded build, boot, normal-run and benchmark evidence.

## Priority 1 — resolve timing evidence, not just averages

The strict natural-delivery design is the baseline. Continue investigating the
repeated PDB tail observed on the reference host without reintroducing backlog
acceleration or synthetic interrupts.

A useful investigation should correlate a long gap with:

- host wall/thread/process CPU time;
- raise, acknowledgement, handler entry, EOI and IRET segments;
- active TBs and PIC/CPU state;
- display/audio/LPT work around the event;
- IRQ nesting depth and XINU IStack margin;
- a clean low-observer-cost reproduction afterward.

Potential host-side improvements must predict normal scheduling delay and
remain bounded; they must never make a late host demand an ever-faster guest.
One smaller maximum is not enough—compare distributions, speed, depth, stack
margin, DCS health and CPU cost across repeated matched runs.

**Complete when:** either the repeated tail has a reproduced owner and a fix
that wins those matched criteria, or the residual host limit is quantified and
the benchmark threshold is justified from cabinet evidence. The current
measurement model is in [CPU, PIT and IRQ0 timing](12-cpu-and-timers.md).

## Priority 1 — physical cabinet qualification

Run the staged ppdev procedure on actual Pinball 2000 hardware with a person
able to observe the mechanism safely. Record:

- an emulated-board control/timing baseline for the intended game and update;
- proof that `required` refuses an absent or unrecognized board instead of
  hiding it behind emulation;
- host controller, cable, kernel, board and playfield identity;
- signature/game auto-detection and explicit-port logs;
- a short first real-port trace while playfield power remains disabled, kept
  separate from later clean timing measurements because tracing adds overhead;
- switch polarity and one-at-a-time input behavior;
- keepalive plus lamp/coil output behavior under the real service procedure;
- timing benchmark before and after physical attachment;
- long play, clean shutdown and port release/reclaim;
- failure behavior for disconnected, unpowered and wrong-game cases.

Do not begin by powering unknown outputs or by hiding a silent board behind
`auto` fallback.

**Complete when:** a retained signed-off cabinet report maps every tested claim
to logs/observations and the installation guide can distinguish certified from
still-unverified hardware behavior. Use the safety sequence in
[Real LPT passthrough](46-real-lpt-passthrough.md).

## Priority 2 — subsystem fidelity

### Expand sound validation before changing the default engine

The default live hybrid engine has strong automated progress/health evidence,
but listening coverage remains narrower than protocol coverage. Build a cue
set spanning boot, attract, gameplay, service tests, looping, volume and pan
for both games and representative updates. Pair listening notes with bounded
PCM captures.

For the sample engines, decide whether stereo pan is worth implementing or
whether their mono behavior should remain an explicit compatibility boundary.
Measure first-generation `pb2kslib-adsp` time and disk use on release hardware.

**Complete when:** the named cue set is repeatable, every miss is assigned to
transport/content/backend, and any engine-default change beats the current
hybrid path in timing, health and audible coverage. Keep engine semantics in
[DCS sound](25-dcs-sound.md).

### Validate optional networking as a separate product surface

Networking is not required for local play. Before recommending it broadly:

- exercise isolated, NAT, automatic, passt, mirror and existing-bridge paths;
- test guest-address discovery and forwarding retarget after reset/change;
- verify local versus all-interface binding and duplicate-port failures;
- document which preserved updates have native network support versus only a
  compatible volatile extension;
- add security guidance/tests for exposed historical HTTP/Telnet services;
- continuously boot-test the SMC8416 graft on admitted QEMU versions.

**Complete when:** each transport has a scoped topology test and no forwarding
mode is described more broadly than its guest/update/security evidence.

### Keep capture honest

If recordings are intended as user-facing evidence, consider an optional
audio-mux path with explicit synchronization and finalization tests. Do not
quietly change the existing video-only 60 fps contract.

**Complete when:** either video-only remains the documented deliberate scope,
or a new audiovisual mode proves rate/channel selection, A/V drift bounds,
clean exit and forced-exit behavior across both live and sample engines.

## Priority 3 — reduce compatibility surface

Several switches exist only for continuity or investigation:

- `--strict` is a no-op alias for the sole natural timing path;
- `bar4-patch` and `io-handled` currently name the same natural DCS transport;
- fast/async QEMU framebuffers, raw DCS A/B knobs and CPU pinning are
  experiments;
- the legacy CMOS fallback and signature-based 4→14 MiB memory override remain
  opt-in;
- five non-default DCS engines primarily support comparisons.

Inventory actual use, keep the diagnostics needed to disprove regressions and
remove or quarantine aliases/experiments that no longer answer a live
question. Avoid replacing one confusing mode list with another hidden list of
environment variables.

**Complete when:** the public CLI describes one normal choice per concern,
experiments have an owner/expiry criterion, and removing an alias does not
destroy unique forensic capability.

## Conditional work — full chip models

Replacing the fixed PCI table, minimal SuperIO/ISA surfaces or game-focused GX
model with general chip emulation is worthwhile only when a concrete guest,
hardware or maintenance requirement needs it. A full PCI host plus real
`PCIDevice` conversions would touch every BAR consumer and offers no automatic
benefit to already validated software.

Start such work only with a failing access trace or a required feature such as
BAR relocation, IRQ routing, hotplug or a newly supported diagnostic. Preserve
the current game path behind matched boot, matrix and benchmark evidence.

## Branch and experiment hygiene

The recent branch inventory should not be treated as eight independent TODOs:

- IRQ/IStack observation, live capture, asset bootstrap, automatic networking
  and hybrid LPT concepts are already represented by newer mainline code;
- hotloop/adaptive and Unicorn timing branches record rejected or superseded
  execution models and must not be merged as feature work;
- the host-menu branch remains a distinct UX experiment, not an accepted
  requirement;
- old branches without a merge base belong in an archive namespace or tag,
  not beside active delivery branches.

Before deleting a branch, compare its unique diff against current owners and
retain any reusable test or diagnostic—not the obsolete delivery mechanism.

**Complete when:** every surviving branch is labelled active, evidence/archive
or obsolete; active branches name an owner, claim and acceptance test.

## Explicit non-goals

Unless new evidence changes the product boundary, the roadmap does not aim to:

- emulate arbitrary PC software on the custom machine;
- support stock QEMU without the Encore machine and required MediaGX patches;
- expose XINA's historical services to the public Internet;
- manufacture missed guest ticks to hide an overloaded host;
- infer cabinet electrical safety from an emulator or open-bus fixture;
- replace working focused device models only for architectural elegance;
- promote every forensic knob into a supported user mode.

---

← [Known limitations](35-known-limitations.md) ·
[Documentation index](README.md) · [Optional network card](48-network.md) →
