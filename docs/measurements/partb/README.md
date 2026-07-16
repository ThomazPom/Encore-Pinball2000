# Part B — TCG-side scheduling fix experiments

All runs `P2K_DIAG=1 timeout 40 bash scripts/run-qemu.sh ...`,
`P2K_IRQ_REPLAY` unset (replay OFF).

## Strategy 1 — TB exit after IRQ0 IRET

`cpu_exit(current_cpu)` from `p2k_timing_audit_note_iret` whenever the
IRET closes an IRQ0 service segment (gated by `p2k_in_clkint`).
Off-switch: `P2K_NO_IRQ0_IRET_TB_EXIT=1`.

| Scenario | natural delivery | xinu_drift | iret_raise p50 | pdb05 wall p50/p95/p99 |
|---|---|---|---|---|
| swe1 headless (baseline) | 42.4 % | 0.413 | 485 µs | 645/1080/1480 µs |
| swe1 headless (B1)       | 40.9 % | 0.406 | 487 µs | 656/1056/1337 µs |
| rfm  headless (baseline) | 42.1 % | 0.419 | 493 µs | 632/987/1222  µs |
| rfm  headless (B1)       | 41.7 % | 0.413 | 505 µs | 638/980/1090  µs |
| swe1 sdl+pa  (baseline)  | 28.6 % | 0.275 | 490 µs | 660/1080/4655 µs |
| swe1 sdl+pa  (B1)        | 30.0 % | 0.289 | 505 µs | 642/1100/3293 µs |

> [!WARNING]
> Strategy 1 alone produces no measurable improvement. The likely
> reason: `cpu_exit` only requests TB-loop exit at end of current TB,
> but if the next i8254 deadline is still in the future (~250 µs vtime
> later), the main loop has nothing useful to do and bounces straight
> back into TCG, which then chains TBs for ~500 µs vtime before the
> next bounce — recreating the original gap. Stability is identical
> to baseline; no crashes; PIT still i8254 / 4003.97 Hz; PIC pattern
> unchanged. Proceeding to Strategy 2.

## Strategy 2 — virtual-clock timer at next PIT deadline

After the IRQ0 IRET, arm a `QEMU_CLOCK_VIRTUAL` `QEMUTimer` at
`latest_raise_ns + pit_period_ns` whose callback does only
`cpu_exit(first_cpu)`. We do NOT raise IRQ0 ourselves; the i8254
model still drives its own line. The intent is to rendezvous the
vCPU thread with the iothread exactly when the next PIT raise is
expected, so the IRQ0 line edge isn't waiting for the next chained
TB-exit to be honored. Off-switch: `P2K_NO_IRQ0_PIT_DEADLINE_TIMER=1`.

| Scenario | natural delivery | xinu_drift | iret_raise p50 |
|---|---|---|---|
| swe1 headless (B1)  | 40.9 % | 0.406 | 487 µs |
| swe1 headless (B2)  | 43.1 % | 0.420 | 506 µs |
| rfm  headless (B1)  | 41.7 % | 0.413 | 505 µs |
| rfm  headless (B2)  | 44.1 % | 0.438 | 506 µs |
| swe1 sdl+pa (B1)    | 30.0 % | 0.289 | 505 µs |
| swe1 sdl+pa (B2)    | 39.7 % | 0.388 | 487 µs |

> [!IMPORTANT]
> Strategy 2 does not move headless (already at the structural ~42 %
> delivery floor) but on the SDL + PulseAudio scenario it lifts
> delivery from 28.6 % → 39.7 % and drift from 0.275 → 0.388
> (a ~+40 % relative improvement). The conclusion is that the
> headless gap is structural to the i8254 + i8259 + non-icount TCG
> scheduling and not a TB-chain symptom; the display/audio gap
> (where iothread contention dominates) is significantly closeable
> by ensuring the vCPU bounces out at PIT deadlines.


## Strategy 3 — short-TB / no-chain window after IRQ0 IRET

On the IRQ0 IRET, set `cpu->cflags_next_tb` with `CF_NO_GOTO_TB |
CF_NO_GOTO_PTR` so the very next TB does not chain via goto_tb /
goto_ptr. After it executes, `cpu_exec` returns to the scheduler
loop and re-checks `interrupt_request` / `exit_request`. This makes
asynchronous IRQ0 raises from the iothread visible at the next TB
boundary instead of waiting for a chain break. Off-switch:
`P2K_NO_IRQ0_IRET_NO_CHAIN=1`.

| Scenario | natural delivery | xinu_drift | iret_raise p50 |
|---|---|---|---|
| swe1 headless (B3)  | 43.0 % | 0.426 | 500 µs |
| rfm  headless (B3)  | 43.6 % | 0.424 | 504 µs |
| swe1 sdl+pa (B3)    | 39.4 % | 0.386 | 481 µs |

> [!NOTE]
> Strategy 3 stacked on top of B1+B2 adds essentially nothing
> measurable (within run-to-run variance). Single-TB no-chain is
> too narrow a window to materially shorten the iret→raise gap;
> a full no-chain *window* (e.g., for the next ~300 µs vtime) would
> require translator-side cooperation, which is out of scope here.
> Kept enabled by default because cost is one branch + one cflags
> store per IRQ0 IRET.

### Verdict (Part B)

- replay still OFF; PIT still i8254 / 4003.97 Hz; PIC still i8259.
- All three switches default ON, individually disablable.
- Headless service rate stays at ~43 % structural floor.
- SDL+audio service rate climbs from 28.6 % → 39.4 % (drift 0.275 → 0.386).
- No crashes introduced; pdb05 wall percentiles unchanged.
- iret_raise p50 essentially unchanged because the structural gap is
  i8254/i8259 scheduling under non-icount TCG, not TB chaining.
