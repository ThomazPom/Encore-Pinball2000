/*
 * p2k-gp-blt.c — Cyrix MediaGX GP (Graphics Pipeline) BLT engine.
 *
 * STATUS: device emulation (NOT a temporary symptom patch).
 *
 * The MediaGX GP block at GX_BASE+0x8100..+0x820F drives screen-to-screen
 * blits inside the framebuffer at 0x800000. Without this engine the
 * framebuffer stays empty: the game's drawing path hands packed
 * src/dst/width to the GP, then writes the trigger register. Hardware
 * does the copy. We were leaving regs1 as plain RAM, so triggers were
 * silently ignored and nothing ever appeared on screen.
 *
 * Behaviour ported from unicorn.old/src/bar.c:603-666 (the "x64 POC"
 * packed register layout that worked end-to-end on Unicorn):
 *
 *   off 0x8100 (W) — packed dst (x = bits[15:0], y = bits[31:16])
 *   off 0x8104 (W) — width in pixels (RGB555 = 2 bytes/pixel)
 *   off 0x8108 (W) — packed src (x = bits[15:0], y = bits[31:16])
 *   off 0x8200 (W) — raster mode (bit 12 = transparent blit, key=0x7C1F)
 *   off 0x8208 (W) — TRIGGER: write executes one row of the blit
 *   off 0x820C (R) — STATUS: returns 0x300 (idle) so the guest never spins
 *
 * Implementation: install a small MMIO overlay at GX_BASE+0x8100 of size
 * 0x110 with priority 1 over the regs1 RAM region (priority 0). The
 * overlay covers ONLY 0x40008100..0x4000820F, which is the GP block.
 * It deliberately does NOT cover the DC block at 0x40008300+ nor the
 * BC at 0x40020000 — those keep their plain-RAM behaviour (DC_FB_ST_OFFSET
 * is read by p2k-display.c via address_space_ldl_le, BC_DRAM_TOP is
 * pre-seeded by p2k-gx.c). Reads of unknown GP slots and unhandled
 * writes are mirrored back to a small shadow so RMW patterns behave.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "hw/boards.h"
#include "hw/qdev-core.h"

#include "p2k-internal.h"

#define GX_BASE              0x40000000u
#define GP_BLOCK_OFF         0x00008100u
#define GP_BLOCK_SIZE        0x00000110u

#define GP_FB_PHYS           0x00800000u
#define GP_FB_SIZE           0x00400000u
#define GP_ROW_STRIDE        2048u
#define GP_TRANS_KEY         0x7C1Fu

/* Offsets are relative to GP_BLOCK_OFF (i.e. 0x40008100). */
#define R_DST                0x000
#define R_WIDTH              0x004
#define R_SRC                0x008
#define R_RASTER_MODE        0x100
#define R_BLT_TRIGGER        0x108
#define R_BLT_STATUS         0x10C

typedef struct P2KGpState {
    uint16_t  dst_x, dst_y;
    uint16_t  src_x, src_y;
    uint16_t  width;
    uint32_t  raster_mode;
    bool      transparent;
    uint32_t  blt_count;

    /* Shadow storage so reads of the GP block return the last value
     * written, mirroring the RAM behaviour we're replacing. */
    uint32_t  shadow[GP_BLOCK_SIZE / 4];
} P2KGpState;

static P2KGpState s_gp;

static void gp_execute_blt(void)
{
    uint32_t src_off = (uint32_t)s_gp.src_x * 2u
                     + (uint32_t)s_gp.src_y * GP_ROW_STRIDE;
    uint32_t dst_off = (uint32_t)s_gp.dst_x * 2u
                     + (uint32_t)s_gp.dst_y * GP_ROW_STRIDE;
    uint32_t copy_bytes = (uint32_t)s_gp.width * 2u;

    if (copy_bytes == 0 || copy_bytes > GP_ROW_STRIDE) {
        s_gp.blt_count++;
        return;
    }
    if (src_off + copy_bytes > GP_FB_SIZE ||
        dst_off + copy_bytes > GP_FB_SIZE) {
        s_gp.blt_count++;
        return;
    }

    uint8_t row[GP_ROW_STRIDE];
    address_space_read(&address_space_memory, GP_FB_PHYS + src_off,
                       MEMTXATTRS_UNSPECIFIED, row, copy_bytes);

    if (!s_gp.transparent) {
        address_space_write(&address_space_memory, GP_FB_PHYS + dst_off,
                            MEMTXATTRS_UNSPECIFIED, row, copy_bytes);
    } else {
        uint8_t dst_row[GP_ROW_STRIDE];
        address_space_read(&address_space_memory, GP_FB_PHYS + dst_off,
                           MEMTXATTRS_UNSPECIFIED, dst_row, copy_bytes);
        uint16_t *src_px = (uint16_t *)row;
        uint16_t *dst_px = (uint16_t *)dst_row;
        for (uint32_t i = 0; i < s_gp.width; i++) {
            if (src_px[i] != GP_TRANS_KEY) {
                dst_px[i] = src_px[i];
            }
        }
        address_space_write(&address_space_memory, GP_FB_PHYS + dst_off,
                            MEMTXATTRS_UNSPECIFIED, dst_row, copy_bytes);
    }

    /* Log first 20 BLTs in full detail so we can verify the engine is
     * reached and that the packed DST/SRC/WIDTH values look sane. */
    if (s_gp.blt_count < 20) {
        info_report("pinball2000: GP BLT #%u: src(%u,%u) dst(%u,%u) "
                    "w=%u raster=0x%08x %s key=0x%04x",
                    s_gp.blt_count, s_gp.src_x, s_gp.src_y,
                    s_gp.dst_x, s_gp.dst_y, s_gp.width,
                    s_gp.raster_mode,
                    s_gp.transparent ? "TRANSPARENT" : "memcpy",
                    s_gp.transparent ? GP_TRANS_KEY : 0);
    }
    s_gp.blt_count++;
}

static uint64_t p2k_gp_read(void *opaque, hwaddr off, unsigned size)
{
    if (off == R_BLT_STATUS) {
        return 0x300;            /* idle */
    }
    if (off + size > GP_BLOCK_SIZE) {
        return 0;
    }
    /* Mirror RAM behaviour: return last written dword (truncated). */
    uint32_t v = s_gp.shadow[off >> 2];
    if (size == 4) return v;
    if (size == 2) return (v >> ((off & 2) * 8)) & 0xFFFFu;
    return (v >> ((off & 3) * 8)) & 0xFFu;
}

static void p2k_gp_write(void *opaque, hwaddr off, uint64_t val, unsigned size)
{
    if (off + size > GP_BLOCK_SIZE) {
        return;
    }

    /* Update the shadow first so reads/RMW behave like RAM. */
    if (size == 4) {
        s_gp.shadow[off >> 2] = (uint32_t)val;
    } else {
        uint32_t cur = s_gp.shadow[off >> 2];
        uint32_t mask, shift = (off & 3) * 8;
        if (size == 2) mask = 0xFFFFu << shift;
        else           mask = 0xFFu   << shift;
        s_gp.shadow[off >> 2] = (cur & ~mask)
                              | (((uint32_t)val << shift) & mask);
    }

    /* Prove the guest reaches each GP reg at least once — log first
     * write to every dword slot in the block. */
    static uint64_t seen_mask;
    unsigned slot = off >> 2;
    if (slot < 64 && !(seen_mask & (1ULL << slot))) {
        seen_mask |= (1ULL << slot);
        info_report("pinball2000: GP first-write off=0x%03x val=0x%08x size=%u",
                    (unsigned)(GP_BLOCK_OFF + off), (uint32_t)val, size);
    }

    switch (off) {
    case R_DST:
        s_gp.dst_x = (uint16_t)(val & 0xFFFFu);
        s_gp.dst_y = (uint16_t)((val >> 16) & 0xFFFFu);
        break;
    case R_WIDTH:
        s_gp.width = (uint16_t)(val & 0xFFFFu);
        break;
    case R_SRC:
        s_gp.src_x = (uint16_t)(val & 0xFFFFu);
        s_gp.src_y = (uint16_t)((val >> 16) & 0xFFFFu);
        break;
    case R_RASTER_MODE:
        s_gp.raster_mode = (uint32_t)val;
        s_gp.transparent = ((val >> 12) & 1u) != 0;
        break;
    case R_BLT_TRIGGER:
        gp_execute_blt();
        break;
    default:
        break;
    }
}

static const MemoryRegionOps p2k_gp_ops = {
    .read       = p2k_gp_read,
    .write      = p2k_gp_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid      = { .min_access_size = 1, .max_access_size = 4 },
    .impl       = { .min_access_size = 1, .max_access_size = 4 },
};

void p2k_install_gp_blt(void)
{
    MemoryRegion *sys = get_system_memory();
    MemoryRegion *mr  = g_new(MemoryRegion, 1);

    memory_region_init_io(mr, NULL, &p2k_gp_ops, &s_gp,
                          "p2k.gx.gp", GP_BLOCK_SIZE);
    /* Priority 1 wins over the GX regs1 RAM (default priority 0). */
    memory_region_add_subregion_overlap(sys, GX_BASE + GP_BLOCK_OFF, mr, 1);
    info_report("pinball2000: GP BLT engine MMIO at 0x%x size 0x%x",
                GX_BASE + GP_BLOCK_OFF, GP_BLOCK_SIZE);
}
