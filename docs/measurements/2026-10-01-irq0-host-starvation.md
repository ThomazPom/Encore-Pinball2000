# IRQ0 runaway: host-starvation precursor

Date: 2026-10-01

Branch: `forensic/pit-overdue-trigger`

Game/update: SWE1 2.00

IRQ mode: natural i8254 → i8259 (`--strict` compatibility alias)

## Verdict

The rare IRQ0/IStack failure has a second, non-diagnostic initiator in
addition to the already proven overdue-timer replay: the Linux host can
temporarily stop giving the TCG vCPU enough execution time while QEMU's
virtual-clock PIT continues at 4004 Hz.

Two AC-unplug events coincided to the exact second with natural depth rises,
making the power transition a strong controlled reproducer. AC power is not
the root cause, however: a third rise occurred with AC continuously online.
In that AC-stable capture, the vCPU ran on a host CPU reporting about 400 MHz
(the machine's hardware minimum), obtained only 370 ms of CPU over the
preceding 498 ms, and spent 86 ms runnable in Linux's run queue. IRQ0 depth
reached 16 without GDB, a QEMU pause or an injected load.

> [!IMPORTANT]
> This is a host-service deficit, not a guest-requested speed change and not
> an adaptive hotloop. The PIT does not need to accelerate for the failure:
> ordinary 250 us edges are enough when the vCPU cannot finish and unwind the
> preceding handler before subsequent edges arrive.

## What is proven

Three memory-only timer rings froze automatically when tracked IRQ0 depth
first reached 16. The hook records fixed-size binary events around QEMU timer
callbacks and performs no formatting, file I/O or GDB attachment on the hot
path.

| Capture | Power observation | Final non-zero-depth interval | IRQ0 edges | `clkint` entries | PIT lateness in interval | Depth at state copy |
|---|---|---:|---:|---:|---:|---:|
| `pit-trigger` | capture 12:05:16.223; UPower records discharge at 12:05:16 | 12.762 ms | 51 | 18 | mean 107.9 us, max 440.2 us | 21 |
| `power-repro` | capture 12:13:06.573; UPower records discharge at 12:13:06 | 19.469 ms | 78 | 15 | mean 109.6 us, max 468.7 us | 18 |
| `ac-stable-host-starvation` | AC online for every scheduler sample | 70.937 ms | 279 | 116 | mean 560.3 us, max 4.910 ms | 16 |

The first capture is the cleanest proof that a single large main-loop hole is
not required. Its complete 611 ms timer history contains no 25–31 ms timer
stall: PIT callback lateness peaks at 440 us and callback execution itself
peaks at 17.6 us. Depth nevertheless climbs from zero to 16 because only 18
handler entries complete while 51 rising edges occur.

UPower's persistent battery-rate history independently confirms both observed
unplugs: Unix timestamps `1790849116` and `1790849586` decode to 12:05:16 and
12:13:06 local time and change the state to `discharging`. This is not an
alignment inferred from a coarse periodic sample; both state transitions and
both ring files have the same wall-clock second.

The second capture also adds direct Linux scheduler evidence. In the roughly
10 ms before the ring froze, the vCPU received about 5.5 ms of CPU and
accumulated about 3.1 ms of runnable wait; one scheduler interval alone records
2.104 ms of run-queue delay.

The third capture sampled host power state as well as `schedstat`:

| Window before freeze | Observed wall span | vCPU execution | Runnable wait | Reported frequency | AC |
|---|---:|---:|---:|---:|---:|
| ~500 ms | 498.1 ms | 370.2 ms | 86.4 ms | 400–904 MHz | online |
| ~30 ms | 33.7 ms | 22.2 ms | 8.6 ms | ~400 MHz throughout | online |
| final sample | 4.5 ms | 3.2 ms | 0.7 ms | 400 MHz | online |

The host is an Intel i5-1145G7 using `intel_pstate`; sysfs reports a 400 MHz
minimum and 4.4 GHz maximum. After the test, CPU 0 again reported about
3.8 GHz. The exact frequency reading is sampled and may race a task migration,
but the repeated minimum-frequency samples, scheduler accounting and rising
IRQ depth all agree on the same low-service window.

## Mechanism

```text
host frequency / scheduling service falls
                 │
                 ▼
TCG vCPU executes less guest work per 250 us
                 │
                 ├── QEMU main timer thread still emits normal PIT edges
                 ▼
XINU acknowledges IRQ0 and restores IF before the handler has fully returned
                 │
                 ▼
the next IRQ0 nests another clkint frame on the same 8 KiB IStack
                 │
                 ▼
more handler work must unwind while new 250 us ticks keep arriving
                 │
                 ▼
rare recovery, or self-sustaining growth to IStack overflow
```

This explains both healthy and fatal runs. Most short service deficits unwind;
the captured healthy runs previously peaked at depths 5–8. A sufficiently
bad window crosses the point where incoming handler work equals or exceeds
what the vCPU can retire. The normal no-`-v` fatal then retained 100 consecutive
`clkint_x` frames and exhausted the IStack.

## Two distinct host-side initiators

The evidence now distinguishes two paths that must not be conflated:

1. A delayed **QEMU timer/main thread** can make upstream i8254 process expired
   transitions from their old deadlines. The old detailed diagnostic report
   created a measured 31.272 ms example and a fast replay burst.
2. A delayed or under-clocked **vCPU thread** can fall behind while the timer
   thread continues at an approximately ordinary cadence. The new captures
   prove this path and show direct Linux run-queue delay.

Both expose the same emulator-level mismatch: without instruction counting,
`QEMU_CLOCK_VIRTUAL` follows elapsed host time while guest instruction progress
can temporarily stop. A physical PIT also continues while its CPU is busy, but
the original CPU is not descheduled by an unrelated host OS and does not
suddenly run its instruction stream at one tenth of normal speed.

## Consequences for a correction

Raising host priority, pinning the vCPU or forcing a performance profile may
reduce occurrence, but none is a portable correctness fix. An IRQ-depth guard
or silently dropping arbitrary PIT transitions bounds the symptom by changing
guest time and was already rejected.

The conceptually clean direction is to make device time follow demonstrated
guest execution progress during host starvation, using QEMU's instruction
counting/time-dilation machinery rather than another Encore feedback loop.
That must be benchmarked for audio, PDB cadence, wall-clock speed and IStack
margin before it can replace the current default. A narrower compatibility
policy that coalesces IRQ0 only while an older IRQ0 handler remains active is
possible, but it deliberately differs from the physical PIC behavior after
the guest's early EOI and should be treated as a fallback, not as the factual
model.

> [!NOTE]
> No corrective timing policy is committed by this investigation. The branch
> adds opt-in observation tools only; the normal guest timing policy and state
> are unchanged. The generic timer hook takes only its disabled predicate path
> unless `P2K_TIMER_PRECURSOR=1` is present.

## Observer boundary

The scheduler samplers poll `/proc` at 1 ms and therefore add host work. The
third sampler additionally reads sysfs frequency and power state, so it is the
most intrusive of the three. It was not used to prove that the fatal exists:
the prior 55-second no-`-v` fatal used only the bounded stack sampler. Its role
is to classify the already-known failure mechanism. The first ring is also
less intrusive than the later scheduler correlation and shows the same
gradual depth signature.

No artificial CPU load was injected in the AC-stable run: the ring reached its
freeze threshold before the planned controlled-load step, so that step was
cancelled.

## Reproduction and artifacts

The ring can be summarized without attaching to QEMU:

```sh
tools/analyze-qemu-timer-precursor.py \
  /var/tmp/encore-crashes/swe1-200-ac-stable-host-starvation-20261001
```

| Artifact | SHA-256 |
|---|---|
| `/var/tmp/encore-crashes/swe1-200-pit-trigger-20261001/timer-precursor-ring.bin` | `fe221fd48d744b860f7706f77ff110eae2c290b30b978fb83d76091281ac667e` |
| `/var/tmp/encore-crashes/swe1-200-power-repro-20261001/timer-precursor-ring.bin` | `1dabfac95718258fe3abb812ca87afd945933532ef513a4d956bbbbc7917aab1` |
| `/var/tmp/encore-crashes/swe1-200-ac-stable-host-starvation-20261001/timer-precursor-ring.bin` | `6356e7f742ab001f227a32e56ef5a4259634e73be9aae0c6ac61b6d56585e299` |
| `/var/tmp/encore-crashes/swe1-200-power-repro-20261001/vcpu-sched.csv` | `11cfd492328c873ac63ccf85d97142dc6d2e063fb2912aab9e1c89bad2208497` |
| `/var/tmp/encore-crashes/swe1-200-ac-stable-host-starvation-20261001/vcpu-power-sched.csv` | `3e32b2769d4599f45d2f2a4a323f0af53b8d2deaf44aaa74aaaf566f179c601e` |
| `/var/tmp/encore-crashes/2026-10-01-upower-rate-history.tsv` | `8636a3a4954a0173828586b2080e2df5875c6fdb283ccf4d89e284314ba5e819` |

The corresponding no-`-v` fatal and its deterministic 100-frame `clkint_x`
chain remain documented in
[`2026-09-30-irq0-observer-effect.md`](2026-09-30-irq0-observer-effect.md).
