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

/* SIGALRM handler — increment pending tick count and stop emulation so the
 * main loop processes the tick immediately. Signals don't queue in Linux,
 * so we use a counter to avoid losing ticks during long batches. */
void cpu_timer_handler(int sig)
{
    (void)sig;
    g_emu.timer_pending++;
    if (g_emu.uc)
        uc_emu_stop(g_emu.uc);
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

    /* GX_BASE registers only (0x40000000 - 0x407FFFFF).
     * FB at 0x40800000+ is direct-mapped — no hooks needed there. */
    uc_hook h_gx_r, h_gx_w;
    e = uc_hook_add(uc, &h_gx_r, UC_HOOK_MEM_READ, (void*)bar_mmio_read,
                NULL, (uint64_t)GX_BASE, (uint64_t)(GX_BASE + 0x800000 - 1));
    if (e) LOG("cpu", "GX read hook FAILED: %s\n", uc_strerror(e));
    e = uc_hook_add(uc, &h_gx_w, UC_HOOK_MEM_WRITE, (void*)bar_mmio_write,
                NULL, (uint64_t)GX_BASE, (uint64_t)(GX_BASE + 0x800000 - 1));
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

    /* VSYNC callback (0x19BF64) and clkint callback dispatcher monitor */
    uc_hook h_vsync_trace;
    uc_hook_add(uc, &h_vsync_trace, UC_HOOK_CODE, (void*)hook_code_trace,
                NULL, (uint64_t)0x19BF60, (uint64_t)0x19BF70);
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
    g_emu.irq_ok_count = inject_ok;
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
    /* Pre-check: don't even try if IF=0 (interrupts disabled) */
    uint32_t eflags = 0;
    uc_reg_read(g_emu.uc, UC_X86_REG_EFLAGS, &eflags);
    if (!(eflags & 0x200)) return;

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
    case 0x19BF64: {
        static uint32_t vs_call = 0;
        vs_call++;
        if (vs_call <= 5)
            LOG("vsync", "callback #%u\n", vs_call);
        break;
    }
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


    struct timespec last_time, now;
    clock_gettime(CLOCK_MONOTONIC, &last_time);

    /* Performance counters — reset every heartbeat */
    unsigned long prof_emu_calls = 0;   /* total uc_emu_start calls */
    unsigned long prof_0f3c = 0;        /* 0F3C handled in main loop */
    unsigned long prof_hlt = 0;         /* HLT returns */
    unsigned long prof_other_err = 0;   /* other errors */
    unsigned long prof_ok = 0;          /* UC_ERR_OK (batch exhausted or stopped) */

    /* Initial EIP read — carried across iterations to avoid double-read */
    uc_reg_read(uc, UC_X86_REG_EIP, &eip);

    while (g_emu.running) {
        /* Timer tick processing — driven by SIGALRM at 100Hz.
         * SIGALRM calls uc_emu_stop to break the current batch.
         * If CPU is slower than real-time, ticks are naturally dropped
         * (timer_pending is a counter but we only process up to 2 per
         * iteration to prevent flood). */
        {
            int ticks = g_emu.timer_pending;
            if (ticks > 2) ticks = 2;  /* limit catch-up */
            g_emu.timer_pending -= ticks;

            for (int t = 0; t < ticks && g_emu.xinu_ready; t++) {
                g_emu.pic[0].irr |= 0x01;  /* IRQ0 pending in PIC */

                /* VSYNC at 50Hz (every 2 ticks). BAR2 SRAM[4]=1 signals
                 * the game's VSYNC callback to wake dispmgr. */
                static int vsync_ctr = 0;
                if (++vsync_ctr >= 2) {
                    vsync_ctr = 0;
                    g_emu.vsync_count++;
                    g_emu.bar2_sram[4] = 1;
                    g_emu.bar2_sram[5] = 0;
                    g_emu.bar2_sram[6] = 0;
                    g_emu.bar2_sram[7] = 0;
                    uint32_t one = 1;
                    uc_mem_write(uc, WMS_BAR2 + 4, &one, 4);
                }

                /* DC_TIMING2: VBLANK when VSYNC fires, active lines otherwise */
                static uint32_t dc_timing2_counter = 0;
                if (vsync_ctr == 0) {
                    dc_timing2_counter = 241;
                } else {
                    dc_timing2_counter += 120;
                    if (dc_timing2_counter > 240) dc_timing2_counter = 0;
                }
                g_emu.dc_timing2 = dc_timing2_counter;
                uc_mem_write(uc, GX_BASE + 0x8354, &dc_timing2_counter, 4);
            }
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
            if (idtr.base != 0 && idtr.limit >= 0x20 * 8 + 7 && idtr.base < RAM_SIZE) {
                uint32_t idt_off = idtr.base + 0x20 * 8;
                uint16_t off_lo = *(uint16_t *)(g_emu.ram + idt_off);
                uint16_t off_hi = *(uint16_t *)(g_emu.ram + idt_off + 6);
                uint32_t handler = off_lo | ((uint32_t)off_hi << 16);
                if (handler > 0x100000u) {
                    if (g_emu.clkint_ready_exec == 0) {
                        g_emu.clkint_ready_exec = g_emu.exec_count;
                        LOG("irq", "clkint detected: IDT[0x20]=0x%08x EIP=0x%08x exec=%u\n",
                            handler, eip, (unsigned)g_emu.exec_count);
                    }
                    if (g_emu.xinu_booted &&
                        g_emu.exec_count >= g_emu.clkint_ready_exec + 50) {
                        g_emu.xinu_ready = true;
                        LOG("irq", "XINU ready: timer injection enabled EIP=0x%08x exec=%u\n",
                            eip, (unsigned)g_emu.exec_count);

                        /* Install Cyrix 0F3C emulator at 0x500 and patch IDT[6]. */
                        {
                            uint8_t h6[48];
                            int p = 0;
                            h6[p++] = 0x50;                         /* PUSH EAX */
                            h6[p++] = 0x56;                         /* PUSH ESI */
                            h6[p++] = 0x8B; h6[p++] = 0x74;        /* MOV ESI,[ESP+8] */
                            h6[p++] = 0x24; h6[p++] = 0x08;
                            h6[p++] = 0x66; h6[p++] = 0x81;        /* CMP WORD [ESI],0x3C0F */
                            h6[p++] = 0x3E; h6[p++] = 0x0F; h6[p++] = 0x3C;
                            h6[p++] = 0x75; h6[p++] = 0x14;        /* JNE .not_cyrix */
                            h6[p++] = 0x8B; h6[p++] = 0x44;        /* MOV EAX,[ESP+4] */
                            h6[p++] = 0x24; h6[p++] = 0x04;
                            h6[p++] = 0x89; h6[p++] = 0x02;        /* MOV [EDX],EAX */
                            h6[p++] = 0x89; h6[p++] = 0x5A;        /* MOV [EDX+4],EBX */
                            h6[p++] = 0x04;
                            h6[p++] = 0x83; h6[p++] = 0xC2;        /* ADD EDX,8 */
                            h6[p++] = 0x08;
                            h6[p++] = 0x83; h6[p++] = 0x44;        /* ADD DWORD [ESP+8],2 */
                            h6[p++] = 0x24; h6[p++] = 0x08; h6[p++] = 0x02;
                            h6[p++] = 0x5E;                         /* POP ESI */
                            h6[p++] = 0x58;                         /* POP EAX */
                            h6[p++] = 0xCF;                         /* IRET */
                            /* .not_cyrix: */
                            h6[p++] = 0x5E;                         /* POP ESI */
                            h6[p++] = 0x58;                         /* POP EAX */
                            h6[p++] = 0xB8; h6[p++] = 0xFF;        /* MOV EAX,-1 */
                            h6[p++] = 0xFF; h6[p++] = 0xFF; h6[p++] = 0xFF;
                            h6[p++] = 0xC9;                         /* LEAVE */
                            h6[p++] = 0xC3;                         /* RET */
                            uc_mem_write(uc, 0x500, h6, p);

                            uint32_t idt6 = 0x2F7AD8u + 6u * 8u;
                            uint8_t gate[8] = {
                                0x00, 0x05, 0x08, 0x00,
                                0x00, 0x8F, 0x00, 0x00
                            };
                            uc_mem_write(uc, idt6, gate, 8);
                            LOG("cpu", "Installed 0F3C emulator at 0x500, IDT[6]=0x%x→0x500 (%d bytes)\n",
                                idt6, p);
                        }
                    }
                } else if (g_emu.exec_count % 5000 == 0) {
                    LOG("irq", "waiting for clkint: IDT[0x20]=0x%08x EIP=0x%08x exec=%u xinu_booted=%d\n",
                        handler, eip, (unsigned)g_emu.exec_count, g_emu.xinu_booted);
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

            /* --- 2. PIC base_mask fix (x64 POC BT-66/71) ---
             * XINU's disable()/restore() use base_mask at 0x2F7CA8 to
             * manage IMR. device_init (which we NOP'd) would call
             * set_evec(0x20,handler) clearing bit 0 (IRQ0 unmasked).
             * Without this, base_mask=0xFF and every restore() re-masks IRQ0.
             * Address 0x2F7CA8 confirmed V1.19 (x64 POC + agent1264 logs). */
            {
                uint16_t base_mask;
                uc_mem_read(uc, 0x2F7CA8u, &base_mask, 2);
                base_mask &= ~0x0001u;  /* clear bit 0 = unmask IRQ0 */
                base_mask &= ~0x0004u;  /* clear bit 2 = unmask cascade */
                uc_mem_write(uc, 0x2F7CA8u, &base_mask, 2);
                LOG("init", "PIC base_mask: [2F7CA8]=0x%04X (IRQ0+cascade unmasked)\n",
                    base_mask);
            }

            /* --- 3. DC register pre-init (x64 POC) --- */
            {
                uint32_t dc_cfg = 0x303u;  /* DFLE=1, display enabled */
                uint32_t dc_fb_off = 0x0u;
                uint32_t dc_line = 320u;   /* 320 DWORDs = 1280 bytes = 640 pixels */
                uc_mem_write(uc, GX_BASE + DC_GENERAL_CFG, &dc_cfg, 4);
                uc_mem_write(uc, GX_BASE + DC_FB_ST_OFFSET, &dc_fb_off, 4);
                uc_mem_write(uc, GX_BASE + DC_LINE_SIZE, &dc_line, 4);
                g_emu.dc_fb_offset = 0;
                LOG("init", "DC pre-init: CFG=0x303 FB_OFF=0 LINE=320\n");
            }

            /* --- 4. RET stub at 0x400000 (safe trampoline area) --- */
            {
                uint8_t ret_byte = 0xC3;
                uc_mem_write(uc, 0x400000u, &ret_byte, 1);
                /* XOR EAX,EAX; RET at 0x400010 */
                uint8_t xor_ret[3] = { 0x31, 0xC0, 0xC3 };
                uc_mem_write(uc, 0x400010u, xor_ret, 3);
            }

            /* NOTE: All V1.12 BSS address writes REMOVED (BT-63 / BT-91).
             * The i386 POC proves the game boots with scheduler patches
             * disabled ("natural flow"). Addresses like 0x2AF81C (resched
             * gate), 0x2AF6A4 (ISR depth), 0x2AF7D4 (preempt guard),
             * 0x2797C4 (DCS2 init), 0x2C181C (XINACMOS ptr), 0x2D577C
             * (free list head) are V1.12 BSS locations. In V1.19 SWE1,
             * these addresses fall within the code/data section and
             * writing to them CORRUPTS game code — causing clkint to
             * malfunction (preempt never decrements).
             *
             * XINU's sysinit() naturally initializes all scheduler state,
             * free list, process table, and queue structures. The 48
             * processes created confirm this works. We only need:
             * - Function patches (Fatal/NonFatal → safe returns)
             * - PIC base_mask fix (V1.19 address confirmed)
             * - DC hardware register init (GX_BASE offsets)
             * - 0F3C emulator (already installed above)
             */

            LOG("init", "=== Post-XINU V1.19 patches complete ===\n");
        }

        /* pid2 (terminal) crash guard — x64 POC BT-98:
         * fn@0x1EF7D0 is called every timer tick from clkint at 0x1D0B73.
         * When pid2 doesn't exist yet, proctab[2]+0x18 ([0x2FCAAC]) has
         * garbage=1 → crash at 0x1D83E5, aborting the ENTIRE clkint handler
         * including VSYNC callback and sleep queue processing.
         * Fix: force [0x2FCAAC]=0 until pid2 is PRREADY (pstate=3).
         * Runs every 64 iterations to reduce overhead. */
        if (g_emu.xinu_ready && (g_emu.exec_count & 0x3F) == 0) {
            uint8_t pt2_state = RAM_RD8(0x2FCA94u); /* proctab[2].pstate */
            if (pt2_state != 3 && pt2_state != 2 && pt2_state != 7) {
                uint32_t flag = RAM_RD32(0x2FCAACu);
                if (flag != 0) {
                    RAM_WR32(0x2FCAACu, 0);
                    static int guard_log = 0;
                    if (guard_log < 5)
                        LOG("irq", "pid2 guard: [0x2FCAAC]=0x%x→0 (pt2_st=%u) #%d\n",
                            flag, pt2_state, ++guard_log);
                }
            }
        }

        /* BT-118 IStack magic repair — x64 POC:
         * clkint checks currpid's IStack magic (0xAAA9) every tick.
         * If corrupted → Fatal → callback chain aborted → VSYNC never fires.
         * Repair magic between timer ticks so ISR always sees valid value.
         * Runs every 64 iterations to reduce overhead. */
        if (g_emu.xinu_ready && (g_emu.exec_count & 0x3F) == 0) {
            uint32_t cpid = RAM_RD32(0x2FC8BCu);    /* currpid */
            uint32_t nproc_chk = RAM_RD32(0x303E94u);
            if (cpid > 0 && cpid < 130 && nproc_chk >= 24) {
                uint32_t pe = 0x2FC8C4u + cpid * 232u;
                uint32_t istack_ptr = RAM_RD32(pe + 0x24);
                if (istack_ptr > 0x100000 && istack_ptr < 0xFFFF00) {
                    uint32_t magic = RAM_RD32(istack_ptr);
                    if (magic != 0xAAA9) {
                        RAM_WR32(istack_ptr, 0xAAA9);
                        static int magic_log = 0;
                        if (magic_log < 10)
                            LOG("irq", "BT-118 magic repair pid=%u istack=0x%x was=0x%x (#%d)\n",
                                cpid, istack_ptr, magic, ++magic_log);
                    }
                }
            }
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
            /* IRQ injection modifies Unicorn's EIP — sync local copy */
            uc_reg_read(uc, UC_X86_REG_EIP, &eip);
        }

        /* Fixed batch size — keeps main loop cycling frequently for
         * watchdog/sentinel suppression. SIGALRM still breaks long batches. */
        size_t batch = 200000;

        /* Execute a batch of instructions.
         * eip is carried from previous iteration (or initial read before loop). */
        /* Periodic TLB flush: Unicorn caches MMIO read hooks in translation
         * blocks.  Without flushing, DC_TIMING2 reads see stale values and
         * the VSYNC callback never detects VBLANK.  Flush every 64 cycles
         * to balance correctness vs. performance. */
        if ((g_emu.exec_count & 0x3F) == 0)
            uc_ctl_flush_tlb(uc);
        uc_err err = uc_emu_start(uc, eip, 0, 0, batch);

        g_emu.exec_count++;
        prof_emu_calls++;

        /* Watchdog health register: keep [watchdog_flag_addr] = 0xFFFF so
         * pci_read_watchdog() always sees an in-range health value and returns
         * 0 (not expired). This simulates the XINU timer regularly feeding the
         * hardware watchdog; real 200MHz hardware completes game init before
         * the watchdog can expire, but emulation is slower. (BT-107)
         * Runs every 64 iterations + uses direct RAM for zero overhead. */
        if (g_emu.watchdog_flag_addr && g_emu.game_started
            && (g_emu.exec_count & 0x3F) == 0) {
            RAM_WR32(g_emu.watchdog_flag_addr, 0x0000FFFFu);
        }

        /* BT-107b: pci_watchdog_bone suppression (POC 0x2E98C8).
         * The game's watchdog_bone() countdown at 0x2E98C8 decrements each
         * tick. When it reaches 0, Fatal fires. Keep it at 0 so the check
         * never triggers. Separate from the health register above. */
        if (g_emu.game_started && (g_emu.exec_count & 0x3F) == 0) {
            RAM_WR32(0x002E98C8u, 0);
        }

        /* Memory-zero sentinel: interval_0_25ms() checks [0x00000000] == 0
         * as a corruption guard. Guest code or stray DMA can corrupt address 0
         * during normal operation. Keep it zeroed to prevent false Fatal.
         * MUST run every iteration (not periodic) — address 0 corruption
         * accumulates fast in game phase. */
        if (g_emu.game_started) {
            RAM_WR32(0, 0);
        }

        /* BT-98: Display Manager render-pass watchdog suppression.
         * Once the game reaches its post-Allegro display path, a watchdog
         * counter at 0x002e8e30 is periodically refreshed by render_pass().
         * Without a host render thread, the counter can expire and trip
         * Fatal() before the game finishes printing its startup banner. */
        if (g_emu.game_started && (g_emu.exec_count & 0x3F) == 0) {
            uint32_t dm_mode = RAM_RD32(0x002e8e2Cu);
            if (dm_mode == 1u) {
                uint32_t wd_cnt = RAM_RD32(0x002e8e30u);
                if (wd_cnt < 2500u) {
                    RAM_WR32(0x002e8e30u, 5000u);
                }
            }
        }

        /* Read EIP after execution stopped */
        uc_reg_read(uc, UC_X86_REG_EIP, &eip);

        if (err != UC_ERR_OK) {
            /* Count non-0F3C, non-HLT errors separately at the end */
            if (err == UC_ERR_INSN_INVALID) {
                uint8_t insn_buf[4];
                uint8_t *insn;
                if (eip < RAM_SIZE - 4) {
                    insn = g_emu.ram + eip;
                } else {
                    uc_mem_read(uc, eip, insn_buf, 4);
                    insn = insn_buf;
                }
                /* Periodic log of ALL invalid instruction encounters */
                static int inv_cnt = 0;
                inv_cnt++;
                if (inv_cnt <= 30 || (inv_cnt % 10000) == 0)
                    LOG("cpu", "INSN_INVALID #%d EIP=0x%08x bytes=%02x %02x %02x %02x\n",
                        inv_cnt, eip, insn[0], insn[1], insn[2], insn[3]);
                if (insn[0] == 0xF4) {
                    prof_hlt++;
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

                    /* Normal HLT: sleep until next timer tick if no IRQ pending.
                     * Without this, the idle loop spins: HLT→emu_start(1 insn)→HLT
                     * burning host CPU and wasting uc_emu_start overhead. */
                    uint8_t irq_pend = g_emu.pic[0].irr & ~g_emu.pic[0].imr;
                    if (!irq_pend) {
                        eip++;
                        static const struct timespec hlt_sleep = {0, 1000000}; /* 1ms */
                        while (!g_emu.timer_pending && g_emu.running)
                            nanosleep(&hlt_sleep, NULL);
                    }
                    goto handle_display;
                }
                /* Cyrix/MediaGX-specific opcodes */
                if (insn[0] == 0x0F) {
                    switch (insn[1]) {
                    case 0x3C: {
                        prof_0f3c++;
                        /* Cyrix scratchpad write (x64 POC BT-79):
                         * MOV [EDX], EAX; MOV [EDX+4], EBX; EDX += 8
                         * dispmgr uses this to write GP register pairs.
                         *
                         * CRITICAL: uc_mem_write() bypasses MMIO hooks!
                         * Must manually dispatch GX_BASE writes through
                         * bar_mmio_write so GP BLT engine and DC register
                         * tracking actually see the values. */
                        uint32_t eax, ebx, edx;
                        uc_reg_read(uc, UC_X86_REG_EAX, &eax);
                        uc_reg_read(uc, UC_X86_REG_EBX, &ebx);
                        uc_reg_read(uc, UC_X86_REG_EDX, &edx);

                        uint32_t a0 = edx, a1 = edx + 4;
                        /* Write to backing memory */
                        uc_mem_write(uc, a0, &eax, 4);
                        uc_mem_write(uc, a1, &ebx, 4);
                        /* Dispatch GX_BASE MMIO writes to bar handler */
                        if (a0 >= GX_BASE && a0 < GX_BASE + GX_BASE_SIZE)
                            bar_mmio_write(uc, UC_MEM_WRITE, a0, 4, (int64_t)eax, NULL);
                        if (a1 >= GX_BASE && a1 < GX_BASE + GX_BASE_SIZE)
                            bar_mmio_write(uc, UC_MEM_WRITE, a1, 4, (int64_t)ebx, NULL);

                        edx += 8;
                        uc_reg_write(uc, UC_X86_REG_EDX, &edx);
                        static int cyrix_cnt = 0;
                        if (cyrix_cnt < 20)
                            LOG("cpu", "0F3C: [0x%x]=0x%x [0x%x]=0x%x (#%d)\n",
                                a0, eax, a1, ebx, ++cyrix_cnt);
                        eip += 2;
                        goto handle_display;
                    }
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
                uint8_t dump_buf[16] = {0};
                uint8_t *dump;
                if (eip < RAM_SIZE - 16) {
                    dump = g_emu.ram + eip;
                } else {
                    uc_mem_read(uc, eip, dump_buf, 16);
                    dump = dump_buf;
                }
                LOG("cpu", "uc_emu_start error: %s (EIP=0x%08x, exec=%lu)\n",
                    uc_strerror(err), eip, (unsigned long)g_emu.exec_count);
                LOG("cpu", "  bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                    dump[0], dump[1], dump[2], dump[3],
                    dump[4], dump[5], dump[6], dump[7]);
            }

            /* Skip problematic instruction */
            prof_other_err++;
            eip++;
        } else {
            prof_ok++;
        }

handle_display:
        /* Write back eip to Unicorn — required because cpu_inject_interrupt()
         * reads Unicorn's EIP (uc_reg_read) to push the return address for
         * the interrupt frame. If we don't sync, error-handler modifications
         * (eip += 2 for 0F3C, eip++ for HLT) won't be reflected, and IRET
         * returns to the wrong address. */
        uc_reg_write(uc, UC_X86_REG_EIP, &eip);

        /* Display update at ~60 Hz using wall clock, not every iteration.
         * i386 POC used SDL_Flip vsync throttle; we use explicit timing. */
        {
            static struct timespec last_display = {0, 0};
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (last_display.tv_sec == 0) last_display = now;
            long disp_ms = (now.tv_sec - last_display.tv_sec) * 1000
                         + (now.tv_nsec - last_display.tv_nsec) / 1000000;
            if (disp_ms >= 16) {  /* ~60 Hz */
                last_display = now;
                if (g_emu.display_ready) {
                    display_handle_events();
                    display_update();
                }
            }
        }

        /* VSYNC is driven by timer_handler (L381-389), not display loop.
         * No duplicate VSYNC write needed here. */

        {
            /* Heartbeat log */
            clock_gettime(CLOCK_MONOTONIC, &now);
            double elapsed = (now.tv_sec - last_time.tv_sec) +
                             (now.tv_nsec - last_time.tv_nsec) / 1e9;

            /* One-shot: dump VSYNC callback code from live memory */
            {
                static int dump_count = 0;
                if (g_emu.xinu_ready && dump_count < 4) {
                    dump_count++;
                    uint32_t enable = RAM_RD32(0x2E8AF4);
                    uint32_t gxptr = RAM_RD32(0x2E8B74);
                    uint32_t dm_mode_v = RAM_RD32(0x2E8E2C);
                    LOG("dbg", "VSYNC enable=0x%x gx_ptr=0x%x dm_mode=%u (exec=%lu)\n",
                        enable, gxptr, dm_mode_v, g_emu.exec_count);
                }
            }

            if (elapsed >= 5.0) {
                uint32_t preempt = RAM_RD32(0x2F7AB0u); /* XINU preempt counter */
                uint32_t nproc = RAM_RD32(0x303E94u);    /* XINU nproc */
                uint32_t guard1 = RAM_RD32(0x2C902Cu);
                uint32_t guard2 = RAM_RD32(0x2C9038u);
                uint32_t gate = RAM_RD32(0x2D7274u);
                uint32_t tinit = RAM_RD32(0x335980u);     /* timer init flag */
                uint32_t tick_cycle = RAM_RD32(0x3358D0u); /* tick counter */
                LOG("hb", "exec=%lu EIP=0x%08x post=0x%02x vsync=%u frames=%d irq_ok=%u\n",
                    (unsigned long)g_emu.exec_count, eip, g_emu.post_code,
                    g_emu.vsync_count, g_emu.frame_count, g_emu.irq_ok_count);
                LOG("hb", "  preempt=%u nproc=%u guards=%u/%u/%u tinit=%u tcyc=%u\n",
                    preempt, nproc, guard1, guard2, gate, tinit, tick_cycle);
                /* PIC state for diagnostics */
                LOG("hb", "  PIC0: IRR=0x%02x IMR=0x%02x ISR=0x%02x  PIC1: IRR=0x%02x IMR=0x%02x ISR=0x%02x\n",
                    g_emu.pic[0].irr, g_emu.pic[0].imr, g_emu.pic[0].isr,
                    g_emu.pic[1].irr, g_emu.pic[1].imr, g_emu.pic[1].isr);
                /* DM state — compact */
                {
                    uint32_t dmm = RAM_RD32(0x2E8E2C);
                    uint32_t gxp = RAM_RD32(0x2E8B74);
                    LOG("hb", "  DM: mode=%u gxp=0x%x dt2=%u\n", dmm, gxp, g_emu.dc_timing2);
                }
                /* Performance stats */
                LOG("hb", "  PERF: calls=%lu/5s ok=%lu 0f3c=%lu hlt=%lu other=%lu\n",
                    prof_emu_calls, prof_ok, prof_0f3c, prof_hlt, prof_other_err);
                prof_emu_calls = prof_0f3c = prof_hlt = prof_other_err = prof_ok = 0;
                /* One-shot process table summary */
                static int proctab_dumped = 0;
                if (!proctab_dumped && nproc >= 35) {
                    proctab_dumped = 1;
                    int active = 0;
                    for (uint32_t pid = 0; pid < 130; pid++) {
                        uint8_t ps = RAM_RD8(0x2FC8C4u + pid * 232u);
                        if (ps != 2) active++;  /* not FREE */
                    }
                    LOG("hb", "  proctab: %u active processes\n", active);
                }
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
