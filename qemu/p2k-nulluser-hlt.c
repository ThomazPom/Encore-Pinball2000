/*
 * ============================================================================
 * STATUS: TEMPORARY SYMPTOM PATCH — replace with proper QEMU device behaviour.
 *
 * Why temporary: this module rewrites 3 bytes inside the guest's own
 * `nulluser()` idle loop so the prnull process executes HLT instead of
 * busy-spinning a `JMP $`. Modifying guest .text from the host is a
 * band-aid; it relies on a fixed instruction pattern and breaks
 * silently if the build changes. The real fix is to figure out why
 * the upstream chain (CMOS test fail → Game_ptr never set → duplicate
 * resource ID → Fatal → trap → reschedule to prnull) ends in prnull
 * being the only runnable process. Once that chain is fixed in the
 * device layer, prnull is no longer the indefinite resident, and the
 * busy-spin shape of nulluser stops mattering.
 *
 * Removal condition: delete this file once a clean v2.10 boot reaches
 * attract / menu_init without ever being permanently parked in
 * prnull. Until then: kill switch is P2K_NO_NULLUSER_HLT.
 *
 * Background — without this patch, the boot wedges at:
 *
 *   *** Fatal: free_resource(0x35ab28, 1): wrong type 0, xpid 0
 *   ... XINA monitor, exception 13 from prnull, infinite re-loop ...
 *
 * because nulluser() ends with `MOV [rdy],1; NOP; JMP $` (`90 EB FE`)
 * which never yields to the QEMU host, so the PIT IRQ that drives
 * clkint can't preempt. Patching to `HLT; JMP -3` (`F4 EB FD`) lets
 * QEMU exit the TCG block on HLT, the PIT delivers, clkint runs, and
 * the scheduler can switch to higher-priority processes (menu_init,
 * attract, ...) — which is exactly the behaviour observed in unicorn,
 * where the same Fatal print happens but the game keeps making
 * forward progress to menu_init.
 *
 * Reference: unicorn.old/src/io.c:523-562 (BT-74).
 *
 * One concern per file: this module ONLY discovers and rewrites the
 * nulluser idle loop. No other patches.
 * ============================================================================
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "exec/cpu-common.h"

#include "p2k-internal.h"

#define P2K_NU_SCAN_BASE        0x00100000u
#define P2K_NU_SCAN_LEN         0x00300000u   /* 3 MiB game-code window */
#define P2K_NU_PERIOD_NS        (250 * 1000 * 1000)
#define P2K_NU_PRIME_DELAY_NS   (300 * 1000 * 1000)

/*
 * 17-byte fingerprint that anchors nulluser's idle tail across builds:
 *
 *   75 B1                    JNZ -0x4F        (end of ctor walk loop)
 *   C7 05 ?? ?? ?? ??        MOV DWORD [imm32], imm32    (ready flag = 1)
 *   01 00 00 00
 *   90                       NOP
 *   EB FE                    JMP $            ← we patch here
 *
 * Wildcard offsets: 4..7 (imm32 address) — matched as "any byte".
 */
static bool nu_match(const uint8_t *p)
{
    return p[0]  == 0x75 && p[1]  == 0xB1
        && p[2]  == 0xC7 && p[3]  == 0x05
        /* p[4..7] = imm32 address — wildcard */
        && p[8]  == 0x01 && p[9]  == 0x00 && p[10] == 0x00 && p[11] == 0x00
        && p[12] == 0x90 && p[13] == 0xEB && p[14] == 0xFE;
}

static QEMUTimer *p2k_nu_timer;
static bool       p2k_nu_patched;

static void p2k_nu_tick(void *opaque)
{
    if (p2k_nu_patched) {
        return;
    }

    uint8_t *buf = g_malloc(P2K_NU_SCAN_LEN);
    cpu_physical_memory_read(P2K_NU_SCAN_BASE, buf, P2K_NU_SCAN_LEN);

    int hits = 0;
    for (uint32_t off = 0; off + 17 <= P2K_NU_SCAN_LEN; off++) {
        if (!nu_match(buf + off)) {
            continue;
        }
        /* Patch the trailing `90 EB FE` (NOP; JMP $) to `F4 EB FD`
         * (HLT; JMP -3). HLT exits the TCG block so QEMU can deliver
         * the next PIT IRQ; on IRET we land on the JMP, which loops
         * back to the HLT. */
        uint32_t patch_addr = P2K_NU_SCAN_BASE + off + 12;
        const uint8_t patch[3] = { 0xF4, 0xEB, 0xFD };
        cpu_physical_memory_write(patch_addr, patch, sizeof(patch));
        info_report("pinball2000: BT-74 nulluser idle patched at "
                    "0x%08x: 90 EB FE -> F4 EB FD (NOP;JMP$ -> HLT;JMP-3)",
                    patch_addr);
        p2k_nu_patched = true;
        hits++;
        /* Keep scanning — there may be a second copy in update vs. game
         * image (unicorn observed up to 2 hits). */
    }

    g_free(buf);

    if (!p2k_nu_patched) {
        timer_mod(p2k_nu_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_NU_PERIOD_NS);
    } else if (hits > 1) {
        info_report("pinball2000: BT-74 found %d nulluser sites", hits);
    }
}

void p2k_install_nulluser_hlt(Pinball2000MachineState *s)
{
    (void)s;
    if (getenv("P2K_NO_NULLUSER_HLT")) {
        info_report("pinball2000: BT-74 nulluser HLT patch DISABLED via env");
        return;
    }
    p2k_nu_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, p2k_nu_tick, NULL);
    timer_mod(p2k_nu_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_NU_PRIME_DELAY_NS);
    info_report("pinball2000: BT-74 nulluser HLT patcher armed");
}
