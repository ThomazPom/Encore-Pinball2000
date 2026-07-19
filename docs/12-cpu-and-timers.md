# 12 — CPU and timing

Encore runs one QEMU TCG `486` CPU with 16 MiB of RAM. The game programs a
QEMU i8254 PIT and i8259 PIC, installs its own XINU interrupt handler, and
acknowledges IRQ0 through its normal EOI/IRET path.

Encore provides three IRQ0 delivery modes. Adaptive HOTLOOP-only is the
default because it keeps game time close to real time on modern hosts.

## Runtime modes

| Command | IRQ0 source | Intended use |
|---|---|---|
| `scripts/run-qemu.sh` | Adaptive HOTLOOP-only | Normal play and cabinet testing. |
| `scripts/run-qemu.sh --strict` | Natural i8254 PIT only | Diagnostic comparison without HOTLOOP assistance. |
| `scripts/run-qemu.sh --with-pit` | Adaptive HOTLOOP plus natural PIT | Controlled timing comparison. |

All three modes feed QEMU's i8259 interrupt controller and enter the guest's IDT
handler. The guest acknowledges IRQ0 and returns through its normal path.

## Why HOTLOOP exists

The guest programs PIT channel 0 near 4,004 Hz, or about one edge every
250 microseconds. Under TCG, the i8259 can coalesce PIT edges while the guest is
executing translated blocks. On affected hosts, XINU therefore receives fewer
timer interrupts than intended and commands such as `sleep 10` take too long in
wall time.

HOTLOOP avoids that slowdown by pacing normal i8259 IRQ0 pulses from a
host-wall-clock timer. QEMU's i8259 arbitrates the request,
the x86 CPU enters the guest's IDT vector, and XINU performs its normal EOI and
IRET.

In the default HOTLOOP-only mode, the IRQ0 tap suppresses natural PIT IRQ0
edges so there is one controlled source. The guest still programs and reads the
PIT normally; only delivery of its IRQ0 wire is replaced.

Implementation:

- `qemu/p2k-clkint-hotloop.c` — rate control and IRQ0 re-raise decisions;
- `qemu/pinball2000.c` — PIT IRQ0 tap and HOTLOOP-only suppression;
- `qemu/p2k-timing-audit.c` — observes PIT, IRQ0, handler entry, EOI and drift;
- QEMU upstream hooks installed by `scripts/build-qemu.sh` — call the timing
  observer and retain the legacy TB-boundary HOTLOOP for controlled A/B tests.

## Adaptive rate control

The target at normal speed is approximately 4,003.97 guest `clkint` entries per
second. HOTLOOP measures the achieved rate and adjusts its minimum re-raise gap.
This compensates for host scheduling, display and DCS workload.

The current gap and measured rate appear with `-v`:

```text
p2k-clkint-hotloop ... adaptive=on gap_ns=... measured_hz=...
```

The gap is the controller's minimum interval between IRQ0 raises; it is
separate from the guest PIT divisor.

## Speed target

`--speed-target PERCENT` changes the requested game speed:

```sh
scripts/run-qemu.sh --speed-target 75
scripts/run-qemu.sh --speed-target 100
scripts/run-qemu.sh --speed-target 120
```

In HOTLOOP modes, the percentage scales the adaptive `clkint` target. In
`--strict`, the wrapper scales the PIT input clock presented to the guest.
`100` is the default.

This control scales the game clock, not audio pitch or MediaGX instruction
throughput.

## Measuring correctness

Use the built-in self-diagnostic:

```sh
scripts/run-qemu.sh --bench
scripts/run-qemu.sh --bench --strict
scripts/run-qemu.sh --bench --with-pit
```

The benchmark runs two fresh guests so its measurement mechanisms cannot
contaminate one another:

1. The IRQ pass finds XINU's active IDT and `clkint` handler, replaces its six
   prologue bytes with a jump to a temporary RAM trampoline, and records real
   handler-entry intervals with `RDTSC`. It counts every IRQ but timestamps one
   consecutive pair in sixteen to keep probe cost small. After attaching GDB,
   it resumes, settles, clears the ring and rearms a clean measurement window.
   The original six bytes are restored before the pass exits.
2. The LPT pass boots an unmodified guest without GDB or the IRQ trampoline and
   measures host-side DATA traffic and completed PDB05 frames independently.

Both passes apply the same coin-door, credit and volume-button workload before
10 seconds of guest-time warmup. Use `--bench-long` to retain 30 seconds of
post-workload settling for final validation. The benchmark then reports:

- wall time for the guest command `sleep 10`;
- guest-side IRQ0 delivery, rate and interval distribution;
- LPT DATA rate;
- PDB05 frame gaps.

Probe code and counters live only in unused guest RAM for the duration of the
IRQ pass. Update ROMs, saved data and guest files are never changed. Raw logs,
the assembled probe, memory discovery data and JSON results are retained in the
printed `/tmp/p2k-bench-*` artifact directory.

The IRQ report includes both raw sigma and `core_sigma`. Raw sigma includes
every sampled interval and therefore reacts strongly to a single host stall.
`core_sigma` removes only the slowest 0.1% before calculating sigma; use it
with p99 for steady jitter, while `worst` retains the excluded tail. At the end
of the window the monitor stops the vCPU before GDB reads the counters, so GDB
startup time cannot inflate IRQ delivery.

For normal 100% operation, the most direct check is that XINU `sleep 10` takes
approximately ten wall seconds after warmup. Boot-time cumulative delivery can
be lower without indicating a steady-state problem.

## Interpreting delivery

“IRQ0 delivery” is the ratio of observed XINU `clkint` entries to IRQ0 requests
in the measured window. It is useful only together with game-clock speed:

- A low cumulative value during boot can be harmless.
- A steady-state value near 100% with `sleep 10` near ten seconds means game
  time is correct.
- `--strict` results depend on how the host schedules TCG and can run slower
  than wall time.
- Values above 100% mean the requested mode is overspeeding the guest.

`--bench` uses only the clean guest-side probe window for IRQ results and only
post-warmup rolling windows from the separate LPT pass. It returns `2` when
speed or delivery is unhealthy, or when a steady PDB05 gap exceeds 2.5 ms.

## Jitter and cabinet traffic

IRQ0 jitter describes variation between guest timer-handler entries. PDB05
gaps describe the LPT driver-board frame stream and are more directly relevant
to cabinet communication.

Use `--bench` on the target host to compare clock delivery and LPT gaps. A
physical trace is required to establish cabinet timing limits.

## CPU and MediaGX instructions

The QEMU machine selects a `486` CPU model. Pinball 2000-specific MediaGX
instructions are implemented in TCG and enabled only for the `pinball2000`
machine. This is CPU emulation, not guest-code patching.

The CPU begins at the PRISM protected-mode entry.

Details: [CLI reference](03-cli-reference.md), [boot path](14-boot-recipe.md),
and [LPT board](26-lpt-board.md).

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
