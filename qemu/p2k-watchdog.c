/*
 * pinball2000 watchdog flag scribbler.
 *
 * The Pinball 2000 game code embeds a `pci_watchdog_bone()` style
 * health register inside its DCS-probe call:
 *
 *     CMP DWORD [<addr>], 0xFFFF
 *     JE  RET-0
 *     MOV EAX, 1
 *
 * meaning "DCS PRESENT when [addr] != 0xFFFF".  But there are also
 * sibling CMPs in genuine watchdog code paths that REQUIRE the cell
 * to remain 0xFFFF — they treat any other value as "watchdog timer
 * has expired, reset the system".  When IRQ0 is firing but no real
 * pci_watchdog_bone() process is running, those checks fail
 * silently and XINU's sysinit() never finishes.
 *
 * Strategy (mirrors unicorn.old/src/io.c:380-460 and cpu.c:766-800):
 *
 *   1. At startup, scan the relocated game code (0x100000.. 0x600000)
 *      for the byte pattern  81 3D <ADDR32> FF FF 00 00  i.e. CMP
 *      DWORD [imm32], 0xFFFF where imm32 is in the BSS range
 *      (0x100000..0x1000000).
 *   2. Collect the unique target cells (typically 4-8 of them, all
 *      in a small struct).
 *   3. Periodically write 0xFFFF to each of those cells from a QEMU
 *      timer.  This keeps the watchdog-expiry checks happy.
 *
 * One-time prime + 1 ms cadence is fine — any code path that races
 * with the scribble would have already exhibited issues on real
 * hardware.
 *
 * No Unicorn timing/vticks/budget heuristics are involved.  This is
 * a pure I/O cadence concern.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "exec/cpu-common.h"

#include "p2k-internal.h"

#define P2K_WD_SCAN_BASE        0x00100000u
#define P2K_WD_SCAN_LEN         0x00500000u
#define P2K_WD_BSS_LO           0x00100000u
#define P2K_WD_BSS_HI           0x01000000u
#define P2K_WD_MAX_CELLS        16
#define P2K_WD_PERIOD_NS        (1 * 1000 * 1000)   /* 1 ms */
#define P2K_WD_RESCAN_NS        (500 * 1000 * 1000) /* 500 ms while empty */
#define P2K_WD_PRIME_DELAY_NS   (500 * 1000 * 1000) /* 500 ms after boot */

static QEMUTimer *p2k_wd_timer;
static uint32_t   p2k_wd_cells[P2K_WD_MAX_CELLS];
static int        p2k_wd_n_cells;
static bool       p2k_wd_scanned;

static void p2k_wd_write_cell(uint32_t addr, uint32_t v)
{
    uint8_t le[4] = {
        (uint8_t)(v & 0xff),
        (uint8_t)((v >> 8) & 0xff),
        (uint8_t)((v >> 16) & 0xff),
        (uint8_t)((v >> 24) & 0xff),
    };
    cpu_physical_memory_write(addr, le, 4);
}

static uint32_t p2k_wd_read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void p2k_wd_scan_once(void)
{
    if (p2k_wd_scanned) {
        return;
    }

    uint8_t *buf = g_malloc(P2K_WD_SCAN_LEN);
    cpu_physical_memory_read(P2K_WD_SCAN_BASE, buf, P2K_WD_SCAN_LEN);

    for (uint32_t off = 0;
         off + 10 <= P2K_WD_SCAN_LEN && p2k_wd_n_cells < P2K_WD_MAX_CELLS;
         off++) {
        uint8_t *p = buf + off;
        if (p[0] != 0x81 || p[1] != 0x3D) {
            continue;
        }
        if (p[6] != 0xFF || p[7] != 0xFF || p[8] != 0x00 || p[9] != 0x00) {
            continue;
        }
        uint32_t cell = p2k_wd_read_le32(p + 2);
        if (cell < P2K_WD_BSS_LO || cell >= P2K_WD_BSS_HI) {
            continue;
        }
        bool seen = false;
        for (int i = 0; i < p2k_wd_n_cells; i++) {
            if (p2k_wd_cells[i] == cell) {
                seen = true;
                break;
            }
        }
        if (seen) {
            continue;
        }
        p2k_wd_cells[p2k_wd_n_cells++] = cell;
        info_report("pinball2000: watchdog cell discovered: "
                    "CMP [0x%08x],0xFFFF at game+0x%08x",
                    cell, P2K_WD_SCAN_BASE + off);
    }

    g_free(buf);

    if (p2k_wd_n_cells > 0) {
        p2k_wd_scanned = true;
        for (int i = 0; i < p2k_wd_n_cells; i++) {
            p2k_wd_write_cell(p2k_wd_cells[i], 0x0000FFFFu);
        }
        info_report("pinball2000: watchdog suppression active "
                    "(%d cells primed = 0xFFFF)", p2k_wd_n_cells);
    }
}

static void p2k_wd_tick(void *opaque)
{
    if (!p2k_wd_scanned) {
        p2k_wd_scan_once();
        if (!p2k_wd_scanned) {
            /* Game code not yet relocated — retry later. */
            timer_mod(p2k_wd_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                      P2K_WD_RESCAN_NS);
            return;
        }
    }
    for (int i = 0; i < p2k_wd_n_cells; i++) {
        p2k_wd_write_cell(p2k_wd_cells[i], 0x0000FFFFu);
    }
    timer_mod(p2k_wd_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_WD_PERIOD_NS);
}

void p2k_install_watchdog(void)
{
    p2k_wd_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, p2k_wd_tick, NULL);
    timer_mod(p2k_wd_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + P2K_WD_PRIME_DELAY_NS);
    info_report("pinball2000: watchdog scribbler armed "
                "(scan after %d ms, then %d ms cadence)",
                (int)(P2K_WD_PRIME_DELAY_NS / 1000000),
                (int)(P2K_WD_PERIOD_NS / 1000000));
}
