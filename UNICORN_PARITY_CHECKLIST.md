# Unicorn ↔ QEMU Parity Checklist

One page. Living document — edit it in the same commit as the work it
tracks. Goal: bring the `unicorn` branch (clean-room Unicorn Engine
emulator, `src/`) to the same level of performance, functionality, test
rigor and honesty as the `main` branch (QEMU machine, `qemu/`), without
becoming QEMU or losing Unicorn's advantages (single binary, no vendored
CPU core to patch, fast build).

Update this file whenever: a gap below is closed, a new gap is found, or
a "don't" is violated and causes a regression. Keep it to one page —
link out to `docs/` for detail, don't inline detail here.

## How to use this file

- Before starting work, scan **Do** for the relevant area and **Don't**
  for known traps.
- When closing a gap, move it from **Gaps vs main** to done (strike
  through or delete) in the same PR, with a doc/test reference.
- Never let this file describe a state the code doesn't currently have.

## Do

- **Match `main`'s honesty standard.** Every claim of "works" must cite
  an automated test result, not a one-time manual boot. See `main`
  `docs/26-testing-validation-matrix.md` for the bar.
- **Throttle the guest to real hardware speed** before trusting any
  timing-sensitive result (LPT, DCS handshake, watchdog). Unicorn JIT
  with no rate limit is not representative of the ~233 MHz MediaGX.
  Track in `docs/50-cpu-clock-mismatch.md`.
- **Give every state exactly one owner** (DCS protocol, LPT switch
  matrix, savedata) — same rule `main` enforces in
  `docs/05-development-guidelines.md`. Port that document's 10 core
  rules to this branch's own guidelines doc once `src/` stabilizes.
- **Resolve guest addresses through `sym_lookup()`**, never hardcode.
  A few remain in `src/cpu.c` / `src/io.c` — finish that migration
  before adding new patch points.
- **Build an ELF-based guest probe/benchmark harness** equivalent to
  `main`'s `scripts/bench-qemu.py` + `scripts/guest-irq-probe.S` +
  `scripts/guest-load.S`: a small `.S` → `.o` → `.elf` → raw-binary
  pipeline injected into guest RAM to measure IRQ timing, guest IPS and
  scheduler behavior in isolation, independent of full-game boot.
- **Automate the bundle regression matrix.** Turn
  `docs/26-testing-bundle-matrix.md`'s manual 11-bundle × 2-mode
  procedure into a script (`--headless`, grep log for `dcs_wr`/`FPS:`,
  exit 0/1) mirroring `main`'s
  `docs/measurements/validation-matrix/run-matrix.py`. Wire it into CI.
- **Add unit tests for pure-parsing code** (config YAML loader, keymap,
  update-bundle detection) with no emulator boot required — mirror
  `main`'s `scripts/tests/test_console_script.py` style: fast,
  deterministic, no I/O side effects.
- **Persist savedata atomically on exit** (NVRAM/flash/SEEPROM), same
  guarantee `main` documents in `docs/09-savedata.md` — verify
  `src/bar.c` matches on crash/kill paths, not just clean exit.
- **Keep the single-binary/no-vendored-core advantage.** Prefer
  Unicorn context save/restore (`uc_context_save`/`restore`) over
  reinventing snapshotting; don't reach for a second CPU engine.
- **Document every guest-behavior claim with the exact command,**
  game, update, DCS mode and log excerpt — same discipline as `main`'s
  known-limitations page.

## Don't

- **Don't claim cabinet-verified behavior.** Both branches are
  emulator-only validated; LPT/PDB, switch polarity and coil timing are
  unverified on real hardware on both sides. Don't let one branch's
  docs imply more confidence than the other's.
- **Don't hardcode a host action to simulate another** (e.g. faking a
  digit+Ctrl combo to synthesize a mapped key). This is the exact bug
  class `main` explicitly forbids in its input-handling rules.
- **Don't add a second timing/rate-limit knob per symptom.** The
  current `--lpt-bus-pace` band-aid must not multiply into more
  per-feature pacing flags; fix guest-IPS throttling generically once
  (see Do, above) and retire the band-aids.
- **Don't ship a manual-only test result as "passing."** A human
  running one boot once and eyeballing the screen is not a regression
  test; it must be scriptable and re-runnable headless.
- **Don't skip the `sym_lookup()` migration** when touching `src/cpu.c`
  or `src/io.c` — don't add a fifth hardcoded address next to the four
  that still need cleanup.
- **Don't treat reference-only (below-chip-ROM-baseline) update
  bundles as bugs.** SWE1 v1.3/v1.4 and RFM v1.2/v1.4/v1.5 are
  historical-only per `docs/26-testing-bundle-matrix.md`; failures
  there are not regressions.
- **Don't merge `--keyboard-tcp` / E0-scancode / net-bridge feature
  work without updating `docs/38-known-limitations.md` in the same
  change** — these are explicitly flagged as experimental/incomplete;
  don't let the docs drift stale the moment code changes.
- **Don't add global filters ahead of the established input-owner
  path** (PS/2, LPT) — extend the existing dispatch chain instead of
  layering a second handler that must special-case unhandled events.

## Gaps vs `main` (update as closed)

| Area | `main` (QEMU) has it | `unicorn` status | Doc |
|---|---|---|---|
| Guest-speed throttling | PIT-accurate virtual time | unthrottled JIT | `docs/50-cpu-clock-mismatch.md` |
| ELF probe/benchmark harness | `scripts/bench-qemu.py` + `.S`→`.elf` probes | none | — |
| Automated regression matrix | `run-matrix.py`, scripted pass/fail | manual procedure only | `docs/26-testing-bundle-matrix.md` |
| Unit tests (parser/config) | `scripts/tests/*.py` | none found | — |
| `make install` / packaging | n/a (QEMU build) | absent | `docs/28-build-system.md`, `docs/39-future-work.md` |
| Symbol-based patch addresses | fully migrated | a few hardcoded left in `src/cpu.c`/`src/io.c` | `docs/39-future-work.md` |
| Snapshot/restore | n/a | not implemented (Unicorn context API available) | `docs/39-future-work.md` |
| Project license file | missing on `main` too | missing | `main` `docs/35-known-limitations.md` |

---
Keep this file to one page. If it grows past that, split detail into
`docs/` and leave only pointers here.
