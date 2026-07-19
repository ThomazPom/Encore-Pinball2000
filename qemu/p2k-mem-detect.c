/*
 * Optional XINU memory-size override.
 *
 * Supported game images contain a small sizmem() function whose body is:
 *
 *     55 89 E5 B8 00 04 00 00 C9 C3
 *     push ebp; mov ebp,esp; mov eax,0x400; leave; ret
 *
 * XINU consequently sets its memory ceiling to 4 MiB.  That is the native
 * behavior and is Encore's default.  Current SWE1 update boots, including
 * 2.10, have been observed reaching the running game without changing it.
 *
 * P2K_MEM_DETECT_PATCH=1 enables a retained compatibility experiment.  It
 * changes the immediate to 0xE00, giving XINU a 14 MiB ceiling.  The scanner
 * is bounded because the update loader first relocates the function into RAM:
 * it checks the game-code window every 10 ms for at most one second, changes
 * one signature-matched byte, then retires permanently.
 *
 * Keep this opt-in until the update matrix has had enough unpatched field
 * use to show whether any older combination genuinely needs the larger heap.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "exec/cpu-common.h"

#include "p2k-internal.h"

#define P2K_MD_SCAN_BASE        0x00100000u
#define P2K_MD_SCAN_LEN         0x00500000u
#define P2K_MD_PERIOD_NS        (10 * 1000 * 1000)
#define P2K_MD_MAX_ATTEMPTS     100

static const uint8_t p2k_md_pattern[] = {
    0x55, 0x89, 0xE5, 0xB8, 0x00, 0x04, 0x00, 0x00, 0xC9, 0xC3,
};

static QEMUTimer *p2k_md_timer;
static bool p2k_md_patched;
static unsigned p2k_md_attempts;

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
        uint8_t newval = 0x0E;
        cpu_physical_memory_write(patch_addr, &newval, 1);
        info_report("pinball2000: optional sizmem override applied at "
                    "0x%08x: 4 MiB -> 14 MiB", patch_addr);
        p2k_md_patched = true;
        break;
    }

    g_free(buf);

    if (!p2k_md_patched && p2k_md_attempts < P2K_MD_MAX_ATTEMPTS) {
        timer_mod(p2k_md_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_MD_PERIOD_NS);
        return;
    }

    if (!p2k_md_patched) {
        info_report("pinball2000: optional sizmem signature absent after "
                    "%u scans -- override retired", p2k_md_attempts);
    }
    timer_free(p2k_md_timer);
    p2k_md_timer = NULL;
}

void p2k_install_mem_detect(void)
{
    const char *enabled = getenv("P2K_MEM_DETECT_PATCH");

    if (!enabled || strcmp(enabled, "1") != 0) {
        return;
    }

    p2k_md_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, p2k_md_tick, NULL);
    timer_mod(p2k_md_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
    info_report("pinball2000: optional 4 MiB -> 14 MiB sizmem override armed");
}
