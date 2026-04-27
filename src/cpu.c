/*
 * cpu.c — Unicorn Engine CPU setup, interrupt injection, execution loop.
 *
 * Key design: Unicorn UC_MODE_32 for i386 guest on x64 host.
 * Timer interrupt injection between emulation slices (no hardware PIC in Unicorn).
 * SIGALRM at 100Hz triggers uc_emu_stop() → check IRQs → inject → resume.
 *
 * ==========================================================================
 *  REMAINING HARDCODED PATCHES — ground-truth minimization audit
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
 *  B) Dropped 2026-04-21 — previously gated behind ENCORE_KEEP_V19_PATCHES
 *     (OFF by default).  Regression-tested all 7 update bundles (SWE1 base,
 *     SWE1 v1.5/v2.1, RFM v1.2/1.6/1.8/2.5/2.6) with killswitch OFF and
 *     everything that booted with them enabled still booted without.
 *     Deleted to honor minimization doctrine (patches are symptoms, not cures).
 *     Removed: Fatal/NonFatal/CMOS/LocMgr/watchdog returns; PIC base_mask
 *     @0x2F7CA8; DC register pre-init; DCS2 fake channel @0x400000;
 *     pid2 crash guard [0x2FCAAC]=0; BT-118 IStack magic-word repair.
 *
 *  C) RFM/SWE1 both, read-only maintenance @ 64-cycle tick:
 *      RAM_WR32(0, 0)           — NULL page zeroing, harmless
 *      watchdog_flag_addr=FFFF  — uses scanned address (safe)
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

/* doc 50 — guest-CPU pace measurement & throttle.
 * Updated by UC_HOOK_BLOCK whenever --cpu-stats or --cpu-target-mhz are
 * enabled. Block byte size is a cheap proxy for instruction count
 * (i386 average ~3.5 bytes/insn). Reset every reporting period.
 *
 * NOTE: bytes/3.5 stays only as a SECONDARY diagnostic now. The
 * authoritative virtual-time clock is `s_sched.vticks_total` below,
 * fed by Unicorn's `count` parameter. bytes/3.5 vs vticks divergence
 * is reported as a sanity check (HLT-heavy periods diverge legitimately
 * because vticks credits idle time but bytes do not). */
static uint64_t s_cpu_blocks_total   = 0;
static uint64_t s_cpu_bytes_total    = 0;
static uint64_t s_cpu_blocks_window  = 0;
static uint64_t s_cpu_bytes_window   = 0;
static uint64_t s_cpu_window_start_ns = 0;
static uint64_t s_cpu_run_start_ns    = 0;

/* Virtual-time scheduler state (file-scope so cpu_dump_irq_snapshot
 * can read it on guest Fatal). vticks_total is the AUTHORITATIVE
 * virtual-time clock — incremented by the `count` we asked Unicorn
 * to run after each batch. It is not "retired instructions" exactly:
 *   - on UC_ERR_OK: full batch credited (Unicorn hit count or HLT idle —
 *     either way that virtual time slot has elapsed)
 *   - on UC_ERR_INSN_INVALID (HLT / 0F3C / similar emulated insn):
 *     credit the full batch for HLT (idle equivalence), or a small
 *     bounded value for emulated insns
 *   - on hard error (mem unmap etc.): credit a small bounded value */
#define IRQ_RING 5
static struct sched_state {
    uint64_t vticks_total;
    uint64_t next_irq0_at_vticks;

    /* cumulative counters since cpu_run started */
    uint64_t irq0_due;
    uint64_t irq0_fired;
    uint64_t irq0_collapsed;       /* coll_irr + coll_isr */
    uint64_t irq0_coll_irr;        /* IRR bit0 already pending at fire time */
    uint64_t irq0_coll_isr;        /* ISR bit0 still in-service (no EOI) */
    uint64_t irq0_inject;
    /* Inject block reasons specific to IRQ0 (sampled in check_and_inject_irq) */
    uint64_t irq0_blk_if;          /* IRR pending, not masked, not in service, but IF=0 */
    uint64_t irq0_blk_imr;         /* IRR pending but masked in IMR */
    uint64_t irq0_blk_stub;        /* injected but IDT entry is stub (handler==0/0x20000) */
    uint64_t emu_calls;

    /* ISR-busy tracking (in vticks within the current 5 s window) */
    uint64_t isr_set_at_vticks;
    uint64_t isr_max_busy_vticks_w;
    uint8_t  isr_prev;
    bool     isr_warned_this_span;

    /* Window baselines for computing 5 s deltas */
    uint64_t w_start_ns;
    uint64_t w_vticks_base;
    uint64_t w_due_base, w_fired_base, w_collapsed_base;
    uint64_t w_coll_irr_base, w_coll_isr_base;
    uint64_t w_blk_if_base, w_blk_imr_base, w_blk_stub_base;
    uint64_t w_inject_base, w_eoi_base, w_resched_base;

    /* Ring of last IRQ_RING completed windows (newest at head). */
    struct sched_window {
        uint64_t end_ns;
        uint64_t d_vticks;
        uint64_t d_due, d_fired, d_collapsed, d_inject, d_eoi, d_resched;
        uint64_t max_isr_busy_vticks;
        double   wall_hz, target_hz, vt_scale, host_mips;
        uint8_t  pic_irr, pic_isr, pic_imr;
        char     health[16];
    } ring[IRQ_RING];
    int ring_head;     /* index of most-recent completed window */
    int ring_count;    /* number of valid windows in ring (0..IRQ_RING) */

    /* Cached pit_period_insns / target_ips so the snapshot has them. */
    uint64_t cached_pit_period_insns;
    uint64_t cached_target_ips;

    /* Wall-time floor on IRQ0 cadence.
     * IRQ0 must fire when BOTH (a) the guest has consumed one PIT
     * period of virtual time, AND (b) at least one PIT period of WALL
     * time has elapsed since the previous IRQ0. The wall-time floor
     * prevents over-firing on a host that runs faster than the guest
     * CPU model rate (vt-scale > 1) — without it, XINU's tick-based
     * scheduler watchdogs see compressed wall timeouts and Fatal. */
    uint64_t next_irq0_at_ns;
    uint64_t pit_period_ns;

    /* QEMU-style PIT channel 0 OUT-line model (mode 2 / rate generator).
     * In mode 2 OUT is normally HIGH; at terminal count it pulses LOW
     * for one PIT clock then returns HIGH. The LOW->HIGH rising edge
     * is what the PIC latches as IRQ0. We approximate this as an
     * instantaneous pulse per period: pic_set_irq(0,0) then
     * pic_set_irq(0,1) on each period boundary, which matches the
     * IRR-latching effect without modelling the sub-clock LOW window. */
    int      pit0_out_level;          /* current OUT line level (0/1) */
    uint64_t pit_edges_due;           /* expected PIT rising edges (cumulative) */
    uint64_t pit_edges_emitted;       /* edges actually fed to PIC */
    uint64_t w_pit_edges_due_base;
    uint64_t w_pic_edges_latched_base;
    uint64_t w_pic_edges_collapsed_base;
} s_sched;

/* Injection ring buffer — captures the last N interrupt injections so
 * that when the CPU later wedges (e.g. EIP lands in the GDT at 0x1016
 * after a corrupt IRET), we can dump exactly which injection set up
 * the bad return frame. */
#define IRQ_INJ_RING 16
struct inj_record {
    uint64_t seq;          /* monotonic injection counter (0 = empty slot) */
    uint8_t  vector;
    uint8_t  idt_bytes[8];
    uint16_t selector;
    uint32_t handler;
    uint32_t pre_eip;
    uint32_t pre_cs;
    uint32_t pre_esp;
    uint32_t pre_eflags;
    uint32_t post_esp;
    uint32_t frame_eip;
    uint32_t frame_cs;
    uint32_t frame_eflags;
};
static struct inj_record s_inj_ring[IRQ_INJ_RING];
static uint64_t          s_inj_seq = 0;          /* next seq to assign */
static int               s_inj_head = 0;         /* next ring slot */
static int               s_gdt_eip_trapped = 0;  /* once-shot trap flag */

static void cpu_dump_inj_ring(const char *why)
{
    LOG("irq", "=== INJ RING DUMP (%s) — last %d injections ===\n", why, IRQ_INJ_RING);
    /* Walk oldest → newest. */
    for (int i = 0; i < IRQ_INJ_RING; i++) {
        int idx = (s_inj_head + i) % IRQ_INJ_RING;
        struct inj_record *r = &s_inj_ring[idx];
        if (r->seq == 0) continue;
        LOG("irq",
            "  #%llu vec=0x%02x sel=0x%04x handler=0x%08x  "
            "pre EIP=0x%08x CS=0x%04x ESP=0x%08x EFL=0x%08x  "
            "postESP=0x%08x  frame[EIP=0x%08x CS=0x%08x EFL=0x%08x]  "
            "idt=%02x %02x %02x %02x %02x %02x %02x %02x\n",
            (unsigned long long)r->seq, r->vector, r->selector, r->handler,
            r->pre_eip, r->pre_cs, r->pre_esp, r->pre_eflags,
            r->post_esp, r->frame_eip, r->frame_cs, r->frame_eflags,
            r->idt_bytes[0], r->idt_bytes[1], r->idt_bytes[2], r->idt_bytes[3],
            r->idt_bytes[4], r->idt_bytes[5], r->idt_bytes[6], r->idt_bytes[7]);
    }
    LOG("irq", "=== end INJ RING ===\n");
}

/* One-shot dump of N bytes at addr — for offline objdump. */
static void cpu_dump_handler_bytes(uint32_t addr, size_t n, const char *tag) __attribute__((unused));
static void cpu_dump_handler_bytes(uint32_t addr, size_t n, const char *tag)
{
    if (addr + n > RAM_SIZE) return;
    LOG("irq", "=== HANDLER BYTES @0x%08x (%s, %zu bytes) ===\n", addr, tag, n);
    char line[256];
    for (size_t off = 0; off < n; off += 16) {
        int p = snprintf(line, sizeof(line), "  %08x:", (unsigned)(addr + off));
        for (size_t j = 0; j < 16 && off + j < n; j++) {
            p += snprintf(line + p, sizeof(line) - p, " %02x",
                          g_emu.ram[addr + off + j]);
        }
        LOG("irq", "%s\n", line);
    }
    LOG("irq", "=== end HANDLER BYTES ===\n");
}

/* In-handler tracker for IRQ0. Real x86 keeps IF=0 throughout an
 * interrupt handler until IRET pops the saved EFLAGS. clkint here
 * however issues `out 0x20,0x20` (EOI) early, then executes
 * `popa; sti; iret`. The EOI clears PIC ISR but the CPU is still in
 * the handler. Without modeling the IF=0 / STI-shadow window, a new
 * IRQ0 tick can arrive and we re-inject mid-handler, which makes
 * XINU panic with "resched: called from interrupt handler".
 *
 * We track the IRQ0 ESP baseline at injection and refuse to inject
 * IRQ0 again until ESP returns to >= that baseline (i.e. clkint has
 * IRET'd back to the interrupted task's stack frame). Other IRQs are
 * untouched. This guard maps directly to the hardware semantic of
 * "no nested IRQ0 until the prior frame is popped". */
static uint32_t s_irq0_pre_esp     = 0;     /* ESP captured BEFORE we pushed frame */
static uint32_t s_irq0_pre_eip     = 0;     /* return EIP */
static uint32_t s_irq0_handler_eip = 0;     /* clkint base */
static uint32_t s_irq0_pre_currpid = 0xffffffffu; /* XINU currpid at inject (proves task switch) */
static uint32_t s_currpid_addr = 0;  /* cached sym_lookup("currpid") — 0 until resolved */
static uint64_t s_irq0_in_flight   = 0;     /* 0 = idle, else seq number */
static uint64_t s_irq0_in_flight_arms   = 0;     /* total times in_flight was armed (== inject count) */
static uint64_t s_irq0_in_flight_clears = 0;     /* total clears (sum of all reasons) */
static uint64_t s_irq0_in_flight_clear_iret = 0; /* clear by EIP-return-to-pre_eip with ESP recovered */
static uint64_t s_irq0_in_flight_clear_tsw  = 0; /* clear by XINU currpid change AND ISR=0 */
static uint64_t s_irq0_in_flight_max_vt = 0;     /* longest in_flight duration in vticks */

/* Resolve currpid's actual RAM address from the symbol table (cached).
 * Returns 0 if symbols aren't loaded yet — callers must treat 0 as
 * "unknown" and skip the currpid check rather than reading RAM[0]. */
static uint32_t cpu_currpid_addr(void)
{
    if (s_currpid_addr) return s_currpid_addr;
    if (sym_loaded()) {
        uint32_t a = sym_lookup("currpid");
        if (a >= 0x100000u && a < RAM_SIZE) {
            s_currpid_addr = a;
        }
    }
    return s_currpid_addr;
}
static inline uint32_t cpu_read_currpid(void)
{
    uint32_t a = cpu_currpid_addr();
    return a ? RAM_RD32(a) : 0xffffffffu;
}
static uint64_t s_irq0_in_flight_arm_vt = 0;     /* vticks at last arm */
static uint64_t s_irq0_tsw_logged = 0;           /* sample-log first N task-switch clears */
static uint64_t s_irq0_guard_defer = 0;     /* count of injections deferred by this guard */

/* Ring of recent in_flight CLEAR events — for forensic dump on the
 * first resched-in-ISR storm so we can prove which clear event let
 * a nested inject through. */
#define IRQ_CLR_RING 16
struct clr_record {
    uint64_t seq;            /* matches inject seq this clear was for */
    uint64_t arm_vt;         /* vticks at arm */
    uint64_t clear_vt;       /* vticks at clear */
    uint64_t dur_vt;         /* clear_vt - arm_vt */
    uint8_t  reason;         /* 1=iret, 2=task-switch */
    uint32_t pre_eip, pre_esp, pre_currpid;
    uint32_t cur_eip, cur_esp, cur_currpid;
    uint8_t  pic_irr, pic_isr, pic_imr;
    uint8_t  guest_if;
};
static struct clr_record s_clr_ring[IRQ_CLR_RING];
static int               s_clr_head = 0;
static int               s_resched_dump_done = 0;
static uint64_t s_inj_defer_iret = 0;
static uint64_t s_inj_defer_progress = 0;
static uint64_t s_inj_defer_burst = 0;
static uint64_t s_inj_defer_esp = 0;
static uint64_t s_isr_force_eoi_total = 0;

static void hook_block_count(uc_engine *uc, uint64_t addr,
                             uint32_t size, void *user_data)
{
    (void)user_data;
    s_cpu_blocks_total++;
    s_cpu_bytes_total += size;
    s_cpu_blocks_window++;
    s_cpu_bytes_window += size;
    /* IRQ0 nested-injection guard: clear in_flight when we observe the
     * guest is back at the pre-injection EIP AND ESP has returned to
     * >= the pre-injection baseline (clkint's IRET completed). */
    if (s_irq0_in_flight && (uint32_t)addr == s_irq0_pre_eip) {
        uint32_t cur_esp = 0;
        uc_reg_read(uc, UC_X86_REG_ESP, &cur_esp);
        if (cur_esp >= s_irq0_pre_esp) {
            uint64_t dur = s_sched.vticks_total - s_irq0_in_flight_arm_vt;
            if (dur > s_irq0_in_flight_max_vt) s_irq0_in_flight_max_vt = dur;
            /* Record into clears ring for forensic dump. */
            uint32_t cur_eflags = 0;
            uc_reg_read(uc, UC_X86_REG_EFLAGS, &cur_eflags);
            struct clr_record *cr = &s_clr_ring[s_clr_head];
            cr->seq = s_irq0_in_flight;
            cr->arm_vt = s_irq0_in_flight_arm_vt;
            cr->clear_vt = s_sched.vticks_total;
            cr->dur_vt = dur;
            cr->reason = 1; /* iret */
            cr->pre_eip = s_irq0_pre_eip; cr->pre_esp = s_irq0_pre_esp;
            cr->pre_currpid = s_irq0_pre_currpid;
            cr->cur_eip = (uint32_t)addr; cr->cur_esp = cur_esp;
            cr->cur_currpid = cpu_read_currpid();
            cr->pic_irr = g_emu.pic[0].irr; cr->pic_isr = g_emu.pic[0].isr;
            cr->pic_imr = g_emu.pic[0].imr;
            cr->guest_if = (cur_eflags >> 9) & 1;
            s_clr_head = (s_clr_head + 1) % IRQ_CLR_RING;
            s_irq0_in_flight_clears++;
            s_irq0_in_flight_clear_iret++;
            s_irq0_in_flight = 0;
        }
    }
    /* Task-switch fallback: when XINU's clkint calls resched, the new
     * task's stack is loaded and the original pre_eip is never executed
     * again from this context. Previous heuristic used ESP+duration —
     * that fired during clkint's own post-EOI body (printf etc.) and
     * caused resched-in-ISR storms. The PROOF of an actual XINU task
     * switch is a change in the kernel's own currpid variable
     * (XINU stores it at 0x2FC8BC). Require ALL of:
     *   (a) currpid != currpid at inject time (real XINU resched),
     *   (b) PIC ISR for IRQ0 is already 0 (clkint EOI sent), AND
     *   (c) at least one full PIT period of guest time has elapsed
     *       (gives the iret-clear path first chance for the simple
     *       case where clkint returned without rescheduling).
     * NOTE: still a heuristic on top of in_flight, but currpid is
     * scheduler ground truth, not derived. */
    if (s_irq0_in_flight) {
        uint64_t dur = s_sched.vticks_total - s_irq0_in_flight_arm_vt;
        /* 2 PIT periods: gives the iret-clear path two full ticks to
         * fire the common "clkint IRETs straight back" case before we
         * fall back to the currpid-change heuristic. */
        uint64_t min_dur = s_sched.cached_pit_period_insns * 2;
        if (min_dur < 2000) min_dur = 2000;
        if (dur < min_dur) {
            return;
        }
        uint32_t cur_currpid = cpu_read_currpid();
        if (s_currpid_addr &&
            s_irq0_pre_currpid != 0xffffffffu &&
            cur_currpid != s_irq0_pre_currpid &&
            cur_currpid != 0 &&                  /* not transient/kernel-idle */
            cur_currpid < 0x100 &&               /* sane PID range (XINU PIDs are small) */
            !(g_emu.pic[0].isr & 0x01)) {
            uint32_t cur_esp = 0, cur_eflags = 0;
            uc_reg_read(uc, UC_X86_REG_ESP, &cur_esp);
            uc_reg_read(uc, UC_X86_REG_EFLAGS, &cur_eflags);
            if (!((cur_eflags >> 9) & 1)) {
                /* IF=0: guest in critical section — wait for sti before clearing. */
                return;
            }
            if (dur > s_irq0_in_flight_max_vt) s_irq0_in_flight_max_vt = dur;
            /* Record into clears ring. */
            struct clr_record *cr = &s_clr_ring[s_clr_head];
            cr->seq = s_irq0_in_flight;
            cr->arm_vt = s_irq0_in_flight_arm_vt;
            cr->clear_vt = s_sched.vticks_total;
            cr->dur_vt = dur;
            cr->reason = 2; /* task-switch */
            cr->pre_eip = s_irq0_pre_eip; cr->pre_esp = s_irq0_pre_esp;
            cr->pre_currpid = s_irq0_pre_currpid;
            cr->cur_eip = (uint32_t)addr; cr->cur_esp = cur_esp;
            cr->cur_currpid = cur_currpid;
            cr->pic_irr = g_emu.pic[0].irr; cr->pic_isr = g_emu.pic[0].isr;
            cr->pic_imr = g_emu.pic[0].imr;
            cr->guest_if = (cur_eflags >> 9) & 1;
            s_clr_head = (s_clr_head + 1) % IRQ_CLR_RING;
            s_irq0_in_flight_clears++;
            s_irq0_in_flight_clear_tsw++;
            if (s_irq0_tsw_logged < 5) {
                LOG("irq",
                    "in_flight cleared by TASK-SWITCH #%llu: pre_eip=0x%08x pre_esp=0x%08x pre_pid=%u cur_eip=0x%08x cur_esp=0x%08x cur_pid=%u IRR=%02x ISR=%02x IMR=%02x IF=%d dur=%llu vt\n",
                    (unsigned long long)(s_irq0_tsw_logged + 1),
                    s_irq0_pre_eip, s_irq0_pre_esp, s_irq0_pre_currpid,
                    (uint32_t)addr, cur_esp, cur_currpid,
                    g_emu.pic[0].irr, g_emu.pic[0].isr, g_emu.pic[0].imr,
                    (cur_eflags >> 9) & 1,
                    (unsigned long long)dur);
                s_irq0_tsw_logged++;
            }
            s_irq0_in_flight = 0;
        }
    }
}

static void cpu_dump_clr_ring(const char *why)
{
    LOG("irq", "=== CLR RING DUMP (%s) — last %d in_flight clears ===\n",
        why, IRQ_CLR_RING);
    for (int i = 0; i < IRQ_CLR_RING; i++) {
        int idx = (s_clr_head + i) % IRQ_CLR_RING;
        struct clr_record *r = &s_clr_ring[idx];
        if (r->seq == 0) continue;
        const char *rn = r->reason == 1 ? "IRET" :
                         r->reason == 2 ? "TSW " : "????";
        LOG("irq",
            "  seq#%llu %s arm_vt=%llu clear_vt=%llu dur=%llu  "
            "pre[EIP=0x%08x ESP=0x%08x pid=%u] cur[EIP=0x%08x ESP=0x%08x pid=%u]  "
            "IRR=%02x ISR=%02x IMR=%02x IF=%d\n",
            (unsigned long long)r->seq, rn,
            (unsigned long long)r->arm_vt,
            (unsigned long long)r->clear_vt,
            (unsigned long long)r->dur_vt,
            r->pre_eip, r->pre_esp, r->pre_currpid,
            r->cur_eip, r->cur_esp, r->cur_currpid,
            r->pic_irr, r->pic_isr, r->pic_imr, r->guest_if);
    }
    LOG("irq", "=== end CLR RING ===\n");
}

/* Public: forensic dump on first resched-in-ISR storm. Idempotent
 * after first call. Pairs the inject ring and the clears ring so we
 * can see exactly which clear let a nested IRQ0 through. */
void cpu_dump_resched_forensic(const char *why)
{
    if (s_resched_dump_done) return;
    s_resched_dump_done = 1;
    LOG("irq", "############ FIRST RESCHED-IN-ISR FORENSIC (%s) ############\n", why);
    LOG("irq", "  arms=%llu clears=%llu (iret=%llu tsw=%llu)  cur_in_flight=%llu cur_dur=%llu vt\n",
        (unsigned long long)s_irq0_in_flight_arms,
        (unsigned long long)s_irq0_in_flight_clears,
        (unsigned long long)s_irq0_in_flight_clear_iret,
        (unsigned long long)s_irq0_in_flight_clear_tsw,
        (unsigned long long)s_irq0_in_flight,
        (unsigned long long)(s_irq0_in_flight ? (s_sched.vticks_total - s_irq0_in_flight_arm_vt) : 0));
    cpu_dump_inj_ring(why);
    cpu_dump_clr_ring(why);
    LOG("irq", "############ END RESCHED FORENSIC ############\n");
}

/* QEMU-style PIC line-level setter.
 * Updates an electrical input line and latches IRR on the rising
 * (low->high) edge only. Does NOT consult ISR/IMR/IF — those gates
 * live in the CPU acknowledge stage (check_and_inject_irq).
 *
 * On a rising edge with IRR bit already set, the edge is collapsed
 * (real PIC behavior: edge-triggered mode requires the line to fall
 * before re-arming, but we count it for diagnostics).
 *
 * level = 0 or 1; pic_idx = 0 (master) or 1 (slave); irq = 0..7. */
static void pic_set_irq(int pic_idx, int irq, int level)
{
    if (pic_idx < 0 || pic_idx > 1 || irq < 0 || irq > 7) return;
    PICState *pic = &g_emu.pic[pic_idx];
    uint8_t mask  = (uint8_t)(1u << irq);
    uint8_t old   = (pic->irq_level & mask) ? 1 : 0;
    if (level) pic->irq_level |=  mask;
    else       pic->irq_level &= ~mask;
    if (!old && level) {
        if (pic->irr & mask) {
            pic->edges_collapsed_irr++;
        } else {
            pic->irr |= mask;
            pic->edges_latched++;
        }
    }
}

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

    /* doc 50 — guest-CPU pace measurement / throttle.
     * Cheap UC_HOOK_BLOCK accumulator; only attached when at least one
     * of --cpu-stats / --cpu-target-mhz is on, so default-path
     * performance is unchanged. */
    /* Always attach: virtual-time IRQ0 scheduler needs a guest-insn proxy. */
    {
        uc_hook h_block;
        uc_err be = uc_hook_add(uc, &h_block, UC_HOOK_BLOCK,
                                (void*)hook_block_count, NULL, 1, 0);
        if (be) {
            LOG("cpu", "cpu-pace BLOCK hook FAILED: %s\n", uc_strerror(be));
        } else if (g_emu.cpu_stats_enabled || g_emu.cpu_target_mhz > 0) {
            LOG("cpu", "cpu-pace: stats=%s target=%d MHz (period=%ds)\n",
                g_emu.cpu_stats_enabled ? "on" : "off",
                g_emu.cpu_target_mhz,
                g_emu.cpu_stats_period_s);
        }
    }

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
/* Returns 1 if interrupt frame was actually pushed and EIP/ESP updated,
 * 0 if skipped (IF=0 or stub handler — caller may need to roll back ISR). */
int cpu_inject_interrupt(uint8_t vector)
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

    /* IRQ0 nested-injection guard. Real x86 clears IF on int entry and
     * sets it again on IRET, so a new IRQ0 cannot fire until the old
     * handler IRETs. clkint, however, sends EOI early and clears its
     * own "in_handler" flag in its epilogue, BEFORE the actual IRET.
     * If we inject between EOI (ISR=0) and in_handler=0 we get
     * "resched: called from interrupt handler" panics. The PIC ISR
     * bit alone is therefore insufficient — wait for the real IRET to
     * complete (EIP back at pre-injection point with ESP popped). */
    if (vector == 0x20 && s_irq0_in_flight) {
        s_irq0_guard_defer++;
        if (s_irq0_guard_defer <= 5 || (s_irq0_guard_defer % 10000) == 0) {
            LOG("irq",
                "  irq0 nested-defer (n=%llu, in_flight seq=%llu, pre_eip=0x%08x pre_esp=0x%08x cur_eip=0x%08x cur_esp=0x%08x)\n",
                (unsigned long long)s_irq0_guard_defer,
                (unsigned long long)s_irq0_in_flight,
                s_irq0_pre_eip, s_irq0_pre_esp,
                regs_val[0], regs_val[1]);
        }
        return 0;
    }

    if (!(eflags & 0x200)) {
        inject_blocked++;
        return 0;
    }

    /* STI-shadow / no-progress deferral.
     *
     * Real x86 hardware blocks interrupts for ONE instruction after
     * STI / after IRET — so the canonical `sti; iret` exit lets the
     * interrupted task execute at least one instruction before the
     * next IRQ can fire.
     *
     * Unicorn 2.x does NOT implement the STI-shadow. Without this
     * guard we observe IRQ0 firing back-to-back at the SAME guest EIP
     * (the task we just returned to never gets to execute even one
     * instruction): each injection pushes a new frame on top of the
     * previous handler's saved state, ESP shrinks by ~352 bytes per
     * cycle, the task stack overflows, neighbouring memory (incl.
     * IDT[0x20]) is trashed, and the CPU eventually jumps to a
     * corrupt vector and wedges in the GDT or at NULL.
     *
     * Two complementary checks:
     *   1. opcode-based: defer if current insn is IRET (0xCF) or
     *      STI (0xFB) or previous insn was STI (0xFB).
     *   2. progress-based: defer if EIP is identical to the EIP we
     *      interrupted on the previous IRQ0 injection AND that
     *      previous injection's handler has already returned (we
     *      saw an EOI). The interrupted task hasn't advanced.
     *
     * Both kick in for IRQ0 only — other vectors (#GP, #UD, etc.)
     * are exception-driven and don't have this race.
     */
    if (vector == 0x20) {
        /* STI-shadow guard (REAL x86 behavior).
         * Real hardware blocks interrupts for one instruction after STI
         * and after IRET, so `sti; iret` is atomic w.r.t. interrupt
         * delivery. Unicorn 2.x does not implement that 1-insn shadow,
         * so we approximate it via opcode peek: if EIP currently points
         * at IRET (0xCF) or STI (0xFB), or the byte just before is STI,
         * defer one iteration. This is the only inject-time guard we
         * keep beyond IF and in_flight — burst gate and no-progress
         * were removed because PIC ISR + in_flight (cleared on real
         * IRET completion or task-switch) cover those cases. */
        static uint32_t s_irq0_last_defer_eip = 0;
        if (eip < RAM_SIZE - 1 && eip != s_irq0_last_defer_eip) {
            uint8_t cur  = g_emu.ram[eip];
            uint8_t prev = (eip > 0) ? g_emu.ram[eip - 1] : 0;
            if (cur == 0xCF || cur == 0xFB || prev == 0xFB) {
                s_inj_defer_iret++;
                s_irq0_last_defer_eip = eip;
                if (s_inj_defer_iret <= 5 || (s_inj_defer_iret % 100000) == 0) {
                    LOGV3("irq",
                        "vec=0x20 DEFERRED (sti/iret shadow): EIP=0x%08x byte=%02x prev=%02x (n=%llu)\n",
                        eip, cur, prev,
                        (unsigned long long)s_inj_defer_iret);
                }
                return 0;
            }
        }
        s_irq0_last_defer_eip = 0;
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
        if (err != UC_ERR_OK) return 0;
        idt_entry = idt_buf;
    }

    uint16_t offset_lo = idt_entry[0] | (idt_entry[1] << 8);
    uint16_t selector  = idt_entry[2] | (idt_entry[3] << 8);
    uint16_t offset_hi = idt_entry[6] | (idt_entry[7] << 8);
    uint32_t handler   = offset_lo | (offset_hi << 16);
    (void)selector;

    if (handler == 0 || handler == 0x20000u || handler == 0x20000000u) {
        inject_stub++;
        return 0;
    }

    inject_ok++;
    g_emu.irq_ok_count = inject_ok;
    if (inject_ok <= 5 || (inject_ok % 100 == 0)) {
        LOGV3("irq", "vec=0x%02x → handler=0x%08x EIP=0x%08x (ok=%d blk=%d stub=%d)\n",
            vector, handler, eip, inject_ok, inject_blocked, inject_stub);
    }

    /* Sanity: refuse to inject if ESP isn't pointing into mapped RAM
     * with room for the 12-byte interrupt frame. We've observed task
     * ESPs drifting to 0xff3fxxxx (above ROM alias, unmapped) where
     * uc_mem_write silently fails — the push doesn't land, but ESP
     * is still decremented and a handler is invoked. The subsequent
     * IRET pops garbage and the CPU jumps to a corrupt vector,
     * eventually wedging in the GDT or at NULL. Defer instead so
     * the line stays pending; the next attempt may catch a sane ESP. */
    if (esp < 16 || esp >= RAM_SIZE || (esp - 12) >= RAM_SIZE) {
        static uint64_t s_inj_defer_esp_local = 0;
        s_inj_defer_esp++;
        s_inj_defer_esp_local++;
        if (s_inj_defer_esp_local <= 5 || (s_inj_defer_esp_local % 100000) == 0) {
            LOGV3("irq",
                "vec=0x%02x DEFERRED (ESP out of RAM): EIP=0x%08x ESP=0x%08x (n=%llu)\n",
                vector, eip, esp, (unsigned long long)s_inj_defer_esp);
        }
        return 0;
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
        LOGV3("irq", "  frame: [ESP+0]=0x%08x [+4]=0x%08x [+8]=0x%08x ESP=0x%08x→handler=0x%08x\n",
            v_eip, v_cs, v_ef, act_esp, act_eip);
    }

    /* Record this injection in the ring buffer for post-mortem if the
     * guest later wedges (EIP in GDT, CS=0, etc.). */
    {
        struct inj_record *r = &s_inj_ring[s_inj_head];
        r->seq          = ++s_inj_seq;
        r->vector       = vector;
        memcpy(r->idt_bytes, idt_entry, 8);
        r->selector     = selector;
        r->handler      = handler;
        r->pre_eip      = regs_val[0];
        r->pre_cs       = regs_val[3] & 0xFFFF;
        r->pre_esp      = regs_val[1];
        r->pre_eflags   = regs_val[2];
        r->post_esp     = esp;
        r->frame_eip    = regs_val[0];
        r->frame_cs     = cs;
        r->frame_eflags = regs_val[2];
        s_inj_head = (s_inj_head + 1) % IRQ_INJ_RING;
    }

    /* Arm IRQ0 nested-injection guard tracking. */
    if (vector == 0x20) {
        s_irq0_in_flight   = s_inj_seq;
        s_irq0_pre_eip     = regs_val[0];
        s_irq0_pre_esp     = regs_val[1];
        s_irq0_pre_currpid = cpu_read_currpid();
        s_irq0_handler_eip = handler;
        s_irq0_in_flight_arms++;
        s_irq0_in_flight_arm_vt = s_sched.vticks_total;
    }
    return 1;
}

/* Check PIC for pending interrupts and inject highest priority.
 * Called from exec loop to handle non-timer IRQs (e.g. IRQ4 UART THRE). */

/* Returns vector injected (0..255), or -1 if nothing was injected.
 * On injection failure (IF=0 / stub handler), rolls back the ISR set so
 * the line stays pending for the next attempt — no phantom in-service
 * interrupt that would never receive an EOI. */
static int check_and_inject_irq(void)
{
    /* Pre-check: don't even try if IF=0 (interrupts disabled) */
    uint32_t eflags = 0;
    uc_reg_read(g_emu.uc, UC_X86_REG_EFLAGS, &eflags);
    if (!(eflags & 0x200)) {
        /* If IRQ0 is sitting in IRR un-masked and not in service, this
         * is the reason it isn't being delivered: guest CLI'd. */
        if ((g_emu.pic[0].irr & 0x01) &&
            !(g_emu.pic[0].imr & 0x01) &&
            !(g_emu.pic[0].isr & 0x01)) {
            s_sched.irq0_blk_if++;
        }
        return -1;
    }

    /* IRQ0 specifically masked in IMR (would never be in `pending` below). */
    if ((g_emu.pic[0].irr & 0x01) &&
        (g_emu.pic[0].imr & 0x01) &&
        !(g_emu.pic[0].isr & 0x01)) {
        s_sched.irq0_blk_imr++;
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

                if (cpu_inject_interrupt(vector)) {
                    return vector;
                }
                /* Inject failed — restore IRR, drop ISR; line stays
                 * pending so next iteration retries. Without this we'd
                 * stall waiting for an EOI that will never come. */
                pic->isr &= ~(1 << bit);
                pic->irr |= (1 << bit);
                /* For IRQ0 specifically, classify why inject failed. */
                if (pic_idx == 0 && bit == 0) {
                    if (!(eflags & 0x200)) s_sched.irq0_blk_if++;
                    else                   s_sched.irq0_blk_stub++;
                }
                return -1;
            }
        }
    }
    return -1;
}

/* Code hook for tracing Init2 checkpoints */
static void hook_code_trace(uc_engine *uc, uint64_t addr, uint32_t size, void *user_data)
{
    uint32_t eax, ebx, esp;
    uc_reg_read(uc, UC_X86_REG_EAX, &eax);
    uc_reg_read(uc, UC_X86_REG_EBX, &ebx);
    uc_reg_read(uc, UC_X86_REG_ESP, &esp);

    switch ((uint32_t)addr) {
    case 0x801BF: LOGV3("trace", "Entry 0x801BF (INT19 handler)\n"); break;
    case 0x801C9: LOGV3("trace", "PM switch at 0x801C9\n"); break;
    case 0x801D9: LOGV3("trace", "PM entry 0x801D9\n"); break;
    case 0x801ED: LOGV3("trace", "Second reloc call 0x801ED\n"); break;
    case 0x801F2: LOGV3("trace", "Second Init2 call 0x801F2\n"); break;
    case 0x801F7: LOGV3("trace", "!!! GARBLED 0x801F7 reached !!!\n"); break;
    case 0x808FC: LOGV3("trace", "Init2 enter ESP=0x%08x\n", esp); break;
    case 0x80904: LOGV3("trace", "Init2 sub-calls start\n"); break;
    case 0x80922: LOGV3("trace", "Pre PCI-enum push\n"); break;
    case 0x80929: LOGV3("trace", "Post PCI-enum EAX=0x%08x → EBX\n", eax); break;
    case 0x80933: LOGV3("trace", "Call 0x83488 (1st) EBX=%d\n", ebx); break;
    case 0x80959: LOGV3("trace", "Call 0x83488 (2nd)\n"); break;
    case 0x80981: LOGV3("trace", "Update check: CMP EBX(%d), 1\n", ebx); break;
    case 0x8098A: LOGV3("trace", "UPDATE PATH: MOV EBX=0x12000000\n"); break;
    case 0x809A4: LOGV3("trace", "Validate boot data call\n"); break;
    case 0x809AC: LOGV3("trace", "Boot data result EAX=%d\n", eax); break;
    case 0x809CF: LOGV3("trace", "GameID check: flash[0x3C]=0x%08x vs BAR5\n", eax); break;
    case 0x80A56: LOGV3("trace", "GAME ENTRY: EAX=[EBX+0x48]=0x%08x\n", eax); break;
    case 0x80A5E: LOGV3("trace", ">>> CALL EAX (game jump!) EAX=0x%08x\n", eax); break;
    case 0x80A98: LOGV3("trace", "FAIL: boot data bad\n"); break;
    case 0x80B9C: LOGV3("trace", "NO UPDATE path at 0x80B9C\n"); break;
    case 0x83B20: LOGV3("trace", "PCI enum 0x83B20 enter\n"); break;
    case 0x83B29: {
        uint8_t guard;
        uc_mem_read(uc, 0x86CB0, &guard, 1);
        LOGV3("trace", "PCI enum guard=[0x86CB0]=%d\n", guard);
        break;
    }
    case 0x83DC5: LOGV3("trace", "PCI enum success EAX=1\n"); break;
    case 0x83DD6: LOGV3("trace", "PCI enum FAIL EAX=-1\n"); break;
    case 0x100000: LOGV3("trace", "*** GAME CODE ENTRY at 0x100000! ***\n"); break;
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
            LOGV2("vsync", "callback #%u\n", vs_call);
        break;
    }
    default:
        if (addr >= 0x88000 && addr < 0x8B000) {
            LOGV3("trace", "!!! STACK EXEC addr=0x%08x ESP=0x%08x !!!\n",
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
    LOGV3("watch", "dcs_mode WRITE addr=0x%llx size=%d val=%lld "
        "EIP=0x%08x ret=0x%08x\n",
        (unsigned long long)addr, size, (long long)value, eip, retaddr);

    /* Dump code around the write instruction */
    if (size == 4 && eip > 0x100000) {
        uint32_t start = (eip > 0x80) ? eip - 0x80 : 0;
        uint8_t *code = g_emu.ram + start;
        LOGV3("watch", "--- code dump %08x-%08x ---\n", start, start + 0x100);
        for (int i = 0; i < 0x100; i += 16) {
            LOGV3("watch", "%08x: %02x %02x %02x %02x %02x %02x %02x %02x "
                "%02x %02x %02x %02x %02x %02x %02x %02x\n",
                start + i,
                code[i+0], code[i+1], code[i+2], code[i+3],
                code[i+4], code[i+5], code[i+6], code[i+7],
                code[i+8], code[i+9], code[i+10], code[i+11],
                code[i+12], code[i+13], code[i+14], code[i+15]);
        }
        /* Also dump stack for call trace */
        LOGV3("watch", "--- stack @ESP=0x%08x ---\n", esp);
        for (int i = 0; i < 8; i++) {
            uint32_t sv = *(uint32_t *)(g_emu.ram + esp + i*4);
            LOGV3("watch", "  [ESP+%02x] = 0x%08x\n", i*4, sv);
        }
    }
}

/* Pre-Fatal IRQ/PIC snapshot — called from the UART line processor
 * when the guest emits a "*** Fatal" / "*** NonFatal" line. Dumps the
 * live counters since the last 5 s window AND the last few completed
 * windows so we can see both the immediate state and the lead-up. */
void cpu_dump_irq_snapshot(const char *trigger)
{
    static uint64_t s_last_dump_ns = 0;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    /* Rate-limit to once per 500 ms so a NonFatal storm doesn't spam. */
    if (s_last_dump_ns && now_ns - s_last_dump_ns < 500000000ULL) return;
    s_last_dump_ns = now_ns;

    if (!trigger) trigger = "snapshot";
    LOG("irq", "=== TRIGGER:%s — IRQ/PIC pre-Fatal snapshot ===\n", trigger);

    /* Live (incomplete) window since last 5 s boundary. */
    uint64_t d_vticks    = s_sched.vticks_total   - s_sched.w_vticks_base;
    uint64_t d_due       = s_sched.irq0_due       - s_sched.w_due_base;
    uint64_t d_fired     = s_sched.irq0_fired     - s_sched.w_fired_base;
    uint64_t d_collapsed = s_sched.irq0_collapsed - s_sched.w_collapsed_base;
    uint64_t d_inject    = s_sched.irq0_inject    - s_sched.w_inject_base;
    uint64_t d_eoi       = g_emu.pic[0].irq0_eoi_count - s_sched.w_eoi_base;
    uint64_t resched_drops = uart_get_resched_drop_count();
    uint64_t d_resched   = resched_drops - s_sched.w_resched_base;
    double dt_s = (s_sched.w_start_ns ? (double)(now_ns - s_sched.w_start_ns) : 1.0) / 1e9;
    if (dt_s < 0.001) dt_s = 0.001;
    double target_mips = (double)s_sched.cached_target_ips / 1e6;
    if (target_mips < 0.01) target_mips = 20.0;
    double host_mips = (double)d_vticks / 1e6 / dt_s;
    double vt_scale  = host_mips / target_mips;
    double wall_hz   = (double)d_inject / dt_s;
    double eoi_ratio = d_inject ? (double)d_eoi / (double)d_inject : 0.0;

    LOG("irq",
        "  live (last %.2fs): wall=%.0f Hz vt-scale=%.2fx host=%.1f MIPS\n",
        dt_s, wall_hz, vt_scale, host_mips);
    LOG("irq",
        "  live: due=%llu fired=%llu collapsed=%llu inject=%llu irq0-eoi=%llu eoi/inject=%.2f\n",
        (unsigned long long)d_due, (unsigned long long)d_fired,
        (unsigned long long)d_collapsed, (unsigned long long)d_inject,
        (unsigned long long)d_eoi, eoi_ratio);
    LOG("irq",
        "  live: max-ISR-busy-vticks=%llu (~%.0f us guest) resched-in-ISR-suppr=%llu\n",
        (unsigned long long)s_sched.isr_max_busy_vticks_w,
        (double)s_sched.isr_max_busy_vticks_w / target_mips,
        (unsigned long long)d_resched);
    LOG("irq",
        "  PIC0 now: IRR=0x%02x ISR=0x%02x IMR=0x%02x  PIT[0]=%u  "
        "blk(cum) if=%llu imr=%llu stub=%llu coll(cum) irr=%llu isr=%llu\n",
        g_emu.pic[0].irr, g_emu.pic[0].isr, g_emu.pic[0].imr,
        g_emu.pit.count[0],
        (unsigned long long)s_sched.irq0_blk_if,
        (unsigned long long)s_sched.irq0_blk_imr,
        (unsigned long long)s_sched.irq0_blk_stub,
        (unsigned long long)s_sched.irq0_coll_irr,
        (unsigned long long)s_sched.irq0_coll_isr);

    /* Live guest CPU state — where exactly is the guest right now?
     * For "*** Fatal" + stuck-ISR triggers, this points at the
     * instruction (usually inside an ISR or right after a corrupted
     * jump) that the snapshot is reacting to. */
    if (g_emu.uc) {
        uint32_t eip=0, esp=0, ebp=0, eax=0, eflags=0, cs=0;
        uc_reg_read(g_emu.uc, UC_X86_REG_EIP, &eip);
        uc_reg_read(g_emu.uc, UC_X86_REG_ESP, &esp);
        uc_reg_read(g_emu.uc, UC_X86_REG_EBP, &ebp);
        uc_reg_read(g_emu.uc, UC_X86_REG_EAX, &eax);
        uc_reg_read(g_emu.uc, UC_X86_REG_EFLAGS, &eflags);
        uc_reg_read(g_emu.uc, UC_X86_REG_CS, &cs);
        LOG("irq",
            "  guest CPU: EIP=0x%08x CS=0x%04x ESP=0x%08x EBP=0x%08x EAX=0x%08x EFLAGS=0x%08x IF=%d\n",
            eip, cs, esp, ebp, eax, eflags, (eflags >> 9) & 1);
        /* Top of stack — last few return addresses if the call chain
         * is healthy, garbage if it isn't. */
        uint32_t stk[6] = {0};
        if (esp >= 0x100 && esp < 0x80000000) {
            uc_mem_read(g_emu.uc, esp, stk, sizeof(stk));
            LOG("irq",
                "  guest stack[ESP..]: %08x %08x %08x %08x %08x %08x\n",
                stk[0], stk[1], stk[2], stk[3], stk[4], stk[5]);
        }
        /* Bytes at EIP — lets us identify the wedged instruction
         * (HLT 0xF4, JMP-self EBFE, tight loop, etc.) without needing
         * a separate disassembler. */
        uint8_t code[16] = {0};
        if (eip < RAM_SIZE - 16) {
            memcpy(code, g_emu.ram + eip, 16);
        } else {
            uc_mem_read(g_emu.uc, eip, code, sizeof(code));
        }
        LOG("irq",
            "  bytes@EIP: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
            code[0], code[1], code[2], code[3], code[4], code[5],
            code[6], code[7], code[8], code[9], code[10], code[11]);
    }

    /* Completed windows (oldest → newest). */
    int n = s_sched.ring_count;
    for (int i = 0; i < n; i++) {
        int idx = (s_sched.ring_head - (n - 1 - i) + IRQ_RING * 2) % IRQ_RING;
        struct sched_window *w = &s_sched.ring[idx];
        double age_s = (double)(now_ns - w->end_ns) / 1e9;
        LOG("irq",
            "  win[-%ds] wall=%.0f Hz vt=%.2fx host=%.1f MIPS [%s] "
            "due=%llu fired=%llu coll=%llu inj=%llu eoi=%llu maxISR=%llu PIC=%02x/%02x/%02x\n",
            (int)(age_s + 0.5),
            w->wall_hz, w->vt_scale, w->host_mips, w->health,
            (unsigned long long)w->d_due,
            (unsigned long long)w->d_fired,
            (unsigned long long)w->d_collapsed,
            (unsigned long long)w->d_inject,
            (unsigned long long)w->d_eoi,
            (unsigned long long)w->max_isr_busy_vticks,
            w->pic_irr, w->pic_isr, w->pic_imr);
    }
    LOG("irq", "=== end snapshot ===\n");
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

    /* doc 50 — wall-clock origin for cpu-stats and (opt-in) realtime throttle. */
    if (g_emu.cpu_stats_enabled || g_emu.realtime) {
        struct timespec t0;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        s_cpu_run_start_ns = (uint64_t)t0.tv_sec * 1000000000ULL + t0.tv_nsec;
        s_cpu_window_start_ns = s_cpu_run_start_ns;
    }

    while (g_emu.running) {
        /* Virtual-time IRQ0 scheduler.
         *
         * IRQ0 fires when the guest has consumed enough VIRTUAL TIME
         * (in vticks) to cover one PIT period — not when host wall-
         * clock says so. vticks is fed by Unicorn's `count` parameter
         * after each batch (see post-uc_emu_start accounting below)
         * and so reflects the host-independent guest clock that the
         * 8253 PIT would actually count down against on real HW.
         *
         * Real-PIC collapse: if IRR bit is already set, OR the previous
         * IRQ0 is still in service (ISR set), we DO NOT requeue — the
         * 8259 latches at most one pending edge per line. Behind-schedule
         * ticks are dropped (bounded resync) instead of bursted, which
         * would re-enter clkint before XINU's resched can clear the
         * scheduler watchdog at [0x30895c]. */
        uint16_t pit_div = g_emu.pit.count[0];
        if (pit_div == 0) pit_div = 0xFFFF;
        /* PIT period in vticks:
         *   pit_period_s = pit_div / 1193182
         *   model_ips    = cpu_target_mhz * 1e6
         *
         * cpu_target_mhz is the GUEST CPU MODEL RATE — used purely as
         * the conversion factor between executed guest ticks and
         * virtual time. It does NOT throttle the host. Unicorn always
         * runs as fast as it can; --realtime is a separate, opt-in
         * wall-clock throttle (off by default).
         *
         * Default 20 MIPS is a livable game-clock-vs-wall-clock ratio
         * on commodity Unicorn hosts that deliver ~10 MIPS effective
         * x86 throughput. Lower (12, 10) makes the game appear faster
         * but increases per-second IRQ load. Higher (233 = real Cyrix)
         * is the most physically honest setting, but on a slow host
         * the game crawls. THIS IS NOT THE FINAL ARCHITECTURE — just
         * the pragmatic conversion ratio until full virtual-event
         * scheduling lands across all peripherals.
         *
         * Watch the '[irq] IRQ0 5s' health line: vt-scale labels are
         * AHEAD / OK / HOST-SLOW / HOST-BEHIND, eoi/inject should be
         * 1.00, max-ISR-busy-vticks must stay well below pit_period. */
        uint64_t target_ips = g_emu.cpu_target_mhz > 0
            ? (uint64_t)g_emu.cpu_target_mhz * 1000000ULL
            : 20000000ULL;
        uint64_t pit_period_insns = (target_ips * pit_div) / 1193182ULL;
        if (pit_period_insns < 100) pit_period_insns = 100;
        s_sched.cached_pit_period_insns = pit_period_insns;
        s_sched.cached_target_ips = target_ips;
        /* PIT period in nanoseconds — wall-time floor on IRQ0.
         *   pit_period_ns = pit_div * 1e9 / 1193182  (PIT clk = 1.193182 MHz) */
        uint64_t pit_period_ns = ((uint64_t)pit_div * 1000000000ULL) / 1193182ULL;
        if (pit_period_ns < 1000) pit_period_ns = 1000;
        s_sched.pit_period_ns = pit_period_ns;

        /* Wall-clock now (cheap on Linux x86_64). */
        struct timespec _ts_now;
        clock_gettime(CLOCK_MONOTONIC, &_ts_now);
        uint64_t now_wall_ns = (uint64_t)_ts_now.tv_sec * 1000000000ULL
                             + (uint64_t)_ts_now.tv_nsec;

        if (g_emu.xinu_ready && s_sched.next_irq0_at_vticks == 0) {
            s_sched.next_irq0_at_vticks = s_sched.vticks_total + pit_period_insns;
            s_sched.next_irq0_at_ns     = now_wall_ns + pit_period_ns;
            s_sched.w_vticks_base = s_sched.vticks_total;
        }

        /* Fire IRQ0 only when BOTH virtual-time AND wall-time deadlines
         * have passed. Virtual-time gate keeps slow hosts from over-
         * firing relative to executed guest progress; wall-time gate
         * keeps fast hosts from over-firing relative to real time and
         * thereby compressing XINU's tick-based scheduler watchdog. */
        /* Drain ALL elapsed PIT periods per outer-loop iteration.
         * Previously we incremented due by 1 per check even if the batch
         * had advanced wall-time/vticks across multiple periods, then
         * reset the deadline to now+period when lag_n >= 4*period — so
         * we silently dropped ticks. That makes "delivered = inject/due"
         * look healthier than reality and starves the guest of cadence
         * when vt-scale runs ahead. Real PIT generates one rising edge
         * per period; PIC IRR collapses repeated edges within one ISR
         * span, so multiple due++ inside one window is correct: most
         * collapse into IRR/ISR, but the count tells us the true cadence
         * the guest should have seen. Cap the catch-up at 1024 periods
         * per check to bound CPU spent in the gate after a long stall. */
        if (s_sched.next_irq0_at_vticks) {
            int catchup_budget = 1024;
            while (catchup_budget-- > 0 &&
                   s_sched.vticks_total >= s_sched.next_irq0_at_vticks &&
                   now_wall_ns           >= s_sched.next_irq0_at_ns) {
                s_sched.irq0_due++;
                s_sched.pit_edges_due++;
                s_sched.next_irq0_at_vticks += pit_period_insns;
                s_sched.next_irq0_at_ns     += pit_period_ns;
                if (g_emu.xinu_ready) {
                    /* QEMU-style: PIT OUT0 mode-2 pulse — drop LOW
                     * then back HIGH. The PIC owns edge detection and
                     * IRR latching; ISR/IMR/IF live in the CPU
                     * acknowledge stage (check_and_inject_irq). */
                    uint64_t latched_before  = g_emu.pic[0].edges_latched;
                    uint64_t collapsed_before = g_emu.pic[0].edges_collapsed_irr;
                    pic_set_irq(0, 0, 0);
                    pic_set_irq(0, 0, 1);
                    s_sched.pit_edges_emitted++;
                    if (g_emu.pic[0].edges_latched > latched_before) {
                        s_sched.irq0_fired++;
                    } else if (g_emu.pic[0].edges_collapsed_irr > collapsed_before) {
                        s_sched.irq0_coll_irr++;
                        s_sched.irq0_collapsed++;
                    } else {
                        /* Edge dropped because IRR was clear but ISR
                         * still set (real PIC: edge in mode-2 still
                         * latches IRR even when ISR is set; only EOI
                         * lets it reach the CPU). Our approximation
                         * keeps both counters honest: irq_level was 0
                         * before, so any non-edge case here means a
                         * coding bug. Account for ISR-collapse as a
                         * fallback diagnostic. */
                        s_sched.irq0_coll_isr++;
                        s_sched.irq0_collapsed++;
                    }
                    (void)collapsed_before;
                }
            }
            /* Hard re-sync if the catch-up loop hit its budget — the
             * guest was paused (debugger, host scheduler hiccup) and we
             * should not spend the next 1000+ iterations chasing the
             * wall clock. Drop the deadlines onto "now" without
             * generating spurious due. */
            if (catchup_budget < 0) {
                s_sched.next_irq0_at_vticks = s_sched.vticks_total + pit_period_insns;
                s_sched.next_irq0_at_ns     = now_wall_ns          + pit_period_ns;
            }
        }

        /* Track ISR-busy span (in vticks).
         * Sampled per outer-loop iteration. Sets ts on first observation
         * of ISR=1 after a 0; closes span when we see ISR=0 again. */
        uint8_t isr_now = g_emu.pic[0].isr & 0x01;
        if (isr_now && !s_sched.isr_prev) {
            s_sched.isr_set_at_vticks = s_sched.vticks_total;
            s_sched.isr_warned_this_span = false;
        }
        if (s_sched.isr_set_at_vticks) {
            uint64_t span = s_sched.vticks_total - s_sched.isr_set_at_vticks;
            if (span > s_sched.isr_max_busy_vticks_w)
                s_sched.isr_max_busy_vticks_w = span;
            /* Stuck-ISR watchdog: if guest has been inside the IRQ0 ISR
             * for more than ~50 ms of guest time without sending PIC
             * EOI, that's almost certainly a wedge (not a long but
             * legitimate handler). Auto-trigger a snapshot once per
             * stuck span so we capture WHERE in the guest it's stuck
             * before the *** Fatal storm starts. */
            if (!s_sched.isr_warned_this_span &&
                span > (uint64_t)(s_sched.cached_target_ips / 20ULL)) {
                s_sched.isr_warned_this_span = true;
                cpu_dump_irq_snapshot("stuck-ISR>50ms");
            }
            /* Stuck-ISR recovery (BAND-AID, NOT a fix).
             *
             * The IRET-leak hypothesis was disproved by the lifecycle
             * trace (Δ=+0 across many IRQ0 returns — Unicorn's IRET
             * pops correctly). The actual wedge mode is XINU emitting
             * "resched: called from interrupt handler" then a Fatal
             * storm, which suggests our injection or the PIT/PIC line
             * model lets a new IRQ fire while the kernel is still in
             * an interrupt context. The right fix is a small QEMU-style
             * PIT/PIC line model. Until that lands, this recovery
             * keeps IRQ0 delivery alive after a 250 ms stall so the
             * emulator does not freeze for the user. */
            if (span > (uint64_t)(s_sched.cached_target_ips / 4ULL)) {
                static uint64_t s_isr_force_eoi = 0;
                s_isr_force_eoi++;
                s_isr_force_eoi_total++;
                if (s_isr_force_eoi <= 5 || (s_isr_force_eoi % 1000) == 0) {
                    LOG("irq",
                        "*** STUCK-ISR RECOVERY (band-aid): forcing EOI after %llu vticks (~%llu ms guest) (n=%llu)\n",
                        (unsigned long long)span,
                        (unsigned long long)(span * 1000ULL /
                            (s_sched.cached_target_ips ? s_sched.cached_target_ips : 1)),
                        (unsigned long long)s_isr_force_eoi);
                }
                g_emu.pic[0].isr &= ~0x01;
                g_emu.pic[0].irq0_eoi_count++;
                s_sched.isr_set_at_vticks = 0;
                s_sched.isr_warned_this_span = false;
            }
            if (!isr_now) s_sched.isr_set_at_vticks = 0;
        }
        s_sched.isr_prev = isr_now;

        /* Periodic stats every 5 s wall — full health line. */
        struct timespec _now_ts;
        clock_gettime(CLOCK_MONOTONIC, &_now_ts);
        uint64_t _now_ns = (uint64_t)_now_ts.tv_sec * 1000000000ULL
                         + _now_ts.tv_nsec;
        if (s_sched.w_start_ns == 0) s_sched.w_start_ns = _now_ns;
        if (g_emu.xinu_ready &&
            _now_ns - s_sched.w_start_ns >= 5000000000ULL) {
            uint64_t dt_ns = _now_ns - s_sched.w_start_ns;
            uint64_t d_due       = s_sched.irq0_due       - s_sched.w_due_base;
            uint64_t d_fired     = s_sched.irq0_fired     - s_sched.w_fired_base;
            uint64_t d_collapsed = s_sched.irq0_collapsed - s_sched.w_collapsed_base;
            uint64_t d_inject    = s_sched.irq0_inject    - s_sched.w_inject_base;
            uint64_t d_eoi       = g_emu.pic[0].irq0_eoi_count - s_sched.w_eoi_base;
            uint64_t d_vticks    = s_sched.vticks_total   - s_sched.w_vticks_base;
            uint64_t d_coll_irr  = s_sched.irq0_coll_irr  - s_sched.w_coll_irr_base;
            uint64_t d_coll_isr  = s_sched.irq0_coll_isr  - s_sched.w_coll_isr_base;
            uint64_t d_blk_if    = s_sched.irq0_blk_if    - s_sched.w_blk_if_base;
            uint64_t d_blk_imr   = s_sched.irq0_blk_imr   - s_sched.w_blk_imr_base;
            uint64_t d_blk_stub  = s_sched.irq0_blk_stub  - s_sched.w_blk_stub_base;
            uint64_t resched_drops = uart_get_resched_drop_count();
            uint64_t d_resched   = resched_drops - s_sched.w_resched_base;

            double dt_s = (double)dt_ns / 1e9;
            double wall_hz   = (double)d_inject / dt_s;
            double target_hz = 1193182.0 / (double)pit_div;
            double host_mips = (double)d_vticks / 1e6 / dt_s;
            double target_mips = (double)target_ips / 1e6;
            double vt_scale  = host_mips / target_mips;
            double eoi_ratio = d_inject ? (double)d_eoi / (double)d_inject : 0.0;
            const char *health = (vt_scale >= 1.05) ? "AHEAD"
                              : (vt_scale >= 0.95) ? "OK"
                              : (vt_scale >= 0.50) ? "HOST-SLOW"
                                                   : "HOST-BEHIND";

            /* Sample current PIC0 + guest IF for the block-reason verdict. */
            uint8_t irr0 = g_emu.pic[0].irr & 0x01;
            uint8_t isr0 = g_emu.pic[0].isr & 0x01;
            uint8_t imr0 = g_emu.pic[0].imr & 0x01;
            uint32_t cur_eflags = 0;
            if (g_emu.uc) uc_reg_read(g_emu.uc, UC_X86_REG_EFLAGS, &cur_eflags);
            int cur_if = (cur_eflags >> 9) & 1;

            /* Verdict: if no IRQ0 was delivered this window despite
             * timer ticks being due, classify what blocked it. */
            const char *verdict = "ok";
            if (d_due > 0 && d_inject == 0) {
                if (isr0)              verdict = "DEAD: ISR-stuck (no EOI)";
                else if (imr0)         verdict = "DEAD: IMR masks IRQ0";
                else if (irr0 && !cur_if) verdict = "DEAD: IRR pending, IF=0";
                else if (irr0)         verdict = "DEAD: IRR pending, inject loop not firing";
                else if (d_blk_stub)   verdict = "DEAD: IDT stub for vector 0x08";
                else                   verdict = "DEAD: unknown (no IRR/ISR/IMR/IF block)";
            } else if (d_due > 0 && d_inject < d_due / 2) {
                verdict = "DEGRADED: <50% IRQ0s delivered";
            }

            double delivered_pct = d_due ? 100.0 * (double)d_inject / (double)d_due : 0.0;
            double collapsed_pct = d_due ? 100.0 * (double)d_collapsed / (double)d_due : 0.0;
            double wall_period_us = wall_hz > 0.0 ? 1.0e6 / wall_hz : 0.0;
            double target_period_us = target_hz > 0.0 ? 1.0e6 / target_hz : 0.0;
            LOG("irq",
                "IRQ0 5s: wall=%.0f Hz / target=%.0f Hz (period: wall=%.1f us / target=%.1f us; delivered=%.1f%% collapsed=%.1f%%) (vt-scale=%.2fx, host=%.1f MIPS) [%s] %s\n",
                wall_hz, target_hz, wall_period_us, target_period_us,
                delivered_pct, collapsed_pct,
                vt_scale, host_mips, health, verdict);
            LOG("irq",
                "         due=%llu fired=%llu collapsed=%llu (irr=%llu isr=%llu) inject=%llu irq0-eoi=%llu (eoi/inject=%.2f)\n",
                (unsigned long long)d_due,
                (unsigned long long)d_fired,
                (unsigned long long)d_collapsed,
                (unsigned long long)d_coll_irr,
                (unsigned long long)d_coll_isr,
                (unsigned long long)d_inject,
                (unsigned long long)d_eoi,
                eoi_ratio);
            LOG("irq",
                "         block: if=%llu imr=%llu stub=%llu  PIC0 IRR=0x%02x ISR=0x%02x IMR=0x%02x  guest IF=%d\n",
                (unsigned long long)d_blk_if,
                (unsigned long long)d_blk_imr,
                (unsigned long long)d_blk_stub,
                g_emu.pic[0].irr, g_emu.pic[0].isr, g_emu.pic[0].imr,
                cur_if);
            LOG("irq",
                "         max-ISR-busy=%llu vticks (~%.0f us guest) resched-in-ISR-suppressed=%llu\n",
                (unsigned long long)s_sched.isr_max_busy_vticks_w,
                (double)s_sched.isr_max_busy_vticks_w / target_mips,
                (unsigned long long)d_resched);
            uint64_t cur_inflight_dur = s_irq0_in_flight
                ? (s_sched.vticks_total - s_irq0_in_flight_arm_vt) : 0;
            LOG("irq",
                "         guards: in-flight arms=%llu clears=%llu (iret=%llu tsw=%llu) cur=%llu cur-dur=%llu vt max-vt=%llu  defer: nest=%llu sti/iret=%llu no-prog=%llu burst=%llu esp=%llu  force-EOI=%llu  blk-stub=%llu\n",
                (unsigned long long)s_irq0_in_flight_arms,
                (unsigned long long)s_irq0_in_flight_clears,
                (unsigned long long)s_irq0_in_flight_clear_iret,
                (unsigned long long)s_irq0_in_flight_clear_tsw,
                (unsigned long long)s_irq0_in_flight,
                (unsigned long long)cur_inflight_dur,
                (unsigned long long)s_irq0_in_flight_max_vt,
                (unsigned long long)s_irq0_guard_defer,
                (unsigned long long)s_inj_defer_iret,
                (unsigned long long)s_inj_defer_progress,
                (unsigned long long)s_inj_defer_burst,
                (unsigned long long)s_inj_defer_esp,
                (unsigned long long)s_isr_force_eoi_total,
                (unsigned long long)d_blk_stub);
            /* Health amendment: unhealthy if guard ever permanently
             * stuck in-flight or force-EOI ever fired this run. */
            if (s_isr_force_eoi_total > 0) {
                LOG("irq", "         UNHEALTHY: force-EOI fired %llu times this run\n",
                    (unsigned long long)s_isr_force_eoi_total);
            }
            if (s_irq0_in_flight && s_irq0_in_flight_max_vt > target_ips) {
                LOG("irq", "         UNHEALTHY: in-flight stuck for >1s of guest time (cur=%llu max=%llu vticks)\n",
                    (unsigned long long)s_irq0_in_flight,
                    (unsigned long long)s_irq0_in_flight_max_vt);
            }

            /* Auto-snapshot when IRQ0 delivery has died. This is the
             * hard failure mode the user wants caught immediately, not
             * only when *** Fatal eventually appears in UART. */
            if (d_due > 100 && d_inject == 0) {
                cpu_dump_irq_snapshot("IRQ0-delivery-dead");
            }

            /* Push window into ring (newest at head). */
            int next_head = (s_sched.ring_head + 1) % IRQ_RING;
            struct sched_window *w = &s_sched.ring[next_head];
            w->end_ns      = _now_ns;
            w->d_vticks    = d_vticks;
            w->d_due       = d_due;
            w->d_fired     = d_fired;
            w->d_collapsed = d_collapsed;
            w->d_inject    = d_inject;
            w->d_eoi       = d_eoi;
            w->d_resched   = d_resched;
            w->max_isr_busy_vticks = s_sched.isr_max_busy_vticks_w;
            w->wall_hz     = wall_hz;
            w->target_hz   = target_hz;
            w->vt_scale    = vt_scale;
            w->host_mips   = host_mips;
            w->pic_irr     = g_emu.pic[0].irr;
            w->pic_isr     = g_emu.pic[0].isr;
            w->pic_imr     = g_emu.pic[0].imr;
            snprintf(w->health, sizeof(w->health), "%s", health);
            s_sched.ring_head = next_head;
            if (s_sched.ring_count < IRQ_RING) s_sched.ring_count++;

            s_sched.w_due_base       = s_sched.irq0_due;
            s_sched.w_fired_base     = s_sched.irq0_fired;
            s_sched.w_collapsed_base = s_sched.irq0_collapsed;
            s_sched.w_coll_irr_base  = s_sched.irq0_coll_irr;
            s_sched.w_coll_isr_base  = s_sched.irq0_coll_isr;
            s_sched.w_blk_if_base    = s_sched.irq0_blk_if;
            s_sched.w_blk_imr_base   = s_sched.irq0_blk_imr;
            s_sched.w_blk_stub_base  = s_sched.irq0_blk_stub;
            s_sched.w_inject_base    = s_sched.irq0_inject;
            s_sched.w_eoi_base       = g_emu.pic[0].irq0_eoi_count;
            s_sched.w_vticks_base    = s_sched.vticks_total;
            s_sched.isr_max_busy_vticks_w = 0;
            s_sched.w_resched_base   = resched_drops;
            s_sched.w_start_ns       = _now_ns;
        }

        /* Wall-clock "now" for VSYNC and throttle (independent of IRQ0). */
        struct timespec now_ts;
        clock_gettime(CLOCK_MONOTONIC, &now_ts);
        uint64_t now_ns = (uint64_t)now_ts.tv_sec * 1000000000ULL + now_ts.tv_nsec;

        /* Coarser per-vblank work — still gated to once-every-64-batches
         * to avoid clock_gettime / uc_mem_write overhead on every batch.
         * VSYNC, timer-pending drain, optional throttle and cpu-stats. */
        if (g_emu.xinu_ready && (g_emu.exec_count & 0x3F) == 0) {
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

            /* doc 50 — opt-in realtime throttle (--realtime).
             * Uses the same authoritative virtual-time clock as the
             * IRQ scheduler, so throttle and IRQ timing always agree. */
            if (g_emu.realtime && s_cpu_run_start_ns) {
                uint64_t target_ips = g_emu.cpu_target_mhz > 0
                    ? (uint64_t)g_emu.cpu_target_mhz * 1000000ULL
                    : 20000000ULL;
                uint64_t expected_ns = (s_sched.vticks_total * 1000000000ULL) / target_ips;
                uint64_t actual_ns   = now_ns - s_cpu_run_start_ns;
                if (expected_ns > actual_ns) {
                    uint64_t sleep_ns = expected_ns - actual_ns;
                    if (sleep_ns > 50000000ULL) sleep_ns = 50000000ULL; /* cap 50 ms */
                    struct timespec ts = {
                        .tv_sec  = (time_t)(sleep_ns / 1000000000ULL),
                        .tv_nsec = (long)(sleep_ns % 1000000000ULL),
                    };
                    nanosleep(&ts, NULL);
                }
            }

            /* doc 50 — guest-CPU stats report. */
            if (g_emu.cpu_stats_enabled && s_cpu_window_start_ns) {
                uint64_t period_ns = (uint64_t)g_emu.cpu_stats_period_s
                                     * 1000000000ULL;
                if (now_ns - s_cpu_window_start_ns >= period_ns) {
                    uint64_t win_ns = now_ns - s_cpu_window_start_ns;
                    uint64_t run_ns = now_ns - s_cpu_run_start_ns;
                    uint64_t insns_window = (s_cpu_bytes_window * 2ULL) / 7ULL;
                    uint64_t insns_total  = (s_cpu_bytes_total  * 2ULL) / 7ULL;
                    double mips_window = (double)insns_window * 1000.0
                                         / (double)win_ns;
                    double mips_total  = (double)insns_total  * 1000.0
                                         / (double)run_ns;
                    LOG("cpu-stats",
                        "window=%.2fs blocks=%llu bytes=%llu "
                        "insns≈%llu  → ~%.1f guest-MIPS  "
                        "(run=%.1fs avg≈%.1f MIPS, target Cyrix MediaGX≈233)\n",
                        (double)win_ns / 1e9,
                        (unsigned long long)s_cpu_blocks_window,
                        (unsigned long long)s_cpu_bytes_window,
                        (unsigned long long)insns_window,
                        mips_window,
                        (double)run_ns / 1e9,
                        mips_total);
                    s_cpu_blocks_window = 0;
                    s_cpu_bytes_window  = 0;
                    s_cpu_window_start_ns = now_ns;
                }
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
                        LOGV3("irq", "clkint detected: IDT[0x20]=0x%08x EIP=0x%08x exec=%u\n",
                            handler, eip, (unsigned)g_emu.exec_count);
                    }
                    if (g_emu.xinu_booted &&
                        g_emu.exec_count >= g_emu.clkint_ready_exec + 50) {
                        g_emu.xinu_ready = true;
                        /* Cache IDT base for fast interrupt injection */
                        uc_x86_mmr idtr;
                        uc_reg_read(uc, UC_X86_REG_IDTR, &idtr);
                        g_emu.idt_base = (uint32_t)idtr.base;
                        LOGV3("irq", "XINU ready: timer injection enabled EIP=0x%08x exec=%u idt_base=0x%x\n",
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
                            /* .not_cyrix: came from #UD on a non-0F3C
                             * opcode. Real CPUs would re-raise; we
                             * advance saved EIP by 1 and IRET so the
                             * guest can't infinite-loop on us, and
                             * critically we DO NOT use LEAVE+RET here
                             * (that pops EBP+CS+EFLAGS as if they
                             * were a C return frame, leaking 8 bytes
                             * of stack on every spurious #UD — over
                             * thousands of preemptions this triggers
                             * XINU's IStack underflow watchdog). */
                            h6[p++] = 0x5E;                         /* POP ESI */
                            h6[p++] = 0x58;                         /* POP EAX */
                            h6[p++] = 0x83; h6[p++] = 0x04;         /* ADD DWORD [ESP], 1 */
                            h6[p++] = 0x24; h6[p++] = 0x01;
                            h6[p++] = 0xCF;                         /* IRET */
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
                    LOGV3("irq", "waiting for clkint: IDT[0x20]=0x%08x EIP=0x%08x exec=%u xinu_booted=%d\n",
                        handler, eip, (unsigned)g_emu.exec_count, g_emu.xinu_booted);
                }
            }
        }

        /* ============================================================
         * DCS-mode override — pattern-driven, game-agnostic, generic.
         *
         * The Williams DCS-detect probe in the boot/init code does
         *     CMP EAX, 1                  (83 F8 01)
         *     JNE  +0x21                  (75 21)
         *     MOV  [<bss_slot>], EAX      (A3 ?? ?? ?? ??)
         * after reading the DCS reset response. EAX==1 means "BAR4
         * (MMIO) DCS interface present" → fall-through stores 1 into
         * the dcs_mode slot, downstream sound init uses BAR4.
         * Anything else → JNE → "I/O-port DCS interface" path, which
         * io.c only partially implements (RESETs are answered, but
         * the post-reset cmd stream — dong/init/mixer — never sent).
         *
         * The unique `JNE +0x21` distance (0x21 = 33 bytes) is the
         * discriminator: this exact CMP/JNE/MOV idiom appears in
         * other places in every bundle but always with a different
         * jne offset (0x05, 0x0F, 0x1E, 0x24, 0x26 …). Static scan
         * across all 7 dearchived bundles confirms exactly one
         * `83 F8 01 75 21 A3 ?? ?? ?? ??` per bundle, and the target
         * is always in the per-build BSS range 0x310000-0x390000.
         *
         * Each bundle stores dcs_mode at its own per-build address
         * (SWE1 v1.5 → 0x3444B0, SWE1 v2.1 → 0x34A714, RFM v1.2 →
         * 0x313EBC, …). The MOV-store keeps that bundle's address;
         * we only replace the 5-byte CMP+JNE prologue with
         * `MOV EAX, 1` so the store fires unconditionally.
         *
         * Range 0x80000-0x400000 covers all known relocation targets
         * of the option-ROM copy. One-shot at the XINU-ready
         * transition. NO per-game gate — pure pattern match.
         *
         * NOTE — I/O DCS handshake is DEFERRED per user directive:
         * "you tried multiple times to get the io handshake but never
         * got it. start by getting it work with bar4, we will take a
         * look again on io handshake later. note it somewhere to not
         * forget".  Until io.c implements the full UART command-stream
         * answer pump, BAR4 is the only viable sound path and this
         * patch is required to reach it on every bundle that has the
         * probe (i.e. every bundle observed so far).
         * ============================================================ */
        if (g_emu.xinu_booted && g_emu.xinu_ready &&
            !g_emu.dcs_mode_patch_attempted) {
            g_emu.dcs_mode_patch_attempted = true;
            if (g_emu.dcs_mode_choice == ENCORE_DCS_IO_HANDLED) {
                LOG("init",
                    "DCS-mode patch SKIPPED (--dcs-mode io-handled): "
                    "game uses unmodified PCI probe; UART handlers in io.c "
                    "answer the I/O path.\n");
            } else {
            const uint32_t scan_lo = 0x80000u;
            const uint32_t scan_hi = 0x400000u;
            int hits = 0;
            uint8_t *r = g_emu.ram;
            for (uint32_t a = scan_lo; a + 10 <= scan_hi; a++) {
                if (r[a]   == 0x83 && r[a+1] == 0xF8 && r[a+2] == 0x01 &&
                    r[a+3] == 0x75 && r[a+4] == 0x21 &&
                    r[a+5] == 0xA3) {
                    uint32_t slot =  (uint32_t)r[a+6]
                                  | ((uint32_t)r[a+7] << 8)
                                  | ((uint32_t)r[a+8] << 16)
                                  | ((uint32_t)r[a+9] << 24);
                    /* Sanity: target must look like a per-build BSS slot.
                     * Observed slots: SWE1 v1.5 0x3444B0, SWE1 v2.1
                     * 0x34A714, RFM v1.6 0x36D39C, RFM v1.8 0x36D030,
                     * RFM v2.5 0x382600, RFM v2.6 0x383330. */
                    if (slot < 0x300000u || slot >= 0x400000u) continue;

                    /* Replace ONLY the 5-byte CMP+JNE prologue with
                     * `MOV EAX, 1`. The trailing `A3 ?? ?? ?? ??`
                     * (MOV [<slot>], EAX) MUST remain intact so the
                     * forced value actually lands in this bundle's
                     * dcs_mode slot — without the store, the game's
                     * later sound-init checks see dcs_mode==0 and skip
                     * the BAR4 command stream. */
                    uint8_t patch[5] = { 0xB8, 0x01, 0x00, 0x00, 0x00 };
                    uc_mem_write(uc, a, patch, 5);
                    /* Also keep the RAM mirror in sync so any reader
                     * that bypasses Unicorn's TLB sees the patched code. */
                    memcpy(r + a, patch, 5);
                    LOG("init",
                        "DCS-mode pattern hit @0x%08x slot=0x%08x"
                        " — patched (force BAR4)\n",
                        a, slot);
                    hits++;
                    if (hits >= 4) break;  /* bail-out, shouldn't happen */
                }
            }
            if (hits == 0)
                LOG("init",
                    "DCS-mode pattern absent — no patch applied\n");
            }
        }

        /* Inject all pending PIC interrupts (timer IRQ0 + others like IRQ4 UART).
         * check_and_inject_irq() respects both PIC IMR (hw mask) and CPU IF flag,
         * properly tracks ISR (in-service), and rolls back on inject failure.
         * Only inject after XINU ready so IDT has real handlers. */
        if (g_emu.xinu_ready) {
            uint8_t pending0 = g_emu.pic[0].irr & ~g_emu.pic[0].imr;
            uint8_t pending1 = g_emu.pic[1].irr & ~g_emu.pic[1].imr;
            if (pending0 || pending1) {
                int v = check_and_inject_irq();
                /* Count actual IRQ0 injections (vector == ICW2[0]+0). */
                if (v >= 0 && (uint8_t)v == g_emu.pic[0].icw2)
                    s_sched.irq0_inject++;
            }
            /* Per-iteration EIP refresh — Unicorn advances its internal EIP
             * each batch, and check_and_inject_irq may have rewritten it on
             * success. Either way the local copy must be re-read before the
             * next uc_emu_start, otherwise we'd resume from a stale EIP. */
            uc_reg_read(uc, UC_X86_REG_EIP, &eip);
        }

        /* Variable batch — run until next virtual event.
         *
         * Compute insns to next IRQ0 deadline; clamp to [min_batch,
         * MAX_BATCH]. Without a floor, near-deadline iterations drop
         * to ~50 insns each, doubling uc_emu_start call rate and
         * destabilizing the host. With min_batch ≥ 1 PIT period / 4
         * (and a hard 1000-insn floor for the default 20 MIPS / 4 kHz
         * case), per-batch overhead stays bounded; the 8259 collapse
         * logic absorbs the up-to-min_batch lateness it allows.
         *
         * Pre-XINU there is no IRQ0 to deliver, so a much larger
         * batch is fine and reduces uc_emu_start overhead during ROM
         * boot (the slowest part of the run). */
        size_t batch;
        if (g_emu.xinu_ready) {
            int64_t to_next = (int64_t)s_sched.next_irq0_at_vticks
                            - (int64_t)s_sched.vticks_total;
            size_t min_batch = pit_period_insns / 4;
            if (min_batch > 1000) min_batch = 1000;
            if (min_batch < 100)  min_batch = 100;
            const size_t max_batch = 4000;
            if (to_next <= 0)
                batch = min_batch;
            else if ((uint64_t)to_next > max_batch)
                batch = max_batch;
            else if ((uint64_t)to_next < min_batch)
                batch = min_batch;
            else
                batch = (size_t)to_next;
        } else {
            batch = 200000;
        }

        /* Execute a batch of instructions.
         * eip is carried from previous iteration (or initial read before loop). */
        /* TLB flush: every 64 cycles for DC_TIMING2 VSYNC detection. */
        if ((g_emu.exec_count & 0x3F) == 0)
            uc_ctl_flush_tlb(uc);
        uc_err err = uc_emu_start(uc, eip, 0, 0, batch);

        /* Refresh local eip from Unicorn — after uc_emu_start, the
         * authoritative stop point is in the engine's register file,
         * NOT the value we passed in. Using the entry-point eip below
         * mis-classifies HLT-at-stop as a non-HLT batch (vticks gets
         * +4 instead of +batch), which starves IRQ0 / resched during
         * nulluser idle. */
        uc_reg_read(uc, UC_X86_REG_EIP, &eip);

        g_emu.exec_count++;
        prof_emu_calls++;
        s_sched.emu_calls++;
        int eip_dirty = 0;  /* set when error handlers modify local eip */

        /* Authoritative virtual-time advance.
         *
         *   UC_ERR_OK              : Unicorn either consumed `batch`
         *                            insns or stopped at an HLT.
         *                            Either way the CPU has consumed
         *                            `batch` ticks of virtual time
         *                            (HLT idle = real CPU would be
         *                            idle for that long too).
         *   UC_ERR_INSN_INVALID    : HLT-from-error / 0F3C / similar
         *                            partial run. We can't know exact
         *                            partial count; credit `batch` for
         *                            HLT (idle equivalence), small
         *                            bounded value otherwise so the
         *                            scheduler doesn't stall.
         *   other errors           : unmapped mem etc. Credit small
         *                            bounded value, scheduler will
         *                            still progress; the error itself
         *                            is the real signal.
         *
         * This includes idle/halted virtual time on purpose — vticks
         * is the SCHEDULER clock, not retired-instruction count. The
         * 0F3C / invalid-insn cases credit only a small fixed value
         * since these are emulated single instructions, not idle. */
        if (err == UC_ERR_OK) {
            s_sched.vticks_total += batch;
        } else if (err == UC_ERR_INSN_INVALID) {
            uint8_t maybe_hlt = 0;
            if (eip < RAM_SIZE) maybe_hlt = (g_emu.ram[eip] == 0xF4);
            s_sched.vticks_total += maybe_hlt ? batch : 4;
        } else {
            s_sched.vticks_total += 4;
        }

        /* Periodic maintenance — every iteration. */
        if (g_emu.game_started) {
            if (g_emu.watchdog_flag_addr) {
                /* The "watchdog health register" found by the scanner is
                 * actually inside the DCS PCI-detect probe at 0x1A2ABC:
                 *     cmp dword [<cell>], 0xFFFF ; je RET-0
                 *     mov eax, 1                  ; (cell != 0xFFFF) RET-1
                 * Probe returns 1 (DCS PRESENT) when cell != 0xFFFF.
                 * In bar4-patch mode the CMP is byte-patched to mov eax,1
                 * so the cell value is irrelevant; writing 0xFFFF (legacy
                 * empirical value) is fine.  In io-handled mode we depend
                 * on the *natural* probe → must keep cell != 0xFFFF, so
                 * we scribble 0 instead, which yields probe → 1 → DCS
                 * detected → game writes dcs_mode=1 → audio init runs. */
                /* Staged scribble: keep 0xFFFF until XINU is ready.
                 * Pre-XINU boot code on some bundles (RFM v1.2, SWE1 v1.3,
                 * and base-chips-only --update none) reads this cell as a
                 * boot sentinel; writing 0 there derails the early path
                 * before xinu_ready ever fires.  After xinu_ready the game
                 * stack is up and the only consumer of this cell is the
                 * DCS probe inside dcs_probe(), which returns 1 when
                 * cell != 0xFFFF.  So flip to 0 only after xinu_ready for
                 * io-handled; keep 0xFFFF for bar4-patch (patched CMP
                 * makes the value moot but the watchdog callee is happy). */
                /* IRQ0-cadence-fix follow-up:
                 * The pci_watchdog_bone caller does
                 *     call test_fn; cmp eax,1; jne +20 (skip Fatal); FATAL
                 * i.e. Fatal fires when test_fn returns 1. test_fn returns
                 * 1 iff dcs_probe says "DCS present" AND PLX[0x4C] bit 2
                 * clear. So the *correct* scribble in BOTH modes is the
                 * value that makes dcs_probe return 0 (DCS "absent" path),
                 * which in this firmware is cell == 0xFFFF. Empirical:
                 * before the IRQ0 cadence fix the watchdog bone process
                 * never ran, so scribbling 0 didn't matter; with the
                 * cadence right the polarity matters. Audio init needs
                 * its own DCS-presence path elsewhere — see dcs_mode
                 * choice handling in io.c bar4-patch / io-handled. */
                uint32_t scribble_val = 0x0000FFFFu;
                RAM_WR32(g_emu.watchdog_flag_addr, scribble_val);
            }
            if (g_emu.plx_bar_ptr_addr) {
                /* Keep PLX BAR pointer alive — see include/encore.h. */
                RAM_WR32(g_emu.plx_bar_ptr_addr, WMS_BAR0);
            }
            RAM_WR32(0, 0);
        }

        /* Read EIP after execution stopped */
        uc_reg_read(uc, UC_X86_REG_EIP, &eip);

        /* Hard trap: if EIP enters the GDT region (0x1000..0x1020) or
         * any low non-code page (< 0x800, before our entry stubs), we
         * are running off corrupt return state. Dump full forensic
         * context once and abort the run cleanly so the test exits
         * with the snapshot at the top of the log instead of being
         * buried in spam from the resulting infinite IRET loop. */
        if (!s_gdt_eip_trapped &&
            ((eip >= GDT_ADDR && eip < GDT_ADDR + 0x40) ||
             (eip < 0x400u))) {
            s_gdt_eip_trapped = 1;
            LOG("irq", "*** EIP-IN-DANGER-ZONE TRAP: EIP=0x%08x (GDT=0x%08x) — aborting ***\n",
                eip, (unsigned)GDT_ADDR);
            cpu_dump_irq_snapshot("EIP-in-GDT");
            cpu_dump_inj_ring("EIP-in-GDT");
            /* Stop the emulator gracefully — main loop will see EOF. */
            uc_emu_stop(uc);
            g_emu.running = false;
            break;
        }

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
                    LOGV("cpu", "INSN_INVALID #%d EIP=0x%08x bytes=%02x %02x %02x %02x\n",
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
                            LOGV("cpu", "Fatal/panic HLT @0x%08x → prnull idle (#%d)\n",
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

            /* One-shot: dump VSYNC callback code from live memory.
             * 0x2E8AF4 / 0x2E8B74 / 0x2E8E2C are SWE1-V1.19 BSS slots;
             * on other bundles the reads return unrelated data, so keep
             * the log gated on game_id to avoid noise. */
            if (g_emu.game_id == 50069u) {
                static int dump_count = 0;
                if (g_emu.xinu_ready && dump_count < 4) {
                    dump_count++;
                    uint32_t enable = RAM_RD32(0x2E8AF4);
                    uint32_t gxptr = RAM_RD32(0x2E8B74);
                    uint32_t dm_mode_v = RAM_RD32(0x2E8E2C);
                    LOGV("dbg", "VSYNC enable=0x%x gx_ptr=0x%x dm_mode=%u (exec=%lu)\n",
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
                LOGV2("hb", "exec=%lu EIP=0x%08x post=0x%02x vsync=%u frames=%d irq_ok=%u\n",
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
                            LOGV2("hb", "  STUCK bytes @0x%08x: %s\n", base, hex);
                            uint32_t esp = 0, regs[8];
                            uc_reg_read(g_emu.uc, UC_X86_REG_ESP, &esp);
                            static const int rids[8] = {UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI};
                            for (int i = 0; i < 8; i++) uc_reg_read(g_emu.uc, rids[i], &regs[i]);
                            LOGV2("hb", "  STUCK regs: eax=%08x ecx=%08x edx=%08x ebx=%08x esp=%08x ebp=%08x esi=%08x edi=%08x\n",
                                regs[0],regs[1],regs[2],regs[3],regs[4],regs[5],regs[6],regs[7]);
                            uint8_t stk[32];
                            if (uc_mem_read(g_emu.uc, esp, stk, 32) == UC_ERR_OK) {
                                p = 0;
                                for (int i = 0; i < 32; i++) p += snprintf(hex+p, sizeof(hex)-p, "%02x ", stk[i]);
                                LOGV2("hb", "  STUCK stack @esp: %s\n", hex);
                            }
                        }
                    }
                }
                LOGV2("hb", "  preempt=%u nproc=%u guards=%u/%u/%u tinit=%u tcyc=%u\n",
                    preempt, nproc, guard1, guard2, gate, tinit, tick_cycle);
                /* PIC state for diagnostics */
                LOGV2("hb", "  PIC0: IRR=0x%02x IMR=0x%02x ISR=0x%02x  PIC1: IRR=0x%02x IMR=0x%02x ISR=0x%02x\n",
                    g_emu.pic[0].irr, g_emu.pic[0].imr, g_emu.pic[0].isr,
                    g_emu.pic[1].irr, g_emu.pic[1].imr, g_emu.pic[1].isr);
                /* DM / DCS state — SWE1-V1.19 BSS layout. Gated on game_id
                 * so RFM heartbeats don't print garbage for these slots. */
                if (g_emu.game_id == 50069u) {
                    uint32_t dmm = RAM_RD32(0x2E8E2C);
                    uint32_t gxp = RAM_RD32(0x2E8B74);
                    /* DCS state addresses decoded from io-handled state
                     * machine at 0x195a16 (commit decoding the post-RESET
                     * flow). All in the 0x34a??? page, not 0x344???. */
                    uint32_t dcs_mode = RAM_RD32(0x34a714);
                    uint32_t dcs_state = RAM_RD32(0x34a54c);
                    uint32_t dcs_count = RAM_RD32(0x34a55c);
                    uint32_t ww,wr,bw,br,fr;
                    dcs_io_get_counters(&ww,&wr,&bw,&br,&fr);
                    LOGV2("hb", "  DM: mode=%u gxp=0x%x dt2=%u dcs_mode=%u dcs_st=%u dcs_cnt=%u io:ww=%u wr=%u bw=%u br=%u fr=%u\n",
                        dmm, gxp, g_emu.dc_timing2, dcs_mode, dcs_state, dcs_count,
                        ww, wr, bw, br, fr);
                    uint32_t dcs_send_gate = RAM_RD32(0x34a554);
                    uint32_t dcs_init_done = RAM_RD32(0x34a558);
                    uint32_t dcs_pump_flag = RAM_RD32(0x34a674);
                    LOGV2("hb", "  DCS-v19: send_gate=%u init_done=%u pump_flag=%u\n",
                        dcs_send_gate, dcs_init_done, dcs_pump_flag);
                    uint32_t q_wr = RAM_RD32(0x34a66c);
                    uint32_t q_rd = RAM_RD32(0x34a670);
                    LOGV2("hb", "  Q: out[wr=%u rd=%u]\n", q_wr, q_rd);
                } else {
                    uint32_t ww,wr,bw,br,fr;
                    dcs_io_get_counters(&ww,&wr,&bw,&br,&fr);
                    LOGV2("hb", "  DCS-io: ww=%u wr=%u bw=%u br=%u fr=%u\n",
                        ww, wr, bw, br, fr);
                }
                /* One-shot dump of guest DCS state — disabled */
                /* Performance stats */
                LOGV2("hb", "  PERF: calls=%lu/5s ok=%lu 0f3c=%lu hlt=%lu fired=%llu bar2wr=%u\n",
                    prof_emu_calls, prof_ok, prof_0f3c, prof_hlt,
                    (unsigned long long)s_sched.irq0_fired,
                    g_emu.bar2_wr_count);
                prof_emu_calls = prof_0f3c = prof_hlt = prof_other_err = prof_ok = 0;
                /* IRQ0 inject/EOI deltas are computed by the [irq] 5s
                 * health line above (s_sched.* monotonic counters). */
                /* One-shot process table dump.
                 * XINU layout: proctab=0x2FC8C4, stride=232(0xE8),
                 * pstate@+0, paddr@+0x28, pname@+0x30(16 chars) */
                static int proctab_dumped = 0;
                if (!proctab_dumped && nproc >= 35) {
                    proctab_dumped = 1;
                    uint32_t currpid = RAM_RD32(0x2FC8BCu);
                    LOGV2("hb", "  proctab: currpid=%u nproc=%u\n", currpid, nproc);
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
                        LOGV2("hb", "    pid%-3u %-4s fn=%08x '%s'\n",
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
