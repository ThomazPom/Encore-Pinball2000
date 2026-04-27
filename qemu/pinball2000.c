/*
 * QEMU machine type "pinball2000" — Williams Pinball 2000 hardware.
 *
 * Out-of-tree QEMU source. To build, drop into a QEMU 8.x or 9.x source
 * tree under hw/i386/ and add to hw/i386/meson.build + hw/i386/Kconfig
 * (see qemu/README.md).
 *
 * Architecture:
 *   - Cyrix MediaGX core      → modeled as "486" CPU + #UD trap for 0F3C
 *   - CS5530 south bridge     → reuses QEMU's i8254 (PIT) and i8259 (PIC)
 *   - PLX 9054 PCI bridge     → custom PCI device (plx9054.c)
 *   - 256 KiB BAR2 SRAM       → MemoryRegion mapped via plx9054.c
 *   - DCS sound port 0x13c    → custom ISA device (dcs2.c)
 *   - LPT driver board        → custom ISA device on parport (lpt_board.c)
 *   - Watchdog / health reg   → MMIO inside BAR2 (plx9054.c)
 *
 * Crucially, this file does NOT implement:
 *   - PIT timing heuristics (QEMU i8254 owns it)
 *   - PIC IRR/ISR/IMR guards (QEMU i8259 owns it)
 *   - CPU run-budget loop    (QEMU TCG owns it)
 *   - LPT pacing             (LPT remains pure I/O)
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/i386/pc.h"
#include "hw/loader.h"
#include "hw/qdev-properties.h"
#include "qemu/error-report.h"
#include "sysemu/sysemu.h"

#include "pinball2000.h"

#define TYPE_PINBALL2000_MACHINE  MACHINE_TYPE_NAME("pinball2000")

typedef struct Pinball2000MachineState {
    MachineState parent;
    /* Per-board configuration parsed from -machine pinball2000,game=<id> */
    char *game;
} Pinball2000MachineState;

DECLARE_INSTANCE_CHECKER(Pinball2000MachineState, PINBALL2000_MACHINE,
                         TYPE_PINBALL2000_MACHINE)

/*
 * Top-level board init.
 *
 * Order:
 *   1. Create CPU (i486 + 0F3C trap; full Cyrix MediaGX model is TODO).
 *   2. Allocate guest RAM (16 MiB — Pinball 2000 board has 16 MiB DRAM).
 *   3. Map BIOS image at 0xFFFF0000..0xFFFFFFFF (top 64 KiB of 4 GiB).
 *   4. Create CS5530 south bridge: PIT (i8254) + PIC (i8259) on standard ports.
 *   5. Create PLX 9054 PCI bridge (BAR0 ROM window, BAR2 SRAM, BAR4 regs).
 *   6. Create DCS2 sound device on port 0x13c.
 *   7. Create LPT board device on port 0x378.
 *   8. Wire IRQ0 from PIT → PIC line 0 (QEMU i8254 + i8259 do this natively;
 *      we just have to instantiate them, NOT implement timing).
 */
static void pinball2000_init(MachineState *machine)
{
    Pinball2000MachineState *s = PINBALL2000_MACHINE(machine);

    /* TODO #1 — CPU.  For first pass: */
    /*     X86CPU *cpu = X86_CPU(cpu_create(machine->cpu_type)); */
    /* MediaGX model not in upstream; "486" is the closest reasonable base. */
    /* The 0F3C #UD trap is handled by hooking the CPU's invalid-opcode path. */

    /* TODO #2 — RAM.  Pinball 2000 ships with 16 MiB DRAM. */
    /*     memory_region_init_ram(ram, NULL, "p2k.ram", P2K_RAM_SIZE, &error_fatal); */
    /*     memory_region_add_subregion(get_system_memory(), 0, ram); */

    /* TODO #3 — Boot entry path.
     * Do NOT load roms/bios.bin and do NOT use real-mode reset vector.
     * The proven boot recipe (from unicorn.old/src/cpu.c:200-227):
     *   - Load game ROM bank0 (chips u100 + u101 interleaved, 1 MiB).
     *   - Copy first P2K_OPTROM_SIZE bytes (PRISM option ROM) to
     *     P2K_OPTROM_LOAD_ADDR (= 0x80000) in RAM.
     *   - Build a flat 32-bit GDT with CS sel = P2K_INITIAL_CS_SEL,
     *     DS sel = P2K_INITIAL_DS_SEL.
     *   - Set CPU: CR0.PE=1, EFLAGS=0x2 (IF=0), ESP=P2K_INITIAL_ESP,
     *     EIP=P2K_PM_ENTRY_EIP (skips real-mode call pair at 0x801BF/4).
     * This must run AFTER QEMU instantiates the CPU but BEFORE TCG starts. */

    /* TODO #4 — CS5530 south bridge.  Reuse QEMU's standard i8254 + i8259: */
    /*     ISABus *isa = isa_bus_new(...); */
    /*     i8259 = i8259_init(isa, x86ms->gsi[0]); */
    /*     pit = i8254_pit_init(isa, 0x40, 0, NULL); */
    /* QEMU then drives PIT OUT0 → PIC IRQ0 with correct semantics. */

    /* TODO #5 — PLX 9054.  Custom PCIDevice in plx9054.c. */
    /*   - BAR0: 1 MiB ROM window, banks selected by writes to BAR4. */
    /*   - BAR2: 256 KiB SRAM + watchdog/DC_TIMING2 MMIO. */
    /*   - BAR4: PLX runtime regs (mailbox, DMA, doorbell). */

    /* TODO #6 — DCS2 sound.  ISA device in dcs2.c, port 0x13c. */
    /*   Byte-stream protocol: two consecutive bytes form a 16-bit command. */
    /*   Reset byte 0x00 → reply 0xF0. */

    /* TODO #7 — LPT driver board.  ISA device in lpt_board.c, port 0x378. */
    /*   Idle opcode 0x00 → reply 0xF0. */
    /*   No pacing.  No sleeps.  Pure I/O. */

    error_report("pinball2000: scaffold only — see qemu/README.md "
                 "(game=%s)", s->game ?: "(none)");
}

static char *pinball2000_get_game(Object *obj, Error **errp)
{
    Pinball2000MachineState *s = PINBALL2000_MACHINE(obj);
    return g_strdup(s->game ?: "");
}

static void pinball2000_set_game(Object *obj, const char *value, Error **errp)
{
    Pinball2000MachineState *s = PINBALL2000_MACHINE(obj);
    g_free(s->game);
    s->game = g_strdup(value);
}

static void pinball2000_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Williams Pinball 2000 (Cyrix MediaGX + CS5530 + PLX9054)";
    mc->init = pinball2000_init;
    mc->max_cpus = 1;
    mc->default_cpu_type = X86_CPU_TYPE_NAME("486");
    mc->default_ram_size = 16 * MiB;
    mc->default_ram_id = "p2k.ram";
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_parallel = 1;   /* we provide our own LPT board device */
    mc->no_serial = 0;     /* COM1 may be used for service interface */

    object_class_property_add_str(oc, "game",
                                  pinball2000_get_game, pinball2000_set_game);
    object_class_property_set_description(oc, "game",
        "Game ROM bank to load (e.g. swe1, rfm)");
}

static const TypeInfo pinball2000_machine_info = {
    .name           = TYPE_PINBALL2000_MACHINE,
    .parent         = TYPE_MACHINE,
    .instance_size  = sizeof(Pinball2000MachineState),
    .class_init     = pinball2000_machine_class_init,
};

static void pinball2000_machine_register_types(void)
{
    type_register_static(&pinball2000_machine_info);
}

type_init(pinball2000_machine_register_types)
