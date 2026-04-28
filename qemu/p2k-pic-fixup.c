/*
 * pinball2000 PIC IRQ0/cascade unmask fix-up.
 *
 * Direct port of the proven unicorn.old/src/io.c:121-127 fix:
 *
 *   "After XINU boots, XINU's restore() does OUT 0x21, saved|base_mask.
 *    Because we NOP'd device_init, base_mask has bit 0 set, so every
 *    restore() re-masks IRQ0.  Fix: always keep IRQ0 + cascade unmasked
 *    once XINU is ready."
 *
 * In our QEMU board we bypass the BIOS/PRISM init path the same way
 * (p2k-boot.c jumps straight to PM entry), so the master PIC IMR is left
 * at its reset value 0xFF.  The XINU restore_irq routine
 * (game @ 0x231e4a) is OR-only:
 *
 *     mov ax, [saved];  or ax, [0x2fe854];  out 0x21, al
 *
 * with [0x2fe854] = 0xff7b — bit 0 (IRQ0/PIT) and bit 1 (IRQ1/KBD) are
 * SET in the OR mask, so once IMR=0xff it can never come back down to
 * deliver the timer.  We poll isa_pic->imr at high frequency and
 * re-issue OUT 0x21 with bits 0 (IRQ0) + 2 (cascade) cleared whenever
 * we see them set after XINU has finished its own PIC programming.
 *
 * "XINU ready" detection: master PIC has been ICW-initialised to vector
 * base 0x20 and the guest IDT base is non-zero (i.e. lidt has run).
 * That matches the live state we observed (IDT@0x2fe684, master vector
 * base 0x20, [0x2fe854]=0xff7b).
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h"
#include "hw/intc/i8259.h"
#include "hw/isa/i8259_internal.h"
#include "exec/ioport.h"
#include "exec/cpu-common.h"
#include "target/i386/cpu.h"

#include "p2k-internal.h"

/* Mask of master IRQs we force-keep enabled: IRQ0 (PIT) + IRQ2 (cascade). */
#define P2K_FORCED_UNMASK   0x05u

/* Poll interval — fast enough that a missed PIT edge costs <1ms. */
#define P2K_PIC_FIXUP_NS    (250 * 1000)   /* 250 us */

static QEMUTimer *p2k_pic_fixup_timer;
static bool       p2k_xinu_ready_logged;

static bool p2k_xinu_ready(void)
{
    CPUState *cs;
    if (!isa_pic) {
        return false;
    }
    /* PIC must have been ICW-programmed by guest (irq_base != 0xff reset). */
    if (isa_pic->irq_base == 0) {
        return false;
    }
    /* Guest must have loaded a real IDT. */
    CPU_FOREACH(cs) {
        X86CPU *cpu = X86_CPU(cs);
        CPUX86State *env = &cpu->env;
        if (env->idt.base != 0 && env->idt.limit != 0) {
            return true;
        }
    }
    return false;
}

static void p2k_pic_fixup_tick(void *opaque)
{
    if (isa_pic && p2k_xinu_ready()) {
        if (!p2k_xinu_ready_logged) {
            info_report("pinball2000: PIC fix-up active "
                        "(IRQ0+cascade force-unmask, mirrors unicorn.old)");
            p2k_xinu_ready_logged = true;
        }
        if (isa_pic->imr & P2K_FORCED_UNMASK) {
            /* Route through the official write handler so the i8259 does
             * its IRR/ISR re-evaluation and forwards a pending IRQ to the
             * CPU.  cpu_outb is safe — we are not the registered handler
             * for port 0x21. */
            uint8_t newval = isa_pic->imr & ~P2K_FORCED_UNMASK;
            cpu_outb(0x21, newval);
        }
    }
    timer_mod(p2k_pic_fixup_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_PIC_FIXUP_NS);
}

void p2k_install_pic_fixup(void)
{
    p2k_pic_fixup_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                       p2k_pic_fixup_tick, NULL);
    timer_mod(p2k_pic_fixup_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_PIC_FIXUP_NS);
    info_report("pinball2000: PIC fix-up timer armed (%d us)",
                (int)(P2K_PIC_FIXUP_NS / 1000));
}
