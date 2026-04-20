/*
 * io.c — I/O port handlers.
 *
 * PIC (i8259), PIT (i8254), CMOS/RTC, KBC, UART, LPT,
 * VGA, PRISM config (0x22/0x23), PCI (0xCF8/0xCFC), DCS2 status (0x6F96).
 */
#include "encore.h"

static uint8_t s_prism_regs[256] = {
    [0xB8] = 0x09, /* GCR — matches working i386 PoC / P2K-driver */
};

void io_init(void)
{
    /* PIC1 defaults */
    g_emu.pic[0].imr = 0xFF;  /* all masked initially */
    g_emu.pic[0].icw2 = 0x08; /* default vector base */

    /* PIC2 defaults */
    g_emu.pic[1].imr = 0xFF;
    g_emu.pic[1].icw2 = 0x70; /* default vector base */

    /* PIT defaults — channel 0 count */
    g_emu.pit.count[0] = 0xFFFF;
    g_emu.pit.count[1] = 0xFFFF;
    g_emu.pit.count[2] = 0xFFFF;

    /* CMOS — RTC time fields are populated dynamically on read.
     * Only static configuration fields set here. */
    g_emu.cmos_data[0x0A] = 0x26; /* status A */
    g_emu.cmos_data[0x0B] = 0x02; /* status B (24h mode) */
    g_emu.cmos_data[0x0C] = 0x00; /* status C */
    g_emu.cmos_data[0x0D] = 0x80; /* status D (battery OK) */
    g_emu.cmos_data[0x0E] = 0x00; /* diagnostic status */
    g_emu.cmos_data[0x0F] = 0x00; /* shutdown code */
    g_emu.cmos_data[0x10] = 0x00; /* floppy */
    g_emu.cmos_data[0x14] = 0x06; /* equipment byte */
    g_emu.cmos_data[0x15] = 0x80; /* base memory low (640KB = 0x0280) */
    g_emu.cmos_data[0x16] = 0x02; /* base memory high */
    g_emu.cmos_data[0x17] = 0x00; /* ext memory low (15MB = 0x3C00) */
    g_emu.cmos_data[0x18] = 0x3C; /* ext memory high */
    g_emu.cmos_data[0x30] = 0x00; /* ext memory low (copy) */
    g_emu.cmos_data[0x31] = 0x3C; /* ext memory high (copy) */
    g_emu.cmos_data[0x32] = 0x20; /* century BCD */

    /* KBC */
    g_emu.kbc_status = 0x14; /* self-test passed, input buffer empty */

    /* UART — default idle state */
    g_emu.uart_regs[5] = 0x60; /* LSR: THRE + TEMT (transmitter empty+ready) */

    /* PRISM config — GX_BASE at 0x40000000 */
    g_emu.gx_base_addr = GX_BASE;

    /* LPT — starts inactive. Activated on XINU boot or Allegro detection.
     * Before activation, returns 0xFF (no device) for BIOS probe.
     * After activation, implements P2K-driver parallel port protocol (BT-120). */
    g_emu.lpt_status = 0xFF; /* no printer port present until activated */
    g_emu.lpt_ctrl = 0x00;
    g_emu.lpt_active = false;

    LOG("io", "I/O ports initialized\n");
}

/* ===== PIC handlers ===== */
static uint32_t pic_read(int idx, uint16_t port)
{
    PICState *pic = &g_emu.pic[idx];
    if (port & 1) {
        return pic->imr;
    } else {
        return pic->read_isr ? pic->isr : pic->irr;
    }
}

static void pic_write(int idx, uint16_t port, uint8_t val)
{
    PICState *pic = &g_emu.pic[idx];

    if (port & 1) {
        /* Data port */
        if (pic->init_mode) {
            switch (pic->icw_step) {
            case 1: /* ICW2: vector base */
                pic->icw2 = val & 0xF8;
                LOG("pic", "PIC%d ICW2=0x%02x (vec base)\n", idx, pic->icw2);
                if (pic->icw1 & 0x02) {
                    /* SNGL=1: no ICW3, skip to ICW4 or done */
                    if (pic->icw1 & 0x01) {
                        pic->icw_step = 3; /* need ICW4 */
                    } else {
                        pic->icw_step = 0;
                        pic->init_mode = false;
                    }
                } else {
                    pic->icw_step = 2; /* need ICW3 */
                }
                break;
            case 2: /* ICW3: cascade config */
                pic->icw3 = val;
                if (pic->icw1 & 0x01) {
                    pic->icw_step = 3; /* need ICW4 */
                } else {
                    pic->icw_step = 0;
                    pic->init_mode = false;
                }
                break;
            case 3: /* ICW4: mode */
                pic->icw4 = val;
                pic->icw_step = 0;
                pic->init_mode = false;
                break;
            }
        } else {
            /* OCW1: Interrupt Mask Register */
            static int s_imr_log_cnt = 0;
            s_imr_log_cnt++;
            if (s_imr_log_cnt <= 20)
                LOG("pic", "PIC%d IMR=0x%02x (cnt=%d, prev=0x%02x)\n",
                    idx, val, s_imr_log_cnt, pic->imr);
            /* x64 POC BT-71 / i386 POC BT-130 compatibility:
             * After XINU boots, XINU's restore() does OUT 0x21, saved|base_mask.
             * Because we NOP'd device_init, base_mask has bit 0 set, so
             * every restore() re-masks IRQ0.  Fix: always keep IRQ0 + cascade
             * unmasked once XINU is ready. */
            if (idx == 0 && g_emu.xinu_ready)
                val &= ~0x05;  /* clear bits 0 (IRQ0) + 2 (cascade) */
            pic->imr = val;
        }
    } else {
        /* Command port */
        if (val & 0x10) {
            /* ICW1 */
            pic->icw1 = val;
            pic->icw_step = 1;
            pic->init_mode = true;
            pic->imr = 0;
            pic->isr = 0;
            pic->irr = 0;
            LOG("pic", "PIC%d ICW1=0x%02x (SNGL=%d IC4=%d)\n",
                idx, val, (val >> 1) & 1, val & 1);
        } else if (val == 0x20) {
            /* Non-specific EOI */
            uint8_t old_isr = pic->isr;
            for (int i = 0; i < 8; i++) {
                if (pic->isr & (1 << i)) {
                    pic->isr &= ~(1 << i);
                    break;
                }
            }
            static int eoi_log = 0;
            if (++eoi_log <= 20 || (eoi_log % 200 == 0))
                LOG("pic", "PIC%d EOI: ISR 0x%02x→0x%02x (cnt=%d)\n",
                    idx, old_isr, pic->isr, eoi_log);
        } else if (val == 0x60) {
            /* Specific EOI for IRQ0 */
            pic->isr &= ~0x01;
        } else if ((val & 0x60) == 0x60) {
            /* Specific EOI */
            pic->isr &= ~(1 << (val & 7));
        } else if (val == 0x0B) {
            /* OCW3: read ISR */
            pic->read_isr = 1;
        } else if (val == 0x0A) {
            /* OCW3: read IRR */
            pic->read_isr = 0;
        }
    }
}

/* ===== PIT handlers ===== */
static uint32_t pit_read(uint16_t port)
{
    int ch = port - PORT_PIT_CH0;
    if (ch < 0 || ch > 2) return 0;

    PITState *pit = &g_emu.pit;
    uint16_t val;

    if (pit->latched[ch]) {
        val = pit->latch[ch];
    } else {
        /* Simulate decrementing counter based on host time */
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint32_t ticks = (uint32_t)(ts.tv_nsec / 838); /* ~1.193MHz */
        val = pit->count[ch] - (ticks & 0xFFFF);
    }

    if (pit->access_lo[ch]) {
        pit->access_lo[ch] = 0;
        pit->latched[ch] = false;
        return (val >> 8) & 0xFF;
    } else {
        pit->access_lo[ch] = 1;
        return val & 0xFF;
    }
}

static void pit_write(uint16_t port, uint8_t val)
{
    if (port == PORT_PIT_CMD) {
        int ch = (val >> 6) & 3;
        if (ch == 3) return; /* readback, ignore */
        g_emu.pit.rw_mode[ch] = (val >> 4) & 3;
        g_emu.pit.mode[ch] = (val >> 1) & 7;
        g_emu.pit.access_lo[ch] = 0;
        if (g_emu.pit.rw_mode[ch] == 0) {
            /* Latch command */
            g_emu.pit.latch[ch] = g_emu.pit.count[ch];
            g_emu.pit.latched[ch] = true;
        }
        LOG("pit", "CMD: ch=%d rw=%d mode=%d val=0x%02x\n",
            ch, g_emu.pit.rw_mode[ch], g_emu.pit.mode[ch], val);
    } else {
        int ch = port - PORT_PIT_CH0;
        if (ch < 0 || ch > 2) return;
        if (g_emu.pit.access_lo[ch] == 0) {
            g_emu.pit.count[ch] = (g_emu.pit.count[ch] & 0xFF00) | val;
            g_emu.pit.access_lo[ch] = 1;
        } else {
            g_emu.pit.count[ch] = (g_emu.pit.count[ch] & 0x00FF) | (val << 8);
            g_emu.pit.access_lo[ch] = 0;
            if (ch == 0) {
                uint16_t div = g_emu.pit.count[0];
                LOG("pit", "CH0 divisor=%u → %u Hz\n",
                    div, div ? 1193182u / div : 0);
            }
        }
    }
}

/* ===== UART handler (COM1) ===== */

/* Apply RAM patches at game code start.
 *
 * i386 POC BT-91: "Disabled ALL patches → guest naturally produces golden log."
 * All hardcoded addresses (q-table, memtop, watchdog NOP, mem_detect, CMOS) are
 * for game v0.40 and corrupt V1.19 memory, causing watchdog expiry and Xinu traps.
 * V1.19 is self-sufficient: it initialises its own BSS, builds its process table,
 * loads the symbol table from flash, and feeds the watchdog via the timer interrupt.
 * We only need correct timer injection timing (see cpu.c phase-2 logic).
 *
 * Exception: ROM-agnostic watchdog flag scan — this is NOT a code patch.
 * We locate the pci_watchdog_bone() "expired" flag address so cpu.c can
 * keep it =0 each iteration, suppressing a false-alarm Fatal caused by
 * emulation running slower than real 200MHz hardware. */
static void apply_sgc_patches(void)
{
    LOG("sgc", "applying minimal post-start fixes for watchdog/mem_detect/display bring-up\n");

    /* === Watchdog flag scan ===
     * Strategy:
     *   1. Find "pci_watchdog_bone(): the watchdog has expired" string in RAM.
     *   2. Find the PUSH imm32 that pushes the string address (68 <addr LE>).
     *   3. Scan backward ~160 bytes for CMP [mem32],0  (83 3D <addr32> 00)
     *      or TEST [mem32],imm  or MOV EAX,[mem32] (8B 05 <addr32>)
     *      — the first such pattern is the watchdog flag load.
     *   4. Store the flag address; cpu.c writes 0 there each iteration.
     *
     * This mirrors the i386 POC's *(phys_ram + 0x2E98C8) = 0 but works for
     * both SWE1 and RFM without hardcoded addresses. */
    if (!g_emu.uc) return;

    /* Read 1 MB of live guest RAM (0x100000-0x1FFFFF) into a temp buffer.
     * We cannot use g_emu.ram directly: that buffer is the initial snapshot
     * written at memory_init() time. All subsequent guest writes (option ROM
     * copying game code to 0x100000) go into Unicorn's internal copy only.
     * uc_mem_read gives us the live view. */
    const uint32_t scan_base = 0x100000u;
    const uint32_t scan_size = 0x300000u;  /* 3 MB — mem_detect can be at 0x2BFxxx */
    uint8_t *buf = malloc(scan_size);
    if (!buf) {
        LOG("sgc", "watchdog scan: malloc failed\n");
        return;
    }
    if (uc_mem_read(g_emu.uc, scan_base, buf, scan_size) != UC_ERR_OK) {
        LOG("sgc", "watchdog scan: uc_mem_read failed\n");
        free(buf);
        return;
    }

    static const char needle[] = "pci_watchdog_bone(): the watchdog has expired";
    const size_t nlen = sizeof(needle) - 1;

    /* 1. Find the error string in the live game code buffer */
    uint32_t str_off = 0;   /* offset within buf */
    bool found = false;
    for (uint32_t off = 0; off + nlen < scan_size; off++) {
        if (memcmp(buf + off, needle, nlen) == 0) {
            str_off = off;
            found = true;
            break;
        }
    }
    if (!found) {
        LOG("sgc", "watchdog scan: string not found in guest RAM — suppression inactive\n");
        free(buf);
        return;
    }
    uint32_t str_addr = scan_base + str_off;  /* guest physical address */
    LOG("sgc", "watchdog scan: string at 0x%08x\n", str_addr);

    /* 2. Find PUSH imm32 <str_addr> (opcode 0x68 + LE32 == str_addr) */
    uint32_t push_off = 0;
    found = false;
    uint32_t ps = (str_off > 0x1000) ? str_off - 0x1000 : 0;
    uint32_t pe = str_off + 0x1000;
    if (pe > scan_size - 5) pe = scan_size - 5;
    for (uint32_t off = ps; off + 5 <= pe; off++) {
        if (buf[off] == 0x68) {
            uint32_t imm;
            memcpy(&imm, buf + off + 1, 4);
            if (imm == str_addr) {
                push_off = off;
                found = true;
                break;
            }
        }
    }
    if (!found) {
        LOG("sgc", "watchdog scan: PUSH str_addr not found — suppression inactive\n");
        free(buf);
        return;
    }
    LOG("sgc", "watchdog scan: PUSH at 0x%08x\n", scan_base + push_off);

    /* 3. Resolve the CALL before the string PUSH to find pci_read_watchdog.
     *    Scan backward from push_off for CALL rel32 (E8 xx xx xx xx).
     *    The CALL is typically within 200 bytes before the push. */
    uint32_t call_off = 0;
    bool call_found = false;
    uint32_t cb = (push_off > 200) ? push_off - 200 : 0;
    for (uint32_t off = cb; off + 5 <= push_off; off++) {
        if (buf[off] == 0xE8) {
            int32_t rel;
            memcpy(&rel, buf + off + 1, 4);
            /* target = (scan_base + off + 5) + rel must be valid code */
            int64_t target = (int64_t)(scan_base + off + 5) + rel;
            if (target >= scan_base && target < (int64_t)(scan_base + scan_size)) {
                call_off = off;
                call_found = true;
                /* keep searching: we want the LAST CALL before push */
            }
        }
    }
    if (!call_found) {
        LOG("sgc", "watchdog scan: CALL before PUSH not found — suppression inactive\n");
        free(buf);
        return;
    }
    /* Resolve callee: target = (scan_base + call_off + 5) + rel32 */
    int32_t call_rel;
    memcpy(&call_rel, buf + call_off + 1, 4);
    uint32_t callee_guest = (uint32_t)((int64_t)(scan_base + call_off + 5) + call_rel);
    uint32_t callee_off   = callee_guest - scan_base;  /* offset within buf */
    LOG("sgc", "watchdog scan: CALL at 0x%08x → callee 0x%08x\n",
        scan_base + call_off, callee_guest);

    /* 4. In the callee (pci_read_watchdog, possibly via wrapper), find
     *    CMP [addr32], 0xFFFF  →  81 3D <addr32> FF FF 00 00
     * This is the "watchdog health register" kept at 0xFFFF by the timer.
     * The callee may itself CALL pci_read_watchdog (one-level wrapper), so
     * we follow one nested CALL if the direct scan fails. */
    uint32_t health_addr = 0;
    /* Try direct callee first, then follow first E8 CALL within it */
    uint32_t search_starts[2] = { callee_off, 0 };
    int n_starts = 1;
    /* Look for a nested CALL inside callee (up to 32 bytes in) */
    uint32_t ce0 = callee_off + 32;
    if (ce0 > scan_size - 5) ce0 = scan_size - 5;
    for (uint32_t off = callee_off; off + 5 <= ce0; off++) {
        if (buf[off] == 0xE8) {
            int32_t rel2;
            memcpy(&rel2, buf + off + 1, 4);
            int64_t t2 = (int64_t)(scan_base + off + 5) + rel2;
            if (t2 >= scan_base && t2 < (int64_t)(scan_base + scan_size)) {
                search_starts[1] = (uint32_t)(t2 - scan_base);
                n_starts = 2;
                break;
            }
        }
    }
    for (int si = 0; si < n_starts && !health_addr; si++) {
        uint32_t ce = search_starts[si] + 64;
        if (ce > scan_size - 4) ce = scan_size - 4;
        for (uint32_t off = search_starts[si]; off + 10 <= ce; off++) {
            uint8_t *p = buf + off;
            /* 81 3D <addr32> FF FF 00 00 — CMP dword [addr32], 0x0000FFFF */
            if (p[0] == 0x81 && p[1] == 0x3D &&
                p[6] == 0xFF && p[7] == 0xFF && p[8] == 0x00 && p[9] == 0x00) {
                uint32_t cand;
                memcpy(&cand, p + 2, 4);
                if (cand >= 0x100000u && cand < 0x1000000u) {
                    health_addr = cand;
                    LOG("sgc", "watchdog health reg: CMP [0x%08x],0xFFFF at 0x%08x\n",
                        health_addr, scan_base + off);
                    break;
                }
            }
        }
    }

    free(buf);

    if (!health_addr) {
        LOG("sgc", "watchdog scan: health register not found — suppression inactive\n");
        return;
    }

    /* Store health_addr; cpu.c writes 0xFFFF there each exec iteration,
     * simulating the timer regularly feeding the watchdog. (BT-107) */
    g_emu.watchdog_flag_addr = health_addr;
    LOG("sgc", "watchdog suppression active: [0x%08x] will be kept =0xFFFF (BT-107)\n",
        health_addr);

    /* === BT-130: mem_detect() patch ===
     * XINU's mem_detect() returns 0x400 pages (4MB) in our emulator because the
     * MediaGX memory controller doesn't respond like real hardware.  With only 4MB,
     * prnull's stack is too small → stack overflow → Fatal crash before graphics.
     * Fix: scan for the known function prologue + immediate return of 0x400 and
     * change the returned page count to 0xE00 (14MB).
     *
     * Pattern (ROM-agnostic — same across SWE1 v1.5 and RFM v1.6):
     *   55 89 E5 B8 00 04 00 00 C9 C3
     *   ↑PUSH EBP ↑MOV EBP,ESP ↑MOV EAX,0x400 ↑LEAVE ↑RET
     * Patch: byte at +5 (low byte of imm32) → 0x0E (MOV EAX,0xE00 = 14MB pages)
     * (BT-130) */
    {
        static const uint8_t mem_pat[] = {
            0x55, 0x89, 0xE5, 0xB8, 0x00, 0x04, 0x00, 0x00, 0xC9, 0xC3
        };
        const uint32_t pat_len = sizeof(mem_pat);
        /* Re-read live RAM to search (watchdog scan already freed the buffer) */
        uint8_t *mb = malloc(scan_size);
        bool mem_found = false;
        if (mb && uc_mem_read(g_emu.uc, scan_base, mb, scan_size) == UC_ERR_OK) {
            for (uint32_t off = 0; off + pat_len < scan_size; off++) {
                if (memcmp(mb + off, mem_pat, pat_len) == 0) {
                    uint32_t patch_guest = scan_base + off + 5;
                    uint8_t newval = 0x0E;
                    uc_mem_write(g_emu.uc, patch_guest, &newval, 1);
                    LOG("sgc", "mem_detect patch: [0x%08x] 0x04→0x0E (4MB→14MB pages) (BT-130)\n",
                        patch_guest);
                    mem_found = true;
                    break;
                }
            }
        }
        if (!mem_found)
            LOG("sgc", "mem_detect patch: pattern not found in 0x%x-0x%x\n",
                scan_base, scan_base + scan_size);
        free(mb);
    }

    /* Minimal display bootstrap from the working i386 PoC.
     * These BSS fields stay zero in Encore because we do not emulate the full
     * PRISM/display bring-up sequence yet; leaving them zero sends the guest
     * down bad callback paths after ez0 init. */
    {
        uint32_t zero = 0u;
        uint32_t one = 1u;
        uint32_t fb_base = 0x800000u;
        uint8_t active = 1u;

        uc_mem_write(g_emu.uc, 0x002C902Cu, &one, sizeof(one));
        uc_mem_write(g_emu.uc, 0x002C9038u, &one, sizeof(one));
        uc_mem_write(g_emu.uc, 0x002D7274u, &one, sizeof(one));

        uc_mem_write(g_emu.uc, 0x00294494u, &zero, sizeof(zero));
        uc_mem_write(g_emu.uc, 0x002D31F8u, &active, sizeof(active));
        uc_mem_write(g_emu.uc, 0x002D3204u, &zero, sizeof(zero));

        uc_mem_write(g_emu.uc, 0x002935C8u, &fb_base, sizeof(fb_base));
        uc_mem_write(g_emu.uc, 0x00293638u, &one, sizeof(one));
        uc_mem_write(g_emu.uc, 0x002935B4u, &zero, sizeof(zero));

        LOG("sgc", "display bootstrap: render guards/gate + text/display state primed\n");
    }

    /* === prnull idle code at 0xFF0000 (can be written early, it's just code) === */
    {
        uint8_t idle_code[64];
        idle_code[0] = 0xFB;  /* STI */
        idle_code[1] = 0xF4;  /* HLT */
        idle_code[2] = 0xEB;  /* JMP short -4 (back to STI) */
        idle_code[3] = 0xFC;
        for (int i = 4; i < 64; i++) idle_code[i] = 0x90; /* NOP pad */
        uc_mem_write(g_emu.uc, 0x00FF0000u, idle_code, 64);
        LOG("sgc", "prnull idle code @ 0xFF0000 (STI+HLT+JMP)\n");
    }

    /* === BT-74: Patch JMP$ idle loop at 0x22f432 → HLT+JMP ===
     * The game's main idle spin is EB FE (JMP -2, jump to self).
     * In Unicorn, this creates a tight loop that prevents interrupt delivery.
     * Patch to F4 EB FD (HLT; JMP -3) so HLT exits Unicorn's emu_start,
     * allowing the host to deliver timer interrupts. */
    {
        uint8_t code[3];
        uc_mem_read(g_emu.uc, 0x0022f432u, code, 2);
        if (code[0] == 0xEB && code[1] == 0xFE) {
            uint8_t patch[3] = { 0xF4, 0xEB, 0xFD };
            uc_mem_write(g_emu.uc, 0x0022f432u, patch, 3);
            LOG("sgc", "BT-74: JMP$ → HLT+JMP at 0x22f432\n");
        } else {
            LOG("sgc", "BT-74: 0x22f432 = %02x %02x (not EB FE, skipped)\n",
                code[0], code[1]);
        }
    }

    /* === Fatal() → HLT (BT-122) ===
     * V1.12: Fatal at 0x1CF7F4. V1.19: Fatal at 0x22722C (patched in cpu.c).
     * Only apply V1.12 patch for RFM (game_id=50070). */
    if (g_emu.game_id == 50070) {
        uint8_t fatal_patch[] = {
            0xFF, 0x05, 0xF0, 0xFF, 0x2B, 0x00,  /* INC [0x2BFFF0] — counter */
            0x89, 0x35, 0xF4, 0xFF, 0x2B, 0x00,  /* MOV [0x2BFFF4], ESI — caller */
            0xF4,                                   /* HLT (stops guest) */
            0xEB, 0xFE                              /* JMP $ (safety spin) */
        };
        uc_mem_write(g_emu.uc, 0x001CF7F4u, fatal_patch, sizeof(fatal_patch));
        LOG("sgc", "Fatal() 0x1CF7F4 → marker+HLT (BT-122, V1.12/RFM)\n");
    } else {
        LOG("sgc", "Fatal() 0x1CF7F4 skipped (V1.19 uses 0x22722C, patched in cpu.c)\n");
    }

    /* === Panic loop → HLT (0x1D96AE: EB FE) — V1.12 only === */
    if (g_emu.game_id == 50070) {
        uint8_t code[2];
        uc_mem_read(g_emu.uc, 0x001D96AEu, code, 2);
        if (code[0] == 0xEB && code[1] == 0xFE) {
            uint8_t hlt2[2] = { 0xF4, 0xF4 };
            uc_mem_write(g_emu.uc, 0x001D96AEu, hlt2, 2);
            LOG("sgc", "panic loop 0x1D96AE → HLT HLT (V1.12/RFM)\n");
        }
    }

    /* === Function stubs: V1.12-specific addresses — skip for V1.19 ===
     * These addresses (0x18BF70, 0x18C148, 0x17BA9C, 0x2C6D00, 0x2712A8)
     * are V1.12 functions. In V1.19 they're different code — patching them
     * corrupts the game (EIP falls through garbage → crash to 0x000000). */
    if (g_emu.game_id == 50070) {
        uint8_t ret = 0xC3;
        uint32_t zero = 0u;
        uc_mem_write(g_emu.uc, 0x0018BF70u, &ret, 1);
        uc_mem_write(g_emu.uc, 0x0018C148u, &ret, 1);
        uc_mem_write(g_emu.uc, 0x0017BA9Cu, &ret, 1);
        uc_mem_write(g_emu.uc, 0x002712A8u, &zero, 4);
        uint32_t ret_zone = 0x20000000u;
        uc_mem_write(g_emu.uc, 0x002C6D00u, &ret_zone, 4);
        LOG("sgc", "stubs: 0x18BF70, 0x18C148, 0x17BA9C → RET, [0x2C6D00]→0x20000000 (V1.12/RFM)\n");
    } else {
        LOG("sgc", "V1.12 stubs skipped for V1.19 (different addresses)\n");
    }

    /* === SuperIOType = PC97338 (POC: [0x2C6DFC]=1) ===
     * V1.12 BSS address. In V1.19, this address is in a different section.
     * Only apply for V1.12; V1.19 game detects chip via I/O probing. */
    if (!g_emu.is_v19_update) {
        uint32_t one = 1u;
        uc_mem_write(g_emu.uc, 0x002C6DFCu, &one, sizeof(one));
        LOG("sgc", "[0x2C6DFC]=1 (SuperIOType=PC97338, V1.12)\n");
    }

    /* === EARLY getstk() free list injection (POC BT-64) ===
     * 0x2D577C is the V1.12 free-list head address. In V1.19, the free list
     * is at a different BSS address. XINU's sysinit() correctly initializes
     * the free list at the right address for both versions.
     * Only inject for V1.12 to avoid corrupting V1.19 data. */
    if (!g_emu.is_v19_update) {
        uint32_t head = 0;
        uc_mem_read(g_emu.uc, 0x002D577Cu, &head, sizeof(head));
        if (head == 0u) {
            uint32_t blk = 0x300000u;
            uint32_t sz  = 0xC00000u;  /* 12MB */
            uint32_t zero = 0u;
            uc_mem_write(g_emu.uc, blk + 0u, &zero, sizeof(zero));
            uc_mem_write(g_emu.uc, blk + 4u, &sz, sizeof(sz));
            uc_mem_write(g_emu.uc, 0x002D577Cu, &blk, sizeof(blk));
            LOG("sgc", "getstk free list: [0x2D577C]=0x%08x size=0x%x (V1.12)\n", blk, sz);
        } else {
            LOG("sgc", "getstk free list: head already set (0x%08x)\n", head);
        }
    }

    /* === PLX BAR0 pointer for init gate (POC: [0x279768]=WMS_BAR0) ===
     * V1.12 address. In V1.19, the init gate uses a different variable. */
    if (!g_emu.is_v19_update) {
        uint32_t bar0 = WMS_BAR0;
        uc_mem_write(g_emu.uc, 0x00279768u, &bar0, sizeof(bar0));
        LOG("sgc", "[0x279768]=0x%08x (PLX BAR0 ptr, V1.12)\n", bar0);
    }

    /* === IVT null-pointer guard (POC: bar2_patch_ivt) ===
     * In protected mode, the real-mode IVT at address 0 is unused for interrupts
     * (IDT is used instead). However, the game's interval_0_25ms() checks
     * [0x00000000] == 0 as a corruption sentinel. Do NOT fill IVT — it corrupts
     * the sentinel. Instead, install a safe HLT at address 0 so null pointer
     * execution stops cleanly without destroying the sentinel check.
     * NOTE: Address 0 must stay 0x00000000 for the game's corruption check! */
    {
        /* Install IRET+EOI stub at physical 0x20000 (safe landing zone) */
        uint8_t stub[] = {
            0x50,                   /* PUSH AX */
            0xB0, 0x20,             /* MOV AL, 0x20 (EOI) */
            0xE6, 0x20,             /* OUT 0x20, AL (PIC master EOI) */
            0x58,                   /* POP AX */
            0xCF,                   /* IRET */
        };
        uc_mem_write(g_emu.uc, 0x00020000u, stub, sizeof(stub));
        LOG("sgc", "IRET+EOI stub at phys 0x20000 (address 0 left as zero sentinel)\n");
    }
}

/* === Late-stage patches: applied when "XINU: V7" is detected in UART ===
 * By this point XINU has finished sysinit() and the process table is populated.
 * The prnull ctxsw frame and scheduler flags are now at their correct addresses. */
static void apply_xinu_boot_patches(void)
{
    if (!g_emu.uc) return;

    LOG("sgc", "applying XINU boot patches (prnull + scheduler)\n");

    /* prnull ctxsw frame fix: savsp is at proctab[0].savsp = [0x2B3E9C].
     * ctxsw does POP EBX,EDI,ESI,EBP then RET.
     * savsp+16 = return address after 4 POPs. Set to 0xFF0000 (idle code). */
    {
        uint32_t savsp = 0;
        uc_mem_read(g_emu.uc, 0x002B3E9Cu, &savsp, 4);
        if (savsp >= 0x3F0000u && savsp < 0x410000u) {
            uint32_t idle_addr = 0x00FF0000u;
            uint32_t safe_ebp  = 0x003FFFFCu;
            uc_mem_write(g_emu.uc, savsp + 16u, &idle_addr, 4);
            uc_mem_write(g_emu.uc, savsp + 12u, &safe_ebp, 4);
            LOG("sgc", "prnull ctxsw: savsp=0x%08x, [savsp+16]=0xFF0000\n", savsp);
        } else {
            LOG("sgc", "prnull savsp=0x%08x (unexpected), skipping ctxsw fix\n", savsp);
        }
    }

    /* XINU scheduler patches:
     * - prnull.pstate = PRRUN so resched() can re-insert prnull
     * - sched_en = 1 so resched() actually runs
     * - tick_init = 1 so IRQ0 (PIT timer) triggers rescheduling
     * - clkruns = 1 so the defclk callback chain fires */
    {
        uint32_t one = 1u;
        uint32_t magic = 0x003C2000u;
        uint8_t prrun = 1u;

        uc_mem_write(g_emu.uc, 0x002B3E94u, &prrun, 1);
        uc_mem_write(g_emu.uc, 0x002B3EB8u, &magic, 4);
        uc_mem_write(g_emu.uc, 0x002BD544u, &one, 4);
        uc_mem_write(g_emu.uc, 0x002BD5ECu, &one, 4);
        uc_mem_write(g_emu.uc, 0x002BD540u, &one, 4);

        LOG("sgc", "scheduler: pstate=1@0x2B3E94, magic=0x3C2000@0x2B3EB8, "
            "sched_en@0x2BD544, tick_init@0x2BD5EC, clkruns@0x2BD540\n");
    }
}

/* ===== SMC8216T NIC emulation (BT-131) =====
 * ez0: port 0x300 irq 7 mac 00:00:c0:01:02:03 type SMC8216T (8 bit)
 * Mirrors poc-P2K-runtime-i386/src/ports.c SMC8216T block.
 * 0x300-0x30F: WD/SMC board registers (mode-switched by reg[4] bit 7)
 *   bit7=0 → LAN ROM mode: regs 0x08-0x0E return MAC + board ID
 *   bit7=1 → internal mode: returns raw reg shadow
 * 0x310-0x31F: DP8390 NIC registers (paged by CR bits 6-7)
 *
 * Also: NIC shared memory at 0xD0000 (16KB) mirrors LAN ROM data.
 * Call nic_dseg_init() from bar_init() to populate guest RAM there. */
static uint8_t s_wd_regs[16] = {
    0x80, 0x00, 0x00, 0x00,  /* [0]-[3] */
    0x20, 0x00, 0x00, 0x40,  /* [4]=control (bit5=1, bit7=0 → LAN ROM mode) */
    0x22, 0x00, 0x00, 0x18,  /* [8], [9], [A], [B] */
    0x00, 0x4C, 0x01, 0x00   /* [C], [D]=IRQ7 cfg, [E]=1, [F] */
};
static const uint8_t s_nic_mac[6] = {0x00, 0x00, 0xC0, 0x01, 0x02, 0x03};
static uint8_t s_8390_regs[64];  /* 4 pages × 16 regs */
static uint8_t s_8390_cr = 0x21; /* CR: STP=1, RD2=1 (stopped) */

static uint32_t nic_board_rd(uint16_t port)
{
    uint32_t off = (port - 0x300u) & 0xF;
    int mode = (s_wd_regs[4] & 0x80) != 0; /* bit7: internal vs LAN ROM */
    switch (off) {
    case 0x00 ... 0x07: return s_wd_regs[off];
    case 0x08: return mode ? s_wd_regs[9]   : s_nic_mac[0];
    case 0x09: return                          s_nic_mac[1]; /* always MAC[1] */
    case 0x0A: return mode ? s_wd_regs[0xA] : s_nic_mac[2];
    case 0x0B: return mode ? s_wd_regs[0xB] : s_nic_mac[3];
    case 0x0C: return mode ? s_wd_regs[0xC] : s_nic_mac[4];
    case 0x0D: return mode ? s_wd_regs[0xD] : s_nic_mac[5];
    case 0x0E: return mode ? s_wd_regs[0xE] : 0x2A; /* board ID: SMC8216T */
    case 0x0F:
        if (mode) return s_wd_regs[0xE];
        { /* checksum: sum of bytes 0x08-0x0E + checksum = 0xFF */
            uint8_t sum = 0;
            for (int i = 8; i <= 0xE; i++)
                sum += (uint8_t)nic_board_rd(0x300u + i);
            return (uint8_t)(0xFF - sum);
        }
    default: return 0;
    }
}

static void nic_board_wr(uint16_t port, uint8_t val)
{
    uint32_t off = (port - 0x300u) & 0xF;
    s_wd_regs[off] = val;
}

static uint32_t nic_8390_rd(uint16_t port)
{
    uint32_t off = (port - 0x310u) & 0xF;
    uint8_t page = (s_8390_cr >> 6) & 3;
    if (off == 0x00) return s_8390_cr;
    if (off == 0x07 && page == 0) return 0x80; /* ISR: RST=1 after reset */
    if (page == 1 && off >= 1 && off <= 6)
        return s_nic_mac[off - 1]; /* PAR0-PAR5 = MAC */
    return s_8390_regs[page * 16 + off];
}

static void nic_8390_wr(uint16_t port, uint8_t val)
{
    uint32_t off = (port - 0x310u) & 0xF;
    uint8_t page = (s_8390_cr >> 6) & 3;
    if (off == 0x00) { s_8390_cr = val; return; }
    if (off == 0x07 && page == 0) {
        s_8390_regs[page * 16 + off] &= ~val; /* ISR: write 1 to clear */
        return;
    }
    s_8390_regs[page * 16 + off] = val;
}

/* Populate NIC LAN address ROM data in D-segment shared memory (0xD0000).
 * The WD/SMC driver reads MAC + board ID from shared memory, not just IO ports.
 * Must be called after Unicorn engine is set up (uc != NULL). */
void nic_dseg_init(void)
{
    if (!g_emu.uc) return;
    /* MAC at 0xD0008-0xD000D, board ID at 0xD000E */
    for (int i = 0; i < 6; i++)
        uc_mem_write(g_emu.uc, 0xD0008u + i, &s_nic_mac[i], 1);
    uint8_t board_id = 0x2A;
    uc_mem_write(g_emu.uc, 0xD000Eu, &board_id, 1);
    /* Checksum at 0xD000F: bytes 0xD0008-0xD000E must sum to 0xFF */
    uint8_t sum = 0;
    for (int i = 0; i < 6; i++) sum += s_nic_mac[i];
    sum += board_id;
    uint8_t csum = 0xFF - sum;
    uc_mem_write(g_emu.uc, 0xD000Fu, &csum, 1);
    LOG("nic", "D-seg NIC LAN ROM: MAC %02x:%02x:%02x:%02x:%02x:%02x board_id=0x%02x (BT-131)\n",
        s_nic_mac[0], s_nic_mac[1], s_nic_mac[2],
        s_nic_mac[3], s_nic_mac[4], s_nic_mac[5], board_id);
}

/* Port 0x61 (System Control Port B) — file scope so both read and write can access */
static uint8_t s_port61 = 0;

/* ===== LPT (Parallel Port) at 0x378-0x37A ===== */
/* BT-120: Faithful P2K-driver processParallelPortAccess protocol.
 *
 * Protocol summary:
 *   0x378 (data) WRITE: stores to initializationMemoryBlock
 *   0x378 (data) READ:  if renderingFlags bits 0+3 set → retrieveRenderingStatus(opcode)
 *                        else → returns last data written (D-type latch echo)
 *   0x379 (status) READ: always 0x87 (device ID signature) when active
 *   0x37A (control) WRITE: edge-detect protocol:
 *     - bit 2 rising: captures data → opcode
 *     - bit 0 falling: dispatches processDataBasedOnParameter(opcode, data)
 *     - stores renderingFlags for gating data reads
 */

/* P2K-runtime rendering/switch state machine — exact copy of i386 POC (BT-118) */
static uint8_t s_rendering_flags     = 0;
static uint8_t s_data_for_rendering  = 0; /* captured opcode */
static int     s_access_mode4_prev   = 0; /* bit2 edge detect */
static int     s_access_mode1_prev   = 0; /* bit0 edge detect */

/* Switch matrix state (mirrors P2K-runtime globals) */
static uint8_t s_rendering_status[8];
static uint8_t s_rendering_data_val  = 0;
static uint8_t s_start_button_held   = 0;
static uint8_t s_probe_held          = 0;   /* digit-key debug probe */
static uint8_t s_probe_bit           = 0;   /* which bit of c0 to set  */
static uint8_t s_data_val2           = 0;
static uint8_t s_data_val3           = 0;
static uint8_t s_data_val4           = 0;
static uint8_t s_data_flag1          = 0;
static int     s_data_flag2          = 0;
static uint8_t s_data_flag3          = 0;
static uint8_t s_data_flag4          = 0;
static uint8_t s_data_flag5          = 0;
static uint8_t s_data_flag6          = 0;
static uint8_t s_data_flag7          = 0;
static int     s_data_bit4           = 0;
static int     s_data_bit6           = 0;

/* Game button/switch state — active-HIGH (bit SET = pressed, 0x00 = idle).
 *   s_lpt_button_state → Physical[10] (flippers + action buttons, masked to bits 4-7)
 *   s_lpt_switch_state → Physical[9]  (4 coin-door buttons, masked to bits 0-3) */
static uint8_t s_lpt_button_state = 0x00;
static uint8_t s_lpt_switch_state = 0x00;

/* Persistent cabinet interlocks — Physical[10]:
 *   bit 0 = sw=80 SLAM TILT       (must stay 0 in idle, else game refuses play)
 *   bit 1 = sw=81 Coin Door CLOSED (interlock; 1 = door physically closed)
 *   bit 2 = sw=82 Plumb-bob tilt   (stays 0)
 * F4 toggles s_coin_door_closed. Boot default = closed (1) so the
 * "OPEN COIN DOOR" overlay disappears and play is enabled. */
static uint8_t s_coin_door_closed = 1;

/* One-shot pulse for sw=75 'Enter' button (Physical[9] bit 3).
 * F5 calls lpt_pulse_enter() which holds the bit high for ~60 LPT
 * reads — long enough for the 2ms scan to validate one press+release
 * edge. From attract this enters the diagnostics menu; inside a menu
 * it acts as ENTER/select. */
static int     s_enter_pulse = 0;

/* Mirrors P2K-runtime calculateBitwiseSumBasedOnInput */
static int calc_bitwise_sum(uint8_t val)
{
    int has_bit = 0, sum = 0, pos = 0;
    for (unsigned v = val; v != 0; v >>= 1, pos++) {
        has_bit = 1;
        if (v & 1) sum += pos;
    }
    return has_bit + sum;
}

/* LPT switch matrix — RE-VERIFIED from SWE1 ROM table @ 0x2e3018:
 *
 *   The SwitchInit table has a subtle off-by-one quirk: for entry index N
 *   the *name strings* (offsets +0x14..+0x20) describe sw_num = N, but
 *   the *callback pointer* at +0x2c is actually entry (N+1)'s first
 *   field — i.e. each entry's REAL stride begins at +0x2c of the previous
 *   one. So the swd_*_proc symbols line up with the strings of entry N+1.
 *   Double-check by following a few: entry 1 cb=swd_start_button_proc /
 *   entry 2 names=["Start Button"] → sw_num=2 IS Start Button. ✓
 *
 *   Authoritative map (sw_num → col,bit → meaning):
 *     sw= 2  c0 b2  Start Button
 *     sw=64  c8 b0  Left   Coin Slot
 *     sw=65  c8 b1  Center Coin Slot
 *     sw=66  c8 b2  Right  Coin Slot
 *     sw=67  c8 b3  4th    Coin Option
 *     sw=72  c9 b0  'Escape' button (= Service Credits in attract)
 *     sw=73  c9 b1  'Down'   button (= Volume −     in attract)
 *     sw=74  c9 b2  'Up'     button (= Volume +     in attract)
 *     sw=75  c9 b3  'Enter'  button (= Begin Test   in attract)
 *     sw=80  c10 b0 SLAM TILT             (must be 0 idle!)
 *     sw=81  c10 b1 Coin Door Closed      (interlock; 1=closed)
 *     sw=82  c10 b2 Plumb-bob Tilt        (0 idle)
 *     sw=84  c10 b4 Right flipper button
 *     sw=85  c10 b5 Left  flipper button
 *     sw=86  c10 b6 Right Action button
 *     sw=87  c10 b7 Left  Action button
 *
 *   Column → opcode mapping (verified twice in scan loop @ 0xad137):
 *     col 8  → opcode 0x00  (push 0)
 *     col 9  → opcode 0x03  (push 3)
 *     col 10 → opcode 0x01  (push ebx=1)
 *     col 11 → opcode 0x02  (push 2)
 *     col 0..7 → opcode 0x04 + col selected via opcode 0x05 latch
 */
static uint8_t retrieve_rendering_status(uint8_t opcode)
{
    uint8_t result = 0;
    switch (opcode) {
    case 0x00:
        /* Physical[8] — coin slots only (bits 0-3). Idle = 0x00. */
        result = s_lpt_button_state >> 4;  /* unused for now; coin-slot keys not bound */
        result = 0x00;                     /* keep zero until a coin-slot key is added */
        break;
    case 0x01: {
        /* Physical[10] — flippers/actions + door interlock + tilts */
        uint8_t v = s_lpt_button_state & 0xF0;       /* bits 4-7 from host keys */
        if (s_coin_door_closed) v |= 0x02;           /* bit 1 = door closed (interlock) */
        result = v;                                  /* bit 0 (slam) and bit 2 (plumb) stay 0 */
        break;
    }
    case 0x02: result = 0xF0; break;                 /* status hi nibble; low = Physical[11] */
    case 0x03: {
        /* Physical[9] — 4 coin-door buttons (bits 0-3); flipper EOS bits 4-7 stay 0 */
        uint8_t v = s_lpt_switch_state & 0x0F;
        if (s_enter_pulse > 0) { v |= 0x08; s_enter_pulse--; }   /* F5 → 'Enter' button */
        result = v;
        break;
    }
    case 0x04: {
        /* Physical[0..7] matrix scan. The i386 POC always returns
         * s_rendering_status[1] regardless of selected col, so all 8
         * cols end up with the same byte. To inject Start Button
         * (sw=2, col 0, bit 2) we OR bit 2 unconditionally — which
         * also lights bit 2 in cols 1..7 (Right Bank Lower / Trough 2
         * / Shield Tgt / Upper Jet / unused), but those are harmless
         * in attract mode where the user is pressing Start.
         * Probe bits (digit-key debug) override bit 2 with whatever
         * bit s_probe_bit selects so we can find the real Start. */
        uint8_t v = s_rendering_status[1];
        if (s_probe_held) v |= (uint8_t)(1u << s_probe_bit);
        else if (s_start_button_held) v |= 0x04;
        result = v;
        break;
    }
    /* Cases 0x0F-0x13: matches P2K-driver retrieveRenderingStatus exactly.
     * These are auxiliary status reads (data flags / strobe).
     * Returning a constant 0xFF here causes the game to read phantom
     * switch/volume bits as active. */
    case 0x0F:
        result = (s_data_flag1 << 6) | (s_data_bit6 << 7);
        s_data_bit6 = !s_data_bit6;
        break;
    case 0x10:
    case 0x11:
        result = s_data_bit6 ? 0x00 : 0xFF;
        break;
    case 0x12:
    case 0x13:
        result = 0;
        break;
    default: result = 0x00; break;
    }
    return result;
}

/* i386 POC process_data_command — verbatim copy */
static void process_data_command(uint8_t opcode, uint8_t data)
{
    switch (opcode) {
    case 0x05:
        s_rendering_data_val = data; s_data_flag1 = 1;
        break;
    case 0x06: s_data_val2 = data; break;
    case 0x07: s_data_val3 = data; break;
    case 0x08:
        s_data_val4 = data;
        if (data != 0) {
            int idx = calc_bitwise_sum(data);
            if (idx > 0 && idx < 8) s_rendering_status[idx] = s_data_val2;
        }
        break;
    case 0x09: s_data_flag3 = s_data_flag2 ? (s_data_flag3 | data) : 0; break;
    case 0x0A: s_data_flag4 = s_data_flag2 ? (s_data_flag4 | data) : 0; break;
    case 0x0B: s_data_flag5 = s_data_flag2 ? (s_data_flag5 | data) : 0; break;
    case 0x0C: s_data_flag6 = s_data_flag2 ? (s_data_flag6 | data) : 0; break;
    case 0x0D: {
        int new_bit4 = (data & 0x10) >> 4;
        if (new_bit4 != s_data_bit4) s_data_bit4 = new_bit4;
        s_data_flag2 = (data & 0x20) >> 5;
        s_data_bit6  = (data & 0x80) >> 7;
        s_data_flag7 = s_data_flag2 ? (s_data_flag7 | (data & 0x0F)) : 0;
        break;
    }
    }
}

void lpt_set_host_input(uint8_t buttons, uint8_t switches)
{
    s_lpt_button_state = buttons;
    s_lpt_switch_state = switches;
}

void lpt_set_start_button(int held)
{
    /* LPT-only Start button injection. The state flag is read in
     * retrieve_rendering_status case 0x04 (matrix col 0) where bit 2
     * is OR'd in while held. The game's switch consumer then sees a
     * normal sw=2 press. No CPU/RAM patching — if the game doesn't
     * act on it (gameplay state preconditions), we don't fight it. */
    s_start_button_held = held ? 1 : 0;
}

/* Debug probe: temporarily wire one bit of Physical[Cn] / Logical[Cn]
 * to a digit key. Col 0..11, bit 0..7 covers all 96 switches. Use to
 * discover which (col,bit) actually triggers Start Game. */
void lpt_set_probe_bit(int bit, int held)
{
    extern int g_probe_col;  /* set by display.c via [ and ] keys */
    int col = g_probe_col;
    if (col < 0) col = 0;
    if (col > 11) col = 11;
    if (bit < 0 || bit > 7) { s_probe_held = 0; return; }
    uint8_t prev_held = s_probe_held;
    uint8_t prev_bit  = s_probe_bit;
    static int s_prev_col = 0;
    int       prev_col   = s_prev_col;
    s_probe_bit  = (uint8_t)bit;
    s_probe_held = held ? 1 : 0;
    s_prev_col   = col;

    if (g_emu.uc) {
        uint32_t v;
        uint32_t mask  = 1u << bit;
        uint32_t phys  = 0x003450ccu + (uint32_t)col * 4u;
        uint32_t logc  = 0x003451bcu + (uint32_t)col * 4u;
        /* Clear previously held probe bit if changing col/bit/release */
        if (prev_held && (prev_bit != s_probe_bit || prev_col != col || !s_probe_held)) {
            uint32_t pmask  = 1u << prev_bit;
            uint32_t pphys  = 0x003450ccu + (uint32_t)prev_col * 4u;
            uint32_t plogc  = 0x003451bcu + (uint32_t)prev_col * 4u;
            uc_mem_read (g_emu.uc, pphys, &v, 4); v &= ~pmask; uc_mem_write(g_emu.uc, pphys, &v, 4);
            uc_mem_read (g_emu.uc, plogc, &v, 4); v &= ~pmask; uc_mem_write(g_emu.uc, plogc, &v, 4);
        }
        uc_mem_read (g_emu.uc, phys, &v, 4);
        if (s_probe_held) v |= mask; else v &= ~mask;
        uc_mem_write(g_emu.uc, phys, &v, 4);
        uc_mem_read (g_emu.uc, logc, &v, 4);
        if (s_probe_held) v |= mask; else v &= ~mask;
        uc_mem_write(g_emu.uc, logc, &v, 4);
    }

    if (prev_held != s_probe_held || prev_bit != s_probe_bit || prev_col != col) {
        int sw_num = col * 8 + bit;  /* approximation of canonical numbering */
        fprintf(stderr, "[lpt] PROBE c%d.b%d %s (sw≈%d)\n",
                col, bit, s_probe_held ? "SET" : "clr", sw_num);
    }
}

void lpt_toggle_coin_door(void)
{
    /* sw=81 → Physical[10] bit 1 → opcode 0x01 read.
     * Bit=1 = "Coin Door CLOSED" interlock asserted. Game enables play
     * when this is HIGH; when LOW the "OPEN COIN DOOR" overlay shows
     * and the diagnostics buttons are reachable. */
    s_coin_door_closed = !s_coin_door_closed;
    fprintf(stderr, "[lpt] coin door %s (interlock bit=%d)\n",
            s_coin_door_closed ? "CLOSED" : "OPEN", s_coin_door_closed);
    LOG("lpt", "coin door %s\n", s_coin_door_closed ? "CLOSED" : "OPEN");
}

void lpt_pulse_diag_escape(int frames)
{
    /* Kept for ABI compatibility — actually pulses the 'Enter' button
     * (sw=75, Physical[9] bit 3) which is the BEGIN-TEST switch in
     * attract / ENTER in service menus. The SWE1 static table has no
     * "diag_escape" callback at sw=71 (that index is "Not Used"); the
     * REAL escape function lives at sw=72 (the c9 b0 button). */
    if (frames < 25) frames = 25;
    s_enter_pulse = frames;
    fprintf(stderr, "[lpt] 'Enter' button pulse (%d reads)\n", frames);
    LOG("lpt", "enter-button pulse for %d reads\n", frames);
}

void lpt_toggle_slam_tilt(void)
{
    /* DEPRECATED: previously toggled what we wrongly thought was diag_escape
     * (Physical[8] bit 7). That bit is "Not Used" in SWE1. F6 now does
     * nothing — kept as a no-op so the SDL handler still links cleanly. */
    LOG("lpt", "F6 no-op (unused bit in SWE1 switch table)\n");
}

void lpt_inject_switch(int col, uint8_t data)
{
    if (col >= 0 && col < 8)
        s_rendering_status[col] = data;
    LOG("lpt", "inject col=%d data=0x%02x\n", col, data);
}

void lpt_activate(void)
{
    if (g_emu.lpt_active) return;
    g_emu.lpt_active = true;

    s_rendering_flags = 0;
    s_data_for_rendering = 0;
    s_access_mode4_prev = 0;
    s_access_mode1_prev = 0;
    s_data_flag1 = 0;
    s_data_flag2 = 0;
    s_data_bit4 = 0;
    s_data_bit6 = 0;

    /* Idle = 0x00 across the matrix scan store. Coin door starts CLOSED
     * so the "OPEN COIN DOOR" overlay is gone and play is enabled. F4
     * toggles to OPEN to allow service-button access. */
    memset(s_rendering_status, 0x00, sizeof(s_rendering_status));
    s_coin_door_closed = 1;
    s_enter_pulse      = 0;

    LOG("lpt", "activated — i386 POC opcode protocol (latch echo + active-low)\n");
}

static FILE *s_lpt_trace = NULL;
static int   s_lpt_trace_enabled = 0;  /* runtime toggle (F11) */

static void lpt_trace_open(void)
{
    if (s_lpt_trace) return;
    s_lpt_trace = fopen("encore_lpt.log", "w");
    if (s_lpt_trace) setvbuf(s_lpt_trace, NULL, _IOFBF, 65536);
}

void lpt_toggle_trace(void)
{
    s_lpt_trace_enabled = !s_lpt_trace_enabled;
    if (s_lpt_trace_enabled) lpt_trace_open();
    if (s_lpt_trace) {
        fprintf(s_lpt_trace, "\n=== TRACE %s ===\n",
                s_lpt_trace_enabled ? "ON" : "OFF");
        fflush(s_lpt_trace);
    }
    LOG("lpt", "trace %s\n", s_lpt_trace_enabled ? "ON" : "OFF");
}

/* F12: dump the guest's switch state arrays from RAM.
 * Addresses come from the SWE1 symbol table:
 *   Physical  @ 0x003450cc — what the guest currently believes is pressed
 *   OneHit    @ 0x003450fc — debounced "switch just closed" latch
 *   ValidHit  @ 0x0034512c — passed validation
 *   Return    @ 0x0034515c
 *   OnTime    @ 0x0034518c
 *   Logical   @ 0x003451bc
 * Each is 12 dwords (one per LPT scan column). Bits 0..7 = switch state.
 *
 * Diagnostic use: when the bug is happening (menu drifting, volume
 * spammed), press F12. The dump shows which COLUMN/BIT the guest
 * thinks is active. Then we know exactly which fake input we are
 * leaking into the matrix.
 */
void lpt_dump_guest_switch_state(void)
{
    static const struct { const char *name; uint32_t addr; } regions[] = {
        { "Physical ", 0x003450ccu },
        { "OneHit   ", 0x003450fcu },
        { "ValidHit ", 0x0034512cu },
        { "Return   ", 0x0034515cu },
        { "OnTime   ", 0x0034518cu },
        { "Logical  ", 0x003451bcu },
    };
    fprintf(stderr, "\n=== Guest switch-state dump (F12) ===\n");
    for (size_t r = 0; r < sizeof(regions)/sizeof(regions[0]); ++r) {
        uint32_t cols[12] = {0};
        if (uc_mem_read(g_emu.uc, regions[r].addr, cols, sizeof(cols)) != UC_ERR_OK) {
            fprintf(stderr, "  %s: uc_mem_read failed\n", regions[r].name);
            continue;
        }
        fprintf(stderr, "  %s @0x%08x:", regions[r].name, regions[r].addr);
        for (int c = 0; c < 12; ++c) fprintf(stderr, " c%d=%08x", c, cols[c]);
        fprintf(stderr, "\n");
    }
    /* Also surface what we are currently feeding the matrix */
    fprintf(stderr, "  encore-side: lpt_button=0x%02x lpt_switch=0x%02x door_closed=%d enter_pulse=%d\n",
            s_lpt_button_state, s_lpt_switch_state, s_coin_door_closed, s_enter_pulse);
    /* And the LPT presence flag the guest computed */
    uint32_t pinio_lpt = 0xffffffffu, pinio_state = 0;
    uc_mem_read(g_emu.uc, 0x002e992cu, &pinio_lpt, 4);
    uc_mem_read(g_emu.uc, 0x002e9930u, &pinio_state, 4);
    fprintf(stderr, "  pinio_lpt=0x%08x  state_flag=%u (1=present)\n",
            pinio_lpt, pinio_state);
    fflush(stderr);
}

/* Returns 1 if the value is interesting (not the idle/expected default). */
static int lpt_val_interesting(uint8_t op, uint8_t val)
{
    switch (op) {
    case 0x00: return val != 0x00;                    /* Physical[8] — coin slots, idle = 0 */
    case 0x01: return val != 0x00 && val != 0x02;     /* Physical[10] — ignore door-only */
    case 0x02: return val != 0xF0;
    case 0x03: return val != 0x00;                    /* Physical[9] — switch active */
    case 0x04: return val != 0x00;
    case 0x0F: return (val != 0x00 && val != 0x40 && val != 0x80 && val != 0xC0);
    case 0x10: case 0x11: return (val != 0x00 && val != 0xFF);
    case 0x12: case 0x13: return val != 0x00;
    default:   return 1;
    }
}

static uint32_t lpt_read(uint16_t port)
{
    static int s_lpt_rd_cnt = 0;
    uint32_t reg = port - PORT_LPT_DATA;

    if (!g_emu.lpt_active) return 0xFFu;

    switch (reg) {
    case 0: { /* Data port read */
        s_lpt_rd_cnt++;
        /* Periodic auto-dump of guest switch state for self-diagnosis.
         * Triggered every ~12k reads (~once per few seconds in attract). */
        if ((s_lpt_rd_cnt % 12000) == 0) lpt_dump_guest_switch_state();
        uint8_t val;
        int gated = (s_rendering_flags & 0x01) && (s_rendering_flags & 0x08);
        if (gated) {
            val = retrieve_rendering_status(s_data_for_rendering);
        } else {
            val = g_emu.lpt_data;
        }
        if (s_lpt_rd_cnt <= 30 || (s_lpt_rd_cnt % 5000) == 0)
            LOG("lpt", "rd col=0x%02x val=0x%02x ctrl=0x%02x cnt=%d\n",
                    s_data_for_rendering, val, s_rendering_flags, s_lpt_rd_cnt);
        /* Trace only interesting reads to keep file small and fast */
        if (s_lpt_trace_enabled && s_lpt_trace &&
            lpt_val_interesting(s_data_for_rendering, val)) {
            fprintf(s_lpt_trace,
                    "RD col=0x%02x val=0x%02x ctrl=0x%02x gated=%d btn=0x%02x sw=0x%02x cnt=%d\n",
                    s_data_for_rendering, val, s_rendering_flags, gated,
                    s_lpt_button_state, s_lpt_switch_state, s_lpt_rd_cnt);
        }
        return val;
    }
    case 1: return 0x87u;
    case 2: return (uint32_t)s_rendering_flags; /* echo control — matches i386 POC */
    default: return 0xFF;
    }
}

static void lpt_write(uint16_t port, uint8_t val)
{
    static int s_lpt_wr_cnt = 0;
    uint32_t reg = port - PORT_LPT_DATA;

    if (!g_emu.lpt_active) return;

    s_lpt_wr_cnt++;

    switch (reg) {
    case 0:
        g_emu.lpt_data = val;
        break;
    case 2: {
        uint8_t newctrl = val;

        /* Bit 2 rising edge: capture opcode */
        if (!s_access_mode4_prev && (newctrl & 0x04))
            s_data_for_rendering = g_emu.lpt_data;
        s_access_mode4_prev = newctrl & 0x04;

        /* Bit 0 falling edge: dispatch command */
        if (s_access_mode1_prev && !(newctrl & 0x01))
            process_data_command(s_data_for_rendering, g_emu.lpt_data);
        s_access_mode1_prev = newctrl & 0x01;

        s_rendering_flags = newctrl;
        g_emu.lpt_ctrl = newctrl;
        break;
    }
    }
}

/* UART THRE interrupt state.
 * Real 16550A behavior: THRE is a UART-side pending condition that survives
 * PIC delivery and is only cleared when the guest reads IIR reporting THRE.
 * If we key IIR directly off PIC IRR, the CPU's INTA cycle clears the cause
 * before the guest sees it and serial output stalls after the first byte. */
static bool s_uart_thre_pending;

static void uart_update_irq4(void)
{
    bool pending = false;

    if ((g_emu.uart_regs[1] & 0x01) &&
        ((g_emu.monitor_active && g_emu.monitor_inject_pos < 10) ||
         netcon_serial_rx_pending()))
        pending = true; /* RDA */

    if ((g_emu.uart_regs[1] & 0x02) && s_uart_thre_pending)
        pending = true; /* THRE */

    if (pending)
        g_emu.pic[0].irr |= 0x10;   /* IRQ4 on master PIC */
    else
        g_emu.pic[0].irr &= ~0x10;
}

/* Public wrapper — called from netcon_poll after bytes arrive on the
 * serial-tcp socket so the guest's IRQ4 handler is notified promptly
 * (without waiting for the next guest-side LSR poll). */
void uart_notify_rx(void)
{
    uart_update_irq4();
}

static void uart_write(uint16_t port, uint8_t val)
{
    uint16_t off = port - PORT_COM1_BASE;
    if (off == 0) {
        /* THR — transmit character */

        /* Mirror raw byte to TCP serial client (no filtering — preserve
         * binary fidelity for crash dumps, control codes, etc.). */
        netcon_serial_tx(val);

        if (val >= 0x20 || val == '\n' || val == '\r') {
            if (g_emu.uart_pos < (int)sizeof(g_emu.uart_buf) - 1)
                g_emu.uart_buf[g_emu.uart_pos++] = (char)val;
        }
        /* Detect milestones in UART output */
        if (val == '\n' && g_emu.uart_pos > 1) {
            g_emu.uart_buf[g_emu.uart_pos] = '\0';
            /* Print UART lines */
            LOG("uart", "%s", g_emu.uart_buf);

            /* Detect game code start — apply patches once */
            if (!g_emu.game_started &&
                (strstr(g_emu.uart_buf, "STARTING UPDATE GAME CODE") ||
                 strstr(g_emu.uart_buf, "STARTING GAME CODE"))) {
                g_emu.game_started = true;
                LOG("sgc", ">>> game_started set (exec=%lu)\n", (unsigned long)g_emu.exec_count);
                apply_sgc_patches();
            }
            if (strstr(g_emu.uart_buf, "XINU")) {
                if (!g_emu.xinu_booted) {
                    g_emu.xinu_booted = true;
                    /* BT-94: Activate emulated parallel port BEFORE PinIO probes.
                     * PinIO runs after XINU ctors — activating here ensures port
                     * is present when PinIO scans 0x378. Without this, PinIO
                     * fails probe and game shows "power board not connected." */
                    lpt_activate();
                    LOG("sgc", "XINU boot detected (exec=%lu) — LPT activated (BT-94)\n",
                        (unsigned long)g_emu.exec_count);
                }
            }

            /* BT-93: Ensure LPT emulated port is active (backup trigger).
             * Primary activation is on XINU detection above. */
            if (strstr(g_emu.uart_buf, "Allegro")) {
                lpt_activate(); /* no-op if already active */
                LOG("sgc", "Allegro detected — LPT ensure active (BT-93)\n");
            }

            /* Detect "monitor commands" and "%" monitor prompt patterns */
            if (strstr(g_emu.uart_buf, "monitor commands"))
                LOG("uart", ">>> monitor commands detected\n");

            g_emu.uart_pos = 0;
        }

        /* Check for monitor prompt (may NOT end with newline) */
        if (g_emu.uart_pos >= 8 && !g_emu.monitor_active) {
            g_emu.uart_buf[g_emu.uart_pos] = '\0';
            if (strstr(g_emu.uart_buf, "monitor>") ||
                strstr(g_emu.uart_buf, "-> ") ||
                strstr(g_emu.uart_buf, "% ")) {
                g_emu.monitor_active = true;
                g_emu.monitor_inject_pos = 0;
            }
        }
        /* THR becomes empty again immediately after we consume the byte. */
        s_uart_thre_pending = true;
        uart_update_irq4();
    } else if (off == 1) {
        /* IER write: if enabling ETBEI (bit 1), raise IRQ4 immediately (THR always empty) */
        g_emu.uart_regs[1] = val;
        if (val & 0x02)
            s_uart_thre_pending = true;
        uart_update_irq4();
    } else if (off < 8) {
        g_emu.uart_regs[off] = val;
    }
}

static uint32_t uart_read(uint16_t port)
{
    uint16_t off = port - PORT_COM1_BASE;
    switch (off) {
    case 0: { /* RBR */
        /* TCP serial client takes priority (real input from a human/script). */
        uint8_t b;
        if (netcon_serial_rx(&b)) {
            uart_update_irq4();
            return (uint32_t)b;
        }
        /* Fall back to the boot-time "continue\r" auto-injection. */
        if (g_emu.monitor_active && g_emu.monitor_inject_pos < 10) {
            const char *cont = "continue\r";
            char c = cont[g_emu.monitor_inject_pos++];
            if (c == '\0') {
                g_emu.monitor_active = false;
                g_emu.monitor_inject_pos = 0;
                uart_update_irq4();
                return 0;
            }
            uart_update_irq4();
            return (uint32_t)c;
        }
        return 0;
    }
    case 2: {
        /* IIR — report UART-side pending causes, not raw PIC state. */
        if ((g_emu.uart_regs[1] & 0x01) &&
            ((g_emu.monitor_active && g_emu.monitor_inject_pos < 10) ||
             netcon_serial_rx_pending())) {
            return 0x04;  /* RDA */
        }
        if ((g_emu.uart_regs[1] & 0x02) && s_uart_thre_pending) {
            s_uart_thre_pending = false; /* THRE clears when IIR is read */
            uart_update_irq4();
            return 0x02;
        }
        return 0x01;  /* no interrupt pending */
    }
    case 5: {
        /* LSR — THRE + TEMT, plus DR when monitor inject or netcon active */
        uint8_t lsr = 0x60;
        if ((g_emu.monitor_active && g_emu.monitor_inject_pos < 10) ||
            netcon_serial_rx_pending())
            lsr |= 0x01; /* DR (data ready) */

        /* UART poll drain: after many LSR reads with no action, inject NUL (BT-88) */
        g_emu.uart_lsr_count++;
        if (g_emu.uart_lsr_count > 200) {
            g_emu.uart_lsr_count = 0;
        }
        return lsr;
    }
    case 6: return 0x30;               /* MSR — CTS + DSR */
    default: return g_emu.uart_regs[off];
    }
}

/* ===== DCS2 UART + DCS dual-mode handler (ports 0x138-0x13F) =====
 * The Pinball 2000 DCS2 board uses a 16550 UART at base 0x138.
 * Access-size determines interpretation:
 *   Byte access (size=1): standard 16550 UART registers (MCR, LSR, MSR)
 *   Word access (size=2): DCS2 command/data (port 0x13C) and flags (port 0x13E)
 *
 * UART register values (from x64 POC BT-100, P2K-driver deviceContext):
 *   LSR = 0x60 (THRE + TEMT, TX ready, no RX data)
 *   IIR = 0x01 (no interrupt pending)
 *   MSR = 0xB0 (CTS + DSR + DCD active)
 *
 * DCS2 protocol (word access):
 *   Port 0x13C word write: command (e.g. 0x5800=reset, 0x3A=dong)
 *   Port 0x13C word read:  response data
 *   Port 0x13E word read:  flags (bit 6=ready, bit 7=data available)
 *   Port 0x13E byte read:  also returns DCS flags (game uses both sizes)
 */
#define DCS2_UART_BASE 0x138

/* UART register state */
static uint8_t dcs2_dll = 0x01;
static uint8_t dcs2_ier = 0x00;
static uint8_t dcs2_iir = 0x01;
static uint8_t dcs2_lcr = 0x00;
static uint8_t dcs2_mcr = 0x00;
static uint8_t dcs2_lsr = 0x60;  /* THRE + TEMT */
static uint8_t dcs2_msr = 0xB0;  /* CTS + DSR + DCD */
static uint8_t dcs2_scr = 0x00;

/* DCS2 I/O port command/response state */
static struct {
    uint16_t buf[32];
    int      wr, rd;
    uint16_t flags;
    uint8_t  echo;        /* byte echo for SRAM/ADSP handshake */
    int      pending;     /* ACE1 multi-word mode */
    int      active;      /* 0x0E active mode */
    int      cmd;
    int      mixer[8];
    int      layer;
    int      remaining;
    uint32_t cnt_wr, cnt_rd, cnt_flag;
} s_dcs_io;

static void dcs_io_push(uint16_t val) {
    s_dcs_io.buf[s_dcs_io.wr] = val;
    s_dcs_io.wr = (s_dcs_io.wr + 1) & 31;
    static int n = 0;
    if (++n <= 50)
        LOG("dcs-io", "push 0x%04x (wr=%d rd=%d)\n", val, s_dcs_io.wr, s_dcs_io.rd);
}

static uint16_t dcs_io_pop(void) {
    if (s_dcs_io.wr == s_dcs_io.rd) return 0;
    uint16_t val = s_dcs_io.buf[s_dcs_io.rd];
    s_dcs_io.rd = (s_dcs_io.rd + 1) & 31;
    static int n = 0;
    if (++n <= 50)
        LOG("dcs-io", "pop 0x%04x (wr=%d rd=%d)\n", val, s_dcs_io.wr, s_dcs_io.rd);
    return val;
}

static void dcs_io_execute(void) {
    int cmd = s_dcs_io.cmd;

    /* Active mode (0x0E): only 0x0E exits */
    if (s_dcs_io.active && cmd == 0x0E) {
        s_dcs_io.active = 0;
        s_dcs_io.pending = 0;
        dcs_io_push(10);
        return;
    }
    if (s_dcs_io.active) return;

    /* Multi-word mode (ACE1): accumulate mixer parameters */
    if (s_dcs_io.pending) {
        if (s_dcs_io.remaining == 0) {
            s_dcs_io.remaining = ((cmd >> 8) == 0x55) ? 1 : 2;
            s_dcs_io.mixer[0] = cmd;
            s_dcs_io.layer = 1;
            return;
        }
        s_dcs_io.mixer[s_dcs_io.layer++] = cmd;
        if (--s_dcs_io.remaining != 0) return;
        if (s_dcs_io.mixer[0] == 999 || s_dcs_io.mixer[0] == 1000) {
            dcs_io_push(0x100);
            dcs_io_push(0x10);
        } else {
            sound_execute_mixer(s_dcs_io.mixer[0],
                                s_dcs_io.layer > 1 ? s_dcs_io.mixer[1] : 0,
                                s_dcs_io.layer > 2 ? s_dcs_io.mixer[2] : 0);
        }
        s_dcs_io.layer = 0;
        s_dcs_io.remaining = 0;
        memset(s_dcs_io.mixer, 0, sizeof(s_dcs_io.mixer));
        return;
    }

    LOG("dcs-io", "cmd=0x%04x\n", cmd);

    switch (cmd) {
    case 0x5800:
        dcs_io_push(0x1000);
        LOG("dcs-io", "RESET 0x5800 → 0x1000\n");
        break;
    case 0x5A00:
        dcs_io_push(0x1000);
        LOG("dcs-io", "RESET 0x5A00 → 0x1000\n");
        break;
    case 0x3A: {
        dcs_io_push(0xCC01);
        dcs_io_push(10);
        static int s_dong = 0;
        if (!s_dong) { s_dong = 1; sound_play_boot_dong(); }
        break;
    }
    case 0x1B:
        dcs_io_push(0xCC09);
        dcs_io_push(10);
        break;
    case 0xAA:
        dcs_io_push(0xCC04);
        dcs_io_push(10);
        if (sound_is_ready()) sound_process_cmd(0x00AA);
        sound_start_audio_init_thread();
        break;
    case 0x0E:
        s_dcs_io.active = 1;
        s_dcs_io.pending = 0;
        break;
    case 0xACE1:
        s_dcs_io.pending = 1;
        dcs_io_push(0x0100);
        dcs_io_push(0x0C);
        break;
    default:
        sound_process_cmd(cmd);
        break;
    }
}

static uint32_t dcs2_port_read(uint16_t port, int size) {
    int off = port - DCS2_UART_BASE;

    /* Port 0x13C (off=4): word=DCS data, byte=MCR */
    if (off == 4 && size >= 2) {
        s_dcs_io.cnt_rd++;
        uint16_t val = dcs_io_pop();
        /* Log EIP + return address for first few data reads */
        if (s_dcs_io.cnt_rd <= 10) {
            uint32_t eip = 0, esp = 0;
            uc_reg_read(g_emu.uc, UC_X86_REG_EIP, &eip);
            uc_reg_read(g_emu.uc, UC_X86_REG_ESP, &esp);
            uint32_t retaddr = *(uint32_t *)(g_emu.ram + esp);
            uint32_t caller2 = *(uint32_t *)(g_emu.ram + esp + 8);
            LOG("dcs-io", "DATA READ #%u = 0x%04x (EIP=0x%08x ret=0x%08x caller2=0x%08x)\n",
                s_dcs_io.cnt_rd, val, eip, retaddr, caller2);
        }
        return val;
    }

    /* Port 0x13E (off=6): DCS flags (both byte and word) */
    if (off == 6) {
        s_dcs_io.cnt_flag++;
        uint16_t f = 0x40;  /* always ready to accept */
        if (s_dcs_io.wr != s_dcs_io.rd)
            f |= 0x80;
        /* Log first few flag reads after data reads to trace state machine */
        if (s_dcs_io.cnt_rd >= 3 && s_dcs_io.cnt_flag <= 5) {
            uint32_t eip = 0;
            uc_reg_read(g_emu.uc, UC_X86_REG_EIP, &eip);
            LOG("dcs-io", "FLAGS READ #%u = 0x%02x (EIP=0x%08x) after rd=%u\n",
                s_dcs_io.cnt_flag, f, eip, s_dcs_io.cnt_rd);
        }
        /* Byte and word reads both return DCS flags. MSR bits 4-5 (CTS+DSR)
         * are preserved for UART loopback test, but bit 7 (DCD) is replaced
         * with the actual DCS data-available bit. */
        if (size == 1 && !(dcs2_mcr & 0x10))
            return (dcs2_msr & 0x30) | (f & 0xC0);  /* MSR CTS+DSR, DCS bits 6-7 */
        return f;
    }

    /* Byte access: 16550 UART registers */
    switch (off) {
    case 0: return (dcs2_lcr & 0x80) ? dcs2_dll : s_dcs_io.echo;
    case 1: return (dcs2_lcr & 0x80) ? 0x00 : dcs2_ier;
    case 2: return dcs2_iir;
    case 3: return dcs2_lcr;
    case 4: return dcs2_mcr;
    case 5: return dcs2_lsr;  /* 0x60 = TX ready, no RX */
    case 6: /* MSR in loopback */
        return (uint32_t)(((dcs2_mcr & 0x0C) << 4) |
                          ((dcs2_mcr & 0x02) << 3) |
                          ((dcs2_mcr & 0x01) << 5));
    case 7: return dcs2_scr;
    default: return 0xFF;
    }
}

static void dcs2_port_write(uint16_t port, uint32_t val, int size) {
    int off = port - DCS2_UART_BASE;

    /* Port 0x13C (off=4): word=DCS command, byte=MCR */
    if (off == 4 && size >= 2) {
        s_dcs_io.cnt_wr++;
        s_dcs_io.cmd = val & 0xFFFF;
        dcs_io_execute();
        return;
    }

    /* Byte access: 16550 UART registers */
    switch (off) {
    case 0: /* THR / DLL */
        if (dcs2_lcr & 0x80) dcs2_dll = val & 0xFF;
        else s_dcs_io.echo = val & 0xFF;
        break;
    case 1: if (!(dcs2_lcr & 0x80)) dcs2_ier = val & 0xFF; break;
    case 2: break;
    case 3: dcs2_lcr = val & 0xFF; break;
    case 4: dcs2_mcr = val & 0xFF; break;
    case 7: dcs2_scr = val & 0xFF; break;
    default: break;
    }
}

void dcs_io_get_counters(uint32_t *ww, uint32_t *wr, uint32_t *bw, uint32_t *br, uint32_t *fr) {
    *ww = s_dcs_io.cnt_wr; *wr = s_dcs_io.cnt_rd;
    *bw = 0; *br = 0; *fr = s_dcs_io.cnt_flag;
}

/* ===== Main I/O dispatch ===== */

uint32_t io_port_read(uint16_t port, int size)
{
    switch (port) {
    /* PIC */
    case PORT_PIC1_CMD:
    case PORT_PIC1_DATA:
        return pic_read(0, port);
    case PORT_PIC2_CMD:
    case PORT_PIC2_DATA:
        return pic_read(1, port);

    /* PIT */
    case PORT_PIT_CH0:
    case PORT_PIT_CH1:
    case PORT_PIT_CH2:
        return pit_read(port);
    case PORT_PIT_CMD:
        return 0;

    /* KBC */
    case PORT_KBC_DATA: {
        uint8_t scan;
        if (netcon_keyboard_rx(&scan)) {
            g_emu.kbc_outbuf = scan;
            g_emu.kbc_status &= ~0x01;       /* OBF cleared on read */
            return scan;
        }
        return g_emu.kbc_outbuf;
    }
    case PORT_KBC_CMD:
        /* Reflect "output buffer full" while a netcon scancode is queued. */
        return g_emu.kbc_status | (netcon_keyboard_pending() ? 0x01 : 0x00);

    /* CMOS/RTC — return live system time on time-register reads */
    case PORT_CMOS_DATA: {
        uint8_t reg = g_emu.cmos_addr & 0x7F;
        /* Time registers 0x00-0x09: populate from host clock on each read */
        if (reg <= 0x09 || reg == 0x32) {
            time_t t = time(NULL);
            struct tm *tm = localtime(&t);
            #define BCD(v) (uint8_t)((((v)/10)<<4) | ((v)%10))
            g_emu.cmos_data[0x00] = BCD(tm->tm_sec);
            g_emu.cmos_data[0x02] = BCD(tm->tm_min);
            g_emu.cmos_data[0x04] = BCD(tm->tm_hour);
            g_emu.cmos_data[0x06] = BCD(tm->tm_wday ? tm->tm_wday : 7);
            g_emu.cmos_data[0x07] = BCD(tm->tm_mday);
            g_emu.cmos_data[0x08] = BCD(tm->tm_mon + 1);
            g_emu.cmos_data[0x09] = BCD(tm->tm_year % 100);
            g_emu.cmos_data[0x32] = BCD((tm->tm_year + 1900) / 100);
            #undef BCD
        }
        return g_emu.cmos_data[reg];
    }

    /* A20 gate */
    case PORT_A20:
        return g_emu.a20_enabled ? 0x02 : 0x00;

    /* PCI */
    case PORT_PCI_ADDR:
        return g_emu.pci_addr;
    case PORT_PCI_DATA:
    case PORT_PCI_DATA + 1:
    case PORT_PCI_DATA + 2:
    case PORT_PCI_DATA + 3: {
        if (!(g_emu.pci_addr & 0x80000000u)) return 0xFFFFFFFFu;
        uint8_t bus = (g_emu.pci_addr >> 16) & 0xFF;
        uint8_t dev = (g_emu.pci_addr >> 11) & 0x1F;
        uint8_t fn  = (g_emu.pci_addr >> 8) & 0x07;
        uint8_t reg = g_emu.pci_addr & 0xFC;
        uint32_t val = pci_read(bus, dev, fn, reg);
        int byte_off = (port - PORT_PCI_DATA) + (g_emu.pci_addr & 3);
        if (size == 1) return (val >> ((byte_off & 3) * 8)) & 0xFF;
        if (size == 2) return (val >> ((byte_off & 2) * 8)) & 0xFFFF;
        return val;
    }

    /* PRISM/MediaGX config */
    case PORT_PRISM_IDX:
        return g_emu.prism_idx;
    case PORT_PRISM_DATA:
        return s_prism_regs[g_emu.prism_idx];

    /* VGA */
    case PORT_VGA_STATUS:
        g_emu.vga_flipflop = !g_emu.vga_flipflop;
        /* Bit 0 = display active, Bit 3 = VBLANK */
        return g_emu.vga_flipflop ? 0x09 : 0x00;
    case PORT_VGA_SEQ_DATA:
        return g_emu.vga_seq[g_emu.vga_seq_idx & 7];
    case PORT_VGA_CRTC_DATA:
        return g_emu.vga_crtc[g_emu.vga_crtc_idx % 25];
    case 0x03CC: /* VGA misc read */
        return g_emu.vga_misc;

    /* UART */
    case PORT_COM1_BASE ... PORT_COM1_BASE + 7:
        return uart_read(port);

    /* LPT */
    case PORT_LPT_DATA:
    case PORT_LPT_STATUS:
    case PORT_LPT_CTRL:
        return lpt_read(port);

    /* System control port B (0x61): bit 4 toggles on read (BT-107) */
    case 0x0061:
        s_port61 ^= 0x10; /* toggle bit 4 */
        return s_port61 & 0x1F; /* only bits [4:0] valid */

    /* SuperIO W83977EF (0x2E/0x2F): reg 0x20 = chip ID 0x97 */
    case 0x002E:
        return g_emu.superio_idx;
    case 0x002F:
        if (g_emu.superio_idx == 0x20) return 0x97; /* W83977EF chip ID */
        return 0x00;

    /* CC5530 EEPROM (0xEA/0xEB): chip_id=0x02, rev=0x01 */
    case 0x00EA:
        return g_emu.cc5530_idx;
    case 0x00EB:
        switch (g_emu.cc5530_idx) {
        case 0x20: return 0x02;  /* chip ID */
        case 0x21: return 0x01;  /* revision */
        default:   return 0x00;
        }

    /* DCS2 status — must return 0x00 (ready) */
    case PORT_DCS2_STATUS:
        return 0x00;

    /* DCS2 UART ports (0x138-0x13F)
     * Return 0xFF (no UART present) — forces game to use BAR4 mode,
     * matching i386 POC behavior where UART detection fails. */
    case DCS2_UART_BASE ... (DCS2_UART_BASE + 7):
        return 0xFF;

    /* DCS2 control port 0x813C — disabled, game uses BAR4 */
    case 0x813C:
        return 0xFF;

    /* POST code */
    case PORT_POST:
        return g_emu.post_code;

    /* DMA page register */
    case PORT_DMA_PAGE:
    case 0x0089:
    case 0x008A:
    case 0x008B:
        return 0x00;

    /* SMC8216T NIC board registers (0x300-0x30F) */
    case 0x0300 ... 0x030F:
        return nic_board_rd(port);

    /* SMC8216T NIC DP8390 registers (0x310-0x31F) */
    case 0x0310 ... 0x031F:
        return nic_8390_rd(port);

    default:
        break;
    }

    return (size == 1) ? 0xFF : (size == 2) ? 0xFFFF : 0xFFFFFFFF;
}

void io_port_write(uint16_t port, uint32_t val, int size)
{
    switch (port) {
    /* PIC */
    case PORT_PIC1_CMD:
    case PORT_PIC1_DATA:
        pic_write(0, port, val & 0xFF);
        break;
    case PORT_PIC2_CMD:
    case PORT_PIC2_DATA:
        pic_write(1, port, val & 0xFF);
        break;

    /* PIT */
    case PORT_PIT_CH0:
    case PORT_PIT_CH1:
    case PORT_PIT_CH2:
    case PORT_PIT_CMD:
        pit_write(port, val & 0xFF);
        break;

    /* KBC */
    case PORT_KBC_DATA:
        /* Keyboard data write — mostly ignored */
        break;
    case PORT_KBC_CMD:
        if (val == 0xAA) g_emu.kbc_outbuf = 0x55; /* self-test OK */
        else if (val == 0xAB) g_emu.kbc_outbuf = 0x00; /* interface test OK */
        else if (val == 0xD1) {} /* write output port */
        else if (val == 0xFE) {} /* system reset — ignore */
        g_emu.kbc_status = 0x15; /* output buffer full */
        break;

    /* CMOS/RTC */
    case PORT_CMOS_ADDR:
        g_emu.cmos_addr = val & 0x7F;
        break;
    case PORT_CMOS_DATA:
        g_emu.cmos_data[g_emu.cmos_addr & 0x7F] = val;
        break;

    /* A20 gate */
    case PORT_A20:
        g_emu.a20_enabled = (val & 0x02) != 0;
        break;

    /* PCI */
    case PORT_PCI_ADDR:
        if (size == 4) g_emu.pci_addr = val;
        else if (size == 1) {
            int shift = (port & 3) * 8;
            g_emu.pci_addr = (g_emu.pci_addr & ~(0xFF << shift)) | ((val & 0xFF) << shift);
        }
        break;
    case PORT_PCI_DATA:
    case PORT_PCI_DATA + 1:
    case PORT_PCI_DATA + 2:
    case PORT_PCI_DATA + 3:
        if (g_emu.pci_addr & 0x80000000u) {
            uint8_t bus = (g_emu.pci_addr >> 16) & 0xFF;
            uint8_t dev = (g_emu.pci_addr >> 11) & 0x1F;
            uint8_t fn  = (g_emu.pci_addr >> 8) & 0x07;
            uint8_t reg = g_emu.pci_addr & 0xFC;
            pci_write(bus, dev, fn, reg, val);
        }
        break;

    /* PRISM/MediaGX config */
    case PORT_PRISM_IDX:
        g_emu.prism_idx = val & 0xFF;
        break;
    case PORT_PRISM_DATA:
        s_prism_regs[g_emu.prism_idx] = val & 0xFF;
        break;

    /* VGA */
    case PORT_VGA_MISC_W:
        g_emu.vga_misc = val;
        break;
    case PORT_VGA_SEQ_IDX:
        g_emu.vga_seq_idx = val & 7;
        break;
    case PORT_VGA_SEQ_DATA:
        g_emu.vga_seq[g_emu.vga_seq_idx & 7] = val;
        break;
    case PORT_VGA_CRTC_IDX:
        g_emu.vga_crtc_idx = val % 25;
        break;
    case PORT_VGA_CRTC_DATA:
        g_emu.vga_crtc[g_emu.vga_crtc_idx % 25] = val;
        break;
    case PORT_VGA_STATUS:
        g_emu.vga_flipflop = false; /* reset flipflop on write */
        break;

    /* UART */
    case PORT_COM1_BASE ... PORT_COM1_BASE + 7:
        uart_write(port, val & 0xFF);
        break;

    /* LPT */
    case PORT_LPT_DATA:
    case PORT_LPT_CTRL:
        lpt_write(port, val & 0xFF);
        break;

    /* System control port B (0x61) — latch gate bits [1:0] */
    case 0x0061:
        s_port61 = (s_port61 & 0xFC) | (val & 0x03);
        break;

    /* SuperIO W83977EF (0x2E/0x2F) */
    case 0x002E:
        g_emu.superio_idx = val & 0xFF;
        break;
    case 0x002F:
        break; /* writes to SuperIO data are ignored */

    /* CC5530 EEPROM (0xEA/0xEB) */
    case 0x00EA:
        g_emu.cc5530_idx = val & 0xFF;
        break;
    case 0x00EB:
        break; /* writes to CC5530 data are ignored */

    /* POST code */
    case PORT_POST:
        g_emu.post_code = val;
        break;

    /* DCS2 status — write ignored */
    case PORT_DCS2_STATUS:
        break;

    /* DCS2 UART ports (0x138-0x13F) — disabled, game uses BAR4 */
    case DCS2_UART_BASE ... (DCS2_UART_BASE + 7):
        break;

    /* DCS2 control port 0x813C — disabled, game uses BAR4 */
    case 0x813C:
        break;

    /* SMC8216T NIC board registers (0x300-0x30F) */
    case 0x0300 ... 0x030F:
        nic_board_wr(port, val & 0xFF);
        break;

    /* SMC8216T NIC DP8390 registers (0x310-0x31F) */
    case 0x0310 ... 0x031F:
        nic_8390_wr(port, val & 0xFF);
        break;

    default:
        break;
    }
}
