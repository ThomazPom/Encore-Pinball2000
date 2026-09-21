# 26 — Testing and validation

Encore uses several validation layers because no single `PASS` answers every
question. A source build can succeed while the guest does not boot; a boot can
succeed with silent audio; a stable headless matrix can still miss a broken
desktop renderer or physical cabinet interface.

> [!IMPORTANT]
> Name the claim before choosing the test. `--bench` evaluates natural timing
> and live-DSP health on one game path. The support matrix checks boot,
> graphics activity and audio progress across game/update/engine combinations.
> Neither one validates a powered cabinet.

This page is the operator and maintainer entry point. Device-specific evidence
belongs with [CPU and timer timing](12-cpu-and-timers.md),
[DCS sound](25-dcs-sound.md), [the LPT board](26-lpt-board.md) and
[MediaGX display](23-mediagx-and-display.md).

### Implementation owners

| Concern | Primary source |
|---|---|
| normal two-pass self-diagnostic | `scripts/internal/bench-qemu.py` |
| public `--bench` dispatch and options | `scripts/run-qemu.sh` |
| cross-game/update/engine matrix | `docs/measurements/validation-matrix/run-matrix.py` |
| repeatable DCS-engine workload | `docs/measurements/dcs-engines/run-comparison.py` |
| console-script parser tests | `scripts/tests/test_console_script.py` |
| matrix-verdict parser tests | `scripts/tests/test_validation_matrix.py` |
| ROM-backed key/switch smoke | `scripts/tests/smoke-switch-keymap.py` |
| extension structural and ROM ABI checks | `guest-extensions/build.sh`, `guest-extensions/check-romset.py` |
| source/build integration | `scripts/build-qemu.sh`, `scripts/internal/qemu-patch-series.py` |
| CI and release gates | `.github/workflows/qemu-build.yml`, `.github/workflows/release.yml` |
| live failure preservation | `tools/capture-live-crash.sh` |
| Debian installation laboratory | `tools/debian-qemu/lab.sh` |

## Validation ladder

Use the lowest layer that can disprove the claim, then move upward for a
release or risky subsystem change.

| Layer | What it proves | What it does not prove |
|---|---|---|
| syntax and unit tests | parsers and pure decision logic behave as asserted | QEMU builds or a guest boots |
| build and registration | the graft compiles and advertises `pinball2000` | ROM compatibility or runtime progress |
| ROM-backed smoke | a real guest boots and selected input paths work | long-run timing, sound quality or every update |
| normal desktop run | the user-visible display/audio/control path works on this host | other engines, revisions or hosts |
| `--bench` | steady speed, IRQ delivery, PDB cadence, stack safety and live-DSP health | the complete support matrix or physical hardware |
| validation matrix | selected games, revisions and DCS engines reach observable progress | natural desktop presentation, subjective audio or cabinet I/O |
| physical procedure | the named real board/cabinet works under stated conditions | other boards, wiring or kernels |

An emulator change normally needs the focused test that owns it plus one real
normal launch. Timing, interrupt, audio-worker and LPT changes additionally
need `--bench`. Release-wide compatibility claims need the matrix.

## Fast local gates

Run the deterministic checks before spending time on a guest session:

```bash
bash -n scripts/run-qemu.sh scripts/build-qemu.sh tools/capture-live-crash.sh
python3 -m unittest discover -s scripts/tests -p 'test_*.py'
python3 -m py_compile \
  docs/measurements/dcs-engines/run-comparison.py \
  docs/measurements/validation-matrix/run-matrix.py
./guest-extensions/build.sh --check
python3 guest-extensions/check-romset.py
git diff --check
```

`check-romset.py` checks whether every installed update matches the structural
guest-extension signatures. A `SKIP` for a pre-network image is expected; an
`OK` does not mean that the whole game was played. On the 2026-09-21 tree it
reported 24 supported images, two pre-network skips and zero failures.

Build and confirm machine registration separately:

```bash
scripts/build-qemu.sh
"$HOME/.cache/p2k-qemu-build/qemu-10.0.8/build/qemu-system-i386" -M help \
  | grep pinball2000
```

The build script also runs the extension build check and applies every
version-selected upstream patch with zero fuzz. Source compatibility is not
end-to-end runtime compatibility; adding a QEMU version to the known-good list
still requires a boot test.

## Focused real-guest smoke

The keymap smoke boots SWE1 2.10 with a temporary, read-only runtime and uses
QMP key events to prove:

- simultaneous and overlapping custom switch holds;
- independence from numeric cabinet inputs and Ctrl routing;
- atomic rejection of an invalid keymap;
- no coin retrigger on repeated key-down;
- release of every held matrix bit;
- presence of the complete eight-row lamp/switch F12 dump.

Run it with:

```bash
python3 scripts/tests/smoke-switch-keymap.py
```

It exits non-zero on a missing log sequence. It is a functional smoke, not a
timing benchmark.

Console automation has separate parser-only unit tests. For end-to-end guest
behavior, use a script such as `scripts/demos/start-game.p2k`; syntax success
alone cannot prove that the guest accepted an input. See
[Console scripting](42-console-scripting.md).

## Normal user run

Before accepting a release, run the actual desktop and audio defaults rather
than only `--display none --audio wav`:

```bash
scripts/run-qemu.sh --game swe1 --update latest --no-savedata
```

Allow the game to reach attract mode, exercise cabinet input, start a game,
listen beyond the boot bong, toggle fullscreen, take an F3 screenshot and quit
normally with F1. Repeat RFM when shared ROM/update or machine construction
changed. `--no-savedata` keeps this acceptance run from changing the normal
profile; a separate isolated write test owns persistence.

> [!NOTE]
> A normal run is deliberately not reduced to counters. It catches host window,
> focus, scaling, audio-service and interaction failures that headless tools do
> not model.

## The normal two-pass benchmark

Run the self-diagnostic through the public launcher:

```bash
scripts/run-qemu.sh --bench --game swe1 --update latest --no-savedata
```

Do not add headless or no-audio options when validating the normal user path.
`--display none --no-audio` is useful only when the claim is explicitly about
a headless environment.

The benchmark creates a new `/tmp/p2k-bench-*` artifact and two isolated guest
processes:

1. **IRQ pass.** After the cabinet workload and a 10-second guest warmup, GDB
   identifies the active IDT vector 0x20, verifies the known `clkint` prologue,
   installs a temporary six-byte jump and a RAM-only interval ring, settles
   after attachment, then measures one guest `sleep 10`. The original bytes are
   restored before shutdown.
2. **LPT/PDB pass.** A fresh, unpatched guest receives the same workload and
   warmup. Lightweight three-second snapshots measure LPT DATA rate, PDB05
   intervals, IRQ nesting depth and sampled XINU IStack margin without the
   detailed 100 ms diagnostic sampler.

Both passes request a final health record for live ADSP engines. Health requires
an empty command queue, no runtime reset or dropped command, equal enqueued and
consumed counts, and non-zero PCM frames and samples.

The default warmup is 10 guest seconds. `--bench-long` changes it to 30 seconds;
the measured `sleep 10` window is unchanged. `--bench-guest-load` temporarily
installs a cooperative low-priority XINU worker for a separate loaded-guest
claim. Both RAM modifications vanish with the isolated process.

The report and `results.json` expose:

- requested and effective speed;
- guest IRQ rate, delivery and interval percentiles;
- LPT DATA and PDB05 rates;
- PDB p50/p95/p99 and window maxima;
- maximum `clkint` nesting depth and minimum sampled IStack margin;
- DCS health on both passes;
- boot, steady-state and final verdict data.

### Benchmark verdict

`ABNORMAL` means at least one of:

- IRQ delivery outside 95–105% of the selected target;
- effective guest speed outside ±5% of the target;
- mean steady PDB p99 above 1 ms;
- a PDB maximum above 2.5 ms in at least 10% of complete windows, with a
  minimum threshold of two windows.

One isolated >2.5 ms window produces `PASS WITH WARNINGS`. A >10 ms gap is also
reported as a warning, independently of the repeated-window rule. Exit status
is 0 for both PASS forms, 2 for `ABNORMAL`, and 1 when setup or collection
fails.

> [!WARNING]
> Cumulative IRQ delivery includes boot and is expected to recover slowly.
> Judge steady delivery from the measured probe/window fields. Likewise, a
> large single worst interval and a repeated-tail failure are different claims.

Two unchanged normal runs on this workstation on 2026-09-21 produced:

| Run | IRQ delivery | Max depth | Minimum IStack margin | PDB >2.5 ms | Worst PDB | DCS |
|---|---:|---:|---:|---:|---:|---|
| A | 99.97% | 7 | 6,948 B | 2/5 windows | 4.99 ms | PASS/PASS |
| B | 99.86% | 5 | 7,000 B | 3/5 windows | 4.10 ms | PASS/PASS |

Both verdicts were `ABNORMAL` because the PDB tail repeated. The numbers are
dated evidence for that host and commit, not universal thresholds or a DCS
failure.

## Cross-product validation matrix

The current default matrix contains four game paths and all six DCS engines:

| Game path | Revision selection |
|---|---|
| SWE1 base | `--update none --no-savedata` |
| SWE1 latest | latest installed SWE1 bundle |
| RFM base | `--update none --no-savedata` |
| RFM latest | latest installed RFM bundle |

Run it into a new directory:

```bash
python3 docs/measurements/validation-matrix/run-matrix.py \
  --output /tmp/p2k-validation-release
```

This is 24 cells × 60 seconds, so the nominal runtime is 24 minutes plus boot,
shutdown and first-cache-generation overhead. The runner refuses to overwrite
a non-empty output directory. Without `--output`, it creates a timestamped
`/tmp/p2k-validation-matrix-*` directory.

For a quick tool or single-engine regression slice:

```bash
python3 docs/measurements/validation-matrix/run-matrix.py \
  --engine adsp-hybrid-thread \
  --output /tmp/p2k-validation-hybrid
```

Repeat `--engine` to select more engines. `--duration` and `--warmup` may shorten
an investigative run, but such a run is not interchangeable with the default
release matrix.

`--all-updates` replaces latest-only coverage with base plus every locally
extracted bundle containing a game ROM. On the 2026-09-21 tree that is 28 game
paths × 6 engines = 168 runs, or 168 nominal minutes before overhead. Record
the installed asset set with the result. Artifact directories use the complete
bundle name, because version numbers alone are not unique: the current tree
contains two distinct SWE1 1.50 builds.

Each matrix subprocess uses no savedata, no display, the WAV audio backend,
the same F4/three-credit/twenty-volume workload beginning 11 seconds after
launch, and lightweight timing snapshots. Cells and engines run sequentially,
so they do not compete with each other for host CPU. A cell passes only if it
contains:

- the exact requested game identity and update selection;
- the exact engine marker, with no silent fallback;
- at least one MediaGX GP blit and one timing snapshot;
- decoded frames for a sample/cache engine, or DSP cycles for a live engine;
- for live engines, a healthy final queue/reset/drop/PCM record;
- no recognized fatal, initialization, stack-smash, assertion or crash marker.

The top-level artifact contains `metadata.json` with commit, dirty-state flag,
runner hashes, arguments, cells and engines, one directory per game path, raw
per-engine logs and Markdown reports. A report row is evidence only together
with that metadata and its log.

An 18-second runner smoke on 2026-09-21 exercised the default hybrid engine
across all four default paths. It passed 4/4 with 20 GP blits per cell, exact
identity/update/engine selection, non-zero DSP cycles and healthy PCM. Its
short duration proves the revised collector path, not full-duration support.

The matrix does **not** compare rendered pixels, listen to sound, preserve a
user profile, judge natural desktop latency or touch a physical cabinet.

## DCS comparison harness

The lower-level comparison tool runs one identical cabinet workload across
selected engines and reports IRQ delivery, detailed or lightweight jitter,
PDB windows and live-DSP health:

```bash
python3 docs/measurements/dcs-engines/run-comparison.py \
  --game swe1 --update 0210 \
  --output /tmp/p2k-dcs-comparison
```

Its default is six engines × 90 seconds: nine nominal minutes plus overhead.
Engines run sequentially. The tool requires the custom QEMU build plus the
requested update/sound assets and otherwise uses only Python's standard
library.

The default detailed diagnostic sampler is useful for investigation but adds
logging and ring-sorting work; do not promote its latency tails to the normal
benchmark contract. `--lightweight` uses the same low-cost snapshots as the
matrix. `--engine` is repeatable, and `--parse-only DIR` rebuilds a report from
preserved compatible logs. Comparison tables use complete timing `snap`
windows at or after the selected warmup and exclude the partial exit window
from current-delivery aggregation.

This tool compares schedulers; it does not itself certify the full game/update
matrix. For natural timing, prefer public `--bench`.

## CI and release boundaries

Pull-request/manual QEMU CI is configured to run syntax and unit tests, build
the pinned QEMU, verify machine registration and execute the ROM-backed switch
smoke. The release workflow repeats the tooling gates and smoke before
packaging, then checks the stripped binary and archive shape.

CI deliberately does not run:

- the host-load-sensitive timing benchmark;
- the 24-cell or all-update matrices;
- a normal interactive desktop/audio acceptance session;
- physical ppdev or powered-cabinet procedures;
- the Debian VM laboratory's full install/display/acquisition matrix.

Therefore a green workflow is a build-and-smoke gate, not the complete release
claim.

## Preserve failures before retrying

Keep the first failing artifact directory unchanged. Record the commit, exact
command, host/kernel/QEMU build, selected assets and whether the run used
normal or reduced display/audio. Do not rerun into the same output path.

If XINA has entered a fatal monitor while QEMU remains alive, capture it before
closing the process:

```bash
tools/capture-live-crash.sh --disassemble 0x227f3a:0x80
```

The tool briefly pauses and detaches, dumps guest RAM, host mappings,
backtraces, command line, metadata and SHA-256 sums, and does not deliberately
write guest memory or terminate QEMU. See
[Live crash capture](51-live-crash-capture.md) for evidence handling.

Physical LPT tests require `/dev/parportN`, suitable permissions and the actual
board. The disconnected-vport fixture tests only the kernel ppdev/open-bus
path. Emulator-only results must remain labelled as such; follow
[Real LPT passthrough](46-real-lpt-passthrough.md) for cabinet work.

## Acceptance record

For a result intended to outlive the terminal, retain:

1. repository commit and dirty-state note;
2. exact command and environment-affecting options;
3. game/update asset identity;
4. host, kernel and QEMU build identity;
5. raw logs plus generated JSON/Markdown reports;
6. normal-run observations that counters cannot express;
7. an explicit statement of what was not tested, especially physical hardware.

That boundary keeps a useful result from becoming a broader claim as it is
copied into release notes or compatibility documentation.

---

← [LPT driver board](26-lpt-board.md) ·
[Documentation index](README.md) · [Compatibility and support](30-compatibility-support.md)
