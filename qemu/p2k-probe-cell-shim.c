/*
 * p2k-probe-cell-shim.c — STRICTLY GATED Unicorn-style probe-cell scribble.
 *
 * Goal of this file: keep --update none (BASE-image / no-update-flash)
 * boot of SWE1 reaching its v0.40 attract path, matching what Unicorn
 * does today (unicorn.old/src/io.c::apply_sgc_patches +
 * unicorn.old/src/cpu.c:766-801 per-tick maintenance).
 *
 * Strict gating — by design:
 *
 *   - Activates ONLY when env P2K_NO_AUTO_UPDATE is set
 *     (which scripts/run-qemu.sh exports for `--update none`).
 *   - Silent / inert on normal update boots: no RAM scribble, no scan,
 *     not even a timer armed. The caller checks the gate first.
 *   - One-line install banner explicitly says "compatibility shim"
 *     so it cannot be confused with clean device behavior.
 *
 * What it does (Unicorn-equivalent, narrowed to the no-update case):
 *
 *   1. After XINU has loaded its game image into low RAM, scan
 *      [0x100000 .. 0x400000) once for the literal string
 *           "pci_watchdog_bone(): the watchdog has expired"
 *      Then walk the surrounding code to locate the dword cell that
 *      the dcs_probe() inside the watchdog callee compares against
 *      0x0000FFFF (encoded as `81 3D <addr32> FF FF 00 00`).
 *      Same algorithm as unicorn.old/src/io.c:248-422.
 *
 *   2. Once the cell address is known, write 0xFFFF to it on a slow
 *      virtual-time tick (~50 ms — same order of magnitude as
 *      Unicorn's per-emu-slice RAM_WR32). This makes dcs_probe()
 *      return "DCS PRESENT" so pci_watchdog_bone() does NOT declare
 *      the cabinet's watchdog expired before XINU has a chance to
 *      install its own clkint and start servicing the real PCI/DCS
 *      health register.
 *
 * Why this is a guest-data patch (and acknowledged as such):
 *
 *   The cell we scribble is a piece of the game's BSS that the real
 *   PCI watchdog/DCS device (which we do not yet model with full
 *   fidelity) would update on its own. Until we replace this with
 *   correct DCS-probe / PLX BAR4 device behavior, the scribble is
 *   the cheapest way to keep the no-update boot from declaring a
 *   false-alarm Fatal in the first hundreds of ms.
 *
 * Removal condition (the contract):
 *
 *   When QEMU's DCS / PLX9054 model returns the right value at the
 *   probe address natively, this file can be deleted and the
 *   Pinball2000MachineState init no longer needs to call
 *   p2k_install_probe_cell_shim().
 *
 * Cross-references:
 *
 *   - unicorn.old/src/io.c:248-562   apply_sgc_patches() — original
 *     scanner (also includes mem_detect, prnull, BT-74; we already
 *     own those via p2k-mem-detect.c and the deleted shims).
 *   - unicorn.old/src/cpu.c:766-801  per-tick scribble loop.
 *   - qemu/p2k-bar3-flash.c          where P2K_NO_AUTO_UPDATE is honored
 *     for the BAR3 init half of the same parity story.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h"
#include "exec/cpu-common.h"

#include "p2k-internal.h"

#define SCAN_BASE        0x00100000u
#define SCAN_SIZE        0x00300000u   /* 3 MiB — wide enough for SWE1/RFM */
#define POLL_NS          (50ull * 1000ull * 1000ull) /* 50 ms */
#define SCRIBBLE_VAL     0x0000FFFFu

static QEMUTimer *s_timer;
static uint32_t   s_probe_cell;       /* guest phys addr; 0 = not found yet */
static int        s_scan_attempts;    /* bound the work we do */
static bool       s_logged_hit;

/* Scan a contiguous live RAM window for the watchdog string and walk
 * back to the CMP [<probe_cell>], 0xFFFF inside dcs_probe().
 *
 * Returns the guest phys address of the probe cell, or 0 if not found
 * yet (caller will retry on the next tick — game code is copied to
 * low RAM somewhat after STARTING GAME CODE prints).
 *
 * Algorithm mirrors unicorn.old/src/io.c:248-422 step-for-step. */
static uint32_t scan_for_probe_cell(void)
{
    uint8_t *buf = g_malloc(SCAN_SIZE);
    cpu_physical_memory_read(SCAN_BASE, buf, SCAN_SIZE);

    static const char needle[] =
        "pci_watchdog_bone(): the watchdog has expired";
    const size_t nlen = sizeof(needle) - 1;

    /* 1. find the error string in live RAM */
    uint32_t str_off = 0;
    bool found = false;
    for (uint32_t off = 0; off + nlen < SCAN_SIZE; off++) {
        if (memcmp(buf + off, needle, nlen) == 0) {
            str_off = off;
            found = true;
            break;
        }
    }
    if (!found) { g_free(buf); return 0; }
    uint32_t str_addr = SCAN_BASE + str_off;

    /* 2. find PUSH imm32 == str_addr (opcode 0x68) within ±4 KiB */
    uint32_t push_off = 0;
    found = false;
    uint32_t ps = (str_off > 0x1000) ? str_off - 0x1000 : 0;
    uint32_t pe = str_off + 0x1000;
    if (pe > SCAN_SIZE - 5) pe = SCAN_SIZE - 5;
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
    if (!found) { g_free(buf); return 0; }

    /* 3. find the LAST CALL rel32 (E8 ..) within 200 bytes before the PUSH */
    uint32_t call_off = 0;
    bool call_found = false;
    uint32_t cb = (push_off > 200) ? push_off - 200 : 0;
    for (uint32_t off = cb; off + 5 <= push_off; off++) {
        if (buf[off] == 0xE8) {
            int32_t rel;
            memcpy(&rel, buf + off + 1, 4);
            int64_t target = (int64_t)(SCAN_BASE + off + 5) + rel;
            if (target >= SCAN_BASE &&
                target < (int64_t)(SCAN_BASE + SCAN_SIZE)) {
                call_off = off;
                call_found = true;
            }
        }
    }
    if (!call_found) { g_free(buf); return 0; }

    int32_t call_rel;
    memcpy(&call_rel, buf + call_off + 1, 4);
    uint32_t callee_guest =
        (uint32_t)((int64_t)(SCAN_BASE + call_off + 5) + call_rel);
    uint32_t callee_off = callee_guest - SCAN_BASE;

    /* 4. in the callee body, follow the first nested CALL up to 32 B in,
     *    then look in either the direct callee or the nested callee for
     *    `81 3D <addr32> FF FF 00 00` — CMP dword [addr32], 0x0000FFFF */
    uint32_t starts[2] = { callee_off, 0 };
    int n_starts = 1;
    uint32_t ce0 = callee_off + 32;
    if (ce0 > SCAN_SIZE - 5) ce0 = SCAN_SIZE - 5;
    for (uint32_t off = callee_off; off + 5 <= ce0; off++) {
        if (buf[off] == 0xE8) {
            int32_t rel2;
            memcpy(&rel2, buf + off + 1, 4);
            int64_t t2 = (int64_t)(SCAN_BASE + off + 5) + rel2;
            if (t2 >= SCAN_BASE && t2 < (int64_t)(SCAN_BASE + SCAN_SIZE)) {
                starts[1] = (uint32_t)(t2 - SCAN_BASE);
                n_starts = 2;
                break;
            }
        }
    }

    uint32_t hit = 0;
    for (int si = 0; si < n_starts && !hit; si++) {
        uint32_t ce = starts[si] + 64;
        if (ce > SCAN_SIZE - 4) ce = SCAN_SIZE - 4;
        for (uint32_t off = starts[si]; off + 10 <= ce; off++) {
            uint8_t *p = buf + off;
            if (p[0] == 0x81 && p[1] == 0x3D &&
                p[6] == 0xFF && p[7] == 0xFF &&
                p[8] == 0x00 && p[9] == 0x00) {
                uint32_t cand;
                memcpy(&cand, p + 2, 4);
                if (cand >= 0x100000u && cand < 0x01000000u) {
                    hit = cand;
                    break;
                }
            }
        }
    }
    g_free(buf);
    return hit;
}

static void p2k_probe_cell_tick(void *opaque)
{
    if (s_probe_cell == 0) {
        if (s_scan_attempts < 200 /* ~10 s */) {
            s_probe_cell = scan_for_probe_cell();
            s_scan_attempts++;
            if (s_probe_cell && !s_logged_hit) {
                s_logged_hit = true;
                info_report("p2k-probe-cell-shim: located probe cell at "
                            "0x%08x — scribbling 0x%04x every 50 ms "
                            "(--update none compatibility bridge)",
                            s_probe_cell, SCRIBBLE_VAL);
            }
        }
    } else {
        uint32_t v = SCRIBBLE_VAL;
        cpu_physical_memory_write(s_probe_cell, &v, sizeof(v));
    }

    timer_mod(s_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + POLL_NS);
}

void p2k_install_probe_cell_shim(void)
{
    /* Hard gate: only active for the no-update parity path. Normal
     * update / savedata-flash boots get NO scribble whatsoever. */
    if (getenv("P2K_NO_AUTO_UPDATE") == NULL) {
        return;
    }

    info_report("p2k-probe-cell-shim: ACTIVE (P2K_NO_AUTO_UPDATE) — "
                "Unicorn-style probe-cell maintenance scheduled. "
                "This is a TEMPORARY compatibility bridge for "
                "--update none parity, NOT clean QEMU device behavior. "
                "Removal condition: when DCS/PLX9054 model returns the "
                "right value at the probe address natively, delete "
                "p2k-probe-cell-shim.c and stop calling "
                "p2k_install_probe_cell_shim() from pinball2000.c.");

    s_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, p2k_probe_cell_tick, NULL);
    timer_mod(s_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + POLL_NS);
}
