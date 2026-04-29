/*
 * pinball2000 DCS audio MMIO frontend on PLX9054 BAR4
 * (0x13000000, 16 MiB window).
 *
 * This is now a thin view over p2k-dcs-core.c.  All DCS-2 protocol state
 * (response queue, echo, 0x0E suspend, ACE1 capture) lives in the core;
 * this file only translates the BAR4 access shape into core calls.
 *
 * BAR4 access shape (mirrors unicorn.old/src/bar.c:549-589, 892-1010):
 *   off 0 byte read  : echo last byte written
 *   off 0 byte write : store echo
 *   off 0 word read  : pop next DCS response word
 *   off 0 word write : DCS command word
 *   off 2 read       : flag byte (bit6=ready, bit7=output available)
 *   off 2 write      : ignored (game pokes 0x80/0x40 there during init)
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "exec/address-spaces.h"

#include "pinball2000.h"
#include "p2k-internal.h"

#define P2K_BAR4_BASE        0x13000000u
#define P2K_BAR4_SIZE        0x01000000u   /* 16 MiB */

static uint64_t p2k_dcs_read(void *opaque, hwaddr off, unsigned size)
{
    if (off == 0) {
        if (size == 1) {
            return p2k_dcs_core_get_echo();
        }
        return p2k_dcs_core_read_resp();
    }
    if (off == 2) {
        return p2k_dcs_core_flag_byte();
    }
    return 0;
}

static unsigned long long s_bar4_rejected_count = 0;

unsigned long long p2k_dcs_bar4_rejected_count(void)
{
    return s_bar4_rejected_count;
}

static void p2k_dcs_write(void *opaque, hwaddr off, uint64_t val,
                          unsigned size)
{
    static unsigned dropped_log = 0;
    if (off == 0) {
        if (size == 1) {
            /* Echo byte: PCI-side liveness probe response. */
            p2k_dcs_core_set_echo(val & 0xFFu);
            return;
        }
        /* DCS command word.  Per unicorn parity (see core mode docs):
         * BAR4 carries DCS commands in BOTH io-handled and bar4-patch
         * modes — `dcs_mode==1` either way.  The mode label only
         * controls whether the CPU probe is .text-patched (we don't
         * patch in either mode; our BAR4 device responds correctly
         * to the natural probe). */
        p2k_dcs_core_note_source("BAR4");
        p2k_dcs_core_write_cmd(val & 0xFFFFu);
        return;
    }
    /* off == 2 (flags) and everything else: writes are no-ops.
     * Log first 16 to catch attract-mode/credit-insert paths poking
     * unexpected offsets we may need to honour. */
    if (getenv("P2K_DCS_AUDIO_TRACE") && dropped_log < 16) {
        warn_report("dcs-bar4: dropped write off=0x%" HWADDR_PRIx
                    " size=%u val=0x%" PRIx64, off, size, val);
        dropped_log++;
    }
}

static const MemoryRegionOps p2k_dcs_ops = {
    .read  = p2k_dcs_read,
    .write = p2k_dcs_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
};

void p2k_install_dcs(void)
{
    MemoryRegion *sm = get_system_memory();
    MemoryRegion *mr = g_new(MemoryRegion, 1);

    p2k_dcs_core_reset();

    memory_region_init_io(mr, NULL, &p2k_dcs_ops, NULL,
                          "p2k.bar4-dcs", P2K_BAR4_SIZE);
    memory_region_add_subregion(sm, P2K_BAR4_BASE, mr);

    info_report("pinball2000: DCS BAR4 MMIO @ 0x%08x (16 MiB) [shared core, "
                "mode=%s]",
                P2K_BAR4_BASE, p2k_dcs_core_mode_name());
}
