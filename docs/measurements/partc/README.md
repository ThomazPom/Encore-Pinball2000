# Part C measurements: TCG-side translator experiments

40-second runs, replay OFF, `P2K_DIAG=1`, default Part-B strategies on
(`P2K_NO_IRQ0_IRET_TB_EXIT` and `P2K_NO_IRQ0_PIT_DEADLINE_TIMER`
unset → ON). All Part-C strategies are positive opt-in.

Run with:

```sh
bash docs/measurements/partc/run-matrix.sh <tag> [ENV=VAL ...]
bash docs/measurements/partc/extract-metrics.sh docs/measurements/partc/<tag>_*.log
```

`run-matrix.sh` exercises three scenarios:

* `swe1_headless` — `--game swe1 --headless` (auto verbosity ≥1)
* `rfm_headless`  — `--game rfm  --headless`
* `swe1_sdlpa`    — `--game swe1 --display sdl --audio pa -v` (manual `-v`
  needed; SDL does not auto-bump verbosity, and `run-qemu.sh` strips
  `info:` / `warning:` lines below verbosity 1)

## Strategies

| Strategy | Switch | Default | Effect |
|---|---|---|---|
| 4 | `P2K_IRQ0_IRET_WINDOW=1` | OFF | OR `CF_NO_GOTO_TB \| CF_NO_GOTO_PTR` (and optionally cap `CF_COUNT_MASK` via `P2K_IRQ0_IRET_WINDOW_MAX_INSNS`) into the cflags used by `cpu_exec_loop` for `P2K_IRQ0_IRET_WINDOW_NS` ns of vtime after every IRQ0 IRET. |
| 5 | `P2K_IDLE_LOOP_BREAKER=1` | OFF | i386 translator detects `eb fe` (jmp-to-self) at TB-start and caps to 1 insn + clears `dc->jmp_opt`, so each idle iteration returns to `cpu_exec_loop`. |

## Honest matrix

Numbers from the captured `*_*.log` files in this directory.

### Delivery (% of PIT edges actually serviced by guest IRQ0 handler)

| Scenario        | baseline | s4    | s4tight | s5      | final (s5 only) |
|---              |     ---: |  ---: |    ---: |    ---: |            ---: |
| swe1 headless   |   43.5 % | 42.9 % | 42.2 % | **43.7 %** |        42.9 % |
| rfm  headless   |   42.7 % | 43.3 % | 42.6 % | **44.2 %** |        44.0 % |
| swe1 sdl+pa     |   39.4 % | 39.0 % | 38.1 % | **39.9 %** |        40.1 % |

### xinu_drift (XINU-tracked / wall ratio; higher = guest tracks closer)

| Scenario        | baseline | s4    | s4tight | s5      | final |
|---              |     ---: |  ---: |    ---: |    ---: |  ---: |
| swe1 headless   |    0.423 | 0.426 |   0.419 | **0.434** | 0.426 |
| rfm  headless   |    0.424 | 0.430 |   0.422 | **0.439** | 0.436 |
| swe1 sdl+pa     |    0.386 | 0.382 |   0.372 | **0.390** | 0.393 |

### iret_raise (gap from IRET to next observed PIT raise)

| Scenario        | metric | baseline | s4     | s4tight | s5         | final  |
|---              |   ---  |     ---: |   ---: |    ---: |       ---: |   ---: |
| swe1 headless   |   p50  |   500 µs | 477 µs |  493 µs |     491 µs | 513 µs |
| swe1 headless   |   p99  |   967 µs | 994 µs |  988 µs |     933 µs | 948 µs |
| swe1 headless   |   max  | 27095 µs | 2865 µs | 4308 µs | **1233 µs** | 2542 µs |
| rfm  headless   |   p50  |   498 µs | 481 µs |  482 µs |     507 µs | 497 µs |
| rfm  headless   |   max  |  3081 µs | 3204 µs | 4157 µs | **1230 µs** | 2221 µs |
| swe1 sdl+pa     |   p99  |  2365 µs | 2449 µs | 2712 µs |    2426 µs | 2487 µs |
| swe1 sdl+pa     |   max  |  9942 µs | 7687 µs | 9152 µs | **4345 µs** | 6850 µs |

### pdb05_wall_delta (LPT board PDB05 dwell)

No regression on percentiles in any configuration; `s5` and `final`
trim the max in headless `rfm` (3081 → 2392 µs) and `swe1 sdl+pa`
(9942 → 7162 µs). `swe1 headless` retains a single 20-27 ms outlier
across all configurations.

## Verdict

* **Strategy 4** (vtime no-chain window after IRQ0 IRET) is
  mechanically alive (tens of thousands of arms / 40 s, ~110-140 M
  TBs gated, snap line confirms `active`) but **measured neutral**
  on every scenario. TB chaining is *not* the bottleneck. Kept in
  tree, default OFF, opt-in only.
* **Strategy 5** (idle self-loop TB cutter) shows a small natural
  delivery uplift (+0.2 - +1.5 pp headless) and a clear `iret_raise`
  tail-latency improvement (max 27 ms → 1.2 ms in `swe1` headless;
  ~60 % reduction elsewhere). xinu_drift up by 0.011 - 0.015
  headless, neutral on SDL + PulseAudio. No regression on
  `pdb05_wall`. Worth enabling for measurement work; kept default
  OFF until further investigation isolates which Pinball idle loops
  it is breaking.
* **The ~43 % headless delivery floor and the ~500 µs `iret_raise`
  p50 are structural**, not algorithmic. They survive every
  TCG-side strategy in Part B (3) and Part C (2). Within the
  constraints of no `icount`, no synthetic IRQ injection, no guest
  RAM patching, and no PIT/PIC replacement, the TCG path is
  exhausted. The next levers (KVM + `-cpu host`, `kvm-pit`,
  lockstep `icount` mode) all step outside those constraints.
