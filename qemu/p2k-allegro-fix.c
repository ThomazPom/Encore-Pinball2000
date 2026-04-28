/*
 * ============================================================================
 * STATUS: TEMPORARY SYMPTOM PATCH — replace with proper QEMU device behaviour.
 *
 * Why temporary: this module rewrites 4 bytes of the v2.10 game image's
 * `allegro_init` to NULL out a buggy "screen buffer pointer" store.
 * Modifying guest .text from the host is a band-aid; it relies on a
 * fixed instruction pattern. The real fix is to register a graphics
 * driver in the allegro driver list ([0x343e90]) so `set_gfx_mode(1, ...)`
 * succeeds and `gx_fb_init` never reaches the `allegro_exit` cleanup
 * branch in the first place.
 *
 * Removal condition: delete this file once a graphics driver is wired
 * (either by replicating the unicorn behaviour that registers one, or
 * by having QEMU's MediaGX device cause the driver list to populate
 * via the same constructor chain unicorn uses). Until then: kill
 * switch is P2K_NO_ALLEGRO_FIX.
 *
 * Background — without this patch, boot wedges at:
 *
 *   *** Fatal: free_resource(0x35ab28, 1): wrong type 0, xpid 0
 *
 * The chain (reverse-engineered from the v2.10 game ROM with capstone):
 *
 *   gx_fb_init(devsw*)                           @ 0x0019e6d4
 *     -> allegro_init                            @ 0x002aba20
 *           writes [0x343bb0] := 0x35ab48        @ 0x002aba82  (THE BUG)
 *           (0x35ab48 is _scratch_buffer in BSS — NOT malloc'd memory)
 *     -> set_color_depth(15)                                            OK
 *     -> set_gfx_mode(1, 640, 240, 0, 0)         @ 0x002abcf0
 *           scans driver list at [0x343e90]; list is empty in our boot
 *           returns -1
 *     -> kprintf("...allegro_error...")
 *     -> allegro_exit                            @ 0x002aba9c
 *           if ([0x343bb0] != 0): free([0x343bb0])
 *             -> free(0x35ab48)
 *             -> free_resource(0x35ab28, 1)      @ 0x0025b5a8
 *             -> Fatal — wrong type 0  (BSS dword is 0, requested type=1)
 *
 * By NULLing the immediate `0x35ab48` in allegro_init's store, the
 * "screen buffer pointer" stays 0; allegro_exit's `if (ptr) free(ptr)`
 * naturally skips the bogus free. No other code reads [0x343bb0] —
 * verified: only THREE references in the entire 2.27 MiB game image,
 * all inside allegro_init (the store) and allegro_exit (the test/free
 * pair). No allegro bitmap / surface code references it; allegro is
 * effectively dormant in this build, the symbol is allegedly ported
 * but unused at runtime.
 *
 * Reference: pure RE result, no unicorn equivalent needed.
 *
 * One concern per file: this module ONLY discovers and rewrites the
 * one buggy allegro_init store. No other patches.
 * ============================================================================
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "exec/cpu-common.h"

#include "p2k-internal.h"

#define P2K_AL_SCAN_BASE        0x00100000u
#define P2K_AL_SCAN_LEN         0x00300000u   /* 3 MiB game-code window */
#define P2K_AL_PERIOD_NS        (10 * 1000 * 1000)    /* 10 ms */
#define P2K_AL_PRIME_DELAY_NS   (50 * 1000 * 1000)    /* 50 ms */

/*
 * 10-byte fingerprint of the buggy store inside allegro_init:
 *
 *   C7 05 B0 3B 34 00     MOV DWORD [0x343bb0], imm32
 *   48 AB 35 00           imm32 = 0x0035AB48   ← we patch to 0x00000000
 *
 * Verified unique in the v2.10 game image at file off 0x1aba82
 * (virt 0x002aba82, allegro_init+0x62). The two operand addresses are
 * fully fixed, so we anchor on all 10 bytes.
 */
static const uint8_t AL_SIG[10] = {
    0xC7, 0x05, 0xB0, 0x3B, 0x34, 0x00, 0x48, 0xAB, 0x35, 0x00
};

static QEMUTimer *p2k_al_timer;
static bool       p2k_al_patched;

static void p2k_al_tick(void *opaque)
{
    bool text_patched = p2k_al_patched;

    /* (1) Clear the BSS dword [0x343bb0] if allegro_init has already run.
     * This neutralises the bogus pointer even if we missed the .text
     * patch window. allegro_exit will then naturally skip its free(). */
    {
        uint32_t cur = 0;
        cpu_physical_memory_read(0x00343bb0, &cur, 4);
        if (cur == 0x0035AB48u) {
            uint32_t zero = 0;
            cpu_physical_memory_write(0x00343bb0, &zero, 4);
            info_report("pinball2000: cleared bogus screen-buffer ptr "
                        "[0x343bb0] = 0x0035AB48 -> 0");
        }
    }

    if (text_patched) {
        /* Re-arm anyway: BSS may be re-written by another allegro_init. */
        timer_mod(p2k_al_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_AL_PERIOD_NS);
        return;
    }

    uint8_t *buf = g_malloc(P2K_AL_SCAN_LEN);
    cpu_physical_memory_read(P2K_AL_SCAN_BASE, buf, P2K_AL_SCAN_LEN);

    int hits = 0;
    for (uint32_t off = 0; off + sizeof(AL_SIG) <= P2K_AL_SCAN_LEN; off++) {
        if (memcmp(buf + off, AL_SIG, sizeof(AL_SIG)) != 0) {
            continue;
        }
        /* Replace the imm32 (last 4 bytes) with 0x00000000 so the
         * store becomes `MOV DWORD [0x343bb0], 0`. allegro_exit's
         * `if (ptr) free(ptr)` then naturally skips the bogus free. */
        uint32_t patch_addr = P2K_AL_SCAN_BASE + off + 6;
        const uint8_t patch[4] = { 0x00, 0x00, 0x00, 0x00 };
        cpu_physical_memory_write(patch_addr, patch, sizeof(patch));
        info_report("pinball2000: allegro screen-buffer store NULLed at "
                    "0x%08x (allegro_init+0x62): imm 0x0035AB48 -> 0",
                    patch_addr);
        p2k_al_patched = true;
        hits++;
    }

    g_free(buf);

    /* Always re-arm: even after .text patch, keep watching BSS. */
    timer_mod(p2k_al_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_AL_PERIOD_NS);
    if (hits > 1) {
        info_report("pinball2000: allegro fix found %d sites", hits);
    }
}

void p2k_install_allegro_fix(Pinball2000MachineState *s)
{
    (void)s;
    /*
     * OPT-IN ONLY. This module is a diagnostic bridge for use while
     * investigating why the allegro graphics driver list at [0x343e90]
     * is empty under QEMU. It rewrites guest .text and periodically
     * scrubs guest BSS, which is exactly the kind of host-side surgery
     * we are trying to eliminate. The real fix is to make some QEMU
     * device behavior cause the driver list to populate naturally
     * (matching the original hardware / unicorn path). Enable only
     * with P2K_ALLEGRO_FIX=1 for triage of downstream Fatals.
     */
    if (!getenv("P2K_ALLEGRO_FIX")) {
        info_report("pinball2000: allegro_init bug fix DISABLED "
                    "(opt-in: set P2K_ALLEGRO_FIX=1 for diagnostic bridge)");
        return;
    }
    p2k_al_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, p2k_al_tick, NULL);
    timer_mod(p2k_al_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_AL_PRIME_DELAY_NS);
    info_report("pinball2000: allegro_init bug fix armed");
}
