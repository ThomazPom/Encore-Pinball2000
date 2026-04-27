/*
 * pinball2000 PM-entry recipe — runs after every system reset.
 *
 * Mirrors unicorn.old/src/cpu.c:200-227 ground truth.  bios.bin is NOT
 * executed; the PRISM option ROM (first 32 KiB of bank0) is staged at
 * physical 0x80000 and the CPU is reprogrammed to enter 32-bit protected
 * mode at CS:EIP = 0x08:0x801D9 with ESP = 0x8B000 and IF=0.
 *
 * We register this with qemu_register_reset() in the machine init; QEMU
 * runs registered reset handlers AFTER qemu_devices_reset(), so our writes
 * to env->eip/cr0/segs are not clobbered by x86_cpu_reset_hold().
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "exec/cpu-common.h"
#include "target/i386/cpu.h"

#include "p2k-internal.h"

/* Tiny GDT placed at a fixed RAM address well clear of the option ROM. */
#define P2K_GDT_BASE    0x00007000u
#define P2K_GDT_LIMIT   0x0000001Fu   /* 4 entries (null + code + data + tss) */

static void p2k_build_gdt(void)
{
    /* Flat 32-bit code (sel 0x08) + data (sel 0x10) descriptors. */
    static const uint8_t gdt[32] = {
        /* 0x00: null */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /* 0x08: code, base=0, limit=0xFFFFF, G=1, D=1, P=1, DPL=0, Type=0xA */
        0xFF, 0xFF, 0x00, 0x00, 0x00, 0x9A, 0xCF, 0x00,
        /* 0x10: data, base=0, limit=0xFFFFF, G=1, D=1, P=1, DPL=0, Type=0x2 */
        0xFF, 0xFF, 0x00, 0x00, 0x00, 0x92, 0xCF, 0x00,
        /* 0x18: spare */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    cpu_physical_memory_write(P2K_GDT_BASE, gdt, sizeof(gdt));
}

static void p2k_set_seg(SegmentCache *sc, uint16_t sel, uint32_t base,
                        uint32_t limit, uint32_t flags)
{
    sc->selector = sel;
    sc->base     = base;
    sc->limit    = limit;
    sc->flags    = flags;
}

void p2k_post_reset(void *opaque)
{
    Pinball2000MachineState *s = opaque;
    CPUState *cs;

    if (!s->bank0) {
        return;
    }

    /* 1. Copy PRISM option ROM (first 32 KiB of bank0) to RAM 0x80000. */
    cpu_physical_memory_write(P2K_OPTROM_LOAD_ADDR,
                              s->bank0, P2K_OPTROM_SIZE);

    /* 2. Lay down a flat-mode GDT for the protected-mode jump. */
    p2k_build_gdt();

    /* 3. Reprogram CPU0 to the PM entry. */
    CPU_FOREACH(cs) {
        X86CPU *cpu = X86_CPU(cs);
        CPUX86State *env = &cpu->env;

        if (cs->cpu_index != 0) {
            continue;
        }

        env->cr[0] |= CR0_PE_MASK;
        env->hflags |= HF_PE_MASK | HF_CS32_MASK | HF_SS32_MASK;
        env->hflags &= ~HF_ADDSEG_MASK;

        env->gdt.base  = P2K_GDT_BASE;
        env->gdt.limit = P2K_GDT_LIMIT;

        /* CS sel 0x08, flat 4 GiB, 32-bit code, P=1, S=1, Type=0xA. */
        p2k_set_seg(&env->segs[R_CS], P2K_INITIAL_CS_SEL, 0, 0xFFFFFFFFu,
                    DESC_P_MASK | DESC_S_MASK | DESC_CS_MASK |
                    DESC_R_MASK | DESC_A_MASK | DESC_G_MASK | DESC_B_MASK);
        /* Data segs sel 0x10, flat 4 GiB, P=1, S=1, Type=0x2 (RW). */
        for (int r = R_ES; r <= R_GS; r++) {
            if (r == R_CS) continue;
            p2k_set_seg(&env->segs[r], P2K_INITIAL_DS_SEL, 0, 0xFFFFFFFFu,
                        DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                        DESC_A_MASK | DESC_G_MASK | DESC_B_MASK);
        }

        env->eip         = P2K_PM_ENTRY_EIP;
        env->regs[R_ESP] = P2K_INITIAL_ESP;
        env->eflags      = 0x00000002;   /* reserved bit only; IF=0 */

        info_report("pinball2000: PM entry CS:EIP=%04x:%08x ESP=%08x",
                    P2K_INITIAL_CS_SEL, env->eip, env->regs[R_ESP]);
    }
}
