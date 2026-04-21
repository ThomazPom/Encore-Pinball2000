# 23 — Boot Scheduler Fix

This document records how the earliest Encore boot hang was diagnosed
and the pattern-scan fix that resolved it for every bundle.

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## The symptom

Early Encore builds loaded the ROM, reached XINU `sysinit`, printed the
`XINU: V7` banner on COM1, and then silently hung — the SDL window
stayed black and no timer interrupts were delivered. The CPU was pegged
at 100 % host usage.

## Root cause: the tight idle loop

XINU's `nulluser` process ends with:

```c
/* XINU nulluser() — compiled output */
clkruns = 1;   // 89 05 xx xx xx xx
/* ... */
for (;;) ;     // EB FE  — JMP $ (infinite tight spin)
```

Under Unicorn emulation, `uc_emu_start` never returns from the `JMP $`
sequence; the host timer callback (`SIGALRM`) sets `timer_pending` but
the emulation loop cannot drain it because the guest never yields. No
IRQ0 is injected, the XINU scheduler never runs, and every game thread
stays queued forever.

On real 200 MHz MediaGX hardware the same loop runs at wire speed and
the CPU's own timer interrupt fires hardware-side — but Unicorn provides
no asynchronous interrupt delivery; interrupts must be injected by
calling `cpu_inject_interrupt` between `uc_emu_start` calls.

## The fix — BT-74

Discovered while tracing why SWE1 V1.12 hung and subsequently reproduced
game-agnostic. The compiler always emits the same 9-byte sequence before
the `JMP $`:

```
C6 05 <addr32> 01   ; MOV byte ptr [clkruns], 1
90                  ; NOP
EB FE               ; JMP $  ← patched out
```

`apply_sgc_patches()` (`src/io.c:511`) scans the range `0x200000`–
`0x400000` for this 9-byte pattern. When found, it overwrites the
`EB FE` with `F4 EB FD` (`HLT; JMP -3`):

```
F4       ; HLT  → uc_emu_start returns; host loop runs
EB FD    ; JMP -3 → back to MOV [clkruns],1
```

The host loop sees `exec_count` advance, delivers any pending IRQ0,
re-enters `uc_emu_start` at the `MOV` instruction, and the scheduler
fires normally on the next tick.

## Why "BT-74"?

Internal bug-tracker numbering used during development. BT-74 was the
74th recorded behavioural fix. The designation is kept in comments for
cross-reference with the git history (`src/io.c:542`).

## Effect on every bundle

| Bundle          | nulluser idle address | BT-74 hit? |
|-----------------|----------------------|------------|
| SWE1 v1.5       | scan finds pattern   | ✓          |
| SWE1 v2.1       | scan finds pattern   | ✓          |
| RFM v1.2        | boots pre-XINU (n/a) | —          |
| RFM v1.6        | scan finds pattern   | ✓          |
| RFM v1.8        | scan finds pattern   | ✓          |
| RFM v2.5        | scan finds pattern   | ✓          |
| RFM v2.6        | scan finds pattern   | ✓          |

RFM v1.2 is the 1999 pre-XINU build; it does not reach XINU `sysinit`
and is therefore unaffected. See [38-known-limitations.md](38-known-limitations.md).

## Dropped history

`apply_xinu_boot_patches()` was an earlier approach that hardcoded BSS
addresses for SWE1 V1.12 (`clkruns`, `pstate`, `sched_en`, `tick_init`).
It was deleted in the 2026-04-21 minimisation pass after BT-74 proved
sufficient. The function is commemorated in a comment block at
`src/io.c:587` so the git-bisect history remains traceable.

## Cross-references

* XINU boot context: [22-xinu-boot-sequence.md](22-xinu-boot-sequence.md)
* Patch philosophy: [21-patching-philosophy.md](21-patching-philosophy.md)
* Regression matrix: [26-testing-7-bundle-matrix.md](26-testing-7-bundle-matrix.md)

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
