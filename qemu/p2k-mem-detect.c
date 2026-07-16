/*
 * ============================================================================
 * STATUS: ROM COMPATIBILITY LAYER (not an Encore emulation defect).
 *
 * Category: ROM-compat, not emulation-defect. See docs/30-symptom-patches.md
 * "Two different kinds of symptom patch" for the distinction. Encore's
 * emulation is correct — the guest sees 16 MiB of installed RAM, exactly
 * like a real cabinet. The problem is that a specific ROM release
 * (hobbyist swe1 v2.10, dated 2025-10-31, plus the pre-2.00 v1.x
 * updates and rfm v2.60) hard-codes `mov eax, 0x400` in mem_detect(),
 * so XINU tells itself it only has 4 MiB. Williams' shipping v2.00
 * hard-codes `mov eax, 0x1000` (16 MiB) and needs no host intervention.
 *
 * Per-ROM signature scan across every update bundle we ship:
 *
 *   ROM version      | mov eax literal | Needs patch?
 *   swe1 v2.00 (H)   | 0x1000 = 16 MiB | NO -- self-retires as no-op
 *   swe1 v2.10       | 0x400  =  4 MiB | YES -- patched to 0xE00
 *   swe1 v1.30/40/50 | 0x400           | YES
 *   rfm v2.60        | 0x400           | YES
 *
 * The v2.10 hobbyist community rebuild regressed Williams' original
 * fix; likely from an older source tree that predated the 16-MiB
 * constant landing. Since we ship v2.10 as the default update and
 * users can't be expected to know about the regression, we run the
 * scanner by default and let it be a no-op on v2.00 (default OFF
 * with P2K_MEM_DETECT_PATCH=0).
 *
 * Without the patch on v2.10 the guest Fatals at ~15 s with
 *     *** Fatal: malloc(131072): getmem(131104) failed
 *     in alp shld / price_init.
 *
 * Maintenance contract: accepted permanent firmware compatibility. Keep the
 * exact signature and one-byte scope. Genuine v2.00 naturally no-ops; affected
 * releases receive the established 14-MiB value. Do not broaden the matcher.
 * ============================================================================
 *
 * Historical rationale (the low-cost / high-replacement-cost trade-off):
 *
 *   - The patch is one byte at a single, signature-matched site, with a
 *     bounded 10 ms relocation scanner that disarms after the first hit or
 *     after one second. Once
 *     applied, this file consumes zero cycles for the rest of the run.
 *   - A CS5530 model cannot change this hard-coded guest constant. The bounded
 *     compatibility rewrite is therefore retained.
 *
 * Why not device-register: in this XINU build mem_detect() is compiled
 * as a hardcoded
 *     mov eax, 0x400 ; leave ; ret
 * It does NOT probe any MediaGX/CS5530 memory-controller register —
 * there is nothing for a more accurate device model to answer. The
 * function literally returns the constant 4 MiB.
 *
 * Why not ROM-load time (one-shot, no timer):
 * the boot loader copies/relocates the .text image from BAR3 flash
 * (and/or bank0) into RAM at a different offset (~0x00233425 in tests),
 * and the running copy diverges from both ROM sources. Patching the
 * source images before relocation does not survive the copy. Verified
 * 2026-04 by attempting bank0 + bar3-flash one-shot rewrite: the byte
 * was patched at flash+0x18e5e8 / bank0+0x1739f8, but the runtime image
 * at 0x00233425 was still 0x04, mem_detect still returned 4 MiB, and
 * the boot regressed to no-audio.
 *
 * So we have to scan RAM at runtime and patch the live function copy.
 * That is what this module does, starting immediately so mem_detect() cannot
 * initialize the heap from the stale 4 MiB value before the rewrite lands.
 * ============================================================================
 *
 * pinball2000 BT-130: XINU mem_detect() patch.
 *
 * Real Pinball 2000 hardware reports 14 MiB; we have 16 MiB available
 * but cap at 14 MiB to match the reference.  The patch is the exact
 *
 *     pattern  : 55 89 E5 B8 00 04 00 00 C9 C3
 *                ↑PUSH EBP
 *                   ↑MOV EBP,ESP
 *                      ↑MOV EAX,0x400 (4 MiB pages, low byte at +5)
 *                                  ↑LEAVE  ↑RET
 *     patch    : byte at +5 := 0x0E   →  MOV EAX,0xE00  →  14 MiB pages
 *
 * Pattern is ROM-agnostic (same across SWE1 v1.5 and RFM v1.6 per the
 * we find + patch it, then disarm and free the timer (no perpetual
 * 250 ms wakeup leak).
 *
 * One concern per file: this module ONLY discovers and rewrites the
 * mem_detect prologue.  No other patches.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "exec/cpu-common.h"

#include "p2k-internal.h"

#define P2K_MD_SCAN_BASE        0x00100000u
#define P2K_MD_SCAN_LEN         0x00500000u   /* 5 MiB game-code window */
#define P2K_MD_PERIOD_NS        (10 * 1000 * 1000)    /* 10 ms until relocation */
#define P2K_MD_PRIME_DELAY_NS   0                     /* scan before first call */
#define P2K_MD_MAX_ATTEMPTS     100                   /* one-second scan window */

static const uint8_t p2k_md_pattern[] = {
    0x55, 0x89, 0xE5, 0xB8, 0x00, 0x04, 0x00, 0x00, 0xC9, 0xC3,
};

static QEMUTimer *p2k_md_timer;
static bool       p2k_md_patched;
static unsigned   p2k_md_attempts;

static void p2k_md_tick(void *opaque)
{
    if (p2k_md_patched) {
        return;
    }

    uint8_t *buf = g_malloc(P2K_MD_SCAN_LEN);
    p2k_md_attempts++;
    cpu_physical_memory_read(P2K_MD_SCAN_BASE, buf, P2K_MD_SCAN_LEN);

    const uint32_t pat_len = sizeof(p2k_md_pattern);
    for (uint32_t off = 0; off + pat_len <= P2K_MD_SCAN_LEN; off++) {
        if (memcmp(buf + off, p2k_md_pattern, pat_len) != 0) {
            continue;
        }
        uint32_t patch_addr = P2K_MD_SCAN_BASE + off + 5;
        uint8_t  newval = 0x0E;
        cpu_physical_memory_write(patch_addr, &newval, 1);
        info_report("pinball2000: BT-130 mem_detect patch applied at "
                    "0x%08x: 0x04 -> 0x0E (4 MiB -> 14 MiB pages)",
                    patch_addr);
        p2k_md_patched = true;
        break;
    }

    g_free(buf);

    if (!p2k_md_patched && p2k_md_attempts < P2K_MD_MAX_ATTEMPTS) {
        timer_mod(p2k_md_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_MD_PERIOD_NS);
    } else {
        if (!p2k_md_patched) {
            info_report("pinball2000: BT-130 mem_detect signature absent "
                        "after %u scans -- patcher retired",
                        p2k_md_attempts);
        }
        timer_free(p2k_md_timer);
        p2k_md_timer = NULL;
    }
}

void p2k_install_mem_detect(void)
{
    /* mem_detect mempatch is ON by default. Without it, the guest
     * XINU allocator sees 4 MiB total (mem_detect() returns 0x400)
     * and Fatals in `alp shld` (malloc 131072 → getmem 131104 failed)
     * ~15 s into boot. Patching mem_detect to return 0xE00 (14 MiB)
     * gives the allocator enough headroom to service the shell
     * bring-up allocations.
     *
     * Fatal — likely because it exposes its full 16 MiB backing
     * RAM to the guest via a different memory-controller model, so
     * XINU's mem_detect() returns the correct size natively. When
     * The compatibility rewrite is accepted. Diagnostic kill switch:
     * P2K_MEM_DETECT_PATCH=0. */
    const char *v = getenv("P2K_MEM_DETECT_PATCH");
    if (v && v[0] == '0') {
        info_report("pinball2000: BT-130 mem_detect patch DISABLED via "
                    "P2K_MEM_DETECT_PATCH=0 -- expect malloc Fatal in "
                    "~15 s (alp shld getmem 131104 failed)");
        return;
    }
    p2k_md_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, p2k_md_tick, NULL);
    timer_mod(p2k_md_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_MD_PRIME_DELAY_NS);
    info_report("pinball2000: BT-130 mem_detect patcher armed");
}
