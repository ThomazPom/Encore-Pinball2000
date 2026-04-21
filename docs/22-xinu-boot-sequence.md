# 22 — XINU Boot Sequence

XINU is the operating system kernel that the Pinball 2000 game image
uses for multi-process scheduling. Understanding its startup path is
essential for debugging early-boot failures.

## The five milestones

```
Power-on reset
    │
    ▼
1.  BIOS / POST → transfers control to bootdata.rom entry point
    │
    ▼
2.  bootdata: MediaGX setup, RAM test, loads game.rom + im_flsh0.rom
    │
    ▼
3.  XINU sysinit: nulluser, clkruns, prnull processes created
    │
    ▼
4.  clkint installed in IDT[0x20]; PIT CH0 programmed (divisor ≈ 298)
    │
    ▼
5.  xinu_ready: timer injection enabled; game enters attract loop
```

## nulluser — the idle process

`nulluser` is XINU's process 0, the "null" idle process. In the
original embedded XINU it executes an infinite `JMP $` loop whenever
no other process is runnable. On real hardware (a 200 MHz MediaGX this
takes negligible host time. Under Unicorn emulation the tight `JMP $`
burns 100 % of one host core without letting the host deliver
interrupts.

Fix: **BT-74** (`io.c`, `apply_sgc_patches`). A 17-byte pattern scan
locates the `MOV [flag],1; NOP; JMP $` sequence and overwrites the
`JMP $` with `HLT; JMP-3`. The `HLT` causes `uc_emu_start` to return
voluntarily, the host timer loop runs, the next IRQ0 tick is injected,
and execution resumes at the `JMP-3` which loops back to the `MOV`.
No per-bundle address is hardcoded; the scan works on every build.

```
Before patch (JMP $):        After patch (HLT;JMP-3):
  C6 05 xx xx xx xx 01         C6 05 xx xx xx xx 01   ; MOV byte [flag], 1
  90                           90                      ; NOP
  EB FE                        F4                      ; HLT
                               EB FD                   ; JMP -3
```

The patched address is logged:
```
[sgc] BT-74: nulluser idle JMP$ → HLT+JMP at 0x00xxxxxx
```

## prnull — the I/O idle stub

`prnull` is XINU's null output device (a bit-bucket). Before XINU
fully initialises, some code paths call `prnull` for logging. In the
emulated environment `prnull` may be jumped to with an uninitialised
stack if the scheduler is not yet primed.

Encore writes a minimal three-instruction stub at guest physical
`0xFF0000` early in `apply_sgc_patches`:

```asm
STI          ; FA
HLT          ; F4
JMP $-1      ; EB FD
```

This address sits in the unused low-megabyte above the IVT. Any stray
call that lands here simply halts until the next IRQ and loops — it
never corrupts the real XINU stack.

## clkruns and the ready-queue

`clkruns` is XINU's internal flag that enables clock-driven process
scheduling. It is set by XINU's own `clkinit()` routine; no external
poke is needed. Encore historically contained a hardcoded BSS poke for
SWE1 V1.12 (`apply_xinu_boot_patches`) but it was deleted in the
2026-04-21 minimisation pass once pattern-scan BT-74 was proven to work
on all bundles without it.

The ready-queue is primed naturally: once `nulluser` is in the HLT-idle
loop and `clkruns` is nonzero, every IRQ0 tick wakes `nulluser`, the
XINU scheduler inspects the ready queue, picks the highest-priority
runnable process, and resumes it. The game threads (display, DCS,
PinIO) are already in the queue from XINU `sysinit`.

## Emulator state machine

```c
/* cpu.c — simplified */
if (!g_emu.xinu_ready) {
    /* Phase 1: wait for clkint in IDT[0x20] */
    if (IDT[0x20] != old_trap_handler) {
        g_emu.clkint_ready_exec = exec_count;
    }
    /* Phase 2: wait for "XINU" banner on UART + 50 more batches */
    if (g_emu.xinu_booted && exec_count >= clkint_ready_exec + 50) {
        g_emu.xinu_ready = true;  // enable timer injection
    }
}
```

The "XINU" string is detected by the UART receive hook in `io.c`
(`src/io.c:1254`). The 50-batch delay lets XINU finish its own startup
before the first IRQ0 is injected into the IDT handler.

## Cross-references

* XINU ready flag: `include/encore.h` — `xinu_ready`, `xinu_booted`
* nulluser patch: `src/io.c` — `apply_sgc_patches`, BT-74 block
* prnull stub: `src/io.c:499`
* Scheduler discovery: [23-boot-scheduler-fix.md](23-boot-scheduler-fix.md)
* IRQ injection and PIC: [16-irq-pic.md](16-irq-pic.md)
* VBLANK timing: [17-vblank.md](17-vblank.md)
