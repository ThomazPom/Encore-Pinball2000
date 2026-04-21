# 06 — CPU Emulation

Encore does no CPU emulation of its own. The guest i386 is executed
inside **Unicorn Engine 2.x**, which is itself a stripped-down QEMU
TCG packaged as a library. This document covers only the wrapper
logic around Unicorn that turns "an i386 JIT" into "a PC".

Source of truth: `src/cpu.c`, 1203 lines.

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## Guest model

* Architecture: **i386**, `UC_ARCH_X86 / UC_MODE_32`.
* Protected mode is entered before execution starts — we skip the
  real-mode BIOS phase entirely (`cpu_setup_protected_mode()` at
  `cpu.c:…`).
* Flat descriptors: CS/DS/ES/SS/FS/GS → base 0, limit 4 GiB.
  A minimal GDT is built at `0x18000`.
* An IDT is built at `0x19000` with 256 gates; 254 of them point to an
  `IRET+EOI` stub at guest physical `0x20000` so *any* guest
  exception that we have not hand-lifted returns cleanly to the
  caller.
* Paging is **off**. Every guest linear address is a physical address.
  The game's code never enables paging — Pinball 2000 runs as a
  single-address-space RTOS, so we matched it.

## The execution loop

```c
while (g_emu.running) {
    // 1. Wall-clock-driven state
    maybe_tick_pit();          // IRR bit 0 at guest PIT rate
    maybe_tick_vblank();       // 57 Hz, DC_TIMING2 pulse
    maybe_set_xinu_ready();    // IDT[0x20] probe

    // 2. Conditional boot-time patches
    maybe_apply_dcs_patch();   // pattern scan, one-shot

    // 3. Inject any pending IRQ
    if (g_emu.xinu_ready && pending_irq())
        check_and_inject_irq();

    // 4. Run guest
    uc_emu_start(uc, eip, 0, 0, 200000);
    g_emu.exec_count++;

    // 5. Maintenance — runs every tick
    if (g_emu.watchdog_flag_addr)
        RAM_WR32(g_emu.watchdog_flag_addr, scribble_val);
    if ((exec_count & 0x3F) == 0) RAM_WR32(0, 0);

    // 6. Error handling / HLT
    handle_err(err);

    // 7. Periodic SDL / netcon
    if ((exec_count & 0x7F) == 0) {
        display_update();
        display_handle_events();
        netcon_poll();
    }
}
```

Each numbered step is small on its own but matters for the overall
shape of the emulator.

### 1. Wall-clock state

The PIT is not ticked in lockstep with the guest. We read
`CLOCK_MONOTONIC`, compute how many PIT periods have elapsed since
the last tick, and set IRR bit 0 accordingly. Cap at 4 ticks per batch
so we cannot overflow after a suspend. Details:
[16-irq-pic.md](16-irq-pic.md).

### 2. Boot-time patches

Two patches are applied exactly once each, gated on
`g_emu.xinu_ready`:

* `apply_sgc_patches()` in `io.c` (the watchdog scanner + `mem_detect`
  fix, see [15-watchdog-scanner.md](15-watchdog-scanner.md)).
* The DCS-mode CMP/JNE prologue replacement in `cpu.c` (see
  [13-dcs-mode-duality.md](13-dcs-mode-duality.md)).

Both are pattern-scanned at runtime. No per-bundle addresses.

### 3. IRQ injection

`check_and_inject_irq()` walks both PICs (master + slave), honours
`IMR`, finds the highest-priority pending `IRR` bit, promotes it to
`ISR`, computes the vector (`ICW2 + irq`), and calls
`cpu_inject_interrupt(vec)` which:

1. Pushes EFLAGS / CS / EIP to guest ESP.
2. Clears IF.
3. Reads the 8-byte gate descriptor at IDT[vec], extracts the handler
   linear address.
4. Writes the new EIP and CS back to Unicorn.

The `xinu_ready` gate exists because before XINU's `clkinit()` the
IDT is full of our IRET+EOI stubs and injecting real IRQs there is a
waste. See [22-xinu-boot-sequence.md](22-xinu-boot-sequence.md).

### 4. `uc_emu_start` batch

200 000 instructions per call, fixed. Unicorn returns early on:

* `UC_ERR_OK` — batch exhausted normally, or HLT was hit.
* `UC_ERR_INSN_INVALID` — usually a `0F 3C` Cyrix-GX opcode that
  we lift in software (see below).
* Other errors — logged and we advance EIP past the faulting byte to
  keep going.

### 5. Maintenance pokes

Two writes happen on every batch:

* **Watchdog scribble.** The scanner found a `CMP [cell], 0xFFFF` that
  serves as both the watchdog liveness probe and the DCS-presence
  probe. We keep it fed; full analysis in
  [14-dcs-probe-polarity.md](14-dcs-probe-polarity.md).
* **`RAM_WR32(0, 0)`.** Guest physical address 0 is a safety
  sentinel; some code paths still write `1` there during init and our
  stub can be invoked through it. Keeping it zero means any accidental
  far call or null dereference lands on a harmless 0-opcode.

Once every 64 iterations (`exec_count & 0x3F`) additional SWE1-only
BSS slots are reset — these are game-specific and guarded by
`game_id == 50069`.

### 6. Error / HLT handling

The HLT-redirection policy is:

* HLT at a known Fatal/panic address (`0x227238`, `0x1CF800`,
  `0x1D96AE`) → redirect EIP to the idle stub at `0xFF0000`, reset
  ESP to a known-safe value. See
  [34-nonfatal-vs-fatal.md](34-nonfatal-vs-fatal.md).
* HLT with no pending IRQ → sleep 5 ms, force a pending tick, advance
  EIP by 1 to re-enter the batch. Classic idle-loop handling.
* HLT with a pending IRQ → fall through; the next iteration injects
  the IRQ.

### 7. Periodic SDL / netcon

`display_update()` is rate-limited to ~60 Hz by a wall-clock check,
not by an iteration count. The `0x7F` check merely bounds the cost of
`clock_gettime` to once every 128 batches (about 800 µs on a modern
CPU).

## The Cyrix `0F 3C` opcode

The game's PRISM option-ROM uses the MediaGX-specific extension
opcode `0F 3C` for fast framebuffer region copies. Unicorn does not
know about it, so Encore lifts it in software:

```
0F 3C 30  →  MOV dword [ds:EDX], EAX ; MOV dword [ds:EDX+4], EBX
              ; EDX += 8
```

Implemented in `cpu.c` near line 840 as a fall-through from
`UC_ERR_INSN_INVALID`. The resulting writes are dispatched through
`bar_mmio_write()` because EDX typically points into the framebuffer
window at `0x40800000`.

## What we deliberately do *not* do

* **Cycle counting.** Unicorn's JIT does not expose a cycle budget; we
  count instructions only in integer multiples of 200 000.
* **FPU state.** The game does not use x87. Unicorn handles what is
  there; no XMM state is preserved because we do not save/restore.
* **SMP.** Guest is single-CPU. There is no `CPUID`-advertised APIC
  support because the real hardware does not have one either.
* **A20 gate.** Wired permanently on. The real hardware enabled A20
  very early and never disabled it.

## Debug helpers

`cpu.c` contains `LOG("hb", …)` lines that fire every five seconds
(the "heartbeat") and dump:

* `exec_count`, `EIP`, `POST` code, `vsync_count`, `frames`,
  `irq_ok_count`
* Preempt counter, `nproc`, watch-guard words
* Both PIC IRR/IMR/ISR
* DCS state machine words

If the EIP stays at the same value for three heartbeats in a row, 16
bytes of code around EIP and the full register set are dumped so you
can see where the guest is stuck.

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
