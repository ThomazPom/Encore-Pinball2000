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
#include "qemu/main-loop.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "system/system.h"
#include "system/reset.h"
#include "hw/boards.h"
#include "hw/i386/x86.h"
#include "hw/isa/isa.h"
#include "hw/intc/i8259.h"
#include "hw/isa/i8259_internal.h"
#include "hw/timer/i8254.h"
#include "hw/rtc/mc146818rtc.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "target/i386/cpu.h"
#include "hw/core/cpu.h"

#include "p2k-internal.h"

/* ------------------------------------------------------------------------ */
/* Machine init.                                                             */
/* ------------------------------------------------------------------------ */

typedef struct P2KIrq0Tap {
    qemu_irq downstream;
    int last_level;
} P2KIrq0Tap;

/* Single global tap (one PIT ch0 → master IRQ0 path on this board).
 * See docs/12-cpu-and-timers.md. */
static P2KIrq0Tap *p2k_irq0_tap_state;

double p2k_speed_target_percent(void)
{
    static double percent;
    if (percent == 0.0) {
        const char *value = getenv("P2K_SPEED_TARGET_PERCENT");
        percent = (value && *value) ? strtod(value, NULL) : 100.0;
        if (percent <= 0.0) {
            percent = 100.0;
        }
    }
    return percent;
}

/* Strong override for upstream's identity weak hook. Scaling the channel-0
 * divisor changes the real i8254 cadence while preserving the complete
 * PIT -> i8259 -> x86 -> XINU interrupt path used by strict mode. */
int p2k_pit_scale_count(int channel, int count)
{
    if (channel != 0) {
        return count;
    }
    uint64_t raw = count ? (uint64_t)count : 0x10000ull;
    double scaled_f = (double)raw * 100.0 / p2k_speed_target_percent();
    uint64_t scaled = (uint64_t)(scaled_f + 0.5);
    if (scaled < 1) {
        scaled = 1;
    } else if (scaled > 0x10000ull) {
        scaled = 0x10000ull;
    }
    return scaled == 0x10000ull ? 0 : (int)scaled;
}

static void p2k_irq0_tap_set(void *opaque, int n, int level)
{
    P2KIrq0Tap *tap = opaque;

    bool rising = (level && !tap->last_level);
    if (rising) {
        p2k_timing_audit_note_irq0_raised();
    }
    tap->last_level = level;

    /* HOTLOOP-only mode (P2K_TCG_CLKINT_HOTLOOP_NO_PIT=1): swallow the
     * PIT-natural IRQ0 line at the tap so that only HOTLOOP-driven raises
     * no isa-pit at all). Audit still records the rising edge count so
     * we can see what we suppressed.
     *
     * Late engagement: we do NOT swallow PIT edges until XINU has taken
     * enough natural clkints to be past BIOS/optrom/DCS-init and into a
     * stable scheduler state. Empirically, engaging swallow at boot
     * leaves the guest with imr=0xff permanently (delivery drops to 0%
     * and stays there) because early XINU init depends on natural PIT
     * arrival to advance its state machine. Engagement threshold
     * matches HOTLOOP's own prime N (default 800). */
    /* Exclusive HOTLOOP-only (NO_PIT) mode: HOTLOOP owns IRQ0 delivery
     * end-to-end. Drop the line at the tap so neither i8259 IRR nor
     * CPU_INTERRUPT_HARD ever reflects a natural IRQ0 edge. Audit
     * still counted the rising edge above (as an "expected" delivery);
     * a separate swallowed-edge counter records suppressions.
     *
     * Engages from the first PIT edge -- HOTLOOP starts firing at the
     * first TB boundary check with all gates passing, so the guest is
     * never starved of clkints. The historical "wait for 800 natural
     * clkints before swallowing" defensive workaround has been removed
     * along with the priming knob in p2k-clkint-hotloop.c: both were
     * defenses against the over-eager IF=0 gate that has since been
     * fixed. */
    if (p2k_clkint_hotloop_no_pit()) {
        if (rising) {
            p2k_hotloop_note_swallowed_edge();
        }
        /* Do NOT propagate the level to downstream -- see comment
         * further down about why calling qemu_set_irq(downstream, 0)
         * would clobber HOTLOOP's IRR bit 0 set. */
        tap->last_level = 0;
        /* Kick vCPU so TCG yields and HOTLOOP's TB-boundary check runs
         * at PIT cadence even without natural raise. */
        if (rising && first_cpu) {
            cpu_exit(first_cpu);
        }
        return;
    }

    qemu_set_irq(tap->downstream, level);
    /* Sample AFTER the downstream raise so PIC IRR / CPU HARD reflect this
     * edge: the classifier wants to attribute the *reason this raise will
     * not be serviced this instant* (IF=0, IMR mask, ISR busy, halted with
     * no wake, polling spin), not the pre-raise quiescent state. */
    if (rising && p2k_stall_profile_active()) {
        uint64_t raised = p2k_timing_audit_get_irq0_raised();
        uint64_t served = p2k_timing_audit_get_irq0_serviced();
        uint32_t deficit = (raised > served)
                         ? (uint32_t)(raised - served) : 0;
        p2k_stall_profile_sample(deficit);
    }
}

static qemu_irq p2k_irq0_tap(qemu_irq downstream)
{
    P2KIrq0Tap *tap = g_new0(P2KIrq0Tap, 1);

    tap->downstream = downstream;
    p2k_irq0_tap_state = tap;
    return qemu_allocate_irq(p2k_irq0_tap_set, tap, 0);
}

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
    i8259[0] = p2k_irq0_tap(i8259[0]);
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
    /* Enable Cyrix MediaGX TCG opcode extensions for THIS machine only
     * (0F 3C shim + log/UD on the other documented MediaGX 0F 3x
     * slots). Stock i386 stays #UD on these. */
    p2k_mediagx_enable_extensions();

    p2k_install_isa_stubs();
    /* CMOS/RTC: use upstream QEMU mc146818 (default) rather than our
     * hand-rolled CMOS in p2k-isa-stubs.c. The hand-rolled version
     * caused XINA v2.10/v2.0 to display years as shown = 1999 + 2*y
     * (a doubling bug in XINA's date routine triggered by some
     * subtle CMOS behavioural difference). base_year=1999 lines
     * also uses upstream mc146818). Legacy hand-rolled CMOS remains
     * available for A/B via P2K_USE_MC146818=0. */
    {
        const char *mc = getenv("P2K_USE_MC146818");
        if (!mc || mc[0] != '0') {
            mc146818_rtc_init(isa_bus, 1999, NULL);
        }
    }
    /* COM1/UART can fire IRQ4 on TX-empty so the guest's con_putc
     * sem-wait actually returns. Without this, exec hangs in printf. */
    p2k_isa_set_uart_irq(i8259[4]);
    p2k_install_superio();
    p2k_install_cyrix_ccr();
    p2k_install_pci_stub();
    p2k_install_plx_bars(s);
    p2k_install_plx_regs(s);
    p2k_install_bar3_flash(s);
    p2k_install_dcs();
    p2k_install_dcs_uart();
    p2k_install_dcs_audio(s);
    p2k_install_lpt_board();
    p2k_install_gx_stub();
    p2k_install_gp_blt();
    p2k_install_display();
    p2k_install_vsync();
    p2k_install_mem_detect();
    p2k_install_diag(s);
    p2k_install_timing_audit(s);
    p2k_install_nic_dseg();
    p2k_install_gfxlist_watch(s);
    p2k_install_probe_cell_shim();

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
