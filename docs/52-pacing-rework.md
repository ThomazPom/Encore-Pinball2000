# 52 — pacing rework: "250 µs budget then IRQ0"

Status: analysis + measured baseline. No code change yet.
Branch: `experiment/main-clean-and-pace` (off `4b02a63`, the last
pre-LPT-pace commit).

## Context

Pre-LPT-pace Unicorn was wall-clock driven. The user asked whether
we can replace that with a "250 µs budget then IRQ0" slice model,
the way QEMU's `-icount` or a deterministic timer would.

This note records what the current code actually does, what
"slice + IRQ0" can mean, what's already tried, and what a real
implementation costs.

## What the current loop does (cpu.c, post-cleanup)

Single-thread emulator loop, no SIGALRM-driven slicing:

```
while (running) {
    if (xinu_ready && (exec_count & 0x3F) == 0) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        pit_period_ns = pit.count[0] * 838;       /* 1e9 / 1.193182 MHz */
        if (now - last_tick >= pit_period_ns) {
            pic[0].irr |= 0x01;                   /* assert IRQ0 */
            prof_ticks_fired += n_ticks;
        }
        /* VSYNC at 57 Hz, also wall-clock-driven */
        ...
    }
    if (xinu_ready) check_and_inject_irq();       /* deliver pending */
    uc_emu_start(uc, eip, 0, 0, /*batch=*/200000);
    /* maintenance, HLT handling, error decode, display ... */
}
```

Two clocks, both wall-clock-derived:
1. **PIT (IRQ0) tick**  — fires whenever `now - last_tick >=
   pit_period_ns`. With the typical `div=298` programmed by XINU,
   `pit_period_ns = 249 724`. So IRQ0 IS already on a 250 µs
   wall-clock budget.
2. **VSYNC (57 Hz)** — independent 17.5 ms cadence that pokes
   `DC_TIMING2` for the display console.

CPU execution between ticks is unbounded — `uc_emu_start` runs
the JIT until the 200 k-instruction batch counter exhausts OR an
MMIO/MEM/INSN hook returns. There is no host-time bound on a
single batch.

## Measured baseline (5 s windows, --headless --no-audio)

```
PERF: calls=8 980 096/5s ok=8 979 494 0f3c=602 hlt=0 ticks=20 091
        ↑ 1.80 M uc_emu_start calls/s
                              ↑ ~4 020 IRQ0 fires/s = 248 µs apparent period ✓
                                      ↑ 0 HLT — game runs tight
```

Run summary (swe1 default, 30 s):
- 38.9 M `uc_emu_start` calls
- ~4 kHz IRQ0 (matches programmed PIT div=298, ±0.5 %)
- 100 % host CPU, 0 HLT
- exec_count grows ~1.3 M/s

So **the IRQ injection side is already on a 250 µs budget** —
just driven by host wall clock instead of a deterministic counter.

## What "250 µs budget then IRQ0" can actually mean

Three distinct designs hide under one phrase:

### A. Bound IRQ0 cadence to 250 µs of host wall clock
**Already implemented.** This is what the loop above does. There
is nothing to add.

### B. Bound CPU execution to 250 µs of host wall clock per slice
The CPU runs free between IRQ injections today. To bound a single
`uc_emu_start` call to 250 µs of host time, two options:

#### B1. SIGALRM at 250 µs + `uc_emu_stop()` from handler
Tried by the original author and **abandoned** — see
`src/main.c:809-813`:
> "SIGALRM disabled — iteration-count ticks only. SIGALRM signal
>  delivery was interfering with Unicorn JIT execution (signal
>  masking, cpu_exit from signal handler context). HLT wakeup
>  handled by busy-wait with nanosleep."
And `src/cpu.c:751`:
> "No uc_emu_stop from SIGALRM = no stop_request contamination."

So this is a known bad idea in this codebase.

#### B2. Watcher pthread that calls `uc_emu_stop()` from outside
the signal handler
Avoids signal-context pitfalls but trades them for thread-safety
constraints on `uc_emu_stop`. Race: `uc_emu_stop` between
batches is a no-op; inside a batch, it sets the stop_request flag
which Unicorn checks at the next TB. Probable fit, untested.

### C. Bound CPU execution to N guest-instructions per slice
(== deterministic icount-style, no host clock dependency)

Two plausible knobs:

#### C1. Shrink batch to one PIT-period worth of insns + always
inject IRQ0 after the batch.
- Need to calibrate `INSNS_PER_PIT_PERIOD`. From the baseline:
  1.3 M insns/s, 4 kHz IRQ0 → ~325 insns/IRQ0. Tiny batch ⇒ call
  rate would 600× current = 1 G calls/s. Each `uc_emu_start` has
  fixed overhead (~500 ns measured) so 600× is a non-starter.
- The reason for the 200 k batch isn't real CPU work per call;
  it's that something **else** stops the JIT first (MMIO hook,
  IN/OUT, mem-prot, INSN_INVALID for HLT/0F3C). Average call
  actually executes a handful of TBs.

#### C2. `UC_HOOK_BLOCK` callback that increments a virtual cycle
counter and calls `uc_emu_stop` when the budget is exhausted.
- Block-hook fires inside Unicorn's own TCG callback context →
  safe to call `uc_emu_stop` (no signal/thread issues).
- Cost: extra C callback per translated block. Block hook in
  Unicorn typically adds 5–10 % per-TB overhead.
- Pros: fully deterministic, replay-friendly, host-speed
  independent. Two players on different machines see the same
  IRQ cadence.
- Cons: "guest 250 µs" is now defined as `INSN_BUDGET` instructions
  on a synthetic ~200 MHz reference. If the guest expects to
  measure real time via PIT, that real time is now slaved to host
  CPU speed (slow host = slow game-clock).

## Recommendation

The user's literal request — **"give it 250 µs budgets and then
IRQ0"** — for the **injection side** is already delivered. No
work needed.

If the actual goal is **deterministic, replay-friendly pacing**
(host-speed independent, frame-perfect), the right tool is C2 —
`UC_HOOK_BLOCK` + `uc_emu_stop` + post-batch IRQ0. That's a
~150 LOC change and a re-tuning exercise (calibrate the budget
against a known-good attract-mode loop, then sweep games).

If the actual goal is **CPU throttling** (don't burn 100 % host
CPU when guest is idle), the right tool is the existing HLT path
(cpu.c:828–945) — which already `nanosleep`s on HLT. We could
extend it to nanosleep when the inter-IRQ slice finishes early,
yielding a "pace to wall clock, but allow burst" model.

## Surviving guest-state writes after this branch's first commit

1. **mem_detect** (io.c BT-130) — true .text patch, default ON.
   QEMU equivalent of the only allowed-default-on patch. Removing
   it requires either (a) modeling the MediaGX/CS5530 memory probe
   path naturally (proven dead end — XINU never reads the DRAM
   controller) or (b) a `UC_HOOK_BLOCK` at function entry that
   overrides EAX without touching RAM (clean answer).
2. **probe-cell scribble** (cpu.c watchdog_flag_addr) — guest-data
   scribble, **now strictly gated** to `--update none` /
   `P2K_NO_AUTO_UPDATE`. Default boot uses the natural PLX INTCSR
   bit-2 short-circuit (bar.c off=0x4C) — same fix as the QEMU
   branch. Cabinet-purist with real LPT board still disables both.

That's the "two patches" target the user described.

## Open todo for the next session

1. Wire a `--pacing slice-budget --pacing-budget-us 250` (or env
   `P2K_PACING=slice250us`) that switches to design C2.
2. Add `UC_HOOK_BLOCK` cycle counter + `uc_emu_stop` glue.
3. A/B benchmark vs current wall-clock model: boot-to-attract
   time, IRQ0 jitter, CPU usage, exec_count delta.
4. Calibrate `INSN_BUDGET` against attract-mode at multiple host
   speeds; pick a value that gives parity with current 4 kHz IRQ0.
5. If C2 lands cleanly: also retire `mem_detect` via a block-hook
   at the function entry (the second patch falls to the same tool).
