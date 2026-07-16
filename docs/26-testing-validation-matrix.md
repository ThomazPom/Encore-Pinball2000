# 26 — Validation matrix

The automated matrix checks every locally installed game/update combination
with all three sound engines. It boots without savedata, exercises graphics,
door, credit and volume input, verifies sound-engine progress, and rejects
fatal errors, initialization failures, early exits and wedges.

## Run it

```sh
# Supported base and latest-update rows:
docs/measurements/validation-matrix/run-matrix.py

# Every locally installed update:
docs/measurements/validation-matrix/run-matrix.py --all-updates
```

The default duration is 60 seconds per row and includes a 30-second warmup.
Results and raw logs are written below `/tmp/p2k-validation-matrix-*`. Use a
shorter duration only for smoke testing.

## Current result

The complete sweep on 2026-07-16 passed 45 of 45 rows:

| Game | Software versions | Engines per version |
|---|---|---|
| SWE1 | base, 1.30, 1.40, 1.50, 2.00, 2.01, 2.10 | `pb2kslib`, `adsp`, `adsp-thread` |
| RFM | base, 1.20, 1.40, 1.50, 1.60, 1.80, 2.50, 2.60 | `pb2kslib`, `adsp`, `adsp-thread` |

A pass establishes that the game identity appeared, MediaGX blits progressed,
the chosen sound engine progressed, timing snapshots were produced, and no
fatal or DCS initialization failure was detected.

## What it does not prove

Automation cannot judge visible pixel accuracy, subjective sound correctness,
or electrical behavior on a physical cabinet. Those require windowed/manual
testing and the cabinet procedure in
[46-real-lpt-passthrough.md](46-real-lpt-passthrough.md).

Runs use isolated savedata so one row cannot repair or contaminate another.
The report records the exact update directory selected for each row.

## See also

- [25-dcs-sound.md](25-dcs-sound.md)
- [35-known-limitations.md](35-known-limitations.md)
