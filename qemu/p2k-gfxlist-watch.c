/*
 * pinball2000 _gfx_driver_list watcher (DIAGNOSTIC, opt-in).
 *
 * Purpose: investigate why the v2.10 game's pre-initialised
 * `_gfx_driver_list` array at 0x343e8c reads as zero at runtime,
 * causing set_gfx_mode(1, ...) to return -1 and gx_fb_init to take
 * the allegro_exit cleanup branch (which then triggers the
 * free_resource Fatal — see p2k-allegro-fix.c).
 *
 * In the v2.10 image .data segment, the array is initialised to:
 *
 *   _gfx_driver_list[0] = { id=1, ptr=0x343e38 (gfx_mediagx), modes=1 }
 *   _gfx_driver_list[1] = { id=0, ptr=0,          0 }   (terminator)
 *
 * If at runtime we see all-zero there, the .data segment did not
 * land in RAM; if we see the expected pattern, set_gfx_mode is
 * looking at the right place but the search is somehow not matching.
 *
 * Enable with P2K_GFXLIST_WATCH=1.  Logs the 24-byte array contents
 * on every tick (default 100 ms) and prints transitions only.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "exec/cpu-common.h"

#include "p2k-internal.h"

#define P2K_GW_PERIOD_NS    (100 * 1000 * 1000)
#define P2K_GW_PRIME_NS     (50  * 1000 * 1000)
#define P2K_GW_LIST         0x00343e8cu
#define P2K_GW_DRIVER       0x00343e38u
#define P2K_GW_GFX_DRIVER   0x00343788u
#define P2K_GW_SCREEN_PTR   0x0034378cu
#define P2K_GW_SCRBUF_PTR   0x00343bb0u

static QEMUTimer *p2k_gw_timer;
static uint8_t    p2k_gw_last_list[24];
static uint32_t   p2k_gw_last_gfxdrv;
static uint32_t   p2k_gw_last_screen;
static uint32_t   p2k_gw_last_scrbuf;
static int        p2k_gw_ticks;

static void p2k_gw_tick(void *opaque)
{
    uint8_t list[24];
    uint32_t gfxdrv = 0, screen = 0, scrbuf = 0;
    cpu_physical_memory_read(P2K_GW_LIST, list, sizeof(list));
    cpu_physical_memory_read(P2K_GW_GFX_DRIVER, &gfxdrv, 4);
    cpu_physical_memory_read(P2K_GW_SCREEN_PTR, &screen, 4);
    cpu_physical_memory_read(P2K_GW_SCRBUF_PTR, &scrbuf, 4);

    bool list_changed = memcmp(list, p2k_gw_last_list, sizeof(list)) != 0;
    bool ptrs_changed = (gfxdrv != p2k_gw_last_gfxdrv)
                     || (screen != p2k_gw_last_screen)
                     || (scrbuf != p2k_gw_last_scrbuf);
    if (p2k_gw_ticks == 0 || list_changed || ptrs_changed) {
        info_report("p2k-gfxlist: tick=%d list[0]={id=0x%08x ptr=0x%08x m=%u} "
                    "list[1]={id=0x%08x ptr=0x%08x m=%u}",
                    p2k_gw_ticks,
                    *(uint32_t*)(list + 0), *(uint32_t*)(list + 4),
                    *(uint32_t*)(list + 8),
                    *(uint32_t*)(list + 12), *(uint32_t*)(list + 16),
                    *(uint32_t*)(list + 20));
        info_report("p2k-gfxlist: gfx_driver(0x343788)=0x%08x "
                    "screen(0x34378c)=0x%08x scrbuf(0x343bb0)=0x%08x",
                    gfxdrv, screen, scrbuf);
        memcpy(p2k_gw_last_list, list, sizeof(list));
        p2k_gw_last_gfxdrv = gfxdrv;
        p2k_gw_last_screen = screen;
        p2k_gw_last_scrbuf = scrbuf;
    }
    p2k_gw_ticks++;
    timer_mod(p2k_gw_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_GW_PERIOD_NS);
}

void p2k_install_gfxlist_watch(Pinball2000MachineState *s)
{
    (void)s;
    if (!getenv("P2K_GFXLIST_WATCH")) {
        return;
    }
    p2k_gw_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, p2k_gw_tick, NULL);
    timer_mod(p2k_gw_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_GW_PRIME_NS);
    info_report("pinball2000: P2K_GFXLIST_WATCH armed (samples 0x343e8c)");
}
