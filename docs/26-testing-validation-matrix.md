# 26 — Validation matrix

The automated matrix runs every selected game/update combination with all three
sound engines. Each row boots without savedata and receives the same door,
credit and volume input sequence.

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

A pass means `inspect()` found a game identity, at least one MediaGX blit, a
timing snapshot, successful initialization of the selected sound engine,
non-zero audio/DSP progress, and no matched fatal-error text.

> [!NOTE]
> The harness sends cabinet inputs but does not assert each resulting guest
> action. It also does not judge pixels, sound quality or physical electronics.

Runs use isolated savedata so one row cannot repair or contaminate another.
The report records the exact update directory selected for each row.

Details: [DCS sound](25-dcs-sound.md) and
[real LPT testing](46-real-lpt-passthrough.md).
