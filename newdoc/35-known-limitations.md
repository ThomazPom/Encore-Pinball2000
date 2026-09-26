# 35 — Known limitations

Encore boots and runs both Pinball 2000 games, but it is not a cycle-perfect
copy of every device or a hard-real-time appliance on arbitrary hosts. This
page lists current constraints that can change a result, lose state, reduce
fidelity or invalidate a support claim.

> [!IMPORTANT]
> A limitation is not automatically a current crash. Keep three categories
> separate: an observed defect, a deliberate model boundary, and a path that
> simply has not received enough validation. The
> [compatibility page](30-compatibility-support.md) defines those evidence
> levels.

## At a glance

| Area | Current limitation | Safe response |
|---|---|---|
| timing | host scheduling can still create long PDB/IRQ tails; Encore is not hard real time | run the clean benchmark on the target host and retain depth/IStack evidence |
| validation | all 168 installed update/engine cells have not been rerun on the current tree | scope claims to the tested paths and keep artifacts |
| saved state | same-profile instances are not locked; forced exit can lose the last session | run one instance per profile and quit normally |
| assets | an existing partial ROM/update tree is neither audited nor repaired automatically | move it aside and reacquire, or name a verified complete tree |
| sound | sample engines are mono and fixed libraries may omit newer tracks | use the default live ADSP engine for normal play |
| capture | video capture records no audio and samples at a host 60 fps | capture audio separately and expect repeated frames |
| hardware | no current powered-cabinet/physical-LPT certification exists on this host | follow the staged real-board procedure on the target cabinet |
| platform | published binaries target Linux x86_64 only | treat another architecture/OS as a new port |
| device fidelity | PCI, SuperIO, MediaGX and PUB models implement game-used surfaces, not complete chips | do not infer hotplug, arbitrary firmware or general hardware compatibility |
| networking | XINA services are historical and optional forwarding can expose them | bind locally unless an isolated trusted network is intentional |

## Timing is host-sensitive

The game programs PIT channel 0 near 4,004 Hz. Encore uses QEMU's natural
i8254 → i8259 → CPU interrupt path plus a bounded post-IRET rendezvous near the
next projected PIT deadline. It does not inject missed ticks, accelerate a
backlog or adapt the guest clock to recover host delay.

That design avoids positive-feedback catch-up, but it cannot make an overloaded
Linux host deterministic. A late emulator thread can still produce a long
raise-to-entry or PDB05 interval. When the host cannot sustain the workload,
guest time may fall behind wall time instead of being artificially caught up.

Two unchanged normal benchmarks on this workstation on 2026-09-21 showed the
remaining tail clearly:

| Run | IRQ delivery | Max IRQ0 depth | Minimum IStack margin | PDB windows >2.5 ms | Worst PDB |
|---|---:|---:|---:|---:|---:|
| A | 99.97% | 7 | 6,948 B | 2/5 | 4.99 ms |
| B | 99.86% | 5 | 7,000 B | 3/5 | 4.10 ms |

Both were `ABNORMAL` only because the long PDB tail repeated in the measured
windows. Neither showed a small IStack margin or a live-DSP health failure.
Those numbers characterize that host, commit and observation window; they are
not universal failure rates.

> [!WARNING]
> Delivery percentage alone cannot establish stack safety. For a timing or
> rare-crash claim, keep maximum interrupt depth and minimum sampled IStack
> margin with the PDB distribution. `n/a` is missing evidence, not infinite
> safety.

Detailed tracing can itself worsen the tail. Use the normal two-pass `--bench`
for a headline result; enable the stall/PDB profilers or full `-v` sampler only
to answer a narrower question. See
[CPU, PIT and IRQ0 timing](12-cpu-and-timers.md).

## Current validation gaps

The installed assets provide two base paths and 26 extracted updates. With six
DCS engines, `--all-updates` enumerates 168 cells. The current documentation
refresh ran only short default-engine smoke cells for SWE1/RFM base and latest,
plus focused normal launches. The complete current 168-cell cross-product has
not been run.

CI is intentionally narrower still. It builds QEMU 10.0.8, checks machine
registration, repository units, guest-extension signatures and ROM-backed
switch routing. It does not run:

- the host-load-sensitive timing benchmark;
- the 24-cell normal release matrix or 168-cell all-update matrix;
- a human desktop/audio acceptance session;
- physical parallel hardware or a powered cabinet;
- every networking, PUB or experimental renderer combination.

Do not turn a green workflow, a structural ROM signature or a short boot into
a whole-game certification. The exact commands and artifact requirements are
in [Testing and validation](26-testing-validation-matrix.md).

## Persistent state is single-instance and clean-exit oriented

BAR2 NVRAM, BAR3 update flash and PLX SEEPROM are independent files. Their
writers create a sibling temporary file and rename it over the destination,
which protects the previous image from an ordinary partial write. There is no
cross-process profile lock.

Two emulator processes using the same game and savedata directory can race on
the same device files and temporary names. The result is unsupported: one
process can overwrite another's later state, a rename can fail, and no merge
of audits, settings, scores or update-flash writes is attempted.

Exit notifiers perform the final flush. A process kill, host crash or power
loss can discard guest changes that had not reached disk. Atomic rename does
not add a durability guarantee, and leftover temporary files are never chosen
as seeds automatically.

BAR2 and BAR3 seed readers also accept a short read instead of rejecting the
file as strictly as the 128-byte SEEPROM reader. BAR2 leaves the unread range
at its initial value; BAR3 leaves it erased. Treat a truncated `.nvram2` or
`.flash` as corrupt rather than as a supported partial image.

Use one process per profile, back up all three files together and quit with F1
or the window close action. Full semantics are in
[Persistent cabinet state](09-savedata.md).

## Asset discovery does not repair existing trees

First-run acquisition acts only when the selected `roms/` or `updates/`
directory is absent. If a directory exists but is partial, stale or locally
modified, the launcher preserves it and a later selection may fail on one
missing chip or component.

Update auto-selection chooses the lexicographically greatest four-digit
version before the machine validates all four BAR3 components. If that newest
bundle is malformed, it fails; discovery does not retry the next older bundle.
Name a known-good version/path or repair the tree instead of relying on an
implicit fallback.

Likewise, `--update none` disables discovery but does not discard persistent
BAR3. Pair it with `--no-savedata` when testing base ROMs. See
[ROMs, updates and software selection](15-rom-loading.md) and
[Troubleshooting](04-troubleshooting.md).

## Sound fidelity and coverage

The default `adsp-hybrid-thread` engine executes the selected original sound
flash and produces 31.25 kHz stereo. Its health record proves command drainage
and non-zero PCM, not subjective correctness of every cue.

The two sample-library engines have narrower fidelity:

- `pb2kslib` can omit sounds introduced by a newer update;
- `pb2kslib` and `pb2kslib-adsp` mix at 44.1 kHz mono;
- pan is parsed, stored and traced but causes no left/right movement in those
  mono engines;
- a missing/invalid original-format asset can make a requested live engine log
  a fallback to `pb2kslib`, so the engine-selection line is authoritative;
- a full synthetic 64-word response ring drops additional response words.

The first `pb2kslib-adsp` run for a new game/sound-source identity renders a
persistent PCM library before the real game starts (six isolated workers by
default). That startup can be lengthy and the uncompressed PCM cache normally
uses more disk space than the fixed compressed library. Its filename hashes
the exact U109/U110 plus sound-flash bytes, so replacing an input selects a new
cache automatically; manual clearing is not required for identity correctness.

If no compiled host-audio backend also matches an available host service,
automatic selection warns and continues silently. Guest DCS handshakes can
therefore succeed while nothing is audible. Diagnose transport, content engine
and host output separately with [DCS sound](25-dcs-sound.md).

## Display and capture fidelity

The guest framebuffer is interpreted as 640×240 RGB555 and presented as
640×480. The emulated display timing uses an approximate 17.5 ms frame divided
into 30 virtual subticks—about 57 Hz. This is enough for current software but
is not a scan-accurate MediaGX display controller.

`--record-video` is a presentation capture, not an audiovisual cabinet dump:

- a host-monotonic worker samples at 60 fps independently of guest VSYNC, so
  frames may repeat;
- FFmpeg receives video only (`-an`); game audio is not muxed;
- nearest-neighbor scaling produces 640×480 from the native 640×240 image;
- the host status overlay and compositor/window scaling are excluded;
- forced termination can prevent the container from being finalized.

The normal direct SDL renderer and explicit standard QEMU console are the
maintained presentation paths. Fast/async QEMU framebuffer variants and the
RGB565 internal experiment are A/B tools, not equivalent supported renderers.
See [MediaGX and display](23-mediagx-and-display.md).

## Physical driver-board boundary

The emulated LPT board is tested in software. Linux ppdev open/claim and
open-bus behavior have fixture coverage, but this refresh host has no physical
parallel port or powered Pinball 2000 driver board. It cannot establish cable
pinout, voltage safety, direction support, output polarity, playfield identity
or mechanism behavior.

Physical-only mode intentionally disconnects keyboard cabinet contacts.
Experimental hybrid input can OR a keyboard closure with a physically open
contact, but it cannot reopen a switch that the real board reports closed.
F12 reports the software model's state; it does not poll and certify physical
rows.

Do not use `auto` fallback as proof that hardware worked. Commission an
explicit `/dev/parportN`, verify the game's own diagnostics and only then
install `required`. Follow [Real LPT passthrough](46-real-lpt-passthrough.md).

## Optional network and guest extensions

The SMC8416-compatible card supplies the EtherEZ front-end behavior the guest
uses around QEMU's DP8390 engine; it is not a complete SMC chip model. The PUB
preservation model is read-only. Both decode guest memory at `0xD0000`, so the
launcher correctly refuses to enable them together.

The oldest preserved SWE1 1.30 and RFM 1.20 images predate the network shell
signatures used by Encore's volatile guest extension. They boot without that
extension, but `--guest-extensions`/`--setip` cannot add the extension interface
to those images.

XINA's HTTP and Telnet services come from an old embedded stack without modern
security expectations. `--forward` binds on every host interface and can make
them reachable from the surrounding network; prefer `--forward-local` unless
exposure on a trusted isolated network is deliberate. Networking is optional
and does not belong on the critical path for local play. See
[Optional network card](48-network.md).

## Deliberately partial machine models

Several devices implement the behavior exercised by Pinball 2000 rather than
every feature in their data sheets:

- PCI configuration is a fixed `0xcf8/0xcfc` discovery table; there is no
  general QEMU PCI bus, BAR relocation, MSI or hotplug;
- the MediaGX GP engine handles the observed blits and register surfaces, not
  every accelerator or display-controller operation;
- ISA, SuperIO, Cyrix control-register and PLX models cover the ports/registers
  needed by the games;
- the PUB card supports the read path used for preservation dumps, not card
  programming;
- optional memory-size mutation is a signature-bounded, off-by-default
  experiment rather than native hardware behavior.

These boundaries matter to new firmware, arbitrary diagnostics and hardware
research. They are not evidence that a currently validated game path failed.
The ownership map is in [Architecture](10-architecture.md) and the decoded
surfaces are in [Memory map](13-memory-map.md).

## Host and distribution limits

Published packages contain a dynamically linked Linux x86_64 binary. Release
builds run on Debian 13 x86_64 and CI builds on Ubuntu 24.04 x86_64. Another
current Linux x86_64 distribution may build from source when dependencies are
available, but another CPU architecture or non-Linux host is unverified.

Cabinet installation further assumes Linux systemd/logind plus one supported
Wayland or SDL2/KMSDRM session path. It does not install an Xorg/XWayland
cabinet session. Physical LPT requires Linux ppdev. Review
[Cabinet installation](01-cabinet-installation.md) before treating a desktop
source build as a cabinet platform.

## Repository licensing is incomplete

The repository currently has no project-level `LICENSE`, `COPYING` or
equivalent grant covering Encore as a whole. Individual imported components
retain their own notices—for example, the ADSP core identifies BSD-3-Clause
upstream authorship—but those notices do not establish a license for every
project file.

This leaves redistribution and contribution terms undefined at project level;
it is not an emulator runtime fault. Do not infer permission merely because
the source is visible. Establish the project's licensing policy and verify
provenance before presenting a formal source release or accepting third-party
contributions.

## Reporting a new limitation

Preserve the first failing run before trying a workaround. Record:

1. commit and dirty state;
2. exact game, complete update identity and engine;
3. QEMU build and host/kernel/display/audio identity;
4. normal versus headless/reduced path;
5. raw logs and generated report/JSON artifacts;
6. IRQ depth and IStack margin for timing/stack symptoms;
7. what remained untested, especially physical hardware.

If XINA is still in a fatal monitor, capture the live process before closing
it with `tools/capture-live-crash.sh`. General recovery steps are in
[Troubleshooting](04-troubleshooting.md); the evidence contract is in
[Testing and validation](26-testing-validation-matrix.md).

---

← [Compatibility and support](30-compatibility-support.md) ·
[Documentation index](README.md) · [Roadmap](36-roadmap.md) →
