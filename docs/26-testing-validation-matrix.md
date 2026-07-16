# 26 — Testing: Encore Validation Matrix

Encore's supported emulator matrix is the Cartesian product of two games, two
boot sources, and three DCS content engines:

| Axis | Values |
|---|---|
| Game | SWE1, RFM |
| Boot source | `--update none` (base/museum), `--update latest` |
| DCS engine | `pb2kslib`, `adsp`, `adsp-thread` |

This produces twelve test rows. The base path is deliberately retained because it
exercises the gated probe-cell compatibility path that normal update boots do not use.

For a forensic sweep rather than the supported base/latest gate, pass
`--all-updates`. That discovers every extracted `updates/pin2000_<gid>_*`
bundle and runs all three engines against each one. Community bundles remain
local/ignored; the report records versions and results, never their payloads.

## Pass criteria

Each row uses the same workload inherited from the earlier emulator validation:

1. Boot without savedata and reach a Williams game banner.
2. Exercise the MediaGX graphics engine (non-zero GP BLT activity). A headless
   backend intentionally has no frontend refresh listener, so `p2k-fps` is not a
   valid automated criterion; visible pixels remain a manual/windowed check.
3. Initialize the selected DCS engine and continue its renderer/DSP path.
4. At 11 seconds, open the coin door, pulse three credits, then alternate twenty
   volume-up/down presses.
5. Reach post-warmup timing snapshots without a fatal, stack smash, DCS initialization
   failure, early exit, or wedge.

This proves the automated graphics, input, sound-command and execution paths. It cannot
prove subjective sound quality, visible pixel correctness, or physical-cabinet behavior;
those remain manual tests.

## Reproduce

From any directory in this checkout:

```sh
docs/measurements/validation-matrix/run-matrix.py

# Base plus every locally installed update (all three DCS engines):
docs/measurements/validation-matrix/run-matrix.py --all-updates
```

The default is 60 seconds per row, sequentially, so audio engines do not compete for
host CPU. Raw logs, the per-cell DCS timing reports, and a combined `report.md` are
written under `/tmp/p2k-validation-matrix-<timestamp>/`. Short smoke runs may override
`--duration`, but published results must retain the 30-second warmup and run at least
60 seconds.

## All locally installed updates

The latest forensic sweep used `--all-updates`, 40 seconds per engine,
cabinet input at 11 seconds, and steady-state timing windows after 20 seconds.
It covered 15 game/update paths and all three DCS engines: **45/45 passed**.

| Game path | `pb2kslib` | `adsp` | `adsp-thread` |
|---|---|---|---|
| SWE1 base | PASS | PASS | PASS |
| SWE1 1.30 | PASS | PASS | PASS |
| SWE1 1.40 | PASS | PASS | PASS |
| SWE1 1.50 | PASS | PASS | PASS |
| SWE1 2.00 | PASS | PASS | PASS |
| SWE1 2.01 | PASS | PASS | PASS |
| SWE1 2.10 | PASS | PASS | PASS |
| RFM base | PASS | PASS | PASS |
| RFM 1.20 | PASS | PASS | PASS |
| RFM 1.40 | PASS | PASS | PASS |
| RFM 1.50 | PASS | PASS | PASS |
| RFM 1.60 | PASS | PASS | PASS |
| RFM 1.80 | PASS | PASS | PASS |
| RFM 2.50 | PASS | PASS | PASS |
| RFM 2.60 | PASS | PASS | PASS |

Run date: 2026-07-16. Raw local artifacts:
`/tmp/p2k-validation-all-updates-20260716`. Community update payloads and raw
logs are not committed.

Passing means the game identity appeared, GP BLT activity progressed, the
selected sound engine progressed, timing snapshots were produced, and no Fatal
or DCS initialization failure was detected. It does not establish subjective
sound correctness or physical-cabinet behavior.

## Supported base/latest result

The detailed supported base/latest result below predates the all-update sweep
above and retains per-engine progress counters for reference.

<!-- MATRIX_RESULT_START -->

Run on 2026-07-16: 60 seconds per row, input at 11 seconds, timing windows
after 30 seconds. Raw artifacts: `/tmp/p2k-validation-matrix-20260716-current-main`.

| Game path | DCS engine | Game identity | GP BLTs | Host-decoded DCS events | Decoded frames / DSP cycles | Fatal | Result |
|---|---|---|---:|---:|---:|---|---|
| SWE1 base | pb2kslib | SWE1 | 20 | 52 | 790506 | no | PASS |
| SWE1 base | adsp | SWE1 | 20 | 0 | 657398520 | no | PASS |
| SWE1 base | adsp-thread | SWE1 | 20 | 0 | 657411740 | no | PASS |
| SWE1 latest (2.10) | pb2kslib | SWE1 | 20 | 51 | 790506 | no | PASS |
| SWE1 latest (2.10) | adsp | SWE1 | 20 | 0 | 643294380 | no | PASS |
| SWE1 latest (2.10) | adsp-thread | SWE1 | 20 | 0 | 643272940 | no | PASS |
| RFM base | pb2kslib | RFM | 20 | 30 | 213038 | no | PASS |
| RFM base | adsp | RFM | 20 | 0 | 586501540 | no | PASS |
| RFM base | adsp-thread | RFM | 20 | 0 | 586476800 | no | PASS |
| RFM latest (2.60) | pb2kslib | RFM | 20 | 49 | 213038 | no | PASS |
| RFM latest (2.60) | adsp | RFM | 20 | 0 | 597955040 | no | PASS |
| RFM latest (2.60) | adsp-thread | RFM | 20 | 0 | 597959100 | no | PASS |

**Result: 12/12 passed.** A zero in the host-decoded-event column is expected
for native ADSP engines: commands enter the emulated DSP mailbox rather than the
`pb2kslib` host decoder. Advancing DSP cycles provide the native-engine progress
proof. Base ADSP rows loaded `roms/swe1_28f800.rom` and `roms/rfm_28f800.rom`
respectively; neither fell back to `pb2kslib`.

<!-- MATRIX_RESULT_END -->

## Scope notes

- `latest` resolves from the update bundles present in the checkout and the raw log
  records the exact selected directory/version.
- Older, below-chip-baseline bundles are forensic artifacts, not part of this twelve-row
  supported matrix. They can be tested separately with `--update <version>`.
- All runs use throwaway savedata so one row cannot repair or contaminate another.
- Physical LPT passthrough requires its own cabinet matrix; headless emulated LPT cannot
  establish electrical timing or STATUS polarity.

← [Back to documentation index](README.md) · [Known limitations](35-known-limitations.md)
