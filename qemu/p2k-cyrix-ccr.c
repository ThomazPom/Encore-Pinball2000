/*
 * p2k-cyrix-ccr.c — Cyrix MediaGX configuration registers via I/O 0x22/0x23.
 *
 * On the real Cyrix MediaGX (and the older 5x86), CPU configuration
 * registers (CCRs) are accessed through a two-byte indexed scheme:
 *
 *     outb(0x22, index);   // select register
 *     val = inb(0x23);     // read selected reg
 *     outb(0x23, val);     // (re-)write it
 *
 * The pinball v2.10 ROM reads index 0xb8 (the GCR / GX MMIO base
 * register) at gx_fb_init+0x6f to compute GX_base_p:
 *
 *     outb(0x22, 0xc3); ccr3 = inb(0x23);          ; save CCR3
 *     outb(0x22, 0xc3); outb(0x23, ccr3 | 0x10);   ; set MAPEN -> enable
 *                                                  ; extended regs
 *     outb(0x22, 0xb8);
 *     base_id = inb(0x23);
 *     GX_base_p = base_id << 30;                   ; 1 GiB-aligned
 *
 * On unhandled QEMU ports inb returns 0xff, so base_id = 0xff and the
 * guest computes GX_base_p = 0xc0000000.  But our QEMU MediaGX MMIO
 * stub (qemu/p2k-gx.c) is mapped at 0x40000000.  So all guest writes
 * to the DC/GP/BC regs disappear into unmapped memory, and the
 * subsequent readback of DC_H_TIMING1 (0x8330) / DC_V_TIMING1 (0x8340)
 * returns 0 → GX_width = 8, GX_height = 1 → mediagx_init prints
 * "MediaGX only supports 8x1" → set_gfx_mode returns -1 → gx_fb_init
 * calls allegro_exit → free_resource(0x35ab28, 1) wrong type 0 Fatal.
 *
 * This module gives ports 0x22/0x23 a minimal correct response so the
 * guest derives GX_base_p = 0x40000000 (matching p2k-gx.c).  Any other
 * CCR index returns 0 (real chip's reset default for most CCRs).
 *
 * Real device behaviour (clean QEMU model), NOT a guest-code patch.
 *
 * Reference: Cyrix MediaGX Processor TRM rev 1.2, §10 ("Configuration
 * Registers"); also visible in unicorn.old/src/io.c handling of port
 * 0x22/0x23 reads (returns 1 for index 0xb8).
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "exec/address-spaces.h"
#include "exec/ioport.h"
#include "exec/memory.h"

#include "p2k-internal.h"

/* GCR (index 0xb8) — Cyrix MediaGX Configuration Register, per
 * MediaGX Processor Data Book v2.0 §4.1.6:
 *
 *   bits[1:0] : GX_BASE — encode GX MMIO base in 1 GiB chunks
 *               (0 -> 0x00000000   1 -> 0x40000000
 *                2 -> 0x80000000   3 -> 0xc0000000)
 *               p2k-gx.c maps the MMIO at 0x40000000, so we return 1.
 *   bit  [2]  : GCR_BASE_VALID / enable on real chip; ignored here.
 *   bits[3:2] : SCRATCHPAD_SIZE — non-zero enables the Display-Driver
 *               Instructions (0F 3A BB0_RESET, 0F 3B BB1_RESET,
 *               0F 3C CPU_WRITE, 0F 3D CPU_READ); zero means they
 *               must #UD. The storage default is 11 (4 KiB scratchpad),
 *               which matches the on-silicon configuration the SWE1
 *               ROM assumes (it issues 0F3C without ever programming
 *               GCR[B8h] itself, so something below us must have
 *               pre-programmed it). The byte is mutable: if the guest
 *               writes a value with bits[3:2]=00 the helpers WILL see
 *               it and start raising #UD on the Display-Driver
 *               instructions, exactly as a real MediaGX would.
 *
 * Reset byte: 0x0D = (SCRATCHPAD_SIZE=11)<<2 | (GX_BASE=01).
 */
#define P2K_CCR_GCR_INDEX     0xb8u
#define P2K_CCR_GCR_RESET     0x0Du   /* SP=4KB, GX_BASE=0x40000000 */

/* CCR3 (index 0xc3) is also written by the guest with bit 4 ("MAPEN")
 * set to enable access to the extended CCRs (0xb0+).  Read-back must
 * preserve last write so the guest's save/restore round-trip works.
 */
#define P2K_CCR_CCR3_INDEX    0xc3u

static uint8_t  p2k_ccr_index;       /* last write to port 0x22 */
static uint8_t  p2k_ccr_storage[256];/* per-index value cache (writes) */
static bool     p2k_ccr_storage_inited;

static void p2k_ccr_storage_init_once(void)
{
    if (p2k_ccr_storage_inited) {
        return;
    }
    /* Most CCRs reset to 0 on the real Cyrix; the only non-zero default
     * we model is GCR[B8h] which on the real pinball2000 board comes up
     * already programmed (the SWE1 ROM issues 0F3C without ever
     * touching it, so something below us must have configured it). */
    p2k_ccr_storage[P2K_CCR_GCR_INDEX] = P2K_CCR_GCR_RESET;
    p2k_ccr_storage_inited = true;
}

static uint64_t p2k_ccr_read(void *opaque, hwaddr addr, unsigned size)
{
    p2k_ccr_storage_init_once();
    /* addr is 0 (port 0x22) or 1 (port 0x23) relative to base 0x22. */
    if (addr == 0) {
        /* Reads of 0x22 are unusual; return last index. */
        return p2k_ccr_index;
    }
    /* addr == 1: port 0x23 — return register selected by last 0x22 write. */
    return p2k_ccr_storage[p2k_ccr_index];
}

static void p2k_ccr_write(void *opaque, hwaddr addr, uint64_t val,
                          unsigned size)
{
    p2k_ccr_storage_init_once();
    if (addr == 0) {
        /* port 0x22: select index */
        p2k_ccr_index = (uint8_t)val;
        return;
    }
    /* addr == 1: port 0x23: write data into selected index. */
    p2k_ccr_storage[p2k_ccr_index] = (uint8_t)val;
}

static const MemoryRegionOps p2k_ccr_ops = {
    .read       = p2k_ccr_read,
    .write      = p2k_ccr_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl       = { .min_access_size = 1, .max_access_size = 1 },
    .valid      = { .min_access_size = 1, .max_access_size = 1 },
};

void p2k_install_cyrix_ccr(void)
{
    static MemoryRegion mr;
    memory_region_init_io(&mr, NULL, &p2k_ccr_ops, NULL,
                          "p2k.cyrix.ccr", 2);
    memory_region_add_subregion(get_system_io(), 0x22, &mr);
    p2k_ccr_storage_init_once();
    info_report("pinball2000: installed Cyrix CCR at I/O 0x22/0x23 "
                "(GCR[0xb8] reset=0x%02x -> GX_base_p=0x%08x, "
                "scratchpad=4KB; storage is mutable)",
                P2K_CCR_GCR_RESET, (unsigned)(P2K_CCR_GCR_RESET & 0x3) << 30);
}

uint8_t p2k_cyrix_ccr_get(uint8_t index)
{
    p2k_ccr_storage_init_once();
    return p2k_ccr_storage[index];
}
