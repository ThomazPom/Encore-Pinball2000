# Runtime CPU shield validation

Date: 2026-10-01

Branch: `feature/runtime-cpu-shield`

Game/update: SWE1 2.00

Timing mode: natural i8254 → i8259 (`--strict` compatibility alias), 100%
speed, normal bench workload

## Verdict

The selected host mitigation is a temporary three-physical-core QEMU pool.
Ordinary user and systemd work is confined to the remaining physical core
while QEMU exists. Only one logical CPU from each reserved physical core is
usable by QEMU; its SMT sibling stays idle. Linux schedules all QEMU threads
inside that pool instead of Encore hard-pinning the synchronized main, TCG and
DCS roles.

This arrangement preserves normal guest speed and keeps IRQ0 nesting bounded
under the deliberately excessive 12-worker host load. The final integrated
run passes every IRQ, PDB and IStack criterion: there is no IRQ-depth growth
and the IStack retains about 7 KiB of margin.

> [!IMPORTANT]
> The shield mitigates host starvation; it does not change PIT deadlines,
> suppress interrupts, add a guest guard, or accelerate the guest clock.

## Comparable results

All rows use the generic two-pass `--bench`, normal game workload, SWE1 2.00
and no speed target. The loaded rows use twelve `yes` workers. In the final
integrated run those workers began unrestricted: the root broker itself moved
their `user.slice` to `0,4`, while unprivileged QEMU ran on `1,2,3`.

| Variant | Host load | Speed | IRQ delivery | IRQ p99 / worst | Max depth | Min IStack margin | PDB p99 / worst | PDB >2.5 ms | Verdict |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| no shield | none | 100.05% | 99.99% | 320 / 401 us | 5 | 7076 B | 305 us / 1.58 ms | 0/4 | PASS |
| final integrated 3-core pool | none | 99.72% | 99.66% | 289 / 439 us | 5 | 7000 B | 291 us / 2.13 ms | 0/4 | PASS |
| final integrated 3-core pool | 12 initially unrestricted workers | 99.57% | 99.58% | 283 us / 1.46 ms | 5 | 7076 B | 294 us / 1.64 ms | 0/4 | PASS |
| no shield | 12 unrestricted workers | 58.69% | 58.68% | 4.90 / 28.36 ms | 7 | 6924 B | 4.84 / 21.26 ms | 6/6 | ABNORMAL |
| hard-pin main / TCG / DCS | none | 94.77% | 94.72% | 1.22 / 2.09 ms | 4 | 7000 B | 1.29 / 2.53 ms | 1/5 | ABNORMAL |

The loaded final run is the decisive comparison with the loaded no-shield
row: effective speed improves from 58.69% to 99.57%, IRQ worst-case from
28.36 ms to 1.46 ms, and PDB worst-case from 21.26 ms to 1.64 ms. Depth stays
at 5 and the minimum sampled IStack margin is 7076 B.

## Rejected layouts

- Reserving TCG and DCS while leaving the QEMU main loop in the host pool can
  be fast when idle, but under saturation it reached depth 207 with only 88 B
  of sampled IStack margin. Raising its nice priority did not prevent this.
- Hard-pinning main, TCG and DCS to separate physical cores bounded depth, but
  repeatedly delivered only about 94.7% real time. Moving TCG to the least
  interrupted physical core did not change the result.
- Giving main+TCG a two-core pool and DCS a third fixed core also remained at
  about 94.5% and reached depth 20 in an unloaded run.
- Giving every QEMU thread only a two-physical-core pool previously hung with
  depth 151 and 72 B of margin. The selected pool therefore uses three
  physical cores when topology permits.
- Reserving logical CPUs without excluding their SMT siblings allowed hostile
  work on the sibling to steal the same physical execution resources.
- Moving hardware IRQ affinities made the normal benchmark slower and was
  reverted. IRQ affinity is not modified by the final design.

## Frequency finding

This Intel `intel_pstate` host uses the `powersave` governor with a 400 MHz
minimum. Once work was isolated, an early run stayed near that floor and
delivered only 70.8% real time. Temporarily selecting the available
`performance` governor recovered the expected rate. The broker therefore
snapshots and changes only policies covering reserved physical cores, then
restores their exact original governors with the cgroup masks.

## Integration and failure tests

`tools/debian-qemu/lab.sh test-shield` passes on the fresh Debian 13 minimal
image. It verifies:

- the root-owned, socket-activated broker and `0600` per-user socket;
- an unprivileged QEMU child inside the broker service cgroup;
- QEMU and host CPU masks;
- exact `AllowedCPUs` restoration after a normal exit;
- exact restoration after `SIGKILL` of QEMU;
- an idle, reusable socket after cleanup.

The final source also passes 24 Python unit tests, all 24 supported ROM-set
checks, Python byte compilation, shell syntax checks and `git diff --check`.

## Local artifacts

| Run | Artifact directory |
|---|---|
| no-shield baseline | `/tmp/p2k-bench-oezyaqyt` |
| final integrated pool, normal | `/tmp/p2k-bench-0fa74f6h` |
| final integrated pool, loaded | `/tmp/p2k-bench-vswbnew2` |
| hard-pin roles | `/tmp/p2k-bench-ya3xv2e2` |
| no shield, loaded | `/tmp/p2k-bench-_40gfpno` |
| near-overflow main-in-host-pool run | `/tmp/p2k-bench-t9y5unyl` |

These `/tmp` paths are working artifacts rather than release inputs. The
numbers needed to preserve the conclusion are recorded above.

The causal host-starvation captures and their persistent hashes remain in
[IRQ0 runaway: host-starvation precursor](2026-10-01-irq0-host-starvation.md).
