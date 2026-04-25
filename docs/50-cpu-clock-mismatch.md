# CPU clock-rate mismatch — speculative tracking note

> **Status:** open question / hypothesis. Nothing in this document is a
> committed plan. It exists so the next person who hits a "guest-side
> timing got compressed" bug doesn't have to rediscover the underlying
> mechanism from scratch.

## The structural mismatch

Pinball 2000 was designed for a Cyrix MediaGX clocked around 233 MHz.
The ROM firmware (XINA, Asylum, the various drivers) was written
against that wall-clock budget. Specifically:

* iodelay-style busy-waits (counted loops, `inb $0x80` strobes,
  `rep nop` chains) assume an instruction takes a known number of
  nanoseconds on the target CPU.
* Bus protocols handshaking with the cabinet driver-board, the DCS
  sound chip, the DMA engine, and the parallel-port hardware encode
  setup/hold windows in those same loops — *not* in any explicit
  "wait µs" abstraction the host could intercept.

Encore runs the same guest binaries through Unicorn Engine, which is a
JIT translator built on QEMU TCG. On a modern x86_64 host the JIT
typically executes guest instructions **10×–50× faster than the
original CPU did wall-clock-wise**. The guest's iodelay loops therefore
return in a fraction of the time the firmware author assumed they
would. The guest *thinks* it waited; the wire/peripheral never gets the
window it needs.

Symptoms we have observed (or strongly suspect) traceable to this:

| Subsystem | Symptom | Status |
|-----------|---------|--------|
| LPT cabinet bus | Real driver-board's level shifters never see a stable wire; relays chatter, watchdog can trip. | Worked around by `--lpt-bus-pace` (see `19-real-lpt-passthrough.md`). |
| ISA-style `inb(0x80)` delays | Guest reads return instantly; calibrated 1 µs delays collapse to ~0. | Not separately mitigated; unknown which subsystems still rely on this. |
| DCS sound handshake | Theoretical: warm-up / mode-select handshakes between the i960 sound CPU and the host could compress similarly. | Not investigated. Sound currently works "well enough" but jitter / dropouts on hardware mode would be a candidate. |
| Service-menu serial | Some PB2K diagnostic loops bit-bang slow polls and might rely on real-CPU timing for them. | Not investigated. |
| Watchdog / blanking | Driver board asserts blanking after ~2.5 ms of no Switch-Column strobe (see doc 48). Encore feeds the strobe so often the board barely notices, but a missed blanking event on real hardware would look like a CPU-too-fast bug from the *other* direction. | Diagnostic gap-warner exists. |

The shared fingerprint of every bug in this class is that the guest
runs to completion and *thinks* everything is fine, but the external
peripheral disagrees because it never got the wall-clock window the
guest believed it provided.

## Why per-bug band-aids will keep finding us

The `--lpt-bus-pace` knob added in the patch that motivated this doc
restores wall-clock semantics at exactly *one* output boundary: the
parport wire. Cheap, surgical, fixes the immediate hardware-bring-up
problem. But:

* Each new symptom (sound chip handshake, DMA programming window, …)
  needs its own targeted pace knob with its own empirical defaults.
* Knobs accumulate. Operators end up with a wall of `--something-pace`
  flags they can't reason about.
* Some bugs *cannot* be fixed at a wire boundary — e.g., a guest loop
  that spins waiting for an internal flag set by another guest thread
  on a timer. There's no output boundary to insert a busywait at.
* Each band-aid is empirically tuned against one operator's hardware
  and may be wrong on slightly different boards.

Long-term this approach does not scale. The proper fix is to attack
the root: stop letting the guest CPU run faster than the firmware
author expected.

## Speculative direction: target-IPS throttle

> Everything below is hypothesis. None of it has been prototyped against
> the live emulator. Numbers are illustrative.

### What "CPU throttle" would mean here

Instead of letting Unicorn execute guest instructions as fast as the
host can decode and dispatch them, throttle execution so the *average*
guest instructions-per-second roughly matches a real Cyrix MediaGX —
call this `TARGET_IPS`, somewhere around `200–250 × 10⁶` instructions
per second.

The throttle would do, conceptually:

```
on every block exit:
    instructions_done += block_size
    expected_wall_us = instructions_done * 1e6 / TARGET_IPS
    actual_wall_us   = monotonic_now_us() - emulator_start_us
    if actual_wall_us < expected_wall_us:
        nanosleep(expected_wall_us - actual_wall_us)
```

Effects we would expect:

* iodelay loops in the guest now consume real wall-clock time on the
  host, so peripherals see correct setup/hold windows automatically.
* Per-knob band-aids (`--lpt-bus-pace`, future `--something-pace`s)
  become unnecessary or shrink to "fine-tune around the throttle".
* Game pace, audio pitch, frame timing become tied to a single global
  knob (`TARGET_IPS`) instead of being dictated by host CPU speed.

### Why this is non-trivial

* **Block-granularity sleep is too coarse.** Some Unicorn translation
  blocks contain a single instruction (boundary cases); others contain
  hundreds. Naive per-block accounting starves the audio thread or
  oversleeps and stutters frames. Real throttles in QEMU (`-icount
  shift`) interpolate between instruction count and wall clock with
  some hysteresis and run-ahead allowance.
* **Audio/frame coupling.** SDL audio pulls samples on its own thread
  at a fixed sample rate. If the CPU thread oversleeps, audio underruns
  (clicks); if the CPU thread runs ahead and the audio thread can't
  keep up with the producer, you get latency. Whatever throttle we
  pick has to interact gracefully with the existing display and audio
  pacing (see `04-display-and-rendering.md`, `05-sound.md`).
* **Slow hosts.** On a host that *cannot* reach `TARGET_IPS`, the
  throttle is a no-op (it never gets a chance to sleep). The emulator
  runs slow and the symptoms invert: peripherals get *too much*
  setup/hold time, which is mostly harmless except where the guest has
  its own watchdogs that fire when an expected response is late.
* **Calibrating `TARGET_IPS`.** The MediaGX's IPS depends on cache hit
  rate, memory latency, and the specific micro-ops in the hot path.
  Picking a single number is a guess. Likely we'd need a small
  calibration mode that runs a known guest loop and reports the
  measured wall time, then offers a recommended `TARGET_IPS`.
* **Reentrancy with passthrough.** If the throttle ever holds the CPU
  thread mid-translation-block while the LPT thread is mid-bus-cycle,
  we could deadlock or violate the pacing assumptions of the LPT
  patch above. The throttle's sleep point must be outside any
  hardware-bus critical section.
* **Determinism.** A wall-clock throttle is by construction
  non-deterministic. If we ever want input record/replay (mentioned as
  a future idea in `--help`), the throttle has to be replaced or
  bypassed during replay.

### Possible incremental experiments

In rough increasing order of effort and risk, none of which is committed:

1. **Measurement first.** Add a `--cpu-stats` mode that, for the first
   N seconds of run-time, counts guest instructions executed (Unicorn
   `UC_HOOK_BLOCK` or `UC_HOOK_CODE` with cheap accounting) and reports
   the actual achieved guest IPS on the operator's host. We currently
   have no idea whether we're 5× too fast or 50×.
2. **Coarse throttle, opt-in.** A `--cpu-target-mhz N` flag that
   inserts a single nanosleep at a fixed cadence (e.g., once per
   vblank period) sized to bring the running average to the target.
   Cheap, easy to revert, no per-block hot-path cost. May be
   sufficient on its own.
3. **icount-style throttle.** Per-block accounting, sleep only when
   we're more than K µs ahead of schedule. More accurate than (2),
   more invasive.
4. **Calibration mode.** Run a known firmware boot path with a stop
   condition and report measured IPS, suggest a target.
5. **Audio/frame coupling rework.** Once a throttle is in place,
   revisit the existing display and audio pacing to see if any of it
   becomes redundant or starts fighting the throttle.

Order matters: do not skip step 1. Without measurement we cannot tell
whether the throttle is even necessary on a given host, nor how
aggressive to make it.

## When you should suspect this class of bug

Use this checklist when investigating any new "works on the reference
machine, breaks on Encore" timing bug:

* Does the symptom go away if you add an `LD_PRELOAD` shim that
  sleeps inside the affected I/O path? If yes, it is wall-clock-pacing.
* Does the symptom appear *only* on real hardware (not on the
  software emulation)? If yes, suspect an external peripheral that
  needed a real wait.
* Does the bug correlate with host CPU speed? (Try a `cpufreq` step
  down or `taskset` to a slow core.) If yes, throttle territory.
* Is there a guest-side delay loop you can find in the firmware
  disassembly near the symptom? If yes, time it on a 233 MHz CPU
  in your head — does that timing match what the peripheral needed?

If any two of those check, write a band-aid for now (per-boundary
pace knob, like `--lpt-bus-pace`) but file a note here so the case
for the global throttle gets stronger over time.

## Related docs

* `19-real-lpt-passthrough.md` — the LPT bus pace knob, the immediate
  motivating example for this document.
* `48-lpt-protocol-references.md` — driver-board protocol, including
  the explicit timing expectations the firmware encodes in iodelay
  loops.
* `04-display-and-rendering.md`, `05-sound.md` — current display and
  audio pacing, which any future throttle has to coexist with.
