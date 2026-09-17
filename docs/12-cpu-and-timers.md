# 12 — CPU and timing

Encore runs one QEMU TCG `486` CPU with 16 MiB of RAM. The game programs a
QEMU i8254 PIT, the PIT drives IRQ0 through QEMU's i8259 PIC, and XINU enters
and acknowledges its own interrupt handler through the normal EOI/IRET path.

This is Encore's only IRQ0 delivery mechanism. `--strict` remains accepted as
a compatibility alias, but it selects no alternate mode because natural
i8254/i8259 delivery is unconditional.

## Why there is only one IRQ0 path

Earlier versions offered synthetic HOTLOOP sources paced at host or translated
block boundaries. They were useful while diagnosing timer delivery, but they
duplicated the emulated hardware and created multiple timing authorities. The
adaptive host version could also turn a temporary guest slowdown into positive
feedback: fewer completed handlers caused a faster source, deeper nesting and
still less available CPU and stack.

The production result is deliberately simpler:

```text
i8254 channel 0 -> i8259 IRQ0 -> x86 interrupt entry -> XINU clkint -> EOI/IRET
```

There is no alternate source, guest-feedback controller, catch-up clock,
deadline recovery, swallowed PIT edge, or automatic timing fallback.

The optional `--irq0-stack-trace` observer records nested-handler depth and
record-low XINU process-stack margin before interrupt entry. It never delays,
suppresses, injects or reschedules an IRQ. `--irq0-stack-guard` can restrict
the trace to one stack, and `--irq0-stack-dump` captures an 8 KiB image only
if observed margin reaches 128 bytes.

## Speed target

`--speed-target PERCENT` deliberately changes the requested game speed:

```sh
scripts/run-qemu.sh --speed-target 75
scripts/run-qemu.sh --speed-target 100
scripts/run-qemu.sh --speed-target 120
```

The wrapper passes the percentage to the machine and the machine scales only
the i8254 channel-0 divisor. The complete PIT/PIC/CPU/guest path remains the
same. `100` is the default.

This control scales the XINU game clock, not audio pitch or MediaGX instruction
throughput.

## Measuring correctness

Use the built-in self-diagnostic:

```sh
scripts/run-qemu.sh --bench
```

The benchmark runs two fresh guests so its measurement mechanisms cannot
contaminate one another:

1. The IRQ pass finds XINU's active IDT and `clkint` handler, replaces its six
   prologue bytes with a jump to a temporary RAM trampoline, and records real
   handler-entry intervals with `RDTSC`. It counts every IRQ but timestamps one
   consecutive pair in sixteen to keep probe cost small. The original bytes
   are restored before the pass exits.
2. The LPT pass boots an unmodified guest without GDB or the IRQ trampoline and
   measures host-side DATA traffic and completed PDB05 frames independently.

Both passes apply the same coin-door, credit and volume-button workload before
10 seconds of guest-time warmup. Use `--bench-long` to retain 30 seconds of
post-workload settling for final validation. The benchmark reports:

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
with p99 for steady jitter, while `worst` retains the excluded tail.

For normal 100% operation, the most direct check is that XINU `sleep 10` takes
approximately ten wall seconds after warmup. Boot-time cumulative delivery can
be lower without indicating a steady-state problem.

## Interpreting delivery

“IRQ0 delivery” is the ratio of observed XINU `clkint` entries to IRQ0 requests
in the measured window. It is useful only together with game-clock speed:

- A low cumulative value during boot can be harmless.
- A steady-state value near 100% with `sleep 10` near ten seconds means game
  time is correct.
- Values above 100% mean the requested speed is overshooting.

`--bench` uses only the clean guest-side probe window for IRQ results and only
post-warmup rolling windows from the separate LPT pass. It returns `2` when
speed or delivery is unhealthy, or when a steady PDB05 gap exceeds 2.5 ms.

## Jitter and cabinet traffic

IRQ0 jitter describes variation between guest timer-handler entries. PDB05
gaps describe the LPT driver-board frame stream and are more directly relevant
to cabinet communication. A physical trace is required to establish cabinet
timing limits.

## CPU and MediaGX instructions

The QEMU machine selects a `486` CPU model. Pinball 2000-specific MediaGX
instructions are implemented in TCG and enabled only for the `pinball2000`
machine. This is CPU emulation, not guest-code patching.

The CPU begins at the PRISM protected-mode entry.

Details: [CLI reference](03-cli-reference.md), [boot path](14-boot-recipe.md),
and [LPT board](26-lpt-board.md).

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
