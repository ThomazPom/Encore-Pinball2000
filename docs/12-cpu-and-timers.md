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
| `scripts/run-qemu.sh --with-pit` | Adaptive HOTLOOP plus natural PIT | Legacy comparison mode. |

All three modes use QEMU's real i8259 interrupt controller and the guest's real
IDT handler. Encore does not push an interrupt frame, rewrite XINU timer
variables, patch the scheduler, or modify EOI/IRET behavior.

## Why HOTLOOP exists

The guest programs PIT channel 0 near 4,004 Hz, or about one edge every
250 microseconds. Under TCG, the i8259 can coalesce PIT edges while the guest is
executing translated blocks. On affected hosts, XINU therefore receives fewer
timer interrupts than intended and commands such as `sleep 10` take too long in
wall time.

HOTLOOP avoids that slowdown by checking delivery at TCG block boundaries and
re-raising QEMU's IRQ0 input when the CPU can accept another interrupt. It does
not call the guest handler directly. QEMU's i8259 still arbitrates the request,
the x86 CPU enters the guest's IDT vector, and XINU performs its normal EOI and
IRET.

In the default HOTLOOP-only mode, the IRQ0 tap suppresses natural PIT IRQ0
edges so there is one controlled source. The guest still programs and reads the
PIT normally; only delivery of its IRQ0 wire is replaced.

Implementation:

- `qemu/p2k-clkint-hotloop.c` — rate control and IRQ0 re-raise decisions;
- `qemu/pinball2000.c` — PIT IRQ0 tap and HOTLOOP-only suppression;
- `qemu/p2k-timing-audit.c` — observes PIT, IRQ0, handler entry, EOI and drift;
- QEMU upstream hooks installed by `scripts/build-qemu.sh` — call the observer
  and HOTLOOP check at TCG execution boundaries.

## Adaptive rate control

The target at normal speed is approximately 4,003.97 guest `clkint` entries per
second. HOTLOOP measures the achieved rate and adjusts its minimum re-raise gap.
This compensates for host scheduling, display and DCS workload without using a
fixed machine-specific delay.

The current gap and measured rate appear with `-v`:

```text
p2k-clkint-hotloop ... adaptive=on gap_ns=... measured_hz=...
```

The gap is an internal controller value, not the guest PIT divisor and not a
recommended manual tuning knob.

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

This is a deliberate user-facing speed control. It is separate from host CPU
frequency and does not attempt to emulate the MediaGX instruction throughput
cycle by cycle.

## Measuring correctness

Use the built-in self-diagnostic:

```sh
scripts/run-qemu.sh --bench
scripts/run-qemu.sh --bench --strict
scripts/run-qemu.sh --bench --with-pit
```

The benchmark separates boot/warmup from steady state and reports:

- wall time for the guest command `sleep 10`;
- measured game-clock speed;
- current IRQ0 delivery and counts;
- adaptive HOTLOOP gap and measured frequency;
- IRQ0 jitter;
- LPT DATA rate;
- PDB05 frame gaps.

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

`--bench` uses steady-state windows for its final result.

## Jitter and cabinet traffic

IRQ0 jitter describes variation between guest timer-handler entries. PDB05
gaps describe the LPT driver-board frame stream and are more directly relevant
to cabinet communication.

Encore currently has no evidence that CPU throttling or per-access LPT pacing
is required. Do not add sleeps, busy-waits or `-icount` merely because TCG can
execute instructions faster than the original MediaGX. Open pacing work only
if a physical-board trace demonstrates a timing failure.

Likewise, do not choose `--strict` for a cabinet merely because it uses the
natural PIT path. The practical cabinet mode is the one that produces correct
game time and works reliably with the physical board; that must be established
by measurement.

## CPU and MediaGX instructions

The QEMU machine selects a `486` CPU model. Pinball 2000-specific MediaGX
instructions are implemented in TCG and enabled only for the `pinball2000`
machine. This is CPU emulation, not guest-code patching.

The reset recipe deliberately begins at the known PRISM protected-mode entry.
It is documented separately in [14-boot-recipe.md](14-boot-recipe.md) and is
not part of timer delivery.

## Removed experiments

Current Encore does not contain the former:

- direct guest `clkint` dispatch;
- INJECT/NOCHAIN modes;
- missed-PIT-edge replay;
- `prnull` or `nulluser` HLT rewrites;
- scheduler tick injection;
- prime-count boot delays;
- starvation valves or timing wedge detectors.

They are not runtime options and should not be used to explain current Encore
behavior.

## See also

- [03-cli-reference.md](03-cli-reference.md)
- [14-boot-recipe.md](14-boot-recipe.md)
- [26-lpt-board.md](26-lpt-board.md)
- [35-known-limitations.md](35-known-limitations.md)
- [47-recommended-configuration.md](47-recommended-configuration.md)

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
