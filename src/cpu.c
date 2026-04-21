/*
 * cpu.c — Unicorn Engine CPU setup, interrupt injection, execution loop.
 *
 * Key design: Unicorn UC_MODE_32 for i386 guest on x64 host.
 * Timer interrupt injection between emulation slices (no hardware PIC in Unicorn).
 * SIGALRM at 100Hz triggers uc_emu_stop() → check IRQs → inject → resume.
 *
 * ==========================================================================
 *  REMAINING HARDCODED PATCHES — minimization ground-truth minimization audit
 *  Status as of 2026-04-21.  Goal: every patch should self-validate, be
 *  game/version-agnostic, or be clearly gated with a sanity check.
 * ==========================================================================
 *
 *  A) ALWAYS-ON, self-validating or pattern-scanned (safe for any build):
 *  ---------------------------------------------------------------------
 *  [cpu.c] DCS-mode override @ 0x1931E6 (SWE1 v1.5/v1.19 only)
 *      Byte-validated (CMP EAX,1 ; JNE 0x21 → MOV EAX,1).  Forces BAR4
 *      path so DCS sound engine receives commands.  Without it the game
 *      sits in I/O-port mode (ports 0x13C-0x13F) which we stub as 0xFF
 *      → DCS detection fails → no boot dong, no audio init.
 *      TODO: replace with pattern scan for v2.1 compatibility.
 *
 *  [io.c::apply_sgc_patches]  — game-agnostic pattern scans:
 *      • pci_watchdog_bone health register — UART string-anchored scan
 *      • mem_detect 4MB → 14MB — function-prologue scan (BT-130)
 *      • prnull idle code @ 0xFF0000 — low-mem stub (STI+HLT+JMP)
 *      • BT-74 nulluser JMP$ → HLT;JMP-3 — 17-byte pattern scan
 *
 *  [cpu.c::cpu_init] IRET+EOI stub @ 0x20000 — low-mem stub, game-agnostic.
 *
 *  B) GATED off v19_patches_enabled (ENCORE_KEEP_V19_PATCHES env) — OFF
 *  by default.  These hardcode SWE1-V1.19 addresses and corrupt random
 *  code on v2.1 if applied blind.  Each is also prologue-guarded via
 *  PROLOGUE_OK (0x55 0x89 0xE5) so even enabled they no-op on mismatch.
 *  If SWE1 idle + minimization parity confirm they're unused → delete them.
 *      • Fatal/NonFatal/CMOS/LocMgr/watchdog returns
 *      • PIC base_mask @ 0x2F7CA8 (clear IRQ0+cascade bits)
 *      • DC register pre-init (CFG/FB_OFF/LINE_SIZE)
 *      • DCS2 fake channel object @ 0x400000
 *      • pid2 crash guard [0x2FCAAC]=0 until PRREADY
 *      • IStack magic-word repair for SWE1 proctab
 *
 *  C) RFM/SWE1 both, read-only maintenance @ 64-cycle tick:
 *      RAM_WR32(0, 0)           — NULL page zeroing, harmless
 *      watchdog_flag_addr=FFFF  — uses scanned address (safe)
 *
 *  D) Dropped in prior pass — kept only as commented markers:
 *      apply_xinu_boot_patches() (V1.12 BSS pokes) — orphaned, deleted.
 *      SWE1-V1.12 display bootstrap — deleted.
 *      Fatal/panic loop HLT pokes — deleted.
 * ==========================================================================
 */
#include "encore.h"

/* Forward declarations for hooks */
static void hook_insn_in(uc_engine *uc, uint32_t port, int size, void *user_data);
static uint32_t hook_insn_in_val(uc_engine *uc, uint32_t port, int size, void *user_data);
static void hook_insn_out(uc_engine *uc, uint32_t port, int size, uint32_t value, void *user_data);
static bool hook_mem_invalid(uc_engine *uc, uc_mem_type type, uint64_t addr,
                             int size, int64_t value, void *user_data);
static void hook_code_trace(uc_engine *uc, uint64_t addr, uint32_t size, void *user_data);
static void hook_dcs_mode_write(uc_engine *uc, uc_mem_type type,
                                uint64_t addr, int size,
                                int64_t value, void *user_data);

/* SIGALRM handler — sets timer_pending for HLT wakeup only.
 * Does NOT call uc_emu_stop (avoids stop_request contamination).
 * Tick injection is iteration-count based for consistent game speed. */
static volatile unsigned long sigalrm_total = 0;
void cpu_timer_handler(int sig)
{
    (void)sig;
    sigalrm_total++;
    g_emu.timer_pending++;
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

    /* GX_BASE registers — narrowed to GP+DC (0x8000-0x8FFF) and BC (0x20000)
     * ranges. The game only accesses these specific register pages:
     *   GP BLT: 0x40008100-0x40008210
     *   DC:     0x40008300-0x40008358
     *   BC:     0x40020000
     * By narrowing from 8MB to ~96KB, we eliminate millions of unnecessary
     * TB exits for accesses to unused register ranges. */
    uc_hook h_gx_r, h_gx_w;
    e = uc_hook_add(uc, &h_gx_r, UC_HOOK_MEM_READ, (void*)bar_mmio_read,
                NULL, (uint64_t)(GX_BASE + 0x8000), (uint64_t)(GX_BASE + 0x20FFF));
    if (e) LOG("cpu", "GX read hook FAILED: %s\n", uc_strerror(e));
    e = uc_hook_add(uc, &h_gx_w, UC_HOOK_MEM_WRITE, (void*)bar_mmio_write,
                NULL, (uint64_t)(GX_BASE + 0x8000), (uint64_t)(GX_BASE + 0x20FFF));
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

    /* NonFatal hook — log string argument before XOR EAX,EAX;RET patch runs */
    uc_hook h_nonfatal;
    uc_hook_add(uc, &h_nonfatal, UC_HOOK_CODE, (void*)hook_code_trace,
                NULL, (uint64_t)0x24780C, (uint64_t)0x247810);

    /* VSYNC callback (0x19BF64) and clkint callback dispatcher monitor */
    uc_hook h_vsync_trace;
    uc_hook_add(uc, &h_vsync_trace, UC_HOOK_CODE, (void*)hook_code_trace,
                NULL, (uint64_t)0x19BF60, (uint64_t)0x19BF70);

    /* Watchpoint: catch writes to dcs_mode at [0x3444b0] */
    uc_hook h_dcs_wp;
    uc_hook_add(uc, &h_dcs_wp, UC_HOOK_MEM_WRITE, (void*)hook_dcs_mode_write,
                NULL, (uint64_t)0x3444b0, (uint64_t)0x3444b3);

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

    /* Batched register read: EIP, ESP, EFLAGS, CS in one call */
    uint32_t regs_val[4] = {0, 0, 0, 0};
    int regs_ids[4] = {UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_EFLAGS, UC_X86_REG_CS};
    void *regs_ptrs[4] = {&regs_val[0], &regs_val[1], &regs_val[2], &regs_val[3]};
    uc_reg_read_batch(uc, regs_ids, regs_ptrs, 4);

    uint32_t eip = regs_val[0], esp = regs_val[1];
    uint32_t eflags = regs_val[2], cs = regs_val[3] & 0xFFFF;

    if (!(eflags & 0x200)) {
        inject_blocked++;
        return;
    }

    /* Read IDT entry directly from RAM (IDT is always in RAM) */
    uint32_t idt_base = g_emu.idt_base;
    uint32_t idt_addr = idt_base + vector * 8;
    uint8_t *idt_entry;
    uint8_t idt_buf[8];
    if (idt_addr + 8 <= RAM_SIZE) {
        idt_entry = g_emu.ram + idt_addr;
    } else {
        uc_err err = uc_mem_read(uc, idt_addr, idt_buf, 8);
        if (err != UC_ERR_OK) return;
        idt_entry = idt_buf;
    }

    uint16_t offset_lo = idt_entry[0] | (idt_entry[1] << 8);
    uint16_t selector  = idt_entry[2] | (idt_entry[3] << 8);
    uint16_t offset_hi = idt_entry[6] | (idt_entry[7] << 8);
    uint32_t handler   = offset_lo | (offset_hi << 16);
    (void)selector;

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

    /* Push interrupt frame directly to RAM (stack is always in RAM) */
    esp -= 4;
    if (esp < RAM_SIZE) RAM_WR32(esp, eflags); else uc_mem_write(uc, esp, &eflags, 4);
    esp -= 4;
    uint32_t cs32 = cs;
    if (esp < RAM_SIZE) RAM_WR32(esp, cs32); else uc_mem_write(uc, esp, &cs32, 4);
    esp -= 4;
    if (esp < RAM_SIZE) RAM_WR32(esp, eip); else uc_mem_write(uc, esp, &eip, 4);

    /* Batched register write: ESP, EFLAGS (IF cleared), EIP */
    eflags &= ~0x200u;
    uint32_t wregs_val[3] = {esp, eflags, handler};
    int wregs_ids[3] = {UC_X86_REG_ESP, UC_X86_REG_EFLAGS, UC_X86_REG_EIP};
    void *wregs_ptrs[3] = {&wregs_val[0], &wregs_val[1], &wregs_val[2]};
    uc_reg_write_batch(uc, wregs_ids, (void *const *)wregs_ptrs, 3);

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
    if (!(eflags & 0x200)) {
        return;
    }

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
    case 0x24780C: {
        /* NonFatal() called — read string arg from [ESP+4], caller from [ESP] */
        uint32_t ret_addr, str_ptr;
        uc_mem_read(uc, esp, &ret_addr, 4);
        uc_mem_read(uc, esp + 4, &str_ptr, 4);
        char buf[128] = {0};
        uc_mem_read(uc, str_ptr, buf, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        for (int i = 0; buf[i]; i++) { if (buf[i] == '\n') { buf[i] = '\0'; break; } }
        LOG("nonfatal", "NonFatal(\"%s\") [str@0x%08x caller@0x%08x]\n", buf, str_ptr, ret_addr);
        break;
    }
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

/* Watchpoint: catch writes to dcs_mode at [0x3444b0] */
static void hook_dcs_mode_write(uc_engine *uc, uc_mem_type type,
                                uint64_t addr, int size,
                                int64_t value, void *user_data)
{
    (void)type; (void)user_data;
    uint32_t eip = 0, esp = 0;
    uc_reg_read(uc, UC_X86_REG_EIP, &eip);
    uc_reg_read(uc, UC_X86_REG_ESP, &esp);
    uint32_t retaddr = 0;
    if (esp >= 4 && esp < 0x01000000)
        retaddr = *(uint32_t *)(g_emu.ram + esp);
    LOG("watch", "dcs_mode WRITE addr=0x%llx size=%d val=%lld "
        "EIP=0x%08x ret=0x%08x\n",
        (unsigned long long)addr, size, (long long)value, eip, retaddr);

    /* Dump code around the write instruction */
    if (size == 4 && eip > 0x100000) {
        uint32_t start = (eip > 0x80) ? eip - 0x80 : 0;
        uint8_t *code = g_emu.ram + start;
        LOG("watch", "--- code dump %08x-%08x ---\n", start, start + 0x100);
        for (int i = 0; i < 0x100; i += 16) {
            LOG("watch", "%08x: %02x %02x %02x %02x %02x %02x %02x %02x "
                "%02x %02x %02x %02x %02x %02x %02x %02x\n",
                start + i,
                code[i+0], code[i+1], code[i+2], code[i+3],
                code[i+4], code[i+5], code[i+6], code[i+7],
                code[i+8], code[i+9], code[i+10], code[i+11],
                code[i+12], code[i+13], code[i+14], code[i+15]);
        }
        /* Also dump stack for call trace */
        LOG("watch", "--- stack @ESP=0x%08x ---\n", esp);
        for (int i = 0; i < 8; i++) {
            uint32_t sv = *(uint32_t *)(g_emu.ram + esp + i*4);
            LOG("watch", "  [ESP+%02x] = 0x%08x\n", i*4, sv);
        }
    }
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
    unsigned long prof_ticks_fired = 0; /* ticks that successfully set IRR */
    unsigned long prof_ticks_isr = 0;   /* ticks dropped: ISR busy */
    unsigned long prof_ticks_irr = 0;   /* ticks dropped: IRR already set */
    unsigned long prof_irq0_inject = 0; /* IRQ0 actually injected */

    /* Initial EIP read — carried across iterations to avoid double-read */
    uc_reg_read(uc, UC_X86_REG_EIP, &eip);

    while (g_emu.running) {
        /* Timer tick injection — wall-clock based at guest-programmed PIT rate.
         * The guest programs PIT CH0 with a divisor (e.g. 298 → ~4003 Hz).
         * We fire IRQ0 at that rate using host clock, like QEMU's internal PIT.
         * Check every 64 iterations to reduce clock_gettime overhead. */
        if (g_emu.xinu_ready && (g_emu.exec_count & 0x3F) == 0) {
            struct timespec now_ts;
            clock_gettime(CLOCK_MONOTONIC, &now_ts);
            uint64_t now_ns = (uint64_t)now_ts.tv_sec * 1000000000ULL + now_ts.tv_nsec;
            static uint64_t last_tick_ns = 0;
            if (last_tick_ns == 0) last_tick_ns = now_ns;

            /* PIT period in nanoseconds: 1e9 / (1193182 / divisor) */
            uint16_t div = g_emu.pit.count[0];
            if (div == 0) div = 0xFFFF;
            uint64_t pit_period_ns = (uint64_t)div * 838; /* 1e9/1193182 ≈ 838 ns per PIT tick */

            if (now_ns - last_tick_ns >= pit_period_ns) {
                unsigned n_ticks = (unsigned)((now_ns - last_tick_ns) / pit_period_ns);
                if (n_ticks > 4) n_ticks = 4; /* cap catch-up */
                last_tick_ns += (uint64_t)n_ticks * pit_period_ns;

                /* Set IRR bit 0 (level-triggered: just assert, don't drop) */
                g_emu.pic[0].irr |= 0x01;
                prof_ticks_fired += n_ticks;
            }

            /* VSYNC at ~57 Hz (wall-clock based, independent of PIT rate) */
            static uint64_t last_vsync_ns = 0;
            if (last_vsync_ns == 0) last_vsync_ns = now_ns;
            if (now_ns - last_vsync_ns >= 17500000ULL) { /* ~57 Hz = 17.5ms */
                last_vsync_ns += 17500000ULL;
                g_emu.vsync_count++;
                g_emu.bar2_sram[4] = 1;
                g_emu.bar2_sram[5] = 0;
                g_emu.bar2_sram[6] = 0;
                g_emu.bar2_sram[7] = 0;
                uint32_t one = 1;
                uc_mem_write(uc, WMS_BAR2 + 4, &one, 4);

                /* DC_TIMING2: simulate VBLANK pulse */
                g_emu.dc_timing2 = 241;
                uint32_t vbl = 241;
                uc_mem_write(uc, GX_BASE + 0x8354, &vbl, 4);
            } else {
                /* Active lines — cycle through line numbers */
                static uint32_t dc_timing2_counter = 0;
                dc_timing2_counter += 8;
                if (dc_timing2_counter > 240) dc_timing2_counter = 0;
                g_emu.dc_timing2 = dc_timing2_counter;
                uc_mem_write(uc, GX_BASE + 0x8354, &dc_timing2_counter, 4);
            }

            /* Drain SIGALRM timer_pending (used only for HLT wakeup) */
            if (g_emu.timer_pending > 0)
                g_emu.timer_pending = 0;
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
                        /* Cache IDT base for fast interrupt injection */
                        uc_x86_mmr idtr;
                        uc_reg_read(uc, UC_X86_REG_IDTR, &idtr);
                        g_emu.idt_base = (uint32_t)idtr.base;
                        LOG("irq", "XINU ready: timer injection enabled EIP=0x%08x exec=%u idt_base=0x%x\n",
                            eip, (unsigned)g_emu.exec_count, g_emu.idt_base);

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

                            /* Use the runtime-detected IDT base — different
                             * per game (SWE1=0x2F7AD8, RFM=0x325054, …). */
                            uint32_t idt6 = g_emu.idt_base + 6u * 8u;
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
         * **SWE1-V1.19-ONLY** — every address in this block (Fatal/NonFatal/
         * CMOS/LocMgr/watchdog/PIC base_mask/DCS-mode patch) was reverse-
         * engineered against the SWE1 v1.19 update flash. Applying any of
         * them to RFM (or any other game) overwrites unrelated code/data
         * and wedges the boot. RFM's V1.12 patches are handled separately
         * in io.c, gated on game_id==50070.
         *
         * Critical patches (from x64 POC sgc-seed + i386 BT-85/BT-91):
         * - Fatal/NonFatal/CMOS → safe returns
         * - Q-table pre-init (BT-85: garbage causes insert() self-loop!)
         * - BSS zeroing (0x33D800-0x800000)
         * - 8 Fatal call site NOPs + 3 monitor() NOPs
         * - Monitor auto-exit triple patch
         * - Init gate flags + DCS2 + XINACMOS + BAR2 regs
         * ================================================================ */
        /* ================================================================
         * Post-XINU V1.19 patches — KILL-SWITCH for minimization audit.
         * Set ENCORE_KEEP_V19_PATCHES=1 to re-enable the full block.
         * Goal (per minimization ground truth): patches are SYMPTOMS, not cures.
         * Watchdog flag suppression already prevents pci_watchdog_bone
         * from firing; Fatal/NonFatal only fire on real crashes; etc.
         * If SWE1 v1.5 still boots without these → they're dead code. */
        static int v19_patches_enabled = -1;
        if (v19_patches_enabled < 0) {
            const char *e = getenv("ENCORE_KEEP_V19_PATCHES");
            v19_patches_enabled = (e && *e == '1') ? 1 : 0;
            LOG("init", "Post-XINU V1.19 patches: %s (set ENCORE_KEEP_V19_PATCHES=1 to re-enable)\n",
                v19_patches_enabled ? "ENABLED" : "DISABLED");
        }
        if (v19_patches_enabled && g_emu.xinu_booted && g_emu.ctor_phase == 0 &&
            g_emu.game_started && g_emu.xinu_ready &&
            g_emu.game_id == 50069u /* SWE1 only — see comment above */) {
            g_emu.ctor_phase = 3;  /* one-shot */

            /* --- 1. Function patches (x64 POC BT-86/BT-100) ---
             * 2026-04-21 SAFETY: hardcoded SWE1-V1.5 (=V1.19) addresses.
             * On SWE1-V2.1 the same offsets contain DIFFERENT code → patching
             * blindly corrupts random functions → game eventually JMPs to IVT
             * (EIP=0xdfc).  Each patch now sanity-checks the existing bytes
             * (function prologue 0x55 0x89 0xE5 = PUSH EBP; MOV EBP,ESP) before
             * overwriting.  On v2.1 the prologues won't match → patch skipped
             * → game runs unmodified → no corruption.  Minimization-style minimum.  */

            int fp_applied = 0, fp_skipped = 0;

            #define PROLOGUE_OK(addr) ({                          \
                uint8_t _p[3];                                    \
                uc_mem_read(uc, (addr), _p, 3);                   \
                (_p[0]==0x55 && _p[1]==0x89 && _p[2]==0xE5);      \
            })

            /* Fatal() at 0x22722C → INC counter + HLT for recovery */
            if (PROLOGUE_OK(0x0022722Cu)) {
                uint8_t fatal_patch[] = {
                    0xFF, 0x05, 0xF0, 0xFF, 0x2B, 0x00,  /* INC [0x2BFFF0] */
                    0x89, 0x35, 0xF4, 0xFF, 0x2B, 0x00,  /* MOV [0x2BFFF4], ESI */
                    0xF4,                                   /* HLT */
                    0xEB, 0xFE                              /* JMP $ */
                };
                uc_mem_write(uc, 0x0022722Cu, fatal_patch, sizeof(fatal_patch));
                fp_applied++;
            } else fp_skipped++;
            /* LocMgr Fatal wrapper at 0x24743C → XOR EAX,EAX; RET (x64 POC) */
            if (PROLOGUE_OK(0x0024743Cu)) {
                uint8_t xar[] = { 0x31, 0xC0, 0xC3 };
                uc_mem_write(uc, 0x0024743Cu, xar, 3);
                fp_applied++;
            } else fp_skipped++;
            /* NonFatal at 0x24780C → XOR EAX,EAX; RET (x64 POC BT-86) */
            if (PROLOGUE_OK(0x0024780Cu)) {
                uint8_t xar[] = { 0x31, 0xC0, 0xC3 };
                uc_mem_write(uc, 0x0024780Cu, xar, 3);
                fp_applied++;
            } else fp_skipped++;
            /* CMOS mem_test at 0x24FDEC → MOV EAX,1; RET (x64 POC BT-100) */
            if (PROLOGUE_OK(0x0024FDECu)) {
                uint8_t pass[] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3 };
                uc_mem_write(uc, 0x0024FDECu, pass, 6);
                fp_applied++;
            } else fp_skipped++;
            /* pci_watchdog_bone at 0x1A4190 → RET */
            if (PROLOGUE_OK(0x001A4190u)) {
                uint8_t ret = 0xC3;
                uc_mem_write(uc, 0x001A4190u, &ret, 1);
                fp_applied++;
            } else fp_skipped++;

            #undef PROLOGUE_OK

            LOG("init", "Function patches: applied=%d skipped=%d "
                "(skipped means version mismatch — game runs unmodified)\n",
                fp_applied, fp_skipped);

            /* If NO Function patches matched, we're on a different SWE1
             * build (e.g. v2.1).  Skip ALL the V1.19-hardcoded follow-ups:
             * base_mask data write, init-flag reads, BSS pokes — they'd land
             * on different code/data and corrupt the game.  Minimization proves
             * v2.1 boots without any of these. */
            if (fp_applied == 0) {
                LOG("init", "Post-XINU V1.19 patches SKIPPED — non-V1.19 SWE1 build\n");
                goto post_xinu_done;
            }
            g_emu.swe1_v19_detected = true;

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

            /* --- 4. Fake DCS2 channel object at 0x400000 (i386 POC copycat).
             *    stub: MOV EAX,1; RET — all vtable methods return 1.
             *    vtable: 64 entries → stub. channel: vtable ptr.
             *    Provides a valid DCS2 dispatch target for game code. */
            {
                uint8_t stub[6] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3 };
                uc_mem_write(uc, 0x400000u, stub, 6);
                uint32_t vtable[64];
                for (int i = 0; i < 64; i++) vtable[i] = 0x00400000u;
                uc_mem_write(uc, 0x400100u, vtable, sizeof(vtable));
                uint32_t ch_ptr = 0x00400100u;
                uc_mem_write(uc, 0x400200u, &ch_ptr, 4);
                /* XOR EAX,EAX; RET at 0x400010 (secondary trampoline) */
                uint8_t xor_ret[3] = { 0x31, 0xC0, 0xC3 };
                uc_mem_write(uc, 0x400010u, xor_ret, 3);
                LOG("init", "DCS2 fake obj: stub@400000 vtab@400100 ch@400200\n");
            }

            /* --- 5. Init gate flags — diagnostic read only.
             *    The i386 POC writes to 0x2797C4/0x2AF824/0x27979C/0x279768
             *    but those are V1.12 BSS addresses that overlap V1.19 code.
             *    For V1.19 the i386 POC uses "natural flow" — NO patches.
             *    Read-only: check if XINU naturally set them. */
            {
                uint32_t f_dcs2, f_sysinit, f_gate, f_bar0;
                uc_mem_read(uc, 0x2797C4u, &f_dcs2, 4);
                uc_mem_read(uc, 0x2AF824u, &f_sysinit, 4);
                uc_mem_read(uc, 0x27979Cu, &f_gate, 4);
                uc_mem_read(uc, 0x279768u, &f_bar0, 4);
                LOG("init", "Init flags (read): [2797C4]=0x%X [2AF824]=0x%X [27979C]=0x%X [279768]=0x%X\n",
                    f_dcs2, f_sysinit, f_gate, f_bar0);
            }

            /* --- 6. (moved out — now applied unconditionally; see below) */

            LOG("init", "=== Post-XINU V1.19 patches complete ===\n");
        post_xinu_done: ;
        }

        /* ============================================================
         * DCS-mode override — HOISTED out of V1.19 killswitch block so
         * sound works by default on any build where the byte pattern
         * matches (SWE1 v1.5/v1.19). Self-validates via CMP EAX,1 ; JNE
         * 0x21 (83 F8 01 75 21). On v2.1 the bytes don't match → patch
         * no-ops → game runs unmodified. One-shot: only attempted once
         * per XINU-ready transition.
         *
         * WHY THIS IS NEEDED: our DCS2 UART ports (0x13C-0x13F) return
         * 0xFF — the game's DCS-detect then falls to I/O-port mode and
         * its command queue never drains. Forcing BAR4 (MMIO) mode
         * routes commands to bar.c's DCS handler, which plays the boot
         * dong, runs audio init, and processes mixer cmds.
         * ============================================================ */
        if (g_emu.xinu_booted && g_emu.xinu_ready && g_emu.game_id == 50069u &&
            !g_emu.dcs_mode_patch_attempted) {
            g_emu.dcs_mode_patch_attempted = true;
            uint8_t old[5];
            if (uc_mem_read(uc, 0x1931E6u, old, 5) == UC_ERR_OK &&
                old[0] == 0x83 && old[1] == 0xF8 && old[2] == 0x01 &&
                old[3] == 0x75 && old[4] == 0x21) {
                uint8_t patch[] = { 0xB8, 0x01, 0x00, 0x00, 0x00 };
                uc_mem_write(uc, 0x1931E6u, patch, 5);
                LOG("init", "DCS mode patch: @1931E6 CMP+JNE → MOV EAX,1 (force BAR4 for sound)\n");
            } else {
                LOG("init", "DCS mode patch: byte pattern mismatch @1931E6 — skipped (non-V1.19 build; sound may be silent until pattern-scan is added)\n");
            }
        }

        /* pid2 (terminal) crash guard — x64 POC BT-98:  **SWE1-V1.19 only**
         * fn@0x1EF7D0 is called every timer tick from clkint at 0x1D0B73.
         * When pid2 doesn't exist yet, proctab[2]+0x18 ([0x2FCAAC]) has
         * garbage=1 → crash at 0x1D83E5, aborting the ENTIRE clkint handler
         * including VSYNC callback and sleep queue processing.
         * Fix: force [0x2FCAAC]=0 until pid2 is PRREADY (pstate=3).
         * Runs every 64 iterations to reduce overhead.
         * RFM has a different proctab layout — guard would corrupt RAM. */
        if (v19_patches_enabled && g_emu.xinu_ready && g_emu.swe1_v19_detected &&
            (g_emu.exec_count & 0x3F) == 0) {
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

        /* BT-118 IStack magic repair — x64 POC:  **SWE1-V1.19 only**
         * clkint checks currpid's IStack magic (0xAAA9) every tick.
         * If corrupted → Fatal → callback chain aborted → VSYNC never fires.
         * Repair magic between timer ticks so ISR always sees valid value.
         * Runs every 64 iterations to reduce overhead.
         * RFM uses different currpid/proctab addresses — would corrupt RAM. */
        if (v19_patches_enabled && g_emu.xinu_ready && g_emu.swe1_v19_detected &&
            (g_emu.exec_count & 0x3F) == 0) {
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
            int irq0_pending = (pending0 & 0x01) ? 1 : 0;
            if (pending0 || pending1)
                check_and_inject_irq();
            /* IRQ injection modifies Unicorn's EIP — sync local copy */
            uc_reg_read(uc, UC_X86_REG_EIP, &eip);

            /* Track IRQ0 injection count */
            if (irq0_pending)
                prof_irq0_inject++;
        }

        /* Fixed batch size — each call runs exactly this many TBs/instructions.
         * No uc_emu_stop from SIGALRM = no stop_request contamination.
         * Every call does real work. */
        size_t batch = 200000;

        /* Execute a batch of instructions.
         * eip is carried from previous iteration (or initial read before loop). */
        /* TLB flush: every 64 cycles for DC_TIMING2 VSYNC detection. */
        if ((g_emu.exec_count & 0x3F) == 0)
            uc_ctl_flush_tlb(uc);
        uc_err err = uc_emu_start(uc, eip, 0, 0, batch);

        g_emu.exec_count++;
        prof_emu_calls++;
        int eip_dirty = 0;  /* set when error handlers modify local eip */

        /* Periodic maintenance — every 64 iterations. */
        if ((g_emu.exec_count & 0x3F) == 0 && g_emu.game_started) {
            if (g_emu.watchdog_flag_addr)
                RAM_WR32(g_emu.watchdog_flag_addr, 0x0000FFFFu);
            RAM_WR32(0, 0);
            /* SWE1-V1.19 only: 0x002E98C8 / 0x002E8E2C / 0x002E8E30 are SWE1
             * BSS slots; on RFM these addresses point at unrelated game code
             * or data and writing into them wedges the boot. */
            if (g_emu.game_id == 50069u) {
                RAM_WR32(0x002E98C8u, 0);
                uint32_t dm_mode = RAM_RD32(0x002e8e2Cu);
                if (dm_mode == 1u && RAM_RD32(0x002e8e30u) < 2500u)
                    RAM_WR32(0x002e8e30u, 5000u);
            }
        } else if (g_emu.game_started) {
            RAM_WR32(0, 0);
        }

        /* Read EIP after execution stopped */
        uc_reg_read(uc, UC_X86_REG_EIP, &eip);

        if (err != UC_ERR_OK) {
            if (err == UC_ERR_INSN_INVALID) {
                uint8_t insn_buf[4];
                uint8_t *insn;
                if (eip < RAM_SIZE - 4) {
                    insn = g_emu.ram + eip;
                } else {
                    uc_mem_read(uc, eip, insn_buf, 4);
                    insn = insn_buf;
                }
                static int inv_cnt = 0;
                inv_cnt++;
                if (inv_cnt <= 30 || (inv_cnt % 10000) == 0)
                    LOG("cpu", "INSN_INVALID #%d EIP=0x%08x bytes=%02x %02x %02x %02x\n",
                        inv_cnt, eip, insn[0], insn[1], insn[2], insn[3]);
                if (insn[0] == 0xF4) {
                    prof_hlt++;
                    uint32_t efl;
                    uc_reg_read(uc, UC_X86_REG_EFLAGS, &efl);
                    efl |= 0x200;
                    uc_reg_write(uc, UC_X86_REG_EFLAGS, &efl);

                    if (eip == 0x227238u || eip == 0x1CF800u || eip == 0x1D96AEu) {
                        static int s_fatal_redir = 0;
                        if (s_fatal_redir < 20)
                            LOG("cpu", "Fatal/panic HLT @0x%08x → prnull idle (#%d)\n",
                                eip, ++s_fatal_redir);
                        eip = 0xFF0000u;
                        eip_dirty = 1;
                        uint32_t safe_esp = 0xDFFFE0u;
                        uc_reg_write(uc, UC_X86_REG_ESP, &safe_esp);
                        goto handle_display;
                    }

                    uint8_t irq_pend = g_emu.pic[0].irr & ~g_emu.pic[0].imr;
                    if (!irq_pend) {
                        eip++;
                        eip_dirty = 1;
                        static const struct timespec hlt_sleep = {0, 5000000}; /* 5ms */
                        nanosleep(&hlt_sleep, NULL);
                        g_emu.timer_pending = 1; /* force tick after HLT */
                    }
                    goto handle_display;
                }
                if (insn[0] == 0x0F) {
                    switch (insn[1]) {
                    case 0x3C: {
                        prof_0f3c++;
                        uint32_t eax, ebx, edx;
                        uc_reg_read(uc, UC_X86_REG_EAX, &eax);
                        uc_reg_read(uc, UC_X86_REG_EBX, &ebx);
                        uc_reg_read(uc, UC_X86_REG_EDX, &edx);

                        uint32_t a0 = edx, a1 = edx + 4;
                        uc_mem_write(uc, a0, &eax, 4);
                        uc_mem_write(uc, a1, &ebx, 4);
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
                        eip_dirty = 1;
                        goto handle_display;
                    }
                    case 0x3D: case 0x36: case 0x37: case 0x38:
                    case 0x39: case 0x3F:
                        eip += 2;
                        eip_dirty = 1;
                        goto handle_display;
                    default:
                        break;
                    }
                }
            }

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

            prof_other_err++;
            eip++;
            eip_dirty = 1;
        } else {
            /* UC_ERR_OK — execution completed normally (count exhausted or
             * HLT). Check for HLT (0xF4) at current EIP: Unicorn raises
             * EXCP_HLT which returns UC_ERR_OK (not INSN_INVALID), so we
             * need to handle it here to avoid infinite HLT loops. */
            if (eip < RAM_SIZE && g_emu.ram[eip] == 0xF4) {
                prof_hlt++;
                /* Enable interrupts (HLT clears IF in some contexts) */
                uint32_t efl;
                uc_reg_read(uc, UC_X86_REG_EFLAGS, &efl);
                if (!(efl & 0x200)) {
                    efl |= 0x200;
                    uc_reg_write(uc, UC_X86_REG_EFLAGS, &efl);
                }

                /* Fatal/panic HLT → redirect to idle */
                if (eip == 0x227238u || eip == 0x1CF800u || eip == 0x1D96AEu) {
                    static int s_fatal_ok = 0;
                    if (s_fatal_ok < 20)
                        LOG("cpu", "Fatal HLT (OK path) @0x%08x (#%d)\n", eip, ++s_fatal_ok);
                    eip = 0xFF0000u;
                    eip_dirty = 1;
                    uint32_t safe_esp = 0xDFFFE0u;
                    uc_reg_write(uc, UC_X86_REG_ESP, &safe_esp);
                    goto handle_display;
                }

                /* Normal HLT (idle loop) — sleep until interrupt pending */
                uint8_t irq_pend = g_emu.pic[0].irr & ~g_emu.pic[0].imr;
                if (!irq_pend) {
                    eip++;
                    eip_dirty = 1;
                    static const struct timespec hlt_sleep = {0, 5000000}; /* 5ms */
                    nanosleep(&hlt_sleep, NULL);
                    g_emu.timer_pending = 1; /* force tick after HLT */
                }
                goto handle_display;
            }
            prof_ok++;
        }

handle_display:
        /* Write back eip to Unicorn ONLY when error handlers modified it.
         * On the UC_ERR_OK path (99.99% of iterations), Unicorn already has
         * the correct EIP — skipping uc_reg_write saves ~14M API calls/5s.
         * cpu_inject_interrupt reads Unicorn's EIP for the interrupt frame. */
        if (eip_dirty)
            uc_reg_write(uc, UC_X86_REG_EIP, &eip);

        /* Display + heartbeat — check wall clock every 128 iterations. */
        if ((g_emu.exec_count & 0x7F) == 0) {
            static struct timespec last_display = {0, 0};
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (last_display.tv_sec == 0) last_display = now;
            long disp_ms = (now.tv_sec - last_display.tv_sec) * 1000
                         + (now.tv_nsec - last_display.tv_nsec) / 1000000;
            if (disp_ms >= 16) {
                last_display = now;
                uc_ctl_flush_tlb(uc);
                if (g_emu.display_ready) {
                    display_handle_events();
                    display_update();
                }
                /* Always poll netcon — independent of SDL display so that
                 * --headless + --serial-tcp / --keyboard-tcp still work. */
                netcon_poll();
            }
        }

        /* Heartbeat log — checked every 128 iterations */
        if ((g_emu.exec_count & 0x7F) == 0) {
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
                {
                    static uint32_t s_last_eip = 0;
                    static int s_stuck_n = 0;
                    if (eip == s_last_eip) s_stuck_n++; else s_stuck_n = 0;
                    s_last_eip = eip;
                    if (s_stuck_n >= 1 && s_stuck_n <= 3) {
                        uint8_t buf[64];
                        uint32_t base = (eip >= 16) ? eip - 16 : 0;
                        if (uc_mem_read(g_emu.uc, base, buf, sizeof(buf)) == UC_ERR_OK) {
                            char hex[64*3+8]; int p = 0;
                            for (int i = 0; i < 64; i++)
                                p += snprintf(hex+p, sizeof(hex)-p, "%02x ", buf[i]);
                            LOG("hb", "  STUCK bytes @0x%08x: %s\n", base, hex);
                            uint32_t esp = 0, regs[8];
                            uc_reg_read(g_emu.uc, UC_X86_REG_ESP, &esp);
                            static const int rids[8] = {UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI};
                            for (int i = 0; i < 8; i++) uc_reg_read(g_emu.uc, rids[i], &regs[i]);
                            LOG("hb", "  STUCK regs: eax=%08x ecx=%08x edx=%08x ebx=%08x esp=%08x ebp=%08x esi=%08x edi=%08x\n",
                                regs[0],regs[1],regs[2],regs[3],regs[4],regs[5],regs[6],regs[7]);
                            uint8_t stk[32];
                            if (uc_mem_read(g_emu.uc, esp, stk, 32) == UC_ERR_OK) {
                                p = 0;
                                for (int i = 0; i < 32; i++) p += snprintf(hex+p, sizeof(hex)-p, "%02x ", stk[i]);
                                LOG("hb", "  STUCK stack @esp: %s\n", hex);
                            }
                        }
                    }
                }
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
                    uint32_t dcs_mode = RAM_RD32(0x3444b0);
                    uint32_t dcs_state = RAM_RD32(0x3442e8);
                    uint32_t dcs_count = RAM_RD32(0x3442f8);
                    uint32_t ww,wr,bw,br,fr;
                    dcs_io_get_counters(&ww,&wr,&bw,&br,&fr);
                    LOG("hb", "  DM: mode=%u gxp=0x%x dt2=%u dcs_mode=%u dcs_st=%u dcs_cnt=%u io:ww=%u wr=%u bw=%u br=%u fr=%u\n",
                        dmm, gxp, g_emu.dc_timing2, dcs_mode, dcs_state, dcs_count,
                        ww, wr, bw, br, fr);
                    /* V1.19 DCS state machine flags (from disassembly) */
                    uint32_t dcs_ready_flag = RAM_RD32(0x3442f4);
                    uint32_t dcs_complete   = RAM_RD32(0x3442f0);
                    uint32_t dcs_last_val   = RAM_RD32(0x344414);
                    uint32_t dcs_init_flag  = RAM_RD32(0x344410);
                    LOG("hb", "  DCS-v19: rdy=%u cpl=%u lastval=0x%x initf=0x%x\n",
                        dcs_ready_flag, dcs_complete, dcs_last_val, dcs_init_flag);
                    /* Queue indices monitoring */
                    {
                        uint32_t q_wr = RAM_RD32(0x344408);
                        uint32_t q_rd = RAM_RD32(0x34440c);
                        uint32_t oq_wr = RAM_RD32(0x344498);
                        uint32_t oq_rd = RAM_RD32(0x34449c);
                        LOG("hb", "  Q: inner[wr=%u rd=%u] outer[wr=%u rd=%u]\n",
                            q_wr, q_rd, oq_wr, oq_rd);
                    }
                    /* One-shot dump of guest DCS state — disabled, BAR4 mode working */
#if 0
                    static int s_dcs_code_dumped = 0;
                    if (!s_dcs_code_dumped && wr >= 2) {
                        s_dcs_code_dumped = 1;
                        /* Extended DCS dispatch code (pre-statemachine + statemachine) */
                        LOG("hb", "  === Guest DCS code (0x192d40-0x192fd0) ===\n");
                        for (uint32_t a = 0x192d40; a < 0x192fd0; a += 16) {
                            uint8_t *p = g_emu.ram + a;
                            LOG("hb", "  %08x: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
                                a, p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],
                                p[8],p[9],p[10],p[11],p[12],p[13],p[14],p[15]);
                        }
                        /* Command queue getter + DCS globals */
                        LOG("hb", "  === Guest DCS cmd queue fn (0x191870-0x191960) ===\n");
                        for (uint32_t a = 0x191870; a < 0x191960; a += 16) {
                            uint8_t *p = g_emu.ram + a;
                            LOG("hb", "  %08x: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
                                a, p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],
                                p[8],p[9],p[10],p[11],p[12],p[13],p[14],p[15]);
                        }
                        LOG("hb", "  === DCS globals 0x344300-0x3444c0 ===\n");
                        for (uint32_t a = 0x344300; a < 0x3444C0; a += 16) {
                            uint8_t *p = g_emu.ram + a;
                            LOG("hb", "  %08x: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
                                a, p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],
                                p[8],p[9],p[10],p[11],p[12],p[13],p[14],p[15]);
                        }
                    }
#endif
                }
                /* Performance stats */
                LOG("hb", "  PERF: calls=%lu/5s ok=%lu 0f3c=%lu hlt=%lu ticks=%lu bar2wr=%u\n",
                    prof_emu_calls, prof_ok, prof_0f3c, prof_hlt, prof_ticks_fired,
                    g_emu.bar2_wr_count);
                prof_emu_calls = prof_0f3c = prof_hlt = prof_other_err = prof_ok = 0;
                prof_ticks_fired = prof_ticks_isr = prof_ticks_irr = prof_irq0_inject = 0;
                /* One-shot process table dump.
                 * XINU layout: proctab=0x2FC8C4, stride=232(0xE8),
                 * pstate@+0, paddr@+0x28, pname@+0x30(16 chars) */
                static int proctab_dumped = 0;
                if (!proctab_dumped && nproc >= 35) {
                    proctab_dumped = 1;
                    uint32_t currpid = RAM_RD32(0x2FC8BCu);
                    LOG("hb", "  proctab: currpid=%u nproc=%u\n", currpid, nproc);
                    for (uint32_t pid = 0; pid < 70; pid++) {
                        uint32_t pe = 0x2FC8C4u + pid * 232u;
                        uint8_t ps = RAM_RD8(pe);
                        if (ps == 0 && pid > 0) continue; /* PRFREE */
                        uint32_t pfn = RAM_RD32(pe + 0x28u);
                        char pn[17];
                        uc_mem_read(uc, pe + 0x30u, pn, 16); pn[16] = 0;
                        for (int i = 0; i < 16; i++)
                            if (pn[i] && ((uint8_t)pn[i] < 0x20 || (uint8_t)pn[i] > 0x7e))
                                pn[i] = '.';
                        const char *sn[] = {"FREE","CURR","RDY","RECV","SLP","SUSP","WAIT","RTIM"};
                        LOG("hb", "    pid%-3u %-4s fn=%08x '%s'\n",
                            pid, ps<8?sn[ps]:"??", pfn, pn);
                    }
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
