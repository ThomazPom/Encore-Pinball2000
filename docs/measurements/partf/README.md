# Part F — Independent reproduction + strategic reframe (fresh-eyes pass)

> [!IMPORTANT]
> **Superseded in part by the boot-race fix.** This note's central negative
> finding — "direct-clkint inject wedges XINU (`imr=ff`), zeros the LPT, baseline
> is the only option" — was the **pre-fix** behaviour, and this note correctly
> identified it as host-specific. Root cause: with `prime=50`, injection engaged
> *during* the guest's resource-loading boot phase and deadlocked it (~50 % of
> runs, a host-headroom coin-flip). **Post-fix (prime=8000 + starvation valve +
> wedge-detector, re-gated behind `NOCHAIN=1`), live injection reaches ~0.87–0.94×
> game-clock with the screen alive on both headless and windowed, reproducibly
> (9/9 runs)** — no display is
> demoted anymore. Read the tables here as the historical pre-fix record; the
> current recommendation is [docs/47](../../47-recommended-configuration.md) and
> [docs/12 Part G](../../12-cpu-and-timers.md).

> [!IMPORTANT]
> **The actionable conclusion of this pass lives in
> [docs/47 — Recommended Configuration (The Golden Path)](../../47-recommended-configuration.md)
> and is consolidated in [docs/12 Part G](../../12-cpu-and-timers.md).** For a
> cabinet, run the plain baseline (hardware-faithful, meets the 16 kHz LPT KPI);
> for a headless bench, the fixed gated inject mode is the high-game-clock lever.
> This note is the raw (pre-fix) evidence.

> Context: a fresh agent re-read the whole timing history, the erikie PM
> thread (the author of the *other*, working Pinball 2000 emulator), and the
> Part A–E work, then independently re-measured on a different (slightly
> slower) host. This note records what reproduced, what did **not**, and a
> reframe of what "done" should mean. It deliberately adds **no new gated
> knobs** — the point is clarity, not more experimental surface.

## TL;DR

1. **The baseline already meets erikie's actual acceptance criterion.** At the
   stock natural-delivery baseline (only ~36 % `clkint` delivery on this host),
   the emulated LPT already emits **~18.7 k data writes/s and ~45 k control
   writes/s**, i.e. *above* the `target_driverboard_hz = 16000` the board
   expects. The `p2k-lpt-hz` panel shows this directly.
2. **`clkint` delivery-% is probably the wrong North Star.** The project spent
   Parts A–E trying to lift `clkint` delivery from 43 % toward 100 %. But
   erikie explicitly says XINA *tolerates* missing IRQs (it falls back to
   XINU) and that the thing that matters is **16 kHz to the driver board** —
   which baseline already exceeds. The real residual problem is **guest-time
   drift (~0.36×)**, not the raw delivery fraction.
3. **The "direct-clkint inject" path — documented in Part E as the "safest and
   fastest mode" — does not reproduce as safe.** On a ~15 % slower host it
   **wedges XINU** (`imr=ff`, guest interrupts permanently masked) within a few
   thousand dispatches, collapsing delivery to ~1 %, **freezing guest time
   (drift 0.010)** and **zeroing the LPT output (`+0/s`)**. It is *strictly
   worse* than the "broken" baseline. This is host-sensitive and must not be
   trusted on cabinet day.

## Measurements (QEMU 10.0.8, TCG, headless `swe1`, ~40 s, this host)

Command shape:
`P2K_DIAG=1 [mode env] scripts/run-qemu.sh -v --game swe1 --headless --no-audio --no-savedata --uart-quiet`

| Mode | clkint delivery | xinu drift | scale | LPT data/s | stable? | end imr |
|---|---|---|---|---|---|---|
| **Baseline (strict, no env)** | **36–37 %** | **0.357** | 1.000× | **+18.7 k** | **yes** | `60` |
| direct-clkint inject + idle-breaker, cap 8 | 1.0 % | 0.010 | 1.000× | **+0** | **NO — wedged @~1669 disp** | `ff` |
| direct-clkint inject, cap 1 (no-burst) | 2.2 % | 0.010 | 1.000× | +0 | **NO — wedged @~3685 disp** | `ff` |
| prnull-HLT experiment, **update** boot | 37 % | — | 1.000× | +18 k | yes (patch never engaged — addr mismatch) | `60` |
| prnull-HLT experiment, **museum** `--update none` (patch engaged) | **41 %** | **0.373** | 1.000× | +17.6 k | yes — **worse than baseline** | `60↔ff` (healthy) |

Notes:
- Baseline here is 36 % vs Part A–E's ~42 %, consistent with a host ~15 %
  slower. Everything below is interpreted relative to that.
- `cap=1` (the "no-burst, drop-don't-queue" idea) only **delayed** the wedge
  (1669 → 3685 dispatches); it did **not** prevent it. So the Part E abort is
  **not** primarily a burst/coalescing problem — direct injection
  destabilises XINU on this host regardless of cap or the idle-loop breaker.
- The prnull-HLT module armed but never matched its hard-coded `eb fe` address
  (`swe1=0x001d96ae`) under the **auto-update bundle** — those classifier
  addresses were captured under `--update none` and are version-specific. Under
  the **museum boot (`--update none`)** the address *does* match: a follow-up
  run patched it in cleanly (`[eb fe 73 79] → [fb f4 eb fd]`) and confirmed the
  genuine-HLT hypothesis is **disproven** — delivery fell 43 % → 41 % and drift
  did not improve, exactly reproducing the Part E.5 prnull verdict in
  [docs/12](../../12-cpu-and-timers.md). No virtual-clock warp occurred (the
  live PIT keeps the CPU runnable), so HLT does not even buy the "5×" speedup.
  **The genuine-HLT lever is now closed, under both boots.**

## Why this matters: the erikie reframe

From the PM thread (`composis/this_is_getting_serious/eriki talks.secret.txt`),
the author of the working emulator gives four load-bearing statements the
project under-weighted:

1. *"You need real time for timing… Always use real clock. No vm timing."*
2. *"lightweight route into the cpu"* for the IRQ (not the full
   PIT→PIC→TB-boundary path).
3. *"xina core has a check for missing irqs … the code will fall back to xinu.
   So don't worry too much about it."*
4. *"in the end it matters if you get 16khz to driverboard … that is the clock
   it expects."*

Points (3) and (4) reframe the goal. The acceptance test is **wall-time LPT
rate + cadence stability**, with XINA self-healing missed ticks — **not** a
100 % `clkint` count. The `p2k-lpt-hz` panel already measures (4) and baseline
passes it.

## So what is actually wrong, then?

The honest residual problem is **guest-time drift ≈ 0.36×** (verified two ways
in Part D: QEMU counters *and* XINU's own in-RAM tick counter). The guest's
*sense of time* runs at ~0.36× wall. Even though the LPT *write rate* in wall
time is high, every **time-based** behaviour inside the game — animation pacing,
lamp-refresh cadence, the spacing the driver board sees *between* pattern
frames — is stretched by ~2.7×. That is the most likely cause of erikie's
observed "lamps flicker / relay goes wild / timing completely off" when he
tested Encore, **not** an insufficient raw LPT rate.

The `pdb05` panel quantifies this: PDB opcode-0x05 (lamp/data refresh) strobe
gap is p50 ≈ 646 µs wall at baseline. Whether that wall cadence is what the
board wants is the one thing only a real board can answer.

## The "5× faster" the user saw — and why "both" is real

> [!NOTE]
> **Superseded by measurement — see [Part H](../parth/README.md).** This
> section originally attributed the speed-up to "virtual-clock warping
> (`scale ≫ 1`)". Direct measurement disproved that: `scale` stays `1.000×`
> on the current QEMU machine. The real cause is the **game-clock** (`drift`),
> driven by `clkint` over-delivery — not a `QEMU_CLOCK_VIRTUAL` warp.

In short: baseline delivers ~0.36–0.43× of `clkint`, so the game clock crawls.
The high-delivery experiments (frame-injection heartbeat 88–99 %, direct-clkint
78–90 %) pushed it toward ~0.9×, with a predicted over-delivery ceiling near
1.8× natural ≈ **5× the 0.36 baseline** — so animations raced while the display
FPS stayed pinned. "Both good delivery *and* good fps" is physically available
because the two cadences are separable:

- **Heartbeat (clkint + LPT)** must be **real wall-clock** 250 µs — safety
  critical (solenoids/lamps live in wall time).
- **Mainline compute** can free-run and idle in between.

On a host with headroom (this one has it — see Part H), you get real-time
gameplay *and* a faithful heartbeat — *if* delivery reaches `drift = 1.000×`
and is then pinned to wall, never allowed to free-run above 1.0×.

## Recommendations (for cabinet day and the next engineering step)

1. **Change the KPI.** Track `p2k-lpt-hz` (wall data/ctrl rate vs 16 kHz) and
   `pdb05` **wall** cadence as the headline acceptance metrics. Demote raw
   `clkint` delivery-% to a secondary diagnostic. erikie's own guidance and the
   data both point here.
2. **Do not rely on `P2K_TCG_DIRECT_CLKINT_INJECT` on the cabinet.** It is
   host-sensitive and was observed to wedge the guest and zero the LPT. Keeping
   it default-OFF is correct; the Part E "safest and fastest" framing is
   host-specific and has been softened in `docs/12-cpu-and-timers.md`.
3. **Drift is the residual — but no emulator-side lever fixes it.** The most
   erikie-aligned clean idea was **genuine guest idle (HLT) on the natural
   i8259 line**. That has now been tested under museum boot (where the address
   matches) and **disproven** — it slightly lowers delivery and does not
   improve drift (see above and docs/12 Part E.5/G). Direct injection wedges
   the guest. So drift is not closable by any knob we have without
   destabilising XINU; it is a question for the real board (XINA's missing-IRQ
   fallback may absorb it).
4. **First real-board test should be the plain baseline**, not any timing
   experiment: `--lpt-device /dev/parport0 --cabinet-purist`, natural delivery,
   capture `--lpt-trace`. Diff that trace against erikie's description. That
   tells us whether the ~0.36× drift actually matters to the board or whether
   XINA's missing-IRQ fallback already absorbs it — which is unknowable from
   emulation alone.

## What NOT to do (loop avoidance)

- Do **not** keep adding TCG-side strategies to chase 100 % `clkint` delivery.
  Parts B, C, E plus this pass have shown that path is exhausted under the
  no-icount / no-patch / TCG constraints, and (per erikie) the target is the
  LPT wall rate, which baseline already meets.
- Do **not** harden the direct-inject path further without first understanding
  *why* a `do_interrupt_x86_hardirq` dispatch eventually drives XINU to
  `imr=ff`. That is a guest-state-corruption investigation, not a tuning knob,
  and it is the exact rabbit hole the previous iteration fell into.
