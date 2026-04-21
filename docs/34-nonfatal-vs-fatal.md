# 34 — Nonfatal vs Fatal Exception Classification

The Pinball 2000 game binary distinguishes two error classes internally:
`NonFatal()` for recoverable anomalies and `Fatal()` for unrecoverable
conditions. Understanding how Encore intercepts and handles each class
is important for interpreting the log output and for diagnosing boot
failures.

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## Game-side exception model

### NonFatal()

`NonFatal(const char *msg)` is a XINU-level error reporter that logs a
message and returns to the caller. The game continues running. In
Encore's log these appear as:

```
[nonfatal] NonFatal("prnull savsp=0x101e74ff (unexpected)") [str@0x... caller@0x...]
```

Encore installs a code-trace hook on the `NonFatal` function
(`src/cpu.c:138`) to capture and log the string argument before the
function executes. The hook reads the string pointer from `[ESP+4]` and
the caller return address from `[ESP]`.

**These are almost always benign.** Common ones seen during normal boot:

| Message | Cause | Severity |
|---|---|---|
| `prnull savsp=0x101e74ff (unexpected)` | RFM XINU stack sanity check | benign |
| `mem_detect: unexpected size` | Pre-patch RAM size check | benign |

### Fatal()

`Fatal(const char *msg)` is a XINU-level panic. In the original
hardware it halts the machine and displays an error on a seven-segment
display. Under emulation it typically lands in a tight `HLT` or
`JMP $` loop.

Encore has three strategies for taming Fatal calls:

1. **prnull redirect** (`src/cpu.c:826`): If a `HLT` is encountered at
   an address that looks like a Fatal/panic path (outside the normal
   `nulluser` idle range), Encore logs it and redirects EIP to the
   prnull idle stub at `0xFF0000`. The game continues from there as
   if it were idle, which is often enough to let the next scheduler
   tick pick up a live thread.

2. **IRET+EOI stub at 0x20000** (`src/cpu.c:29`): The PIC vector base
   is initially programmed to `0x08`; spurious interrupts before the
   IDT is installed would jump to `0x20000` where a stub `STI; IRET`
   sequence lives. This prevents a fetch-fault from a dead handler
   address from crashing the guest.

3. **Dropped Fatal-return patches**: Earlier versions of Encore had
   hardcoded `RET` bytes patched over `Fatal` and `NonFatal` entry
   points to make them no-ops. These were removed in the 2026-04-21
   minimisation pass — the game handles most Fatal calls gracefully on
   its own once the scheduler is running.

## Invalid memory access handling

The Unicorn `UC_HOOK_MEM_INVALID` callback (`src/cpu.c:1153`) fires
on unmapped reads, writes, or instruction fetches. The handler:

1. Logs the access type, address, and current EIP.
2. For accesses in the GX_BASE MMIO region that are not yet mapped,
   returns `true` (handled) to let the guest see a zero read.
3. For genuine unmapped ranges returns `false`, causing Unicorn to
   raise `UC_ERR_FETCH_UNMAPPED` and terminate the batch.

The first 30 invalid accesses are logged unconditionally (`inv_cnt <= 30`);
subsequent ones are throttled to every 10 000 occurrences to avoid log
flooding:

```c
/* src/cpu.c:813 */
if (inv_cnt <= 30 || (inv_cnt % 10000) == 0)
    LOG("cpu", "[%d] invalid mem ... EIP=0x%08x insn %02x %02x ...\n", ...);
```

## Catastrophic vs benign — decision table

| Condition | Classification | Encore response |
|---|---|---|
| `NonFatal()` string log | benign | log and continue |
| `HLT` in nulluser idle loop | benign | inject next IRQ0 |
| `HLT` at Fatal panic address | potentially fatal | redirect to prnull idle |
| `UC_ERR_FETCH_UNMAPPED` | potentially fatal | log, stop batch |
| `UC_ERR_OK` after `HLT` | benign | normal idle wakeup |
| Invalid write to mapped MMIO | usually benign | handled by MMIO hook |
| `JMP $` without BT-74 | fatal (hang) | prevented by BT-74 patch |

## Cross-references

* BT-74 idle patch: [23-boot-scheduler-fix.md](23-boot-scheduler-fix.md)
* XINU boot sequence: [22-xinu-boot-sequence.md](22-xinu-boot-sequence.md)
* IRQ injection: [16-irq-pic.md](16-irq-pic.md)
* Patching philosophy: [21-patching-philosophy.md](21-patching-philosophy.md)
