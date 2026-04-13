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

/* SIGALRM handler — sets timer flag, stops current emulation slice */
void cpu_timer_handler(int sig)
{
    (void)sig;
    g_emu.timer_fired = true;
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
                UC_HOOK_MEM_FETCH_UNMAPPED, (void*)hook_mem_invalid, NULL, 1, 0);

    /* MMIO hooks for BAR regions + GX_BASE */
    uc_hook h_bar_r, h_bar_w;
    /* BAR0-4 range: 0x10000000 - 0x14000000 */
    uc_hook_add(uc, &h_bar_r, UC_HOOK_MEM_READ, (void*)bar_mmio_read,
                NULL, WMS_BAR0, WMS_BAR4 + BAR4_SIZE - 1);
    uc_hook_add(uc, &h_bar_w, UC_HOOK_MEM_WRITE, (void*)bar_mmio_write,
                NULL, WMS_BAR0, WMS_BAR4 + BAR4_SIZE - 1);

    /* GX_BASE MMIO (0x40000000 - 0x41000000) */
    uc_hook h_gx_r, h_gx_w;
    uc_hook_add(uc, &h_gx_r, UC_HOOK_MEM_READ, (void*)bar_mmio_read,
                NULL, GX_BASE, GX_BASE + GX_BASE_SIZE - 1);
    uc_hook_add(uc, &h_gx_w, UC_HOOK_MEM_WRITE, (void*)bar_mmio_write,
                NULL, GX_BASE, GX_BASE + GX_BASE_SIZE - 1);

    LOG("cpu", "Unicorn Engine initialized (i386 mode)\n");
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

    /* Read current CPU state */
    uint32_t eip, esp, eflags, cs;
    uc_reg_read(uc, UC_X86_REG_EIP, &eip);
    uc_reg_read(uc, UC_X86_REG_ESP, &esp);
    uc_reg_read(uc, UC_X86_REG_EFLAGS, &eflags);
    uc_reg_read(uc, UC_X86_REG_CS, &cs);

    /* Check IF flag — don't inject if interrupts are disabled */
    if (!(eflags & 0x200)) return;

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

    /* Validate handler address */
    if (handler == 0 || handler == 0x20000000u) return; /* IVT stub, not real handler */

    /* Push interrupt frame: EFLAGS, CS, EIP */
    esp -= 4; uc_mem_write(uc, esp, &eflags, 4);
    esp -= 4;
    uint32_t cs32 = cs;
    uc_mem_write(uc, esp, &cs32, 4);
    esp -= 4; uc_mem_write(uc, esp, &eip, 4);

    /* Update registers */
    uc_reg_write(uc, UC_X86_REG_ESP, &esp);

    /* Clear IF */
    eflags &= ~0x200u;
    uc_reg_write(uc, UC_X86_REG_EFLAGS, &eflags);

    /* Jump to handler */
    uc_reg_write(uc, UC_X86_REG_EIP, &handler);

    /* Set CS if needed (XINU uses flat segments, CS stays same) */
    if (selector != cs) {
        uint32_t new_cs = selector;
        uc_reg_write(uc, UC_X86_REG_CS, &new_cs);
    }
}

/* Check PIC for pending interrupts and inject highest priority */
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

/* Main execution loop */
void cpu_run(void)
{
    uc_engine *uc = g_emu.uc;
    uint32_t eip;
    int display_timer = 0;
    struct timespec last_time, now;
    clock_gettime(CLOCK_MONOTONIC, &last_time);

    while (g_emu.running) {
        /* Run a slice of emulation */
        uc_err err = uc_emu_start(uc, 0, 0, 0, 0);

        g_emu.exec_count++;

        if (err != UC_ERR_OK) {
            uc_reg_read(uc, UC_X86_REG_EIP, &eip);

            if (err == UC_ERR_INSN_INVALID) {
                /* Check for HLT instruction */
                uint8_t insn;
                uc_mem_read(uc, eip, &insn, 1);
                if (insn == 0xF4) {
                    /* HLT — wait for interrupt */
                    if (g_emu.timer_fired) {
                        g_emu.timer_fired = false;
                        /* Raise PIT IRQ (IRQ0) */
                        g_emu.pic[0].irr |= 0x01;
                    }
                    check_and_inject_irq();

                    /* If no interrupt injected, advance past HLT */
                    uint32_t new_eip;
                    uc_reg_read(uc, UC_X86_REG_EIP, &new_eip);
                    if (new_eip == eip) {
                        new_eip = eip + 1;
                        uc_reg_write(uc, UC_X86_REG_EIP, &new_eip);
                    }
                    goto handle_display;
                }
            }

            /* Log error periodically */
            if (g_emu.exec_count < 20 || (g_emu.exec_count % 10000) == 0) {
                LOG("cpu", "uc_emu_start error: %s (EIP=0x%08x, exec=%lu)\n",
                    uc_strerror(err), eip, (unsigned long)g_emu.exec_count);
            }

            /* Try to skip problematic instruction */
            uc_reg_read(uc, UC_X86_REG_EIP, &eip);
            eip++;
            uc_reg_write(uc, UC_X86_REG_EIP, &eip);
        }

        /* Handle timer interrupt */
        if (g_emu.timer_fired) {
            g_emu.timer_fired = false;

            /* PIT IRQ0 fires at 100Hz via SIGALRM */
            g_emu.pic[0].irr |= 0x01;
            check_and_inject_irq();
        }

handle_display:
        /* Display update at ~60Hz (every ~16ms) */
        display_timer++;
        if (display_timer >= 6) {  /* 100Hz / 6 ≈ 16.6Hz initially, adjust as needed */
            display_timer = 0;

            if (g_emu.display_ready) {
                display_handle_events();
                display_update();

                /* VSYNC pulse in BAR2 SRAM */
                g_emu.vsync_count++;
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
                uc_reg_read(uc, UC_X86_REG_EIP, &eip);
                LOG("hb", "exec=%lu EIP=0x%08x post=0x%02x vsync=%u uart_pos=%d\n",
                    (unsigned long)g_emu.exec_count, eip, g_emu.post_code,
                    g_emu.vsync_count, g_emu.uart_pos);
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
