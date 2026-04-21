# 16 — IRQ & PIC

Encore emulates a standard cascaded 8259A PIC pair, just enough for
the Pinball 2000 firmware to function. The PIC code lives at the top
of `src/io.c` (lines 14–170). The IRQ injector lives in `src/cpu.c`.

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## Layout

Two PIC chips, chained master → slave, at the canonical ISA ports:

| PIC | Command port | Data port | ICW2 default | IRQs |
|---|---:|---:|---|---|
| Master | `0x20` | `0x21` | `0x08` | 0..7 |
| Slave  | `0xA0` | `0xA1` | `0x70` | 8..15, IRQ2 is cascade |

## State machine

```c
typedef struct {
    uint8_t imr;         // Interrupt Mask Register
    uint8_t irr;         // Interrupt Request Register (pending)
    uint8_t isr;         // In-Service Register (currently executing)
    uint8_t icw_step;    // initialisation ICW1..ICW4 step
    uint8_t icw1, icw2, icw3, icw4;
    uint8_t read_isr;    // OCW3 "read ISR" flag
    bool    init_mode;
} PICState;
```

We implement OCW1 (IMR), OCW2 (non-specific and specific EOI) and OCW3
(read IRR / ISR). Edge vs level triggering is tracked internally but
Encore's interrupt sources are always edge-triggered from the host side.

Full initialisation sequence (ICW1–ICW4) is modelled because XINU
programs the PICs during `clkinit()` and expects them to answer back
correctly.

## IRQ sources

| IRQ | Source | Status |
|--:|---|---|
| 0 | Host-wall-clock PIT tick | implemented |
| 1 | PS/2 keyboard | stub, raised by `--keyboard-tcp` |
| 2 | Cascade to slave PIC | implemented |
| 3 | COM2 | unused |
| 4 | COM1 / UART | raised by `netcon.c` on new RX bytes |
| 5 | — | unused |
| 6 | — | unused |
| 7 | LPT | unused |
| 8..15 | slave PIC | unused except VGA retrace (unused) |

## Injection path

When the host decides an IRQ should be delivered:

```c
g_emu.pic[0].irr |= 0x01;     // e.g. raise IRQ0
```

On the next batch boundary, `check_and_inject_irq()` runs:

```c
uint8_t pending = g_emu.pic[0].irr & ~g_emu.pic[0].imr;
if (!pending) return;
int irq = find_highest_priority(pending);
g_emu.pic[0].isr |= (1 << irq);
g_emu.pic[0].irr &= ~(1 << irq);
uint8_t vector = g_emu.pic[0].icw2 + irq;
cpu_inject_interrupt(vector);
```

`cpu_inject_interrupt()` in `cpu.c` does the work of building the
IRET frame:

1. Read current `EFLAGS`, `CS`, `EIP` from Unicorn.
2. Push `EFLAGS`, then `CS`, then `EIP` onto the guest stack
   (ESP -= 12, `uc_mem_write`).
3. Clear IF in EFLAGS.
4. Read the 8-byte IDT gate at `idt_base + vector*8`. Extract the
   offset (low 16 bits + high 16 bits) and segment selector.
5. Write the new EIP to Unicorn.

The guest then runs the handler on the next `uc_emu_start`. The
handler issues `OUT 0x20, 0x20` (EOI) which our PIC emulation turns
into `ISR &= ~(1<<irq)`, freeing that slot.

## Gating on `xinu_ready`

We do not inject IRQs until `g_emu.xinu_ready` is true. That flag is
set by the two-phase probe in `cpu.c:553-605`:

1. IDT[0x20] must change from the generic IRET+EOI stub (at
   `0x20000000`) to a real handler (anywhere above `0x100000`). This
   indicates XINU's `clkinit` has run `set_evec(0x20, clkint)`.
2. The UART must have received the `"XINU: V7"` banner, indicating
   `sysinit()` returned.
3. Then, 50 more batches of grace are given to let `ctxsw` settle
   before the first tick.

Phase 2 matters because XINU registers `clkint` quite early — before
the process table, scheduler queues and `watchdog_bone` process are
initialised. Firing IRQ0 at that stage would invoke `clkint` on a
half-built system and hang. The 50-batch cushion is conservative but
cheap.

## PIT tick rate

The guest programs PIT channel 0 during init, usually to a divisor
around 298 (= 4003 Hz, pretty common for XINU-based systems). Encore
reads this divisor from `g_emu.pit.count[0]` and computes the
wall-clock period:

```c
uint64_t pit_period_ns = (uint64_t)div * 838;  // 1e9 / 1193182 ≈ 838 ns
```

Catch-up is capped at 4 ticks per batch — a sleep/wake cycle on the
host will not flood the guest with hundreds of queued IRQs.

## IRQ4 (UART)

When a TCP-serial client pushes a byte into `netcon_serial_rx()`, the
`netcon` code also calls `uart_notify_rx()` which does:

```c
g_emu.pic[0].irr |= (1 << 4);
```

and sets the UART IIR to "RDA" (Received Data Available). The next
batch boundary injects vector `icw2 + 4` (typically `0x24`) and XINU's
`com1_isr` reads the byte.

This closed one of the long-standing bugs: previously IRQ4 was never
raised, so the TCP-serial bridge was a one-way channel (guest → TCP
only). The fix is the single `|= (1<<4)` line plus its peer in the
UART state machine; see commit `111bbc1 netcon: raise UART IRQ4 after
pushing RX bytes`.

## EOI handling

A non-specific EOI is most commonly used:

```
OUT 0x20, 0x20   ;  cmd=0x20  => non-specific EOI: clear highest ISR bit
```

For IRQ0 specifically, some XINU variants emit a specific EOI:

```
OUT 0x20, 0x60   ;  cmd=0x60 | 0  => specific EOI for IRQ0
```

Both are handled. Specific EOIs for other IRQs are recognised too;
unused EOI bits are silently cleared.

## Diagnostics

Every 5-second heartbeat dumps PIC state:

```
[hb] PIC0: IRR=0x01 IMR=0xB8 ISR=0x00  PIC1: IRR=0x00 IMR=0xFF ISR=0x00
```

If IRR stays asserted with the matching IMR bit clear and ISR is zero
for many seconds, injection is broken — usually because `xinu_ready`
never fired (see `[irq] waiting for clkint` messages).

## OCW3 read-ISR quirk

OCW3 with `bit 1 = 1` flips the subsequent data-port read to return
ISR instead of IRR. XINU uses this during its "spurious interrupt"
check. Our state machine mirrors it via the `read_isr` flag.

## What we do NOT emulate

* Auto-EOI (AEOI) mode — not used by XINU.
* Special fully-nested mode — ditto.
* Buffered mode handshake — ditto.
* IRQ priority rotation — ditto.

If a future bundle wanders into any of those, the PIC will return
sensible defaults and the guest will log a complaint; nothing will
crash.
