# Part D — guest-visible time test

This directory holds the artefacts that prove (or disprove) that guest
time on the QEMU/TCG build runs slow at the same fraction as the
audited PIT-IRQ delivery rate. See `docs/12-cpu-and-timers.md` Part D
for the full write-up.

## Approach A — in-RAM tick counter (the one that worked)

`mem_diff.py` boots QEMU headless via `scripts/run-qemu.sh`, attaches
to QEMU's QMP socket on a random local TCP port, snapshots a 1 MiB
window of guest physical memory at two wall times via the QMP
`pmemsave` command, and dwords-diffs the two snapshots to find every
counter that monotonically increases at a plausible scheduler cadence.

Key parameters (all environment variables):

| var   | default       | meaning                                         |
| ----- | ------------- | ----------------------------------------------- |
| `W1`  | `12` (s)      | wall-time at first snapshot                     |
| `W2`  | `32` (s)      | wall-time at second snapshot                    |
| `BASE`| `0x00200000`  | guest physical base of the snapshot window      |
| `LEN` | `0x100000`    | snapshot length in bytes (1 MiB by default)     |
| `GAME`| `swe1`        | game ROM passed to `--game`                     |

Snapshots are written to `/home/encore/.cache/p2k-mem/` because the
repo lives on `/mnt/hgfs` (vmware shared folder, much too slow for
two 1 MiB pmemsave calls in tight succession).

## Approaches that did not work

* Approach B — XINA `time` command: contaminated. `qemu/p2k-isa-stubs.c`
  re-fills CMOS RTC bytes from the host `time(NULL)` on every read, so
  the shell's date/time-of-day output reflects host wall regardless of
  guest tick advance.
* Approach C — XINA `sleep N` round-trip: the shell unit is unknown
  and `sleep 1` does not return within 90 s wall, suggesting it counts
  in seconds-of-XINU-clktime which is itself slow. Inconclusive
  without a separate guest-time witness.

## Files

* `mem_diff.py` — the QMP-driven snapshot/diff tool
* `clkint.bin`, `clkint_body.bin` — disassembly inputs
* `swe1_60s_clock_v2.log`, `clk_v3.log` — `P2K_GUEST_CLOCK_ADDRS`
  audit-side sampling runs (kept for reference; the path that produced
  the verdict is `mem_diff.py`)
* `mem_2af680.bin` — physical memory dump used to verify CR0=0x60000011
  (paging is OFF; phys = virt for this XINU build)
