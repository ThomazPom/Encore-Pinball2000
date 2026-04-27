/*
 * pinball2000 PLX9054 / ROM-window memory map.
 *
 * The real board's PLX9054 PCI bridge exposes the game ROM bank0 at several
 * address ranges, plus an SRAM region with a watchdog health register.  In
 * this milestone we only model the read-only ROM windows and the stub MMIO
 * space — that is the bare minimum to prevent PRISM from falling into
 * zero-filled RAM after the option-ROM bootstrap.
 *
 * Mappings created here (mirrors of unicorn.old/src/memory.c):
 *
 *   0x000C0000  PRISM option ROM, first 32 KiB of bank0   (RO, 32 KiB)
 *   0x000F0000  BIOS shadow                               (RW, 64 KiB)
 *   0x08000000  PLX bank0 — full 1 MiB                    (RO, 1 MiB)
 *   0x14000000  BAR5 bank0 — pristine mirror of 0x080..   (RO, 1 MiB)
 *   0xFF000000  ROM bank0 alias — 4 MiB BT-108 mirror     (RO, 4 MiB)
 *
 * Each region is a plain QEMU `memory_region_init_rom` (or _ram for the BIOS
 * shadow) initialised from the in-memory bank0 buffer the rom loader filled.
 *
 * No PCI device, no BAR2 SRAM and no watchdog yet — those live in their own
 * follow-up patches so this file stays small.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"

#include "p2k-internal.h"

/* All sizes/addresses come from unicorn.old/include/encore.h. */
#define P2K_OPTROM_BASE      0x000C0000u
#define P2K_BIOS_SHADOW_BASE 0x000F0000u
#define P2K_BIOS_SHADOW_SIZE 0x00010000u
#define P2K_PLX_BANK0_BASE   0x08000000u
#define P2K_BAR5_BANK0_BASE  0x14000000u
#define P2K_BANK0_ALIAS_BASE 0xFF000000u
#define P2K_BANK0_ALIAS_SIZE 0x00400000u   /* 4 MiB */

static void p2k_map_rom(MemoryRegion *system_memory, const char *name,
                        hwaddr base, uint64_t size,
                        const uint8_t *src, uint64_t src_size)
{
    MemoryRegion *mr = g_new(MemoryRegion, 1);
    memory_region_init_rom(mr, NULL, name, size, &error_abort);
    /* memory_region_init_rom returns a host-backed region; populate it. */
    void *host = memory_region_get_ram_ptr(mr);
    /* Fill with 0xFF (flash-erased default) before copying ROM contents. */
    memset(host, 0xFF, size);
    if (src && src_size) {
        memcpy(host, src, MIN(size, src_size));
    }
    memory_region_add_subregion(system_memory, base, mr);
    info_report("pinball2000: mapped %-20s @ 0x%08" HWADDR_PRIx " (%" PRIu64 " KiB)",
                name, base, (uint64_t)(size >> 10));
}

static void p2k_map_ram(MemoryRegion *system_memory, const char *name,
                        hwaddr base, uint64_t size)
{
    MemoryRegion *mr = g_new(MemoryRegion, 1);
    memory_region_init_ram(mr, NULL, name, size, &error_abort);
    memory_region_add_subregion(system_memory, base, mr);
    info_report("pinball2000: mapped %-20s @ 0x%08" HWADDR_PRIx " (%" PRIu64 " KiB, RW)",
                name, base, (uint64_t)(size >> 10));
}

void p2k_map_rom_windows(Pinball2000MachineState *s)
{
    MemoryRegion *system_memory = get_system_memory();

    if (!s->bank0) {
        error_report("pinball2000: bank0 not loaded; cannot map ROM windows");
        return;
    }

    /* PRISM option ROM at 0xC0000 (first 32 KiB of bank0).  Writable in
     * unicorn.old, but a real option ROM is read-only — keep it RO here. */
    p2k_map_rom(system_memory, "p2k.optrom",
                P2K_OPTROM_BASE, P2K_OPTROM_SIZE,
                s->bank0, P2K_OPTROM_SIZE);

    /* BIOS shadow at 0xF0000.  We have no real BIOS code, but the guest
     * may CALL into this area for legacy BIOS services that PRISM stubs
     * out itself.  Provide a writable scratch region so writes don't fault. */
    p2k_map_ram(system_memory, "p2k.bios-shadow",
                P2K_BIOS_SHADOW_BASE, P2K_BIOS_SHADOW_SIZE);

    /* PLX bank0 — full 1 MiB at 0x08000000. */
    p2k_map_rom(system_memory, "p2k.plx-bank0",
                P2K_PLX_BANK0_BASE, P2K_BANK_SIZE,
                s->bank0, P2K_BANK_SIZE);

    /* BAR5 bank0 — pristine 1 MiB mirror at 0x14000000 (kept for checksum). */
    p2k_map_rom(system_memory, "p2k.bar5-bank0",
                P2K_BAR5_BANK0_BASE, P2K_BANK_SIZE,
                s->bank0, P2K_BANK_SIZE);

    /* BT-108 high alias: 4 MiB read-only mirror of bank0 at 0xFF000000. */
    p2k_map_rom(system_memory, "p2k.bank0-alias",
                P2K_BANK0_ALIAS_BASE, P2K_BANK0_ALIAS_SIZE,
                s->bank0, P2K_BANK_SIZE);
}
