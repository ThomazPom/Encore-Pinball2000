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
#include "hw/irq.h"
#include "qom/object.h"

#include "pinball2000.h"

#define TYPE_PINBALL2000_MACHINE  MACHINE_TYPE_NAME("pinball2000")

typedef struct Pinball2000MachineState {
    X86MachineState parent;
    char            *game;          /* "swe1", "rfm", ... */
    char            *roms_dir;      /* default: <cwd>/roms */
    uint8_t         *bank0;         /* 16 MiB, owned by us */
    uint8_t         *bank1;         /* 16 MiB or NULL if chips absent */
    uint8_t         *bank2;
    uint8_t         *bank3;
    uint8_t         *dcs_rom;       /* 8 MiB DCS sound, NULL if absent */
    void            *pit;           /* ISADevice* for the QEMU i8254 (debug) */
} Pinball2000MachineState;

DECLARE_INSTANCE_CHECKER(Pinball2000MachineState, PINBALL2000_MACHINE,
                         TYPE_PINBALL2000_MACHINE)

/* p2k-rom.c: deinterleave chips u100..u107 (banks 0..3) + DCS u109/u110. */
int  p2k_load_bank0(Pinball2000MachineState *s);
void p2k_load_extra_banks(Pinball2000MachineState *s);
void p2k_load_dcs_rom(Pinball2000MachineState *s);

/* p2k-boot.c: post-reset PM-entry recipe (option ROM copy + GDT + CPU regs). */
void p2k_post_reset(void *opaque);

/* p2k-plx9054.c: install bank0 at the PLX/option-ROM/BAR5/alias windows. */
void p2k_map_rom_windows(Pinball2000MachineState *s);

/* p2k-isa-stubs.c: minimal i8042 etc. so PRISM polling loops terminate. */
void p2k_install_isa_stubs(void);
void p2k_isa_set_uart_irq(qemu_irq irq);
void p2k_install_pci_stub(void);
void p2k_install_plx_bars(Pinball2000MachineState *s);

/* p2k-gx.c: 16 MiB Cyrix MediaGX MMIO + framebuffer stub at 0x40000000. */
void p2k_install_gx_stub(void);
void p2k_install_gp_blt(void);
void p2k_install_nulluser_hlt(Pinball2000MachineState *s);
void p2k_install_allegro_fix(Pinball2000MachineState *s);
void p2k_install_gfxlist_watch(Pinball2000MachineState *s);

/* p2k-display.c: 640×480 SDL/QEMU display reading FB at RAM 0x800000. */
void p2k_install_display(void);

/* p2k-dcs.c: DCS audio MMIO state machine on BAR4 (0x13000000, 16 MiB). */
void p2k_install_dcs(void);
void p2k_install_dcs_uart(void);

/* p2k-lpt-board.c: minimal LPT driver-board protocol on 0x378-0x37A
 * (STATUS=0x87 signature + edge-detect dispatch, all inputs idle). */
void p2k_install_lpt_board(void);

/* p2k-bar3-flash.c: BAR3 update flash @ 0x12000000 seeded from
 * savedata/<game>.flash (4 MiB). */
void p2k_install_bar3_flash(Pinball2000MachineState *s);

/* p2k-pic-fixup.c: keep IRQ0 + cascade force-unmasked once XINU is up
 * (mirrors unicorn.old/src/io.c:121-127). */
void p2k_install_pic_fixup(void);

/* p2k-irq0-shim.c: one-shot inject EOI+IRET stub at 0x500 and patch
 * IDT[0x20] so early IRQ0 doesn't fall into XINU's panic dispatcher.
 * Naturally overridden when XINU's clkinit() installs real clkint. */
void p2k_install_irq0_shim(void);
void p2k_install_cyrix_0f3c(void);
void p2k_install_cyrix_ccr(void);
void p2k_install_superio(void);

/* p2k-vsync.c: ~57 Hz VBLANK ticker — writes BAR2_SRAM[4]=1 + DC_TIMING2
 * at end-of-frame, cycles DC_TIMING2 0..240 in between. */
void p2k_install_vsync(void);

/* p2k-watchdog.c: scan game code for CMP [imm32],0xFFFF watchdog cells
 * and periodically scribble 0xFFFF into them so XINU's pci_watchdog_bone
 * checks pass even without a running watchdog process. */
void p2k_install_watchdog(void);

/* p2k-plx-regs.c: PLX 9050 BAR0 register file + 93C46 SEEPROM model. */
void p2k_install_plx_regs(void);

/* p2k-mem-detect.c: BT-130 — patch XINU mem_detect() prologue to
 * return 14 MiB instead of the 4 MiB the stub controller reports. */
void p2k_install_mem_detect(void);

/* p2k-nic-dseg.c: BT-131 — seed SMC8216T LAN-ROM shadow at 0xD0008. */
void p2k_install_nic_dseg(void);

/* p2k-diag.c: read-only diagnostic sampler — periodically logs PIT
 * channel programming, PIC IMR/ISR/IRR, RTC index, and IDT[0x20]/[0x28].
 * Active only when env P2K_DIAG=1.  No effect on guest execution. */
void p2k_install_diag(Pinball2000MachineState *s);

#endif /* HW_PINBALL2000_INTERNAL_H */
