/*
 * ============================================================================
 * STATUS: DEFAULT-OFF as of the QEMU branch maturity. Was a TEMPORARY
 * SYMPTOM PATCH inherited from unicorn.old; we kept it as a boot bridge
 * while the real PIC/ICW path was unproven. Empirical run with
 * P2K_NO_PIC_FIXUP=1 shows the guest reaches further (exec_pass 0x420+
 * vs 0x130 with the timer armed) and emits more gameplay events with
 * the fix-up disabled — the 250us timer was wasting cycles re-OUTing
 * 0x21 every quarter-millisecond on a PIC the guest had already
 * programmed correctly.
 *
 * Default: OFF.  Opt-in with P2K_PIC_FIXUP=1 if a regression appears.
 * Removal condition: once we confirm no bundle (RFM + RFM update +
 * SWE1 update) regresses, this whole file can be deleted.
 * ============================================================================
 *
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
static uint32_t   p2k_initial_idt20;     /* cached on first IDT load */

/* Read the 32-bit handler offset from IDT[0x20] (PIT vector).
 * Returns 0 if the IDT is too small or unreadable. */
static uint32_t p2k_read_idt20(CPUX86State *env)
{
    uint8_t buf[8];
    uint32_t addr = env->idt.base + 0x20 * 8;
    if (env->idt.limit < 0x20 * 8 + 7) {
        return 0;
    }
    cpu_physical_memory_read(addr, buf, sizeof(buf));
    /* 386 trap/interrupt gate: offset_lo:16 | sel:16 | zero:8 |
     *                          type:8 | offset_hi:16 */
    uint16_t lo = buf[0] | (buf[1] << 8);
    uint16_t hi = buf[6] | (buf[7] << 8);
    return ((uint32_t)hi << 16) | lo;
}

static bool p2k_xinu_ready(void)
{
    CPUState *cs;
    if (!isa_pic) {
        return false;
    }
    if (isa_pic->irq_base == 0) {
        return false;
    }
    /* Gate: PIC ICW-initialised AND guest IDT loaded.
     *
     * We do NOT additionally require IDT[0x20] > 0x100000 here.  In our
     * QEMU build, XINU's autogen IDT[0x20] points to a panic dispatcher
     * inside game code (~0x232xxx), which is also > 0x100000 — that
     * threshold cannot distinguish "panic stub" from "real clkint".
     * Instead, p2k_install_irq0_shim() guarantees IDT[0x20] points to a
     * harmless EOI+IRET stub at 0x500 until clkinit() overwrites it. */
    CPU_FOREACH(cs) {
        X86CPU *cpu = X86_CPU(cs);
        CPUX86State *env = &cpu->env;
        return env->idt.base != 0 && env->idt.limit >= 0x20 * 8 + 7;
    }
    return false;
}

static void p2k_pic_fixup_tick(void *opaque)
{
    if (isa_pic && p2k_xinu_ready()) {
        /* Strict gate: IDT[0x20] must point to our EOI+IRET shim at 0x500
         * before we unmask IRQ0. Otherwise a stray PIT edge would dispatch
         * into XINU's autogen panic stub (or, worse, garbage). The shim
         * installer (p2k-irq0-shim.c) holds IDT[0x20] = 0x500 indefinitely
         * for now, so this gate effectively means "wait until the shim is
         * in place". */
        CPUState *cs;
        bool gated = true;
        CPU_FOREACH(cs) {
            X86CPU *cpu = X86_CPU(cs);
            uint32_t off = p2k_read_idt20(&cpu->env);
            if (off == 0x500u) {
                gated = false;
            }
            break;
        }
        if (gated) {
            goto rearm;
        }

        if (!p2k_xinu_ready_logged) {
            info_report("pinball2000: PIC fix-up active "
                        "(IRQ0+cascade force-unmask, mirrors unicorn.old)");
            p2k_xinu_ready_logged = true;
        }
        if (isa_pic->imr & P2K_FORCED_UNMASK) {
            uint8_t newval = isa_pic->imr & ~P2K_FORCED_UNMASK;
            cpu_outb(0x21, newval);
        }
    }
rearm:
    timer_mod(p2k_pic_fixup_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_PIC_FIXUP_NS);
}

void p2k_install_pic_fixup(void)
{
    const char *force = getenv("P2K_PIC_FIXUP");
    bool enabled = (force && force[0] && force[0] != '0');

    /* P2K_NO_PIC_FIXUP=1 is honored even if someone also passes
     * P2K_PIC_FIXUP=1, so the kill switch always wins. */
    const char *off = getenv("P2K_NO_PIC_FIXUP");
    if (off && off[0] && off[0] != '0') {
        enabled = false;
    }

    if (!enabled) {
        info_report("pinball2000: PIC fix-up disabled (default; "
                    "set P2K_PIC_FIXUP=1 to re-arm the legacy timer)");
        return;
    }
    p2k_pic_fixup_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                       p2k_pic_fixup_tick, NULL);
    timer_mod(p2k_pic_fixup_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_PIC_FIXUP_NS);
    info_report("pinball2000: PIC fix-up timer armed (%d us) "
                "[P2K_PIC_FIXUP=1, opt-in legacy mode]",
                (int)(P2K_PIC_FIXUP_NS / 1000));
}
