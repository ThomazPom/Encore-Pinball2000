# 47 — Recommended Configuration and Runtime Matrix

This is the canonical map of Encore's supported runtime configurations. It
separates four choices that older timing notes sometimes mixed together:

1. game: `swe1` or `rfm`;
2. presentation: graphical or headless;
3. IRQ0 delivery: default HOTLOOP-only, strict natural PIT, or the legacy
   HOTLOOP+PIT comparison mode;
4. LPT destination: emulated driver board or an experimental real parallel
   port.

## Golden paths

Desktop play, with graphics and audio:

```bash
./scripts/run-qemu.sh --game swe1
./scripts/run-qemu.sh --game rfm
```

Automated/headless testing:

```bash
./scripts/run-qemu.sh --game swe1 --headless --no-audio -v
./scripts/run-qemu.sh --game rfm  --headless --no-audio -v
```

Real-cabinet experiments should currently use natural PIT delivery:

```bash
./scripts/run-qemu.sh --game swe1 --strict \
  --cabinet-purist --lpt-device /dev/parport0 --lpt-trace lpt.log -v
```

`--strict` matches the cabinet-verified ~16–18 kHz LPT range, but its guest
clock advances at only about 0.4× wall time under TCG. Cabinet support remains
experimental; read [29 — Cabinet Testing Call](29-cabinet-testing-call.md) and
[46 — Real LPT Passthrough](46-real-lpt-passthrough.md) first.

## Complete operating-mode matrix

Both games and both presentation modes are product goals. Display selection
does not select a different emulated machine or timer implementation; it only
changes whether QEMU presents the framebuffer. After HOTLOOP's adaptive
controller settles, headless and graphical runs should converge on the same
guest-clock target only when the host can execute at least ~4,004 guest clock
handlers per second. The controller has a 50 µs minimum gap but cannot make a
host exceed its TCG/guest-handler throughput ceiling. Check `current_delivery`
and `measured_hz`; a controller pinned at 50 µs below 4 kHz is host-limited.

| Presentation | IRQ0 mode | Command addition | Expected guest clock | Typical emulated LPT DATA | Intended use | Status |
|---|---|---|---:|---:|---|---|
| Graphical | HOTLOOP-only | none (default) | ~1.0× wall | ~40–43 k/s | normal desktop play | **recommended** |
| Headless | HOTLOOP-only | `--headless` | ~1.0× wall | ~40–43 k/s | CI, smoke tests, measurement | **recommended** |
| Graphical | natural PIT only | `--strict` | ~0.4× wall | ~16–18 k/s | cabinet-rate comparison | supported, slow |
| Headless | natural PIT only | `--headless --strict` | ~0.4× wall | ~16–18 k/s | hardware-fidelity regression | supported, slow |
| Graphical | HOTLOOP + PIT | `--with-pit` | ~1.0× wall | ~40–43 k/s | timer-source A/B comparison | diagnostic |
| Headless | HOTLOOP + PIT | `--headless --with-pit` | ~1.0× wall | ~40–43 k/s | timer-source A/B comparison | diagnostic |

Every row applies to both `swe1` and `rfm`. Update versions can change boot
workload and exact rates, so measurements must always record the game and
update version. The current wrapper uses the same 145 µs initial HOTLOOP gap
for every graphical/headless and PIT/no-PIT combination; explicit
`P2K_TCG_CLKINT_HOTLOOP_MIN_GAP_NS` remains a diagnostic override.

### What was actually validated after the memory-timing fix

Fresh-state, sequential, 145 µs smoke tests on 2026-07-11:

| Game | Presentation | IRQ0 mode | Runs | Result |
|---|---|---|---:|---|
| RFM v2.60 | headless | HOTLOOP-only | 3 | 3/3 healthy |
| RFM v2.60 | headless | HOTLOOP + PIT | 10 | 10/10 healthy |
| SWE1 v2.10 | headless | HOTLOOP-only | 3 | 3/3 healthy |
| SWE1 v2.10 | headless | HOTLOOP + PIT | 3 | 3/3 healthy |

These are boot-smoke results, not long gameplay soaks or cabinet proof.
Graphical rate figures in the operating matrix come from earlier SDL
measurements; the post-fix regression sample above was headless. This
distinction is deliberate: supported intent, measured coverage, and physical
cabinet validation are different claims.

## What the modes mean

### Default: HOTLOOP-only

HOTLOOP is Encore's default TCG compatibility path. It is the sole IRQ0 source;
natural i8254 IRQ0 edges are swallowed. An adaptive controller targets the
firmware's nominal ~4 kHz clock-handler cadence. This gives approximately
real-time gameplay on desktop and headless runs.

The trade-off is LPT rate. A modern TCG CPU executes more driver-board loop
iterations per delivered tick than the original MediaGX, producing roughly
40–43 k DATA writes/s. That is above the 25 kHz PinballDiag-tested band and has
not been proven on a physical cabinet.

### `--strict`: natural PIT only

`--strict` disables HOTLOOP. QEMU's natural i8254→i8259 path delivers only
about 40–43% of the expected guest clock interrupts on the measured host, so
the game runs around 2.4× slow. Its ~16–18 k DATA/s output is closest to the
cabinet-verified LPT cadence, making it the conservative physical-board mode.

### `--with-pit`: legacy comparison mode

`--with-pit` keeps both HOTLOOP and the natural PIT source. It is useful for
A/B experiments but provides no known user-facing advantage over default

The adaptive controller allows a much larger maximum HOTLOOP gap in this
combined mode. If natural PIT delivery already approaches 4 kHz, HOTLOOP backs
off to an effectively dormant cadence instead of remaining pinned at a 300 µs
gap and overclocking XINU. On a slower host it still converges to the shorter
gap required to fill the measured PIT delivery deficit.

Older documentation claimed this mode wedged most boots and assigned special
250–700 µs initial gaps. That conclusion was wrong. The suppressed UART hid a
`price_init` allocation Fatal caused by the `mem_detect` patch running after
heap initialization; XINA then entered its monitor and appeared frozen. After
moving the memory rewrite before the first `mem_detect()` call, the full matrix
uses 145 µs and RFM headless+PIT passed 10/10 sequential fresh-state boots.

## How to read the measurements

Do not reduce emulator correctness to one number:

`--speed-target <25..300>` is an intentional pacing override. A value of 100
is the physical-cabinet clock. Strict mode scales only the real i8254 divisor;
HOTLOOP-only scales only its adaptive target; combined mode scales both. Above
100%, strict mode can fall slightly short when PIC delivery coalesces faster PIT
edges, while either adaptive HOTLOOP mode compensates within host capacity.

| Metric | Meaning | Desired interpretation |
|---|---|---|
| `p2k-xinu-drift` | guest clock advance / wall time | ~1.0 for normal play |
| `p2k-timing delivery` | accepted `clkint` entries / raised IRQ0 edges | health diagnostic, mode-dependent |
| `p2k-lpt-hz data/s` | guest DATA-port writes per wall second | ≥16 k/s; physical safety above 25 k/s unproven |
| `p2k-lpt-hz ctrl/s` | control strobes per wall second | protocol-shape companion to DATA |
| `p2k-lpt-hz dispatch/s` | decoded driver-board operations per wall second | should scale consistently with DATA |
| `p2k-pdb05` | lamp/data refresh gap distribution | watch p99/max, not only averages |
| `p2k-fps` | submitted display frames | presentation health, not guest-clock speed |

`scale=1.000x` only says QEMU virtual time follows host wall time. It does not
say that XINU received every clock interrupt. Use `p2k-xinu-drift` for game
speed and the LPT bundle for driver-board traffic.

## Display is a separate axis

`--headless` means `-display none`; the firmware and emulated display hardware
still run. A zero `p2k-fps` value under `-display none` does not by itself mean
the guest is wedged. Check advancing `clkint_entered`, nonzero interval LPT
rates, drift, and the absence of a guest Fatal.

For graphical validation, confirm both image output and timing. A screen that
animates does not prove correct game-clock speed, and healthy timing counters
do not prove the RGB555 conversion or blitter output is visually correct.

## Real LPT caveat

With `--lpt-device /dev/parport0`, the passthrough path returns before the
emulated LPT counters are updated. Consequently `p2k-lpt-hz` and `p2k-pdb05`
can show zero or stale values even while the real port is active. Validate in
two stages:

1. run the same configuration with `--lpt-device emu` and inspect the KPI
   panels;
2. run the cabinet with `--lpt-trace`, a scope if available, and observation
   of the actual driver board.

## Recommendation summary

| Goal | Configuration |
|---|---|
| Play either game on a desktop | graphical default HOTLOOP-only |
| Automated boot/regression test | headless default HOTLOOP-only |
| Compare against natural QEMU PIT behavior | `--strict` |
| Compare single-source and dual-source timer delivery | `--with-pit` |
| Experimental physical cabinet | `--strict --cabinet-purist --lpt-device ...` |

See [26 — LPT Driver Board](26-lpt-board.md) for protocol/rate evidence and
[12 — CPU and Timers](12-cpu-and-timers.md) for the interrupt-delivery model.
