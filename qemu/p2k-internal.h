/*
 * Internal header shared between pinball2000 machine modules.
 * Public board constants live in pinball2000.h; this file is private to
 * the qemu/ source files (rom loader, boot recipe, machine init).
 */
#ifndef HW_PINBALL2000_INTERNAL_H
#define HW_PINBALL2000_INTERNAL_H

#include "qemu/osdep.h"
#include "hw/boards.h"
#include "hw/i386/x86.h"
#include "qom/object.h"

#include "pinball2000.h"

#define TYPE_PINBALL2000_MACHINE  MACHINE_TYPE_NAME("pinball2000")

typedef struct Pinball2000MachineState {
    X86MachineState parent;
    char            *game;          /* "swe1", "rfm", ... */
    char            *roms_dir;      /* default: <cwd>/roms */
    uint8_t         *bank0;         /* 1 MiB, owned by us */
} Pinball2000MachineState;

DECLARE_INSTANCE_CHECKER(Pinball2000MachineState, PINBALL2000_MACHINE,
                         TYPE_PINBALL2000_MACHINE)

/* p2k-rom.c: deinterleave chips u100+u101 into a 1 MiB bank. */
int  p2k_load_bank0(Pinball2000MachineState *s);

/* p2k-boot.c: post-reset PM-entry recipe (option ROM copy + GDT + CPU regs). */
void p2k_post_reset(void *opaque);

/* p2k-plx9054.c: install bank0 at the PLX/option-ROM/BAR5/alias windows. */
void p2k_map_rom_windows(Pinball2000MachineState *s);

/* p2k-isa-stubs.c: minimal i8042 etc. so PRISM polling loops terminate. */
void p2k_install_isa_stubs(void);
void p2k_install_pci_stub(void);
void p2k_install_plx_bars(void);

/* p2k-gx.c: 16 MiB Cyrix MediaGX MMIO + framebuffer stub at 0x40000000. */
void p2k_install_gx_stub(void);

/* p2k-display.c: 640×480 SDL/QEMU display reading FB at RAM 0x800000. */
void p2k_install_display(void);

/* p2k-dcs.c: DCS audio MMIO state machine on BAR4 (0x13000000, 16 MiB). */
void p2k_install_dcs(void);

#endif /* HW_PINBALL2000_INTERNAL_H */
