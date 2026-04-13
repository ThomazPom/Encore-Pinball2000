/*
 * memory.c — Guest physical memory map setup for Unicorn Engine.
 *
 * Maps the guest's physical address space:
 *   0x00000000: 16 MB RAM
 *   0x08000000: PLX ROM banks (read-only)
 *   0x0B800000: DCS sound ROM
 *   0x14000000: BAR5 ROM banks (read-only, pristine for checksum)
 *   0x40800000: Framebuffer (4MB within GX_BASE range)
 *   0xFFFF0000: BIOS reset vector
 *
 * MMIO regions (BAR0-4, GX_BASE config) are handled by hooks, not direct mapping.
 */
#include "encore.h"

static int map_region(uc_engine *uc, uint64_t addr, uint64_t size, uint32_t perms,
                      const char *name)
{
    uc_err err = uc_mem_map(uc, addr, size, perms);
    if (err != UC_ERR_OK) {
        /* May already be mapped — try write anyway */
        if (err != UC_ERR_MAP) {
            fprintf(stderr, "[mem] map %s at 0x%08lx size 0x%lx: %s\n",
                    name, (unsigned long)addr, (unsigned long)size, uc_strerror(err));
            return -1;
        }
    }
    LOG("mem", "%-18s 0x%08lx  %lu KB\n", name, (unsigned long)addr,
        (unsigned long)(size >> 10));
    return 0;
}

int memory_init(void)
{
    uc_engine *uc = g_emu.uc;

    /* Allocate and zero guest RAM */
    g_emu.ram = calloc(1, RAM_SIZE);
    if (!g_emu.ram) {
        fprintf(stderr, "[mem] Failed to allocate %u MB RAM\n", RAM_SIZE >> 20);
        return -1;
    }

    /* 1. Main RAM: 16 MB at 0x00000000 */
    if (map_region(uc, GUEST_RAM, RAM_SIZE, UC_PROT_ALL, "RAM") != 0) return -1;
    uc_mem_write(uc, GUEST_RAM, g_emu.ram, RAM_SIZE);

    /* 2. BIOS at reset vector (0xFFFF0000) and shadow (0xF0000) */
    if (g_emu.bios && g_emu.bios_size > 0) {
        /* Map at high reset vector address */
        map_region(uc, BIOS_RESET, BIOS_SIZE, UC_PROT_ALL, "BIOS (reset)");
        uc_mem_write(uc, BIOS_RESET, g_emu.bios, g_emu.bios_size);

        /* Shadow at 0xF0000 (within RAM, overwrite) */
        uc_mem_write(uc, GUEST_BIOS_SHADOW, g_emu.bios, g_emu.bios_size);
        LOG("mem", "%-18s 0x%08x  %lu KB (shadow)\n", "BIOS",
            GUEST_BIOS_SHADOW, (unsigned long)(g_emu.bios_size >> 10));
    }

    /* 3. PRISM option ROM at 0xC0000 (first 32KB of bank0) */
    if (g_emu.rom_banks[0] && g_emu.rom_sizes[0] >= PRISM_ROM_SIZE) {
        if (g_emu.rom_banks[0][0] == 0x55 && g_emu.rom_banks[0][1] == 0xAA) {
            uc_mem_write(uc, GUEST_OPTION_ROM, g_emu.rom_banks[0], PRISM_ROM_SIZE);
            LOG("mem", "%-18s 0x%08x  %lu KB (55 AA verified)\n", "PRISM option ROM",
                GUEST_OPTION_ROM, (unsigned long)(PRISM_ROM_SIZE >> 10));
        }
    }

    /* 4. PLX ROM banks at 0x08000000+ (runtime copy, may be patched) */
    uint32_t plx_addrs[4] = { PLX_BANK0, PLX_BANK1, PLX_BANK2, PLX_BANK3 };
    for (int b = 0; b < 4; b++) {
        if (!g_emu.rom_banks[b] || g_emu.rom_sizes[b] == 0) continue;
        size_t sz = g_emu.rom_sizes[b];
        /* Align to page boundary */
        size_t aligned = (sz + 0xFFF) & ~0xFFFULL;
        map_region(uc, plx_addrs[b], aligned, UC_PROT_READ | UC_PROT_WRITE,
                   b == 0 ? "PLX bank0" : b == 1 ? "PLX bank1" :
                   b == 2 ? "PLX bank2" : "PLX bank3");
        uc_mem_write(uc, plx_addrs[b], g_emu.rom_banks[b], sz);
    }

    /* 5. BAR5 ROM banks at 0x14000000+ (pristine, for BIOS checksum validation) */
    uint32_t bar5_addrs[4] = { BAR5_BANK0, BAR5_BANK1, BAR5_BANK2, BAR5_BANK3 };
    for (int b = 0; b < 4; b++) {
        if (!g_emu.rom_banks[b] || g_emu.rom_sizes[b] == 0) continue;
        size_t sz = g_emu.rom_sizes[b];
        size_t aligned = (sz + 0xFFF) & ~0xFFFULL;
        map_region(uc, bar5_addrs[b], aligned, UC_PROT_READ,
                   b == 0 ? "BAR5 bank0" : b == 1 ? "BAR5 bank1" :
                   b == 2 ? "BAR5 bank2" : "BAR5 bank3");
        uc_mem_write(uc, bar5_addrs[b], g_emu.rom_banks[b], sz);
    }

    /* 6. DCS sound ROM at 0x0B800000 (PLX CS3) and 0x18000000 (ROMBAR) */
    if (g_emu.dcs_rom && g_emu.dcs_rom_size > 0) {
        size_t aligned = (g_emu.dcs_rom_size + 0xFFF) & ~0xFFFULL;
        map_region(uc, PLX_CS3_DCS, aligned, UC_PROT_READ, "DCS ROM (CS3)");
        uc_mem_write(uc, PLX_CS3_DCS, g_emu.dcs_rom, g_emu.dcs_rom_size);

        map_region(uc, ROMBAR_DCS, aligned, UC_PROT_READ, "DCS ROM (ROMBAR)");
        uc_mem_write(uc, ROMBAR_DCS, g_emu.dcs_rom, g_emu.dcs_rom_size);
    }

    /* 7. BAR MMIO regions — map as R/W so hooks fire but unmapped reads don't crash.
     *    Actual behavior handled by UC_HOOK_MEM_READ/WRITE in cpu.c */
    map_region(uc, WMS_BAR0, 0x01000000, UC_PROT_ALL, "BAR0-4 MMIO");
    /* BAR2-4 follow contiguously from BAR1 through 0x13FFFFFF */
    /* If BAR0 mapping covers 0x10000000-0x10FFFFFF, we need BAR2-4 too */
    map_region(uc, WMS_BAR2, 0x01000000, UC_PROT_ALL, "BAR2 MMIO");
    map_region(uc, WMS_BAR3, 0x01000000, UC_PROT_ALL, "BAR3 MMIO");
    map_region(uc, WMS_BAR4, 0x01000000, UC_PROT_ALL, "BAR4 MMIO");

    /* 8. GX_BASE MMIO (0x40000000 - 0x41000000) — config regs + framebuffer */
    map_region(uc, GX_BASE, GX_BASE_SIZE, UC_PROT_ALL, "GX_BASE+FB");

    /* 9. Panic/safety regions */
    /* 0x20000000 area — map for IVT stub targets */
    map_region(uc, 0x20000000, 0x1000, UC_PROT_ALL, "IVT stub page");

    LOG("mem", "Memory map initialized\n");
    fflush(stdout);
    return 0;
}
