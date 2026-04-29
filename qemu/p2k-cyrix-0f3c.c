/*
 * ============================================================================
 * STATUS: TEMPORARY SYMPTOM PATCH — replace with proper QEMU CPU behavior.
 *
 * Why temporary: this module patches the guest's IDT[6] to vector to a
 * 32-byte hand-injected handler at physical 0x540 that emulates the
 * Cyrix-specific `0F 3C` (BB0_RESET) opcode in software. Mutating the
 * guest interrupt vector table and injecting executable bytes into low
 * RAM is a band-aid — it works because no guest code reads or rewrites
 * IDT[6] after sysinit, but any change in that assumption breaks us
 * silently.
 *
 * Removal condition: implement Cyrix/MediaGX `0F 3C` semantics in QEMU's
 * i386 TCG decoder (target/i386/tcg/translate.c) so the natural #UD
 * never fires, IDT[6] stays the guest's own, and this file can be
 * deleted. Visible exit criterion: full validation matrix passes with
 * P2K_NO_CYRIX_STUB=1 (proves the stub is no longer load-bearing).
 *
 * Validation 2026-04 — kill-switch is currently still load-bearing:
 *
 *   matrix run with P2K_NO_CYRIX_STUB=1 (vs baseline same matrix):
 *     swe1-default       : F=0 T=1   (was T=0)  Cyx=2  monitor-loop
 *     swe1-update-0210   : F=0 T=1   (was T=0)  samples 2 (was 14)
 *     rfm-default        : F=0 T=0
 *     rfm-update-0260    : F=0 T=1   (was T=0)  samples 0 (was 2)
 *
 *   so disabling the stub regresses 3 of 4 configs (one trap +
 *   loss of attract audio on the update paths). We keep it on by
 *   default until a real TCG decoder lands.
 *
 * Until then: the env-var kill switch P2K_NO_CYRIX_STUB lets the
 * patch be disabled while wiring the proper fix.
 * ============================================================================
 *
 * pinball2000 — Cyrix 0F3C (BB0_RESET) #UD emulator.
 *
 * The MediaGX/Cyrix 5530 implements a non-Intel opcode `0F 3C` used by
 * the XINU/Williams init code to manipulate Cyrix-specific MSR-style
 * registers via [EDX]/[EDX+4]. Vanilla QEMU's TCG i386 CPU does not
 * recognise this opcode and raises #UD (vector 6), which on the bare
 * XINU IDT lands in trap_dispatch() → "Trap to monitor".
 *
 * Mirror unicorn.old/src/cpu.c:587-628: once XINU has loaded an IDT and
 * installed clkint at IDT[0x20] (i.e. xinu_ready), inject a 32-bit
 * trap-handler at 0x540 that:
 *   - inspects the faulting instruction at [ESP+8] (return EIP)
 *   - if the bytes are `0F 3C`: stores EAX/EBX into [EDX]/[EDX+4],
 *     advances the saved EIP by 2, IRETs.
 *   - otherwise: skip the instruction and IRET (best-effort, prevents
 *     monitor-trap on other anomalies before clkint).
 *
 * Patch IDT[6] to a 32-bit interrupt gate { offset=0x540, sel=0x08,
 * type=0x8E (P=1, DPL=0, 32-bit interrupt gate) }.
 *
 * Address 0x540 chosen to sit in the XINU "low-mem below IVT" scratch
 * area (the IRQ0 EOI shim that previously lived just below at 0x500
 * has since been deleted; the address is preserved for diff stability).
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "exec/cpu-common.h"
#include "target/i386/cpu.h"

#include "p2k-internal.h"

#define P2K_CYRIX_STUB_ADDR  0x00000540u
#define P2K_CYRIX_POLL_NS    (2 * 1000 * 1000)   /* 2 ms */

/* Hand-assembled 32-bit interrupt-handler stub.
 *
 * On entry, the CPU pushed (in order, top last):
 *   [ESP+0]  EIP   (return address — the faulting instruction)
 *   [ESP+4]  CS
 *   [ESP+8]  EFLAGS
 *
 * Implementation:
 *   PUSH EAX
 *   PUSH ESI
 *   MOV  ESI, [ESP+8]            ; load faulting EIP
 *   CMP  WORD [ESI], 0x3C0F      ; little-endian 0F 3C
 *   JNE  .not_cyrix
 *   MOV  EAX, [ESP+4]            ; saved EAX (we pushed EAX first)
 *   MOV  [EDX], EAX
 *   MOV  [EDX+4], EBX
 *   ADD  EDX, 8
 *   ADD  DWORD [ESP+8], 2        ; skip the 0F 3C in return EIP
 *   POP  ESI
 *   POP  EAX
 *   IRET
 * .not_cyrix:
 *   POP  ESI
 *   POP  EAX
 *   IRET                         ; let other #UD propagate (will re-trap)
 */
static const uint8_t p2k_cyrix_stub[] = {
    0x50,                                     /* PUSH EAX            */
    0x56,                                     /* PUSH ESI            */
    0x8B, 0x74, 0x24, 0x08,                   /* MOV ESI,[ESP+8]     */
    0x66, 0x81, 0x3E, 0x0F, 0x3C,             /* CMP WORD [ESI],0x3C0F */
    0x75, 0x10,                               /* JNE .not_cyrix (+16) */
    0x8B, 0x44, 0x24, 0x04,                   /* MOV EAX,[ESP+4]     */
    0x89, 0x02,                               /* MOV [EDX],EAX       */
    0x89, 0x5A, 0x04,                         /* MOV [EDX+4],EBX     */
    0x83, 0xC2, 0x08,                         /* ADD EDX,8           */
    0x83, 0x44, 0x24, 0x08, 0x02,             /* ADD DWORD [ESP+8],2 */
    0x5E,                                     /* POP ESI             */
    0x58,                                     /* POP EAX             */
    0xCF,                                     /* IRET                */
    /* .not_cyrix: */
    0x5E,                                     /* POP ESI             */
    0x58,                                     /* POP EAX             */
    0xCF,                                     /* IRET                */
};

static QEMUTimer *p2k_cyrix_timer;
static bool       p2k_cyrix_installed;

static bool p2k_xinu_ready(CPUX86State **out_env)
{
    CPUState *cs;
    CPU_FOREACH(cs) {
        X86CPU *cpu = X86_CPU(cs);
        CPUX86State *env = &cpu->env;
        if (env->idt.base == 0 || env->idt.limit < 6 * 8 + 7) {
            continue;
        }
        /* As soon as XINU has loaded *any* IDT we install the handler.
         * unicorn waited for clkint at IDT[0x20], but our 0F3C ops fire
         * during XINU sysinit() — well before clkinit() runs. */
        *out_env = env;
        return true;
    }
    return false;
}

static void p2k_cyrix_install(CPUX86State *env)
{
    cpu_physical_memory_write(P2K_CYRIX_STUB_ADDR,
                              p2k_cyrix_stub, sizeof(p2k_cyrix_stub));

    /* IDT[6] = 32-bit interrupt gate -> 0x08:0x540 */
    uint32_t a = P2K_CYRIX_STUB_ADDR;
    uint8_t gate[8] = {
        (uint8_t)(a & 0xff),
        (uint8_t)((a >> 8) & 0xff),
        0x08, 0x00,            /* selector = 0x08 (kernel CS) */
        0x00,                  /* zero */
        0x8E,                  /* type = P=1, DPL=0, 32-bit interrupt gate */
        (uint8_t)((a >> 16) & 0xff),
        (uint8_t)((a >> 24) & 0xff),
    };
    cpu_physical_memory_write(env->idt.base + 6 * 8, gate, sizeof(gate));

    info_report("pinball2000: Cyrix 0F3C #UD emulator installed at 0x%x; "
                "IDT[6] redirected (xinu_ready, idt.base=0x%lx)",
                P2K_CYRIX_STUB_ADDR, (unsigned long)env->idt.base);
}

static void p2k_cyrix_disarm(void)
{
    if (p2k_cyrix_timer) {
        timer_free(p2k_cyrix_timer);
        p2k_cyrix_timer = NULL;
    }
}

static void p2k_cyrix_tick(void *opaque)
{
    CPUX86State *env = NULL;
    if (!p2k_cyrix_installed && p2k_xinu_ready(&env)) {
        p2k_cyrix_install(env);
        p2k_cyrix_installed = true;
        p2k_cyrix_disarm();
        return;
    }
    timer_mod(p2k_cyrix_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_CYRIX_POLL_NS);
}

void p2k_install_cyrix_0f3c(void)
{
    const char *off = getenv("P2K_NO_CYRIX_STUB");
    if (off && *off && off[0] != '0') {
        info_report("pinball2000: Cyrix 0F3C #UD stub disabled by "
                    "P2K_NO_CYRIX_STUB");
        return;
    }
    p2k_cyrix_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                   p2k_cyrix_tick, NULL);
    timer_mod(p2k_cyrix_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_CYRIX_POLL_NS);
    info_report("pinball2000: Cyrix 0F3C #UD installer armed");
}
