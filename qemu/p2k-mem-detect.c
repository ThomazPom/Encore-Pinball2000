/*
 * pinball2000 BT-130: XINU mem_detect() patch.
 *
 * XINU's mem_detect() probes the MediaGX memory controller and returns
 * the system RAM size in 4 KiB pages.  In our QEMU machine the
 * controller is a stub, so the probe returns 0x400 pages (4 MiB) by
 * default, which is far too small — prnull's stack overflows during
 * cpp_call_ctors(), Location Mgr cannot allocate, and the game enters
 * an unrecoverable "Resource X cannot construct, Location Mgr is not
 * there yet" cascade with malloc(96) failing for everything.
 *
 * Real Pinball 2000 hardware reports 14 MiB; we have 16 MiB available
 * but cap at 14 MiB to match the unicorn reference.  The patch is the
 * exact byte-rewrite from unicorn.old/src/io.c:463-500 (BT-130):
 *
 *     pattern  : 55 89 E5 B8 00 04 00 00 C9 C3
 *                ↑PUSH EBP
 *                   ↑MOV EBP,ESP
 *                      ↑MOV EAX,0x400 (4 MiB pages, low byte at +5)
 *                                  ↑LEAVE  ↑RET
 *     patch    : byte at +5 := 0x0E   →  MOV EAX,0xE00  →  14 MiB pages
 *
 * Pattern is ROM-agnostic (same across SWE1 v1.5 and RFM v1.6 per
 * unicorn).  We scan loaded game code in low RAM periodically until
 * we find + patch it, then disarm the timer.
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
#define P2K_MD_PERIOD_NS        (250 * 1000 * 1000)   /* 250 ms re-scan */
#define P2K_MD_PRIME_DELAY_NS   (300 * 1000 * 1000)   /* 300 ms post-boot */

static const uint8_t p2k_md_pattern[] = {
    0x55, 0x89, 0xE5, 0xB8, 0x00, 0x04, 0x00, 0x00, 0xC9, 0xC3,
};

static QEMUTimer *p2k_md_timer;
static bool       p2k_md_patched;

static void p2k_md_tick(void *opaque)
{
    if (p2k_md_patched) {
        return;
    }

    uint8_t *buf = g_malloc(P2K_MD_SCAN_LEN);
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

    if (!p2k_md_patched) {
        timer_mod(p2k_md_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_MD_PERIOD_NS);
    }
}

void p2k_install_mem_detect(void)
{
    if (getenv("P2K_NO_MEM_DETECT_PATCH")) {
        info_report("pinball2000: BT-130 mem_detect patch DISABLED via env");
        return;
    }
    p2k_md_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, p2k_md_tick, NULL);
    timer_mod(p2k_md_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_MD_PRIME_DELAY_NS);
    info_report("pinball2000: BT-130 mem_detect patcher armed");
}
