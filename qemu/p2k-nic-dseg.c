/*
 * ============================================================================
 * STATUS: TEMPORARY SYMPTOM PATCH — replace with proper QEMU device behavior.
 *
 * Why temporary: real Pinball 2000 hardware has an SMC8216T NIC whose
 * LAN address PROM is shadowed into the BIOS D-segment by the BIOS
 * itself. We do not run a real BIOS — we jump straight into PRISM —
 * so the shadow is never populated. This module pokes the 8 bytes
 * directly into RAM at p2k_post_reset.
 *
 * Removal condition: delete this file once either
 *   (a) we model the SMC8216T at its I/O window (0x300-0x31F) with a
 *       proper 16-byte address PROM that the guest can read via
 *       the chip's own LAN-ROM access cycle, OR
 *   (b) PRISM POST runs the BIOS shadow path that copies the PROM
 *       into the D-segment as on real hardware.
 * No kill switch needed (8-byte poke is harmless if unused) but the
 * helper should be removed when its caller in p2k-boot.c is gone.
 * ============================================================================
 *
 * pinball2000 BT-131: NIC LAN ROM seed in D-segment.
 *
 * The Williams P2K firmware probes for an SMC8216T network card by
 * reading a 16-byte LAN-ROM "shadow" out of the BIOS D-segment shared
 * memory at 0xD0008-0xD000F.  Without this seed the network init runs
 * into a checksum mismatch and the boot continues with a corrupted
 * board_id (0x00) — which downstream code uses to decide what
 * peripheral profile to apply.
 *
 * Mirror unicorn.old/src/io.c:680-700 (BT-131):
 *   0xD0008..0xD000D : MAC = 00:00:C0:01:02:03  (Williams OUI placeholder)
 *   0xD000E         : board_id = 0x2A
 *   0xD000F         : checksum = 0xFF - sum(0xD0008..0xD000E)
 *
 * One concern per file: this module ONLY writes the 8 LAN-ROM bytes
 * once at machine init.  No I/O port handling.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "exec/cpu-common.h"

#include "p2k-internal.h"

#define P2K_NIC_DSEG_BASE   0x000D0008u

void p2k_install_nic_dseg(void)
{
    static const uint8_t mac[6] = { 0x00, 0x00, 0xC0, 0x01, 0x02, 0x03 };
    uint8_t board_id = 0x2A;
    uint8_t buf[8];
    memcpy(buf, mac, 6);
    buf[6] = board_id;
    uint8_t sum = 0;
    for (int i = 0; i < 7; i++) {
        sum += buf[i];
    }
    buf[7] = (uint8_t)(0xFFu - sum);
    cpu_physical_memory_write(P2K_NIC_DSEG_BASE, buf, sizeof(buf));
    info_report("pinball2000: BT-131 NIC LAN ROM seeded at 0xD0008 "
                "(MAC %02x:%02x:%02x:%02x:%02x:%02x board_id=0x%02x csum=0x%02x)",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                board_id, buf[7]);
}
