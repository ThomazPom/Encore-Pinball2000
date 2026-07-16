# Part H — Two clocks: display FPS vs game-time, and the "runs 5× faster" effect

> Goal of this note: answer three questions with numbers — (1) what are the KPIs
> for every runnable scenario, (2) *why* did the emulator sometimes run "very
> too fast" (graphics racing) on earlier runs, and (3) can we reproduce that
> fast regime in a **controlled, documented** way so it's a tool we understand
> for later (e.g. if we ever hit an FPS/throughput problem). All numbers below
> are measured on this host (QEMU 10.0.8, TCG, `swe1`, ~39 s, `--update none`
> unless noted). This is a diagnostic note — for what to actually *run*, see
> [docs/47](../../47-recommended-configuration.md).

## TL;DR — there are TWO different "speeds", and people conflate them

> [!IMPORTANT]
> **Updated after the boot-race fix.** Some numbers in this note predate the
> direct-clkint fix and are explicitly corrected inline (search "post-fix").
> Headline: the gated inject mode (INJECT+NOCHAIN) now reaches **drift ~0.9×
> game-clock on both headless and windowed, screen alive** (it used to
> coin-flip black-screen), and the "~2.2–2.5× game-clock speedup" below is the
> measured, reproducible reconstruction of the subjective "ran 5× faster"
> impression. Authoritative
> config: [docs/47](../../47-recommended-configuration.md).

| | **Display FPS** (redraw rate) | **Game-time rate** (a.k.a. drift / game-clock) |
|---|---|---|
| What it is | how often QEMU repaints the window (`dpy_gfx_update_full`) | how fast the *guest's own sense of time* advances vs wall |
| Measured by | `p2k-fps` panel | `p2k-xinu-drift` (and `scale`) panels |
| Driven by | QEMU's GUI refresh timer (~30 Hz headless egl, ~60 Hz SDL) | **`clkint` delivery rate** (PIT IRQ0 → XINU tick) |
| This host | **~32 FPS**, GUI-refresh paced, *not the speed knob* | **0.36–0.43×** at baseline (game runs **slow**) |
| Makes animation look fast? | **No** — it only resamples whatever the game drew | **Yes** — this is the knob that makes lamps/animation race |

**"Graphics too fast" = the game-time rate (right column) ran ahead, NOT the
redraw FPS.** The redraw FPS is GUI-refresh paced, not game-paced: under
`--display egl-headless` the `p2k-fps` panel reads **31.9 FPS** at baseline.
By construction it *can't* track game speed — `s_disp_frames` only increments
inside QEMU's `gfx_update` callback (`qemu/p2k-display.c`), which fires on the
GUI refresh timer, not on guest progress. (We could not co-measure FPS *and* a
boosted game-clock in one run at the time of this note, because the fast lever —
direct-clkint inject — then crashed the display path; post-fix it runs windowed
too. The decoupling is therefore from the code path plus the baseline
measurement, not a single combined run.)

## KPI matrix (every scenario we can run)

`P2K_DIAG=1 [env] scripts/run-qemu.sh -v --game swe1 {--headless|--display egl-headless} --no-audio --no-savedata --uart-quiet --update none`

| Scenario | clkint delivery | game-clock (drift) | display FPS | LPT data/s | stable? | meaning |
|---|---|---|---|---|---|---|
| **baseline**, update bundle | 36–37 % | **0.357×** | n/a¹ | +18.7 k | ✅ | golden path; game-clock slow |
| **baseline**, museum | 43 % | **0.428×** | ~32 | +18.9 k | ✅ | lightest boot |
| **baseline + egl display** | 41.3 % | 0.407× | **31.9** | ~18 k | ✅ | display costs ~2 pts delivery; FPS pinned |
| **direct-clkint excl inject**, cap 8 | →50 % | **0.476× (Δ0.59)** | 0 headless / **SEGV** w/ display | **+25.2 k** | survived 39 s smoke² | **faster** game+LPT; bounded diagnostic, HEADLESS-ONLY |
| direct-clkint inject + idle-breaker | →1 % | 0.010× | — | +0 | ❌ wedged | `imr=ff`, frozen (prior session) |
| direct-clkint inject **REALTIME** | 0.3 % | 0.003× | — | ~0 | ❌ starves | REALTIME timer fights the VIRTUAL i8254 |
| direct-clkint inject **cap 12** | 0.2 % | 0.002× | — | ~0 | ❌ starves | over XINU's nested-IRQ ceiling |
| prnull-HLT (engaged) | 41 % | 0.373× | — | +17.6 k | ✅ | worse than baseline (see Part F/G) |
| **Part E excl inject, faster host** (historical) | **78–90 %** | **0.77–0.90×** | — | high | host-dependent | the real "fast" regime: ~2× baseline game-clock |
| **legacy Unicorn full-speed** (historical) | n/a | **vt-scale AHEAD >1** | 50–56 | n/a | ✅ | instruction-count game-clock ran *ahead of wall* = "too fast" |

¹ The update-bundle boot does not finish inside the ~39 s smoke window, so a
display run isn't meaningful there; delivery/drift are steady-state regardless.

² "survived 39 s smoke" = no wedge in this single run; it is **not** certified
stable — direct-clkint live injection is a default-OFF diagnostic that has
wedged XINU in other configs (idle-breaker, cap 12, REALTIME, +display). Treat
it as a bounded experiment, never cabinet config.

The decisive row is **direct-clkint excl inject**: pushing more `clkint` into
the guest moved **game-clock 0.43→0.48× AND LPT 18.9k→25.2k/s together**, while
display FPS stayed at ~32. Game-clock, LPT rate, and "animation speed" **co-vary
because they are all downstream of guest scheduler progress** (more `clkint` →
more ticks → more game work per wall second); the display redraw cadence is a
**separate**, GUI-refresh-driven axis. (Co-movement here is strong evidence, not
a proof of strict proportionality.)

## Why it sometimes ran "5× faster" — the mechanism

The guest's sense of time is built from the XINU scheduler tick, which is
driven by `clkint`, which is driven by PIT IRQ0 delivery. So:

```
game-clock rate  ≈  clkint delivered / clkint expected   (= the "drift" number)
```

- At **baseline** only ~0.36–0.43 of PIT edges become `clkint`, so the game
  clock runs at **0.36–0.43× wall** — everything is in slow motion.
- The previous agent's high-delivery experiments (the **H7/H8 manual
  frame-injection heartbeat**, 88–99 % delivery, and **direct-clkint exclusive**
  78–90 %) pushed delivery far up. Going from 0.36× to ~0.9× is a **~2.5×**
  jump — that is the part actually *reproduced* (historically) and the most
  likely thing the user saw. As an upper bound, the dryrun accounting predicted
  an over-delivery ceiling near **1.8× natural**, which would be **~5× the 0.36
  baseline**; treat that "5×" as an explanatory ceiling, **not** a stably
  reproduced current-QEMU result. Either way the graphics didn't redraw faster
  — the **game clock** sped up, so every animation, lamp chase and relay
  cadence raced.
- The **legacy Unicorn** build is the cleanest example: it derived game-time
  from an *instruction count* at an assumed CPU MHz, with the wall-clock
  throttle (`--realtime`) **off by default**. On a fast host the JIT retired
  instructions far faster than the modeled CPU, so virtual/game time ran
  **ahead of wall** (commit `b35516b` literally added an `AHEAD` label for
  `vt-scale > 1`). Same root cause: game-clock decoupled from wall, running
  fast. (`e59277c` separately fixed an *uncapped display* that redrew "every
  iteration" — that was the FPS axis, and it was throttled to ~60 Hz.)

So "good delivery" and "fast graphics" are **not** a trade-off — they are the
**same thing**: more `clkint` → faster game-clock → higher LPT rate → faster
visuals. The genuine trade-off is **speed vs stability vs wall-correctness**.

## Controlled reproduction (documented, headless)

To make the game-clock measurably faster on demand and watch it in the panels:

```bash
P2K_DIAG=1 \
P2K_TCG_DIRECT_CLKINT=1 P2K_TCG_DIRECT_CLKINT_INJECT=1 \
  scripts/run-qemu.sh -v --game swe1 --headless --no-audio --no-savedata \
  --uart-quiet --update none
```

Watch: `p2k-xinu-drift` (rises above the 0.43 baseline), `p2k-timing
delivery=` (climbs toward ~50 % here), and `p2k-lpt-hz` (data/s rises
18.9k→~25k). That is the fast regime, reproducible and bounded.

**The edges of the envelope (also reproduced, so we know the walls):**

- **Stability ceiling — XINU, not QEMU.** `…_CAP=12` (or the idle-breaker
  combo) over-feeds XINU's nested-IRQ guard → it masks everything (`imr=ff`)
  and **starves to ~0.2 %**. cap 8 (default) is the stable sweet spot here.
- **`…_REALTIME=1` starves** (0.3 %): the dispatch timer on
  `QEMU_CLOCK_REALTIME` fights the i8254 model, which still lives on
  `QEMU_CLOCK_VIRTUAL`. Don't mix clocks.
- **Display compatibility (corrected by the fix).** Earlier builds segfaulted /
  collapsed under `--display egl-headless` *or* SDL with direct-clkint inject.
  Post-fix: **egl-headless AND windowed SDL + inject now work** (measured
  65–77 %, screen ~48 % non-black, 3/3 runs each — the fps-vs-game-clock figures
  here were taken egl-headless). The old SDL/GTK wedge was the boot-race
  (prime=50 engaging mid-load), now fixed by prime=8000 + the starvation valve
  + wedge-detector; there is no display allowlist anymore.
- **Host- and fix-bound.** Earlier (pre-fix) builds reached only ~50 % via
  inject and wedged ~half the runs. With the boot-race fix (prime=8000 +
  starvation valve + wedge-detector, re-gated behind `NOCHAIN=1`) this host now
  reaches **~0.9× game-clock**, reproducibly 9/9. The host clearly *has* headroom (egl redraws ~32
  FPS while the vCPU also runs the game faster); the residual limiter is the
  per-`clkint` cost and the XINU acceptance ceiling, not raw CPU. (Older notes
  cited a large SDL redraw-FPS figure under full TB-chaining; that number was
  never instrumented here and is not relied upon.)

## What this buys us for later (the hopeful part)

If we ever hit an FPS/throughput wall, the headroom is real and the levers are
now mapped:

1. **Display FPS** is independent and cheap to raise (it's just QEMU's GUI
   refresh; SDL already does ~60, the engine has shown ~200). It is *not* the
   bottleneck and *not* coupled to correctness.
2. **Game-clock speed** is gated by `clkint` delivery, which is gated by
   per-`clkint` cost (~iret_raise dominates) and XINU's ~8-tick nested-IRQ
   slack. To go faster *without* wedging you must make `clkint` **cheaper**
   (or move to KVM, where the real INT line wakes the CPU every PIT edge with
   no TB-boundary coalescing) — not just inject harder.
3. **The cabinet wants neither slow nor fast — it wants `drift ≈ 1.000`** (wall
   = game time) so solenoids/lamps land in real wall time. The "fast" regime is
   a *diagnostic* showing headroom exists; for a real board you'd pair more
   delivery with the `--realtime`-style wall throttle so it lands exactly at
   1.0, not above. That is the "both fast AND correct" world: enough delivery
   to reach 1.0×, then pinned to wall — never the free-running >1× that made
   the old runs race.

## Caveats

- Every "fast" lever here is **diagnostic, default-OFF, and unstable/headless**
  — none is cabinet config. See the quarantine list in
  [docs/47](../../47-recommended-configuration.md).
- `scale` (`vtime/wall`) stays 1.000× in all current-QEMU runs because
  `QEMU_CLOCK_VIRTUAL` is slaved to host time with `icount=off`; the game-clock
  story is told by **`drift`**, not `scale`. (The legacy "AHEAD" `vt-scale` was
  a different, instruction-count clock.)
- Numbers are single ~39 s runs on one host; treat as directional, not
  certified.
