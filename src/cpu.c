/*
 * cpu.c — Unicorn Engine CPU setup, interrupt injection, execution loop.
 *
 * Key design: Unicorn UC_MODE_32 for i386 guest on x64 host.
 * Timer interrupt injection between emulation slices (no hardware PIC in Unicorn).
 * SIGALRM at 100Hz triggers uc_emu_stop() → check IRQs → inject → resume.
 */
#include "encore.h"

/* Forward declarations for hooks */
static void hook_insn_in(uc_engine *uc, uint32_t port, int size, void *user_data);
static uint32_t hook_insn_in_val(uc_engine *uc, uint32_t port, int size, void *user_data);
static void hook_insn_out(uc_engine *uc, uint32_t port, int size, uint32_t value, void *user_data);
static bool hook_mem_invalid(uc_engine *uc, uc_mem_type type, uint64_t addr,
                             int size, int64_t value, void *user_data);
static void hook_code_trace(uc_engine *uc, uint64_t addr, uint32_t size, void *user_data);

/* SIGALRM handler — sets timer flag only. We use instruction-count-based
 * batching (not SIGALRM-based stopping) to control execution granularity. */
void cpu_timer_handler(int sig)
{
    (void)sig;
    g_emu.timer_fired = true;
}

int cpu_init(void)
{
    uc_err err = uc_open(UC_ARCH_X86, UC_MODE_32, &g_emu.uc);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "[cpu] uc_open failed: %s\n", uc_strerror(err));
        return -1;
    }

    uc_engine *uc = g_emu.uc;

    /* Hook IN/OUT instructions for I/O port emulation */
    uc_hook h_in, h_out;
    uc_hook_add(uc, &h_in, UC_HOOK_INSN, (void*)hook_insn_in_val,
                NULL, 1, 0, UC_X86_INS_IN);
    uc_hook_add(uc, &h_out, UC_HOOK_INSN, (void*)hook_insn_out,
                NULL, 1, 0, UC_X86_INS_OUT);

    /* Hook invalid memory accesses */
    uc_hook h_inv;
    uc_hook_add(uc, &h_inv, UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED |
                UC_HOOK_MEM_FETCH_UNMAPPED | UC_HOOK_MEM_WRITE_PROT |
                UC_HOOK_MEM_FETCH_PROT, (void*)hook_mem_invalid, NULL, 1, 0);

    /* MMIO hooks for BAR regions + GX_BASE */
    uc_hook h_bar_r, h_bar_w;
    uc_err e;
    e = uc_hook_add(uc, &h_bar_r, UC_HOOK_MEM_READ, (void*)bar_mmio_read,
                NULL, (uint64_t)WMS_BAR0, (uint64_t)(WMS_BAR4 + BAR4_SIZE - 1));
    if (e) LOG("cpu", "BAR read hook FAILED: %s\n", uc_strerror(e));
    e = uc_hook_add(uc, &h_bar_w, UC_HOOK_MEM_WRITE, (void*)bar_mmio_write,
                NULL, (uint64_t)WMS_BAR0, (uint64_t)(WMS_BAR4 + BAR4_SIZE - 1));
    if (e) LOG("cpu", "BAR write hook FAILED: %s\n", uc_strerror(e));

    /* GX_BASE MMIO (0x40000000 - 0x41000000) */
    uc_hook h_gx_r, h_gx_w;
    e = uc_hook_add(uc, &h_gx_r, UC_HOOK_MEM_READ, (void*)bar_mmio_read,
                NULL, (uint64_t)GX_BASE, (uint64_t)(GX_BASE + GX_BASE_SIZE - 1));
    if (e) LOG("cpu", "GX read hook FAILED: %s\n", uc_strerror(e));
    e = uc_hook_add(uc, &h_gx_w, UC_HOOK_MEM_WRITE, (void*)bar_mmio_write,
                NULL, (uint64_t)GX_BASE, (uint64_t)(GX_BASE + GX_BASE_SIZE - 1));
    if (e) LOG("cpu", "GX write hook FAILED: %s\n", uc_strerror(e));

    /* Code trace hook for Init2 checkpoints (0x80000 - 0x90000)
     * and game entry point (0x100000) */
    uc_hook h_trace1, h_trace2;
    uc_hook_add(uc, &h_trace1, UC_HOOK_CODE, (void*)hook_code_trace,
                NULL, (uint64_t)0x801BF, (uint64_t)0x801F8);
    uc_hook_add(uc, &h_trace2, UC_HOOK_CODE, (void*)hook_code_trace,
                NULL, (uint64_t)0x808FC, (uint64_t)0x80BA0);
    uc_hook h_trace3, h_trace4, h_trace5;
    uc_hook_add(uc, &h_trace3, UC_HOOK_CODE, (void*)hook_code_trace,
                NULL, (uint64_t)0x83B20, (uint64_t)0x83DE2);
    uc_hook_add(uc, &h_trace4, UC_HOOK_CODE, (void*)hook_code_trace,
                NULL, (uint64_t)0x100000, (uint64_t)0x100010);
    uc_hook_add(uc, &h_trace5, UC_HOOK_CODE, (void*)hook_code_trace,
                NULL, (uint64_t)0x88000, (uint64_t)0x8B000);

    LOG("cpu", "Unicorn Engine initialized (i386 mode)\n");
    return 0;
}

/*
 * Set up 32-bit protected mode with flat segments.
 * Skips BIOS POST entirely — sets up what the PRISM option ROM's
 * INT 19 handler would do after switching to protected mode.
 */
int cpu_setup_protected_mode(void)
{
    uc_engine *uc = g_emu.uc;
    if (!uc) return -1;

    /*
     * GDT layout (from PRISM option ROM at offset 0x5BC):
     *   [0] Null descriptor
     *   [1] CS=0x08: flat 32-bit code (base=0, limit=4GB, DPL=0)
     *   [2] DS=0x10: flat 32-bit data (base=0, limit=4GB, DPL=0)
     *   [3] 16-bit code (base=0, limit=1MB, for transitions)
     */
    static const uint8_t gdt[] = {
        /* [0] Null */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /* [1] CS=0x08: base=0, limit=FFFFF, G=1(4K pages), D=1(32-bit), type=0xF(code+conf+r+a) */
        0xFF, 0xFF, 0x00, 0x00, 0x00, 0x9F, 0xCF, 0x00,
        /* [2] DS=0x10: base=0, limit=FFFFF, G=1, D=1, type=0x3(data+w+a) */
        0xFF, 0xFF, 0x00, 0x00, 0x00, 0x93, 0xCF, 0x00,
        /* [3] 16-bit CS=0x18: base=0, limit=FFFFF, G=0, D=0, type=0xB(code+r+a) */
        0xFF, 0xFF, 0x00, 0x00, 0x00, 0x9B, 0x0F, 0x00,
    };

    /* Write GDT to guest memory at a safe location */
    #define GDT_ADDR 0x00001000
    uc_mem_write(uc, GDT_ADDR, gdt, sizeof(gdt));

    /* Load GDTR */
    uc_x86_mmr gdtr = { .selector = 0, .base = GDT_ADDR,
                        .limit = sizeof(gdt) - 1, .flags = 0 };
    uc_reg_write(uc, UC_X86_REG_GDTR, &gdtr);

    /* Set CR0: PE=1 (protected mode), ET=1 (387 present) */
    uint32_t cr0 = 0x00000011;
    uc_reg_write(uc, UC_X86_REG_CR0, &cr0);

    /* Set segment selectors for flat 32-bit mode */
    uint32_t cs_sel = 0x08;
    uint32_t ds_sel = 0x10;
    uc_reg_write(uc, UC_X86_REG_CS, &cs_sel);
    uc_reg_write(uc, UC_X86_REG_DS, &ds_sel);
    uc_reg_write(uc, UC_X86_REG_ES, &ds_sel);
    uc_reg_write(uc, UC_X86_REG_SS, &ds_sel);
    uc_reg_write(uc, UC_X86_REG_FS, &ds_sel);
    uc_reg_write(uc, UC_X86_REG_GS, &ds_sel);

    /* Set EFLAGS: only reserved bit 1 set, IF=0 */
    uint32_t eflags = 0x00000002;
    uc_reg_write(uc, UC_X86_REG_EFLAGS, &eflags);

    /* Copy PRISM option ROM (32KB from bank0) to 0x80000 (what INT 19 handler does) */
    uint8_t optrom[0x8000];
    uc_mem_read(uc, PLX_BANK0, optrom, 0x8000);
    uc_mem_write(uc, 0x80000, optrom, 0x8000);

    /* Set stack pointer */
    uint32_t esp = 0x8B000;
    uc_reg_write(uc, UC_X86_REG_ESP, &esp);

    /* EIP = 0x801D9 — PM entry point (skips real-mode call pair at 0x801BF/0x801C4) */
    uint32_t eip = 0x801D9;
    uc_reg_write(uc, UC_X86_REG_EIP, &eip);

    LOG("cpu", "Protected mode setup: GDT at 0x%x, CS=0x%x DS=0x%x\n",
        GDT_ADDR, cs_sel, ds_sel);
    LOG("cpu", "Entry point: EIP=0x%08x ESP=0x%08x\n", eip, esp);
    return 0;
}

/*
 * Inject a hardware interrupt into the guest CPU.
 * Protected mode same-privilege delivery:
 *   1. Read IDT entry for vector
 *   2. Push EFLAGS, CS, EIP onto guest stack
 *   3. Clear IF
 *   4. Set EIP to handler address
 */
void cpu_inject_interrupt(uint8_t vector)
{
    uc_engine *uc = g_emu.uc;
    static int inject_ok = 0, inject_blocked = 0, inject_stub = 0;

    /* Read current CPU state.
     * CRITICAL: initialize to 0 before uc_reg_read.
     * Unicorn writes int16_t (2 bytes) for CS — if cs is uninitialized,
     * the high 2 bytes will be stack garbage → corrupted interrupt frame
     * → GP fault on IRET inside the clkint handler. */
    uint32_t eip = 0, esp = 0, eflags = 0, cs = 0;
    uc_reg_read(uc, UC_X86_REG_EIP, &eip);
    uc_reg_read(uc, UC_X86_REG_ESP, &esp);
    uc_reg_read(uc, UC_X86_REG_EFLAGS, &eflags);
    uc_reg_read(uc, UC_X86_REG_CS, &cs);
    cs &= 0xFFFF;  /* Keep only the 16-bit selector */

    /* Check IF flag — don't inject if interrupts are disabled */
    if (!(eflags & 0x200)) {
        inject_blocked++;
        return;
    }

    /* Read IDTR */
    uc_x86_mmr idtr;
    uc_reg_read(uc, UC_X86_REG_IDTR, &idtr);

    /* Read IDT entry (8 bytes per entry in 32-bit protected mode) */
    uint8_t idt_entry[8];
    uint64_t idt_addr = idtr.base + vector * 8;
    uc_err err = uc_mem_read(uc, idt_addr, idt_entry, 8);
    if (err != UC_ERR_OK) return;

    uint16_t offset_lo = idt_entry[0] | (idt_entry[1] << 8);
    uint16_t selector  = idt_entry[2] | (idt_entry[3] << 8);
    uint16_t offset_hi = idt_entry[6] | (idt_entry[7] << 8);
    uint32_t handler   = offset_lo | (offset_hi << 16);
    (void)selector;

    /* Skip if handler is our stub or NULL */
    if (handler == 0 || handler == 0x20000u || handler == 0x20000000u) {
        inject_stub++;
        return;
    }

    inject_ok++;
    if (inject_ok <= 5 || (inject_ok % 100 == 0)) {
        LOG("irq", "vec=0x%02x → handler=0x%08x EIP=0x%08x (ok=%d blk=%d stub=%d)\n",
            vector, handler, eip, inject_ok, inject_blocked, inject_stub);
    }

    /* Push interrupt frame: EFLAGS, CS, EIP (x86 interrupt pushes from
     * high to low: EFLAGS at highest, then CS, then EIP at lowest) */
    esp -= 4; uc_mem_write(uc, esp, &eflags, 4);
    esp -= 4;
    uint32_t cs32 = cs;
    uc_mem_write(uc, esp, &cs32, 4);
    esp -= 4; uc_mem_write(uc, esp, &eip, 4);

    /* Update registers */
    uc_reg_write(uc, UC_X86_REG_ESP, &esp);

    /* Clear IF (interrupt gate behavior) */
    eflags &= ~0x200u;
    uc_reg_write(uc, UC_X86_REG_EFLAGS, &eflags);

    /* Jump to handler */
    uc_reg_write(uc, UC_X86_REG_EIP, &handler);

    /* Set CS if needed (XINU uses flat segments, CS stays same) */
    if (selector != cs) {
        uint32_t new_cs = selector;
        uc_reg_write(uc, UC_X86_REG_CS, &new_cs);
    }

    /* Debug: verify stack frame after injection */
    if (inject_ok <= 3) {
        uint32_t v_eip, v_cs, v_ef;
        uc_mem_read(uc, esp,     &v_eip, 4);
        uc_mem_read(uc, esp + 4, &v_cs,  4);
        uc_mem_read(uc, esp + 8, &v_ef,  4);
        uint32_t act_eip, act_esp;
        uc_reg_read(uc, UC_X86_REG_EIP, &act_eip);
        uc_reg_read(uc, UC_X86_REG_ESP, &act_esp);
        LOG("irq", "  frame: [ESP+0]=0x%08x [+4]=0x%08x [+8]=0x%08x ESP=0x%08x→handler=0x%08x\n",
            v_eip, v_cs, v_ef, act_esp, act_eip);
    }
}

/* Check PIC for pending interrupts and inject highest priority.
 * Called from exec loop to handle non-timer IRQs (e.g. IRQ4 UART THRE). */
static void check_and_inject_irq(void)
{
    for (int pic_idx = 0; pic_idx < 2; pic_idx++) {
        PICState *pic = &g_emu.pic[pic_idx];
        uint8_t pending = pic->irr & ~pic->imr & ~pic->isr;
        if (!pending) continue;

        /* Find highest priority (lowest bit) */
        for (int bit = 0; bit < 8; bit++) {
            if (pending & (1 << bit)) {
                uint8_t vector = pic->icw2 + bit;

                /* If slave PIC, check cascade through master IRQ2 */
                if (pic_idx == 1) {
                    /* Check master allows cascade (IRQ2 not masked) */
                    if (g_emu.pic[0].imr & 0x04) continue;
                }

                /* Mark in-service, clear request */
                pic->isr |= (1 << bit);
                pic->irr &= ~(1 << bit);

                cpu_inject_interrupt(vector);
                return;
            }
        }
    }
}

/* Code hook for tracing Init2 checkpoints */
static void hook_code_trace(uc_engine *uc, uint64_t addr, uint32_t size, void *user_data)
{
    uint32_t eax, ebx, esp;
    uc_reg_read(uc, UC_X86_REG_EAX, &eax);
    uc_reg_read(uc, UC_X86_REG_EBX, &ebx);
    uc_reg_read(uc, UC_X86_REG_ESP, &esp);

    switch ((uint32_t)addr) {
    case 0x801BF: LOG("trace", "Entry 0x801BF (INT19 handler)\n"); break;
    case 0x801C9: LOG("trace", "PM switch at 0x801C9\n"); break;
    case 0x801D9: LOG("trace", "PM entry 0x801D9\n"); break;
    case 0x801ED: LOG("trace", "Second reloc call 0x801ED\n"); break;
    case 0x801F2: LOG("trace", "Second Init2 call 0x801F2\n"); break;
    case 0x801F7: LOG("trace", "!!! GARBLED 0x801F7 reached !!!\n"); break;
    case 0x808FC: LOG("trace", "Init2 enter ESP=0x%08x\n", esp); break;
    case 0x80904: LOG("trace", "Init2 sub-calls start\n"); break;
    case 0x80922: LOG("trace", "Pre PCI-enum push\n"); break;
    case 0x80929: LOG("trace", "Post PCI-enum EAX=0x%08x → EBX\n", eax); break;
    case 0x80933: LOG("trace", "Call 0x83488 (1st) EBX=%d\n", ebx); break;
    case 0x80959: LOG("trace", "Call 0x83488 (2nd)\n"); break;
    case 0x80981: LOG("trace", "Update check: CMP EBX(%d), 1\n", ebx); break;
    case 0x8098A: LOG("trace", "UPDATE PATH: MOV EBX=0x12000000\n"); break;
    case 0x809A4: LOG("trace", "Validate boot data call\n"); break;
    case 0x809AC: LOG("trace", "Boot data result EAX=%d\n", eax); break;
    case 0x809CF: LOG("trace", "GameID check: flash[0x3C]=0x%08x vs BAR5\n", eax); break;
    case 0x80A56: LOG("trace", "GAME ENTRY: EAX=[EBX+0x48]=0x%08x\n", eax); break;
    case 0x80A5E: LOG("trace", ">>> CALL EAX (game jump!) EAX=0x%08x\n", eax); break;
    case 0x80A98: LOG("trace", "FAIL: boot data bad\n"); break;
    case 0x80B9C: LOG("trace", "NO UPDATE path at 0x80B9C\n"); break;
    case 0x83B20: LOG("trace", "PCI enum 0x83B20 enter\n"); break;
    case 0x83B29: {
        uint8_t guard;
        uc_mem_read(uc, 0x86CB0, &guard, 1);
        LOG("trace", "PCI enum guard=[0x86CB0]=%d\n", guard);
        break;
    }
    case 0x83DC5: LOG("trace", "PCI enum success EAX=1\n"); break;
    case 0x83DD6: LOG("trace", "PCI enum FAIL EAX=-1\n"); break;
    case 0x100000: LOG("trace", "*** GAME CODE ENTRY at 0x100000! ***\n"); break;
    default:
        if (addr >= 0x88000 && addr < 0x8B000) {
            LOG("trace", "!!! STACK EXEC addr=0x%08x ESP=0x%08x !!!\n",
                (uint32_t)addr, esp);
        }
        break;
    }
    fflush(stdout);
}

/* Main execution loop */
void cpu_run(void)
{
    uc_engine *uc = g_emu.uc;
    uint32_t eip;
    uc_reg_read(uc, UC_X86_REG_EIP, &eip);
    LOG("cpu", "Starting execution at EIP=0x%08x\n", eip);

    int display_timer = 0;
    struct timespec last_time, now;
    clock_gettime(CLOCK_MONOTONIC, &last_time);

    while (g_emu.running) {
        /* Accumulate timer events — only after XINU's clkinit() has installed
         * the real timer handler. When timer fires, set PIC[0].IRR bit 0 to
         * simulate hardware PIT → PIC → CPU interrupt delivery chain.
         * check_and_inject_irq() then handles delivery respecting both IF and IMR. */
        if (g_emu.timer_fired) {
            g_emu.timer_fired = false;
            if (g_emu.xinu_ready)
                g_emu.pic[0].irr |= 0x01;  /* IRQ0 pending in PIC */
        }

        /* Detect XINU timer readiness via IDT[0x20].
         *
         * Two-phase approach (i386 POC BT-91):
         * Phase 1: IDT[0x20] changes from generic trap → real clkint handler
         *   (handler > 0x100000). Set early by clkinit() → set_evec() during sysinit().
         * Phase 2: Wait until xinu_booted (UART "XINU: V7" seen) + 50 batches.
         *   "XINU: V7" is printed by XINU *after* sysinit() completes, meaning the
         *   process table, scheduler, and watchdog_bone process are all initialised.
         *   50 batches (~10M insns) of grace gives ctxsw a chance to settle before
         *   the first interrupt arrives. This fires well before the watchdog deadline. */
        if (g_emu.game_started && !g_emu.xinu_ready) {
            uc_x86_mmr idtr;
            uc_reg_read(uc, UC_X86_REG_IDTR, &idtr);
            if (idtr.base != 0 && idtr.limit >= 0x20 * 8 + 7) {
                uint8_t idt_entry[8];
                uc_err ierr = uc_mem_read(uc, idtr.base + 0x20 * 8, idt_entry, 8);
                if (ierr == UC_ERR_OK) {
                    uint16_t off_lo = idt_entry[0] | (idt_entry[1] << 8);
                    uint16_t off_hi = idt_entry[6] | (idt_entry[7] << 8);
                    uint32_t handler = off_lo | ((uint32_t)off_hi << 16);
                    if (handler > 0x100000u) {
                        if (g_emu.clkint_ready_exec == 0) {
                            g_emu.clkint_ready_exec = g_emu.exec_count;
                            uint32_t check_eip = 0;
                            uc_reg_read(uc, UC_X86_REG_EIP, &check_eip);
                            LOG("irq", "clkint detected: IDT[0x20]=0x%08x EIP=0x%08x exec=%u\n",
                                handler, check_eip, (unsigned)g_emu.exec_count);
                        }
                        if (g_emu.xinu_booted &&
                            g_emu.exec_count >= g_emu.clkint_ready_exec + 50) {
                            g_emu.xinu_ready = true;
                            uint32_t check_eip = 0;
                            uc_reg_read(uc, UC_X86_REG_EIP, &check_eip);
                            LOG("irq", "XINU ready: timer injection enabled EIP=0x%08x exec=%u\n",
                                check_eip, (unsigned)g_emu.exec_count);
                        }
                    } else if (g_emu.exec_count % 5000 == 0) {
                        uint32_t check_eip = 0;
                        uc_reg_read(uc, UC_X86_REG_EIP, &check_eip);
                        LOG("irq", "waiting for clkint: IDT[0x20]=0x%08x EIP=0x%08x exec=%u xinu_booted=%d\n",
                            handler, check_eip, (unsigned)g_emu.exec_count, g_emu.xinu_booted);
                    }
                }
            }
        }

        /* ================================================================
         * Post-XINU initialization: comprehensive V1.19 patches from
         * both i386 and x64 POC analysis.
         *
         * Critical patches (from x64 POC sgc-seed + i386 BT-85/BT-91):
         * - Fatal/NonFatal/CMOS → safe returns
         * - Q-table pre-init (BT-85: garbage causes insert() self-loop!)
         * - BSS zeroing (0x33D800-0x800000)
         * - 8 Fatal call site NOPs + 3 monitor() NOPs
         * - Monitor auto-exit triple patch
         * - Init gate flags + DCS2 + XINACMOS + BAR2 regs
         * ================================================================ */
        if (g_emu.xinu_booted && g_emu.ctor_phase == 0 &&
            g_emu.game_started && g_emu.xinu_ready) {
            g_emu.ctor_phase = 3;  /* one-shot */

            /* --- 1. Function patches (x64 POC BT-86/BT-100) --- */

            /* Fatal() at 0x22722C → INC counter + HLT for recovery */
            {
                uint8_t fatal_patch[] = {
                    0xFF, 0x05, 0xF0, 0xFF, 0x2B, 0x00,  /* INC [0x2BFFF0] */
                    0x89, 0x35, 0xF4, 0xFF, 0x2B, 0x00,  /* MOV [0x2BFFF4], ESI */
                    0xF4,                                   /* HLT */
                    0xEB, 0xFE                              /* JMP $ */
                };
                uc_mem_write(uc, 0x0022722Cu, fatal_patch, sizeof(fatal_patch));
            }
            /* LocMgr Fatal wrapper at 0x24743C → XOR EAX,EAX; RET (x64 POC) */
            {
                uint8_t xar[] = { 0x31, 0xC0, 0xC3 };
                uc_mem_write(uc, 0x0024743Cu, xar, 3);
            }
            /* NonFatal at 0x24780C → XOR EAX,EAX; RET (x64 POC BT-86) */
            {
                uint8_t xar[] = { 0x31, 0xC0, 0xC3 };
                uc_mem_write(uc, 0x0024780Cu, xar, 3);
            }
            /* CMOS mem_test at 0x24FDEC → MOV EAX,1; RET (x64 POC BT-100) */
            {
                uint8_t pass[] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3 };
                uc_mem_write(uc, 0x0024FDECu, pass, 6);
            }
            /* pci_watchdog_bone at 0x1A4190 → RET */
            {
                uint8_t ret = 0xC3;
                uc_mem_write(uc, 0x001A4190u, &ret, 1);
            }
            LOG("init", "Function patches: Fatal@22722C NonFatal@24780C CMOS@24FDEC LocMgr@24743C watchdog@1A4190\n");

            /* --- 2. Fatal call site NOPs (x64 POC sgc-seed) --- */
            {
                static const uint32_t fatal_sites[] = {
                    0x22F583u, 0x22F611u, 0x22F6AFu, 0x22F6DAu,
                    0x22F71Cu, 0x22F335u, 0x22F3A3u, 0x22F3FDu,
                };
                uint8_t nops[10];
                memset(nops, 0x90, sizeof(nops));
                for (int i = 0; i < 8; i++)
                    uc_mem_write(uc, fatal_sites[i], nops, 10);
                /* monitor() call site NOPs */
                uint8_t nop5[5] = { 0x90, 0x90, 0x90, 0x90, 0x90 };
                uc_mem_write(uc, 0x00247751u, nop5, 5);
                uc_mem_write(uc, 0x0022A384u, nop5, 5);
                uc_mem_write(uc, 0x0029346Fu, nop5, 5);
                LOG("init", "NOPed 8 Fatal + 3 monitor() call sites\n");
            }

            /* --- 3. Monitor auto-exit (x64 POC sgc-seed) --- */
            {
                uint8_t nop2[] = { 0x90, 0x90 };
                uc_mem_write(uc, 0x0028ED5Eu, nop2, 2);  /* eval JNE → NOP */
                uc_mem_write(uc, 0x0028F3AAu, nop2, 2);  /* loop JNE → NOP */
                uint8_t jmp = 0xEB;
                uc_mem_write(uc, 0x0028F390u, &jmp, 1);  /* JE → JMP (unconditional) */
                LOG("init", "Monitor auto-exit: eval@28ED5E loop@28F3AA jmp@28F390\n");
            }

            /* --- 4. PLX found flag (x64 POC BT-100) --- */
            {
                uint32_t plx_id = 0x10B5u;
                uc_mem_write(uc, 0x002E98F8u, &plx_id, 4);
            }

            /* --- 5. BSS zeroing — SKIPPED: applied post-XINU would destroy
             *    process stacks and XINU data. x64 POC applies this at SGC
             *    time (before XINU). Our RAM is already zeroed at startup. --- */

            /* --- 6. BT-85: Q-table pre-init — SKIPPED for same reason.
             *    XINU's initevec/newqueue already initialized it properly.
             *    Writing EMPTY sentinels NOW would destroy XINU's queue state. --- */

            /* --- 7. Init gate flags + DCS2 + misc (i386 POC post-ctor) --- */
            {
                uint32_t one = 1, zero = 0;
                uc_mem_write(uc, 0x2797C4u, &zero, 4);   /* DCS2 init complete */
                uc_mem_write(uc, 0x2AF824u, &one, 4);     /* system init prereq */
                uc_mem_write(uc, 0x27979Cu, &one, 4);     /* game init gate */
                uc_mem_write(uc, 0x2AF5D4u, &one, 4);     /* DCS2 hw ready */
                uc_mem_write(uc, 0x2AF5E0u, &one, 4);     /* DCS2 hw ready */
                uc_mem_write(uc, 0x296AF4u, &one, 4);     /* DCS2 hw ready */
                uc_mem_write(uc, 0x2AF81Cu, &one, 4);     /* resched ctxsw gate */
                uc_mem_write(uc, 0x2AF6A4u, &zero, 4);    /* ISR depth = 0 */
                uc_mem_write(uc, 0x2BDB00u, &one, 4);     /* resource table enabled */
                uint32_t ret_stub = 0x400000u;
                uc_mem_write(uc, 0x296AE4u, &ret_stub, 4); /* DCS2 dispatch → RET */
                uint32_t bar0 = WMS_BAR0;
                uc_mem_write(uc, 0x279768u, &bar0, 4);     /* PLX BAR0 ptr */
                LOG("init", "Init gate + DCS2 flags set\n");
            }

            /* --- 8. XINACMOS fake object (i386 POC BT-33) --- */
            {
                uint8_t xor_ret[3] = { 0x31, 0xC0, 0xC3 };
                uc_mem_write(uc, 0x400010u, xor_ret, 3);
                for (int i = 0; i < 16; i++) {
                    uint32_t stub = 0x400010u;
                    uc_mem_write(uc, 0x400340u + i * 4, &stub, 4);
                }
                uint8_t obj[128];
                memset(obj, 0, sizeof(obj));
                *(uint32_t *)(obj + 0) = 0x400340u;
                *(uint32_t *)(obj + 24) = 0x400010u;
                memcpy(obj + 32, "XINACMOS", 9);
                uc_mem_write(uc, 0x400300u, obj, sizeof(obj));
                uint32_t obj_ptr = 0x400300u;
                uc_mem_write(uc, 0x2C181Cu, &obj_ptr, 4);
                LOG("init", "XINACMOS obj @0x400300\n");
            }

            /* --- 9. BAR2 DCS2 register initialization (i386 POC BT-33) --- */
            {
                struct { uint32_t off; uint32_t val; } bar2_regs[] = {
                    { 0x00, 0x00002400 }, { 0x04, 0 }, { 0x08, 0x00000006 },
                    { 0x0C, 0x00000476 }, { 0x10, 0 }, { 0x14, 0x11000050 },
                    { 0x18, 0x00000008 }, { 0x1C, 0x00000008 },
                    { 0x20, 0x0000011D }, { 0x24, 0 }, { 0x28, 0x11001B14 },
                };
                for (size_t i = 0; i < sizeof(bar2_regs)/sizeof(bar2_regs[0]); i++)
                    uc_mem_write(uc, WMS_BAR2 + bar2_regs[i].off,
                                 &bar2_regs[i].val, 4);
                uint8_t zbuf[6 * 0x11D];
                memset(zbuf, 0, sizeof(zbuf));
                uc_mem_write(uc, WMS_BAR2 + 0x1B14u, zbuf, sizeof(zbuf));
                uint32_t cmos_hdr = 0x11005000u, cmos_z = 0;
                uc_mem_write(uc, WMS_BAR2 + 0x21C0u, &cmos_hdr, 4);
                uc_mem_write(uc, WMS_BAR2 + 0x21C4u, &cmos_z, 4);
                LOG("init", "BAR2 DCS2 regs initialized\n");
            }

            /* --- 10. Free list safety net --- */
            {
                uint32_t fl = 0;
                uc_mem_read(uc, 0x2D577Cu, &fl, 4);
                if (fl == 0 || fl >= 0x1000000u) {
                    uint32_t fblk = 0x400000u, fblk_sz = 0xB00000u;
                    uint32_t zero = 0;
                    uc_mem_write(uc, fblk + 0, &zero, 4);
                    uc_mem_write(uc, fblk + 4, &fblk_sz, 4);
                    uc_mem_write(uc, 0x2D577Cu, &fblk, 4);
                    LOG("init", "Free list: 0x%x sz=0x%x\n", fblk, fblk_sz);
                }
            }

            LOG("init", "=== Post-XINU V1.19 patches complete ===\n");
        }

        /* Inject all pending PIC interrupts (timer IRQ0 + others like IRQ4 UART).
         * check_and_inject_irq() respects both PIC IMR (hw mask) and CPU IF flag,
         * and properly tracks ISR (in-service) for correct priority resolution.
         * Only inject after XINU ready so IDT has real handlers. */
        if (g_emu.xinu_ready) {
            uint8_t pending0 = g_emu.pic[0].irr & ~g_emu.pic[0].imr;
            uint8_t pending1 = g_emu.pic[1].irr & ~g_emu.pic[1].imr;
            if (pending0 || pending1)
                check_and_inject_irq();
        }

        /* Adaptive batch size: small when pending IRQ waiting for IF/IMR window,
         * large for normal execution throughput. */
        uint8_t any_pending = (g_emu.pic[0].irr & ~g_emu.pic[0].imr) |
                              (g_emu.pic[1].irr & ~g_emu.pic[1].imr);
        size_t batch = any_pending ? 500 : 200000;

        /* Execute a batch of instructions */
        uc_reg_read(uc, UC_X86_REG_EIP, &eip);
        uc_err err = uc_emu_start(uc, eip, 0, 0, batch);

        g_emu.exec_count++;

        /* Watchdog health register: keep [watchdog_flag_addr] = 0xFFFF so
         * pci_read_watchdog() always sees an in-range health value and returns
         * 0 (not expired). This simulates the XINU timer regularly feeding the
         * hardware watchdog; real 200MHz hardware completes game init before
         * the watchdog can expire, but emulation is slower. (BT-107) */
        if (g_emu.watchdog_flag_addr && g_emu.game_started) {
            uint32_t healthy = 0x0000FFFFu;
            uc_mem_write(uc, g_emu.watchdog_flag_addr, &healthy, sizeof(healthy));
        }

        /* BT-107b: pci_watchdog_bone suppression (POC 0x2E98C8).
         * The game's watchdog_bone() countdown at 0x2E98C8 decrements each
         * tick. When it reaches 0, Fatal fires. Keep it at 0 so the check
         * never triggers. Separate from the health register above. */
        if (g_emu.game_started) {
            uint32_t zero = 0;
            uc_mem_write(uc, 0x002E98C8u, &zero, sizeof(zero));
        }

        /* Memory-zero sentinel: interval_0_25ms() checks [0x00000000] == 0
         * as a corruption guard. Guest code or stray DMA can corrupt address 0
         * during normal operation. Keep it zeroed to prevent false Fatal. */
        if (g_emu.game_started) {
            uint32_t zero = 0;
            uc_mem_write(uc, 0x00000000u, &zero, sizeof(zero));
        }

        /* BT-98: Display Manager render-pass watchdog suppression.
         * Once the game reaches its post-Allegro display path, a watchdog
         * counter at 0x002e8e30 is periodically refreshed by render_pass().
         * Without a host render thread, the counter can expire and trip
         * Fatal() before the game finishes printing its startup banner. */
        if (g_emu.game_started) {
            uint32_t dm_mode = 0;
            if (uc_mem_read(uc, 0x002e8e2Cu, &dm_mode, sizeof(dm_mode)) == UC_ERR_OK &&
                dm_mode == 1u) {
                uint32_t wd_cnt = 0;
                if (uc_mem_read(uc, 0x002e8e30u, &wd_cnt, sizeof(wd_cnt)) == UC_ERR_OK &&
                    wd_cnt < 2500u) {
                    uint32_t reset = 5000u;
                    uc_mem_write(uc, 0x002e8e30u, &reset, sizeof(reset));
                }
            }
        }

        /* Read EIP after execution stopped */
        uc_reg_read(uc, UC_X86_REG_EIP, &eip);

        if (err != UC_ERR_OK) {
            if (err == UC_ERR_INSN_INVALID) {
                uint8_t insn[2];
                uc_mem_read(uc, eip, insn, 2);
                if (insn[0] == 0xF4) {
                    /* HLT — wait for interrupt. Force IF=1 so pending timer
                     * can be delivered on next iteration. */
                    uint32_t efl;
                    uc_reg_read(uc, UC_X86_REG_EFLAGS, &efl);
                    efl |= 0x200;
                    uc_reg_write(uc, UC_X86_REG_EFLAGS, &efl);

                    /* BT-122: Fatal() / panic HLT recovery.
                     * When Fatal/panic hits HLT, redirect to prnull idle
                     * (0xFF0000: STI+HLT+JMP) with clean stack, matching
                     * POC behavior. Without this, guest falls into JMP $
                     * infinite loop after the HLT.
                     * V1.19: Fatal() at 0x22722C → HLT at 0x227238. */
                    if (eip == 0x227238u || eip == 0x1CF800u || eip == 0x1D96AEu) {
                        static int s_fatal_redir = 0;
                        if (s_fatal_redir < 20)
                            LOG("cpu", "Fatal/panic HLT @0x%08x → prnull idle (#%d)\n",
                                eip, ++s_fatal_redir);
                        eip = 0xFF0000u;
                        uint32_t safe_esp = 0xDFFFE0u;
                        uc_reg_write(uc, UC_X86_REG_ESP, &safe_esp);
                        goto handle_display;
                    }

                    /* Normal HLT: skip if no IRQ pending */
                    uint8_t irq_pend = g_emu.pic[0].irr & ~g_emu.pic[0].imr;
                    if (!irq_pend) {
                        eip++;
                    }
                    goto handle_display;
                }
                /* Cyrix/MediaGX-specific opcodes — treat as NOP */
                if (insn[0] == 0x0F) {
                    switch (insn[1]) {
                    case 0x3C: /* BB0_RESET — reset branch trace buffer 0 */
                    case 0x3D: /* BB1_RESET — reset branch trace buffer 1 */
                    case 0x36: /* RDSHR — read scratch hard register */
                    case 0x37: /* WRSHR — write scratch hard register */
                    case 0x38: /* SMINT — software SMI (no-op in emu) */
                    case 0x39: /* DMINT — debug management interrupt */
                    case 0x3F: /* ALTINP — alternate input */
                        eip += 2;
                        goto handle_display;
                    default:
                        break;
                    }
                }
            }

            /* Log error periodically */
            if (g_emu.exec_count < 20 || (g_emu.exec_count % 10000) == 0) {
                uint8_t dump[16];
                uc_mem_read(uc, eip, dump, 16);
                LOG("cpu", "uc_emu_start error: %s (EIP=0x%08x, exec=%lu)\n",
                    uc_strerror(err), eip, (unsigned long)g_emu.exec_count);
                LOG("cpu", "  bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                    dump[0], dump[1], dump[2], dump[3],
                    dump[4], dump[5], dump[6], dump[7]);
            }

            /* Skip problematic instruction */
            eip++;
        }

handle_display:
        display_timer++;
        if (display_timer >= 6) {
            display_timer = 0;

            if (g_emu.display_ready) {
                display_handle_events();
                display_update();

                g_emu.vsync_count++;
                /* VSYNC flag in BAR2 SRAM offset 4; clear offsets 5-7 (BT-108) */
                g_emu.bar2_sram[4] = 1;
                g_emu.bar2_sram[5] = 0;
                g_emu.bar2_sram[6] = 0;
                g_emu.bar2_sram[7] = 0;
            }

            /* Heartbeat log */
            clock_gettime(CLOCK_MONOTONIC, &now);
            double elapsed = (now.tv_sec - last_time.tv_sec) +
                             (now.tv_nsec - last_time.tv_nsec) / 1e9;
            if (elapsed >= 5.0) {
                LOG("hb", "exec=%lu EIP=0x%08x post=0x%02x vsync=%u frames=%d\n",
                    (unsigned long)g_emu.exec_count, eip, g_emu.post_code,
                    g_emu.vsync_count, g_emu.frame_count);
                if (g_emu.uart_pos > 0) {
                    g_emu.uart_buf[g_emu.uart_pos] = '\0';
                    LOG("uart", "%s\n", g_emu.uart_buf);
                }
                fflush(stdout);
                last_time = now;
            }
        }
    }
}

/* I/O port IN hook */
static uint32_t hook_insn_in_val(uc_engine *uc, uint32_t port, int size, void *user_data)
{
    return io_port_read((uint16_t)port, size);
}

__attribute__((unused))
static void hook_insn_in(uc_engine *uc, uint32_t port, int size, void *user_data)
{
    /* Not used — hook_insn_in_val returns value directly */
}

/* I/O port OUT hook */
static void hook_insn_out(uc_engine *uc, uint32_t port, int size, uint32_t value, void *user_data)
{
    io_port_write((uint16_t)port, value, size);
}

/* Invalid memory access hook */
static bool hook_mem_invalid(uc_engine *uc, uc_mem_type type, uint64_t addr,
                             int size, int64_t value, void *user_data)
{
    static int s_inv_count = 0;
    s_inv_count++;

    /* Handle write-protected and fetch-protected: upgrade permissions */
    if (type == UC_MEM_WRITE_PROT || type == UC_MEM_FETCH_PROT) {
        /* Upgrade the page to full access */
        uint64_t page_start = addr & ~0xFFFULL;
        uc_mem_protect(uc, page_start, 0x1000, UC_PROT_ALL);
        if (s_inv_count <= 20) {
            uint32_t eip;
            uc_reg_read(uc, UC_X86_REG_EIP, &eip);
            LOG("mem", "prot upgrade at 0x%08lx (EIP=0x%08x, %s)\n",
                (unsigned long)addr, eip,
                type == UC_MEM_WRITE_PROT ? "write-prot" : "fetch-prot");
        }
        return true;
    }

    /* Auto-map unmapped regions to keep emulation going */
    uint64_t page_start = addr & ~0xFFFULL;
    uint32_t map_size = 0x1000;

    /* For large unmapped ranges, map bigger chunks */
    if (addr >= 0x20000000 && addr < 0x40000000) {
        page_start = addr & ~0xFFFFULL;
        map_size = 0x10000;
    }

    uc_err err = uc_mem_map(uc, page_start, map_size, UC_PROT_ALL);
    if (err == UC_ERR_OK) {
        /* Fill with RET (0xC3) for code, 0xFF for data */
        uint8_t fill = (type == UC_MEM_FETCH_UNMAPPED) ? 0xC3 : 0xFF;
        uint8_t buf[0x10000];
        memset(buf, fill, map_size);
        uc_mem_write(uc, page_start, buf, map_size);
    }

    if (s_inv_count <= 50 || (s_inv_count % 10000) == 0) {
        uint32_t eip;
        uc_reg_read(uc, UC_X86_REG_EIP, &eip);
        LOG("mem", "unmapped %s addr=0x%08lx size=%d val=0x%lx EIP=0x%08x (#%d)\n",
            type == UC_MEM_READ_UNMAPPED ? "READ" :
            type == UC_MEM_WRITE_UNMAPPED ? "WRITE" : "FETCH",
            (unsigned long)addr, size, (unsigned long)value, eip, s_inv_count);
    }

    return true;  /* continue emulation */
}
