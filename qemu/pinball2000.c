/*
 * QEMU machine type "pinball2000" — Williams Pinball 2000 hardware.
 *
 * This file owns ONLY the MachineClass registration and the top-level
 * init wiring (RAM alias, CPU, ISA bus, PIC, PIT, ROM load, reset hook).
 *
 * Responsibilities split out:
 *   p2k-rom.c   — bank0 ROM loader (chips u100/u101 deinterleave)
 *   p2k-boot.c  — PM-entry post-reset recipe (option ROM copy + GDT + regs)
 *   pinball2000.h     — public board constants
 *   p2k-internal.h    — private declarations shared between p2k-*.c
 *
 * Devices yet to add (each in its own future file — keep this file small):
 *   p2k-plx9054.c   — BAR0 ROM window banking, BAR2 SRAM, watchdog @ +0x420
 *   p2k-dcs2.c      — sound stream device on port 0x13c
 *   p2k-lpt-board.c — driver-board on port 0x378 (idle reply 0xF0)
 *   p2k-display.c   — DC_TIMING2 / VSYNC ~57 Hz path
 *
 * Out-of-tree QEMU source.  scripts/build-qemu.sh copies the qemu/ files
 * into a pinned upstream qemu-x.y.z/hw/i386/ and patches meson.build,
 * Kconfig and configs/devices/i386-softmmu/default.mak.  No vendoring.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "system/system.h"
#include "system/reset.h"
#include "hw/boards.h"
#include "hw/i386/x86.h"
#include "hw/isa/isa.h"
#include "hw/intc/i8259.h"
#include "hw/timer/i8254.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "target/i386/cpu.h"

#include "p2k-internal.h"

/* ------------------------------------------------------------------------ */
/* Machine init.                                                             */
/* ------------------------------------------------------------------------ */

static void pinball2000_init(MachineState *machine)
{
    Pinball2000MachineState *s = PINBALL2000_MACHINE(machine);
    X86MachineState *x86ms = X86_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *ram_alias;
    ISABus *isa_bus;
    qemu_irq *i8259;

    if (!s->game) {
        error_report("pinball2000: -machine pinball2000,game=<id> required "
                     "(e.g. game=swe1)");
        exit(1);
    }
    if (!s->roms_dir) {
        s->roms_dir = g_strdup("roms");
    }

    /* RAM: alias machine->ram (auto-allocated by mc->default_ram_id) at 0. */
    x86ms->below_4g_mem_size = machine->ram_size;
    x86ms->above_4g_mem_size = 0;
    ram_alias = g_new(MemoryRegion, 1);
    memory_region_init_alias(ram_alias, NULL, "p2k.ram-alias",
                             machine->ram, 0, machine->ram_size);
    memory_region_add_subregion(system_memory, 0, ram_alias);

    /* CPU(s): default "486" from mc->default_cpu_type. */
    x86_cpus_init(x86ms, CPU_VERSION_LATEST);

    /* ISA bus + PIC + PIT.  QEMU owns all timing semantics here.
     * No IOAPIC: single-CPU 486-class board.  i8259 outputs route directly
     * to the CPU INTR line via x86_allocate_cpu_irq(); the ISA bus then
     * forwards device IRQs to the 16 i8259 input lines.  Do NOT g_free
     * the i8259 array — isa_bus_register_input_irqs stores the pointer. */
    isa_bus = isa_bus_new(NULL, system_memory, get_system_io(), &error_abort);
    i8259 = i8259_init(isa_bus, x86_allocate_cpu_irq());
    isa_bus_register_input_irqs(isa_bus, i8259);

    s->pit = i8254_pit_init(isa_bus, 0x40, 0, NULL);

    /* Load game ROM bank0 (chips u100 + u101 interleaved). */
    if (p2k_load_bank0(s) < 0) {
        exit(1);
    }
    /* Best-effort load banks 1/2/3 (u102..u107) and DCS sound (u109/u110). */
    p2k_load_extra_banks(s);
    p2k_load_dcs_rom(s);

    /* Map bank0 into the PLX/option-ROM/BAR5/alias windows.  After this
     * the option ROM at 0x80000 (placed by p2k_post_reset) and the full
     * 1 MiB bank0 image at 0x08000000/0x14000000/0xFF000000 are visible
     * to the guest. */
    p2k_map_rom_windows(s);
    p2k_install_isa_stubs();
    /* COM1/UART can fire IRQ4 on TX-empty so the guest's con_putc
     * sem-wait actually returns. Without this, exec hangs in printf. */
    p2k_isa_set_uart_irq(i8259[4]);
    p2k_install_superio();
    p2k_install_cyrix_ccr();
    p2k_install_pci_stub();
    p2k_install_plx_bars(s);
    p2k_install_plx_regs();
    p2k_install_bar3_flash(s);
    p2k_install_dcs();
    p2k_install_dcs_uart();
    p2k_install_dcs_audio(s);
    p2k_install_lpt_board();
    p2k_install_gx_stub();
    p2k_install_gp_blt();
    p2k_install_display();
    p2k_install_pic_fixup();
    p2k_install_vsync();
    p2k_install_mem_detect();
    p2k_install_diag(s);
    p2k_install_timing_audit(s);
    p2k_install_nic_dseg();
    p2k_install_gfxlist_watch(s);

    /* Arrange the PM-entry reset recipe to fire after every system reset. */
    qemu_register_reset(p2k_post_reset, s);

    info_report("pinball2000: machine ready (game=%s, ram=%lu MiB)",
                s->game, (unsigned long)(machine->ram_size / MiB));
}

/* ------------------------------------------------------------------------ */
/* Properties / class registration.                                         */
/* ------------------------------------------------------------------------ */

static char *p2k_get_game(Object *obj, Error **errp)
{
    Pinball2000MachineState *s = PINBALL2000_MACHINE(obj);
    return g_strdup(s->game ?: "");
}
static void p2k_set_game(Object *obj, const char *value, Error **errp)
{
    Pinball2000MachineState *s = PINBALL2000_MACHINE(obj);
    g_free(s->game);
    s->game = g_strdup(value);
}
static char *p2k_get_roms_dir(Object *obj, Error **errp)
{
    Pinball2000MachineState *s = PINBALL2000_MACHINE(obj);
    return g_strdup(s->roms_dir ?: "");
}
static void p2k_set_roms_dir(Object *obj, const char *value, Error **errp)
{
    Pinball2000MachineState *s = PINBALL2000_MACHINE(obj);
    g_free(s->roms_dir);
    s->roms_dir = g_strdup(value);
}
static char *p2k_get_update(Object *obj, Error **errp)
{
    Pinball2000MachineState *s = PINBALL2000_MACHINE(obj);
    return g_strdup(s->update_path ?: "");
}
static void p2k_set_update(Object *obj, const char *value, Error **errp)
{
    Pinball2000MachineState *s = PINBALL2000_MACHINE(obj);
    g_free(s->update_path);
    s->update_path = (value && *value) ? g_strdup(value) : NULL;
}

static void pinball2000_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc              = "Williams Pinball 2000 (Cyrix MediaGX + CS5530 + PLX9054)";
    mc->init              = pinball2000_init;
    mc->family            = "pinball2000_i386";
    mc->max_cpus          = 1;
    mc->default_cpu_type  = X86_CPU_TYPE_NAME("486");
    mc->default_ram_size  = P2K_RAM_SIZE;
    mc->default_ram_id    = "p2k.ram";
    mc->no_floppy         = 1;
    mc->no_cdrom          = 1;
    mc->no_parallel       = 1;   /* the LPT board device will own port 0x378 */
    mc->units_per_default_bus = 1;

    object_class_property_add_str(oc, "game", p2k_get_game, p2k_set_game);
    object_class_property_set_description(oc, "game",
        "Game ROM bank to load (e.g. swe1, rfm)");
    object_class_property_add_str(oc, "roms-dir",
                                  p2k_get_roms_dir, p2k_set_roms_dir);
    object_class_property_set_description(oc, "roms-dir",
        "Directory containing game ROM chip files (default: ./roms)");
    object_class_property_add_str(oc, "update",
                                  p2k_get_update, p2k_set_update);
    object_class_property_set_description(oc, "update",
        "Directory holding *_bootdata.rom + *_im_flsh0.rom + *_game.rom + "
        "*_symbols.rom; assembled into BAR3 flash at boot (overrides "
        "savedata seed). Empty/unset = no update.");
}

static const TypeInfo pinball2000_machine_info = {
    .name           = TYPE_PINBALL2000_MACHINE,
    .parent         = TYPE_X86_MACHINE,
    .instance_size  = sizeof(Pinball2000MachineState),
    .class_init     = pinball2000_class_init,
};

static void pinball2000_register_types(void)
{
    type_register_static(&pinball2000_machine_info);
}

type_init(pinball2000_register_types)
