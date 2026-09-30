# IRQ0 runaway: diagnostic observer effect

Date: 2026-09-30

Branch: `forensic/start-gate-gdb`

Game/update: SWE1 2.10

IRQ mode: natural i8254 → i8259 (`--strict` is the compatibility alias)

## Verdict

The live failure captured during the Game Over investigation was not evidence
that normal strict-mode delivery needs an IRQ-depth guard. The immediate host
stall was created by Encore's own detailed `P2K_DIAG` report: every three
seconds it sorted twelve 4096-entry timing rings and formatted the full report
on QEMU's main/timer thread.

At 12.043 seconds that work consumed about 31.273 ms of **thread CPU time**.
Upstream QEMU's i8254 then replayed the expired PIT transitions from their old
deadlines. At a 250 us game tick, the debt was about 125 ticks. The replay ran
at roughly 10–14 us between fronts, allowing XINU to acknowledge and re-enter
`clkint` between callbacks. The guest reached depth 182 and only 64 bytes of
observed IStack margin. After the final unwind the master PIC mask remained
`0xff`, so IRQ0 stopped reaching the guest even though ordinary processes
could still execute.

The accepted correction removes the expensive report from the periodic timer:
all three-second reports use the bounded snapshot, while the complete report
is emitted only at exit. It does not alter, cap or discard IRQ0.

A later experiment also reproduced an IStack exhaustion with `P2K_DIAG`
disabled but with a one-off full flight recorder enabled. That recorder took
multiple clocks, register snapshots and eight guest-memory reads per delivered
tick. It captured the immediate mechanism—IRQ0 repeatedly entering at
`0x0025040c`, just after `interval_0_25ms()` restores interrupts, with 76 bytes
consumed per nested frame—but it was not a transparent workload. The recorder
is deliberately not retained in the production source.

> [!IMPORTANT]
> The natural nesting observed in XINU is real, and QEMU's overdue-timer replay
> can amplify any sufficiently long main-loop stall. This result identifies
> the stall that caused the captured live failure. It does not prove that no
> unrelated source of long stalls can exist.

## Causal trace

| Event | Wall time | Evidence |
|---|---:|---|
| Last ordinary PIT front before the hole | 12.012443 s | sequence 171069 |
| Next PIT front | 12.043715 s | sequence 171070 |
| Main-thread gap | 31.272 ms | wall and virtual clocks agree |
| Main-thread CPU consumed across gap | 31.273 ms | `CLOCK_THREAD_CPUTIME_ID` |
| Replay after the gap | 106 consecutive captured fronts below 100 us; about 125 ticks of debt in total | 31.272 ms / 250 us ≈ 125 |
| Maximum IRQ0/`clkint` depth | 182 | flight-recorder header |
| Minimum observed IStack margin | 64 bytes | flight-recorder header |
| End state | PIC IMR `ff`, IRR `01`, ISR `00`; no later IRQ0 entry | flight recorder plus live RAM capture |

The 12-second position is also the fourth deadline of the old three-second
full report. The CPU-time delta rules out a 31 ms host deschedule: QEMU was
actively doing work on the timer thread.

## Before/after measurements

| Run | Duration | Delivery / speed | Max depth | Min IStack margin | Largest PIT gap | Fast replay | Result |
|---|---:|---:|---:|---:|---:|---:|---|
| Old full `P2K_DIAG`, captured failure | failure began at 12.04 s; live capture ~150 s later | delivery eventually stopped | 182 | 64 B | 31.272 ms | 106 consecutive captured fronts below 100 us | failed, PIC left masked |
| Corrected `P2K_DIAG`, generic bench | 10.006 s measured after warmup; two-pass bench | 99.90% / 99.94% | 4 | 7064 B | IRQ worst 506 us | none pathological | PASS WITH WARNINGS |
| Corrected `P2K_DIAG`, manual play | ~358 s | 1,432,361 raised / 1,419,023 entered over all phases | 5 | 6688 B | 5.952 ms | 23 fronts below 100 us | clean F1 shutdown; no recorder trigger |
| Full flight recorder, no `P2K_DIAG` | failure began near 282 s | recorder froze after the trigger | 123 | 44 B before overflow | no initiating host stall | repeated entry at `0x0025040c` | failed; recorder considered invasive |
| Minimal stack sampler, manual stress | 2,964,902 acknowledgements, about 12.34 min of IRQ time | normal natural delivery | 8 | 6588 B | not sampled | not sampled | several play/Game Over cycles; clean F1 shutdown |
| Rejected PIT late-transition collapse | 10.006 s measured after warmup | 49.76% / 49.79% | 5 | 7044 B | IRQ worst 1.52 ms | suppressed by dropping ordinary catch-up | ABNORMAL; reverted |

The generic bench warning was one isolated PDB gap above 2.5 ms in five
windows. PDB05 measured p50 249 us, p95 262.6 us, p99 315.8 us and worst
4.421 ms; DCS health passed in both phases.

The manual run was deliberately controlled by a human. The attempted
`natural-drain.p2k` automation was invalid: it could create two players and
then drain only three balls, so its Game Over timeout was not a functional
failure. That script has been removed.

## Rejected alternatives

- A depth/IStack circuit breaker bounded the symptom but changed guest time by
  dropping ticks. It was removed from the implementation, CLI and docs.
- Collapsing every late i8254 transition looked physically attractive but
  discarded ordinary QEMU catch-up too. The bench ran at half speed, so the
  change was reverted.
- Single-thread TCG was not a demonstrated fix: controlled runs reached depth
  5 in single-thread and depth 4 in multi-thread, both healthy.

## Evidence identity

The large raw captures remain outside Git under `/tmp`:

| Artifact | SHA-256 |
|---|---|
| `/tmp/encore-irq0-natural-drain.csv` | `c8b948cfab3f53a15a68b5b615cd72e80217eaa1cd1e3ba8b77e7ab213001991` |
| `/tmp/encore-irq0-manual-diag-fixed.csv` | `18537ee1d6a1037a781be7884bedf1f4472b5babf5cc18f9fb3d1eafa9516bf9` |
| `/tmp/encore-irq0-manual-nodiag.csv` | `7c7b7d8ed7dbdf5f9d1bc3b6a4a516f8774e07ba141e66c9de05476a964817d0` |
| `/tmp/p2k-bench-gamj_a9m/results.json` | `5ef9bf9bfe35b38946d206f52a91f413178d7ddc21e9b5029a9214adcb42dd92` |

The live failed-machine capture is
`/tmp/encore-natural-drain-live-20260930`. The flight-recorder CSV includes
wall time, virtual time, per-thread CPU time, depth, PIC state, registers and
selected SWE1 2.10 XINU scheduler globals.

## Remaining validation boundary

The corrected diagnostic path has passed the generic bench, one long detailed
run and one heavily exercised minimal-sampler run. Reports of rare crashes
from completely normal runs remain unconfirmed by a retained clean capture.
They must not be attributed to an observer effect—or to normal IRQ delivery—
without evidence from a measurement path whose own cost has first been bounded.
