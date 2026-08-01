# Unicorn ↔ QEMU Parity Checklist

One page. Living document — edit it in the same commit as the work it
tracks. Priority order when picking what to work on next: **stability
> performance > functional parity > CLI/arg parity > test-tooling
parity.** Do not spend a session polishing docs while a stability or
performance gap sits open — verify/fix code first, document what you
verified.

Every claim below has been checked against the actual code/docs on
each branch as of this writing (not assumed from memory) — cite file
and line when you update a row.

## How to use this file

- Before starting work, scan **Do**/**Don't**, then the gaps table,
  ordered by priority.
- When closing a gap, move it to "Closed" with the commit/PR and the
  test that proves it, in the same change.
- Never let this file describe a state the code doesn't currently
  have — this branch was written while still discovering the
  hardware, so treat every existing doc claim as unverified until you
  check it against `src/` yourself.

## Do

- **Verify, don't trust existing docs.** Several unicorn-branch docs
  described features as "unprototyped hypothesis" that were already
  shipped in code (`--realtime`/`--cpu-target-mhz`/`--cpu-stats` vs.
  `docs/50-cpu-clock-mismatch.md`, fixed 2026-08). Assume other docs
  have the same drift until you've read the owning `src/*.c`.
- **Run the automated regression matrix before and after every
  change**: `tools/run-bundle-matrix.py --all-updates`. Verified
  2026-08 on this host: all 6 SWE1 bundles + all 7 RFM bundles + both
  base-ROM configs pass (video + DCS audio activity, clean exit,
  20-30s headless, `io-handled` mode). Use this as your stability
  regression gate, not a one-off manual boot.
- **Prefer the coarse throttle that already exists** (`--realtime` +
  `--cpu-target-mhz`) for any cabinet-timing-sensitive work instead of
  adding a new per-symptom pace knob next to `--lpt-bus-pace`.
- **Resolve guest addresses through `sym_lookup()`**; a few hardcoded
  addresses remain in `src/cpu.c`/`src/io.c` — don't add a new one.
- **Keep the honesty discipline this branch already has**: every
  known-limitations/future-work entry should read like
  `docs/38-known-limitations.md`'s DCS/LPT sections — concrete
  commands, concrete log lines, no unverified claims.

## Don't

- **Don't claim ADSP-equivalent audio fidelity.** Unicorn plays
  pre-extracted WAV samples for DCS commands (`src/sound.c`,
  `docs/12-sound-pipeline.md`: "Encore does not emulate the ADSP").
  `main` runs the real ADSP-2105 DSP firmware natively
  (`--dcs-engine adsp*`). This is architectural, not a quick fix —
  don't describe unicorn's audio as "the same" as main's in any doc.
- **Don't treat `timeout`'s exit code 124 as a hang.** `timeout N cmd`
  returns 124 whenever it had to signal the command, even if the
  child then shuts down cleanly on SIGTERM (verified: RFM v2.60 exits
  124 but its log shows a normal `[save]`/`[exit]` sequence). Check
  the log's own exit marker, not just the wrapper's exit code.
- **Don't treat `*** NonFatal` UART lines as failures.** They are
  common, often-benign guest chatter by design (the name says
  non-fatal) — `tools/run-bundle-matrix.py` reports them as warnings,
  not failures. Even `*** Fatal: interval_0_25ms: exec is hung` has
  been observed on a bundle that still completed cleanly with normal
  video/audio activity — investigate before assuming a hard failure.
- **Don't add a second timing knob per symptom** — see Do, above.
- **Don't build a full ADSP emulator or YAML keymap loader speculatively
  without a concrete driving bug** — these are real gaps (below) but
  large scope; confirm priority before starting.

## Gaps vs `main`, in priority order

### 1. Stability — verified, no open gap found this session

Full regression matrix (13 update bundles + 2 base-ROM configs,
`io-handled` mode, 20-30s headless each) passes with no crashes on
this host (`tools/run-bundle-matrix.py --all-updates`, run 2026-08).
Re-run after every non-trivial change; record any new failure here
with the exact command and log excerpt.

### 2. Performance / timing — partially closed

| | `main` (QEMU) | `unicorn` |
|---|---|---|
| Guest-speed throttle | PIT-accurate virtual time, `--speed-target 25..300` | `--realtime` (opt-in, per-vblank nanosleep) + `--cpu-target-mhz` (PIT/IRQ0 math only, does not itself throttle) — coarser granularity, verified working |
| Measurement | `--bench`, `--timing-snapshots` | `--cpu-stats[=N]` — verified working |
| Per-boundary band-aid | n/a | `--lpt-bus-pace` for the LPT wire specifically |

Open: no icount-style per-block throttle (main's PIT model is more
accurate); `--realtime`'s per-vblank granularity is unverified against
a real cabinet — see `docs/50-cpu-clock-mismatch.md` step 3.

### 3. Functional parity — largest real gaps

| Feature | `main` | `unicorn` | Impact |
|---|---|---|---|
| DCS audio | Native ADSP-2105 emulation (`--dcs-engine adsp*`, `pb2kslib` fallback) | Pre-extracted WAV sample playback only (`src/sound.c`) | Architectural; audio behavior/timing can diverge from real DSP under edge cases |
| Switch mapping | `--switch-keymap FILE.yaml`, user-editable | Hardcoded in-code mapping, no config file | Cabinet integrators can't remap without a rebuild |
| Scripted automation | `--script`/`--console-script`: timed input, assertions, screenshots | None | No scripted acceptance tests beyond boot/progress |
| Video capture | `--record-video` (H.264/FFmpeg) | None | Can't produce an artifact for visual regression review |
| E0-prefixed PS/2 scancodes | n/a (own KBC path) | Known incomplete (`docs/38-known-limitations.md`) | RCtrl/RAlt arrive as LCtrl/LAlt in `--keyboard-tcp` mode |

### 4. CLI/arg parity — meaningful subset (excludes QEMU-only flags like `--qemu`, `--monitor`, `--tcg-only`)

Missing on unicorn, present on main: `--dcs-engine`, `--switch-keymap`,
`--script`/`--console-script`, `--record-video`, `--speed-target`,
`--audio`/`--no-audio` (separate from `--headless`), `--diag`,
`--legacy-hotloop`/`--with-pit` (alternate timing models for A/B).
Present on unicorn only: `--realtime`, `--cpu-target-mhz`,
`--cpu-stats`, `--cabinet-purist`/`--lpt-purist`, `--lpt-bus-pace`,
`--lpt-managed-dir`, `--config` (YAML-ish config file — see
`docs/04-config-yaml.md`).

### 5. Test tooling — closed this session

`tools/run-bundle-matrix.py` automates the bundle boot/progress check
(`docs/26-testing-bundle-matrix.md`). No unit tests exist yet for
pure-parsing code (config loader) — `main` has
`scripts/tests/test_console_script.py` as the model to follow.

---
Keep this file to one page. If it grows past that, split detail into
`docs/` and leave only pointers + the priority list here.
