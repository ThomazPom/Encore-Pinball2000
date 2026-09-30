# 12 — CPU, PIT and IRQ0 timing

Encore keeps the original guest clock chain intact. QEMU's i8254 raises IRQ0,
the i8259 arbitrates it, the x86 CPU accepts vector `0x20`, and XINU executes
its `clkint` handler. There is no second clock source, synthetic IRQ injector
or backlog-driven catch-up loop in the normal machine.

```text
PIT channel 0       master PIC          x86 CPU              XINU
divisor 298     →   IRQ0 / vector 20h → interrupt gate   →   clkint
~4003.97 Hz             │                    │                  │
                        │                    │                  └─ one fixed
                        │                    │                     guest tick
                        │                    └─ intack / IRET observed
                        └─ rising edges and EOI observed
```

> [!IMPORTANT]
> A fast host CPU does not guarantee timely interrupt delivery by itself.
> QEMU must leave translated guest code and poll device/interrupt state near
> the PIT deadline. Encore's default rendezvous improves that scheduling
> opportunity without creating, replaying or accelerating guest ticks.

For the surrounding machine structure, start with
[Architecture](10-architecture.md). For user-facing switches and benchmark
syntax, see the [CLI reference](03-cli-reference.md).

## The normal clock path

SWE1 programs channel 0 with divisor 298. With the i8254 input clock this is
approximately `1193182 / 298 = 4003.97 Hz`, or one edge every 249.75 µs.
Encore uses QEMU's upstream i8254 and dual i8259 models:

1. the i8254 changes its channel-0 output;
2. a read-only Encore tap counts each rising edge and forwards the level
   immediately to master-PIC IRQ0;
3. the i8259 asserts the CPU interrupt input when its mask, request and
   in-service state allow it;
4. the CPU acknowledges vector `0x20`, pushes the interrupt frame and enters
   the handler recorded in IDT slot `0x20`;
5. XINU's handler acknowledges the PIC, advances its clock once and returns
   with `IRET`.

The PIC is edge triggered. Multiple PIT edges that arrive while IRQ0 is still
pending or in service do not become an unlimited queue of future interrupts.
That is why `irq0_raised` and `clkint_entered` are intentionally separate
counters, and why expected PIT edges alone are not delivery proof.

Each delivered `clkint` advances XINU by one fixed nominal 4003.97 Hz tick.
The handler does not receive the elapsed host delay and does not turn one late
entry into several clock advances.

`--strict` remains accepted for command-line compatibility. It changes no
setting: the natural hardware path is already the only production path.

## Default PIT-deadline rendezvous

After an IRQ0 handler returns, the timing module projects the next PIT edge
from the latest observed raise plus the programmed PIT period. It arms a
`QEMU_CLOCK_VIRTUAL` timer for that point. When the timer fires it calls
`cpu_exit()` for CPU0, causing the vCPU to leave its current TCG execution
chain and let QEMU poll timers and interrupt state again.

If the projected deadline is already past, the timer is aimed half a PIT
period beyond the current virtual time. This avoids scheduling a timer in the
past; it is not repayment of missed guest time.

| Property | Default behavior |
|---|---|
| IRQ source | upstream i8254 channel 0 |
| interrupt controller | upstream i8259 |
| rendezvous trigger | completion of an IRQ0 `IRET` |
| rendezvous action | request a return from TCG with `cpu_exit()` |
| synthetic edge or direct handler call | never |
| backlog or adaptive acceleration | none |
| change to the guest tick amount | none |

> [!NOTE]
> The rendezvous corrects a host scheduling opportunity, not guest clock
> debt. There is therefore no positive-feedback rule that sees a late guest,
> raises its IRQ rate, creates more work and raises the rate again.

`P2K_NO_IRQ0_PIT_DEADLINE_TIMER=1` disables this timer for internal A/B
testing. `P2K_NO_TIMING_AUDIT=1` disables the complete timing module,
including the rendezvous. Neither is a supported play mode; both exist to
isolate regressions.

## Deliberate speed targets

`--speed-target PERCENT` is independent of the rendezvous. The launcher
accepts 25 through 300 and passes the requested value to the machine. Encore's
narrow upstream PIT hook scales only the channel-0 divisor:

```text
scaled divisor = programmed divisor × 100 / requested percent
```

At 100%, the value is unchanged. At 75%, IRQ0 is deliberately slower; at
120%, it is deliberately faster. The result still travels through the full
i8254 → i8259 → CPU → XINU path. Other QEMU machines retain the hook's weak
identity implementation.

This option controls game-clock speed. It is not an automatic host-load
compensator and is not enabled by the benchmark.

## CPU scope

The machine runs one QEMU TCG `486` CPU with 16 MiB RAM. Its reset recipe
enters the PRISM option ROM directly in protected mode. Pinball 2000-specific
MediaGX instructions are implemented in TCG and enabled only while the
`pinball2000` machine is active; this is CPU emulation, not a guest patch.

The entry registers and memory layout belong in the
[boot recipe](14-boot-recipe.md) and [memory map](13-memory-map.md).

## What the permanent hooks observe

The custom QEMU build adds narrow observation points around the upstream
execution path:

| Observation | Meaning |
|---|---|
| PIT rising edge | IRQ0 request reached the master PIC input |
| x86 interrupt acknowledgement | CPU accepted vector `0x20`, before its frame is pushed |
| translated handler entry | execution reached the active IDT `0x20` target |
| master-PIC EOI | handler cleared IRQ0 in-service state |
| protected-mode `IRET` | one observed handler invocation returned |
| first TB after `IRET` | diagnostic split of scheduler return versus later guest execution |
| PDB opcode `0x05` completion | driver-board refresh cadence in wall and virtual time |

The handler-entry helper is generated only when the translator sees the live
IDT target. Weak defaults let ordinary upstream machines build and behave
unchanged.

Normal play does not create the periodic report timer, sort latency rings,
emit timing reports or collect PDB-gap histories. It retains only the small
counter state and timer needed for the rendezvous. `--timing-snapshots` and
`--diag` opt into more work.

## Reading the counters

The three principal counts answer different questions:

- `irq0_raised`: rising PIT edges presented to the PIC;
- `clkint_entered`: executions of the active XINU IRQ0 handler;
- `eoi_seen`: master-PIC EOI operations attributed to IRQ0.

`delivery` is `clkint_entered / irq0_raised` since collection began;
`current_delivery` is the same ratio over the latest reporting window.
`irq0_edges_pit_expected`, derived from virtual elapsed time and the
programmed divisor, is only a PIT sanity estimate.

`clkint_depth` is the number of currently active observed handler entries.
`max_clkint_depth` and `nested_clkint` expose re-entry: depth 1 means a normal
single handler, while depth above 1 proves nesting during the observation
window.

> [!WARNING]
> A good average delivery percentage cannot prove stack safety. Always retain
> maximum nesting depth and minimum IStack margin when investigating the rare
> overflow failure.

## IStack precursor observation

`--irq0-stack-trace` samples `ESP` at IRQ0 acknowledgement, immediately before
the CPU pushes the interrupt frame. XINU process stacks are treated as 8 KiB
regions in the observed `0x00200000–0x003fffff` range. The acknowledgement hot
path only updates fixed-size memory. It performs no formatting, logging, file
I/O or guest-memory dump. The three-second timing report and exit report emit
the cumulative sample count and smallest margin as `stack_samples` and
`min_stack_margin`.

This split is a correctness requirement, not just an optimization. Synchronous
record-low logging from the acknowledgement hook was observed to lengthen an
already nested handler, provoke still deeper records and form a measurement
feedback loop. The controlled reproduction is preserved in the
[Game Over IRQ0 experiment](measurements/2026-09-29-gameover-irq0.md).
The later live failure caused by the old full periodic report is documented in
[IRQ0 runaway: diagnostic observer effect](measurements/2026-09-30-irq0-observer-effect.md).

Related controls are:

| Option | Purpose |
|---|---|
| `--irq0-stack-trace` | enable record-low margin sampling and summary fields |
| `--irq0-stack-guard ADDR` | restrict the sampled minimum to one guard |
| `--irq0-stack-dump FILE` | schedule a deferred 8 KiB dump after the margin reaches 128 bytes or less |

The self-diagnostic enables the trace automatically and reports the smallest
margin it observed. `n/a` means no in-arena acknowledgement was sampled; it
must not be reported as infinite margin.

## Supported self-diagnostic

Run the normal two-pass diagnostic with:

```bash
scripts/run-qemu.sh --bench --game swe1
```

It always adds `--no-savedata` and uses the normal windowed display and audio
defaults unless the caller explicitly chooses otherwise. The default uses 10
guest seconds of warmup; `--bench-long` changes warmup to 30 guest seconds but
does not lengthen the measured window. Before each warmup, the harness applies
the same short cabinet-key workload so those disturbances drain before the
measurement begins.

The two passes deliberately keep measurement concerns separate:

1. the IRQ pass locates the live IDT, verifies the known `clkint` prologue,
   installs a temporary RAM probe, waits after GDB disturbance, measures a
   guest `sleep 10`, then restores the original bytes;
2. a new unpatched guest uses lightweight three-second snapshots to measure
   LPT/PDB behavior without the RAM probe.

The probe counts actual handler entries. It timestamps one consecutive pair
out of every 16 entries to retain real single-IRQ intervals with low overhead.
The second pass requests the bounded snapshot explicitly and does not enable
the unrelated diagnostic samplers. Complete timing-ring sorting is deferred to
shutdown so it cannot create the tail being measured.

The report contains:

- wall time for XINU `sleep 10` and effective requested speed;
- actual handler rate, delivery, mean, sigma, core sigma, percentiles and
  worst interval;
- maximum `clkint` depth and minimum sampled IStack margin;
- LPT DATA and PDB05 rates;
- PDB05 p50/p95/p99, worst gap and complete-window distribution;
- DCS health when a live ADSP engine is selected.

### Verdict

The benchmark returns status 2 and `ABNORMAL` if any of these hold:

- IRQ delivery is outside 95–105%;
- effective speed is outside ±5% of the requested target;
- mean steady-window PDB p99 exceeds 1 ms;
- gaps above 2.5 ms appear in at least 10% of complete windows, with a minimum
  of two affected windows.

One non-repeated gap above 2.5 ms is a warning. Gaps above 10 ms are also
reported as warnings; they do not replace the distribution rule. A passing
emulator run is evidence for that host, build, guest and observation window,
not proof of physical-cabinet safety.

The command prints and preserves its temporary artifact directory, including
`results.json`, `metadata.json`, both pass logs and `report.md`. The RAM probe
is removed before the first pass exits, and `--no-savedata` prevents either
pass from writing the persistent device images.

## Deeper diagnostics

Use these only to answer a specific question; they have different costs and
must not be mixed blindly into headline performance results.

| Interface | Cost and purpose |
|---|---|
| `--timing-snapshots` | lightweight three-second fields used by the benchmark |
| `--diag` or `-v` | bounded three-second timing snapshots plus the complete report at exit |
| `P2K_PROFILE_STALLS=1` | classify raised-versus-serviced deficits as guest `IF=0`, PIC mask/in-service, halted, TB-delay or other; default deficit threshold 2 |
| `P2K_PROFILE_PDB_GAPS=1` | retain up to 64 rare PDB-gap events and dump them only at exit |
| `P2K_DIAG_ALWAYS_NOCHAIN=1` | forbid TCG TB chaining for a diagnostic experiment; never a play mode |
| `P2K_DUMP_CLKINT=PATH` | one-shot dump of the observed handler for disassembly |
| `P2K_GUEST_CLOCK_ADDRS=...` | compare selected guest counters with wall time |

The PDB-gap profiler reads a clock at every TB. It records wall/virtual time,
thread and process CPU consumption, IRQ/LPT/display deltas, CPU/PIC state and
dominant/slow translated blocks around qualifying events. That overhead is
why its results diagnose a gap but do not replace a clean benchmark.

Full diagnostics divide an IRQ cycle into raise→intack, intack→entry,
entry→EOI, EOI→IRET and IRET→next raise. The last segment is further split at
the first translated block after IRET. These measurements localize delay; they
do not alter the delivery decision.

## Interpretation checklist

When comparing a timing change, keep the build, game, update, savedata policy,
display, audio engine and host workload fixed, then record:

1. exact commit and complete command;
2. warmup and measured durations;
3. effective speed and actual IRQ delivery distribution;
4. maximum nesting depth and minimum IStack margin;
5. PDB percentiles, worst gap and number of affected windows;
6. whether any profiler, verbose report or guest probe was active;
7. whether evidence came from an emulator desktop run or powered cabinet.

Do not select a timing mode from one maximum alone. A change is stronger only
when it preserves speed and delivery, does not trade them for worse nesting or
stack margin, and improves the complete PDB distribution under the same test.

---

← [Architecture](10-architecture.md) ·
[Documentation index](README.md) · [Boot recipe](14-boot-recipe.md)
