# SWE1 Game Over IRQ0 nesting experiment — 2026-09-29

This experiment separates a natural Game Over failure from disturbance caused
by its own IRQ0 stack observer, then exercises a selective source-edge circuit
breaker.

## Configuration

| Item | Value |
|---|---|
| Branch | `forensic/start-gate-gdb`, pre-experiment HEAD `4fedf33` |
| Game/update | SWE1 2.10 |
| Timing | default natural i8254 + i8259 path, 100%, no speed target |
| Display | normal direct SDL machine renderer; not QEMU framebuffer async |
| Audio | `adsp-hybrid-thread`, identical in the three comparable runs |
| Cabinet | emulated LPT |
| State | ordinary persistent user savedata |
| Scenario | wait for Game Over, add credits, press Start once per second until accepted, wait 20 s, issue native `game over`, observe 60 s |

The original automation was later found unsuitable as a general reproducer:
depending on the starting state it could create an extra player and drain an
incomplete number of balls. It is therefore not retained as a supported test;
the raw logs and the exact observed outcomes remain the evidence for these
runs.

The runs used the same script and installation, but persistent savedata evolved
normally between launches; they are controlled normal-user replays, not
bit-identical snapshots.

## Results

| Run | Observer / breaker | Wall duration | Max IRQ0 depth | Minimum sampled IStack margin | Post-command current delivery | Dropped edges | Outcome |
|---|---|---:|---:|---:|---:|---:|---|
| 1 | old synchronous record-low reports, no breaker | 134.7 s | **95** | **2,292 B** | 33.7%, then **0%** | n/a | guest froze; PIC mask `ff`, no more `clkint` |
| 2 | memory-only observer, no breaker | 135.6 s | 6 | 6,948 B | 99.8–100.0% | 0 | clean 60 s observation and shutdown |
| 3 | memory-only observer, breaker at 16 / 1 KiB | 135.4 s | 6 | 6,948 B | 99.9–100.0% | **0** | clean 60 s observation and shutdown |
| 4 | forced-path build, breaker threshold temporarily set to 3 | 18.7 s | **3** | 7,164 B | recovered to 100.0% | **1,970** depth drops | guest stayed responsive; validation build then discarded |

The shutdown fragment in runs 2 and 3 is not included in the delivery ranges:
F1 ends a partial three-second sample. Run 4 deliberately used an unsafe-low
threshold only to prove the intervention path; the source and cached QEMU
binary were rebuilt afterward with threshold 16.

## Failure chronology with the synchronous observer

The first run was healthy through 117 s. Its three-second reports then changed
as follows:

| Wall time | Max depth | Current delivery | Current guest speed |
|---:|---:|---:|---:|
| 120.0 s | 8 | 99.1% | 99.1% |
| 123.0 s | 85 | 77.6% | 77.6% |
| 126.0 s | 95 | 33.7% | 33.7% |
| 129.0 s | 95 | 0.0% | 0.0% |

Successive precursor records consumed exactly 76 bytes per nested entry. One
captured stack fell from 2,976 B at depth 75 to 2,292 B at depth 84. The run
did not print XINU's final IStack fatal: it froze first with IRQ0 masked and six
observed handlers still active.

> [!IMPORTANT]
> The live freeze is evidence of a measurement feedback loop, not proof that
> default strict timing naturally fails on this Game Over sequence. Once the
> acknowledgement hook stopped formatting and writing each new record, the
> same normal-user sequence stayed at depth 6 with 6.9 KiB margin.

The mechanism is direct: a new record-low caused synchronous output on the
emulator thread; that delayed completion of the currently nested handler; the
next 250 µs PIT edge could nest again; the deeper entry produced another
record and another delay. Removing hot-path output breaks that positive
feedback.

This does **not** prove that the original extremely rare field crash is
imaginary. It proves only that this particular live reproduction was tainted
and cannot be used as an unqualified strict-mode crash.

## Circuit-breaker interpretation

At its real threshold, the breaker made zero interventions because the clean
scenario never exceeded depth 6. Its A/B result therefore establishes
transparency, not rescue. The forced threshold-3 run separately establishes
that source-side edge discard bounds observed depth and does not create a
repayment burst.

This is related to the older source-front drop experiment, but it is not
serialization until `IRET` and does not return Encore to the legacy hotloop.
Normal IRQ nesting remains available; only an already-abnormal state loses new
ticks.

## Asynchronous components

`--qemu-framebuffer-async` was absent from all three comparable commands. The
threaded ADSP engine was present but identical in runs 1–3. It may contribute
ordinary host load, yet it cannot explain why only the synchronous-observer
run entered the 95-deep feedback loop. No asynchronous component changed
between the quiet no-breaker and breaker replays.

## Raw evidence identity

| Run | Raw path | Bytes | SHA-256 |
|---|---|---:|---|
| 1 | `/tmp/encore-gameover-depth-20260929-run1/session.log` | 243,025 | `2c8c940634313d7bb74fb341189935200e8567beb14b2363a6946a59460eddf2` |
| 2 | `/tmp/encore-gameover-depth-20260929-run2-quiet/session.log` | 99,522 | `9fe7be8cf7a3807402fcd5f348d5c734434818b0bdae4153bc5175b05ff5b821` |
| 3 | `/tmp/encore-gameover-depth-20260929-run3-breaker/session.log` | 99,672 | `f44f867d31e9b7d7c45e8f35af216388db205cdd4369f7e0e799b8199057d85b` |
| 4 | `/tmp/encore-gameover-depth-20260929-run4-forced-breaker/session.log` | 24,418 | `8d456d8f9c4a5f710e72e55a32ed2d59defb4c80b0e5e55de3402c1df36107b0` |

The report above preserves the decision-relevant lines and hashes. `/tmp`
paths are local raw artifacts and are not release assets.
