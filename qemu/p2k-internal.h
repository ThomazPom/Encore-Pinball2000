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
    char            *update_path;   /* directory holding *_bootdata/im_flsh0/game/symbols.rom; NULL = no update */
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
void p2k_install_gfxlist_watch(Pinball2000MachineState *s);

/* p2k-display.c: 640×480 SDL/QEMU display reading FB at RAM 0x800000. */
void p2k_install_display(void);

/* p2k-dcs-core.c: single shared DCS-2 state machine.  Both p2k-dcs.c
 * (BAR4 MMIO) and p2k-dcs-uart.c (I/O 0x138-0x13F) MUST be thin views
 * over this core; do not introduce a parallel queue/handshake again. */
void     p2k_dcs_core_reset(void);
void     p2k_dcs_core_write_cmd(uint16_t cmd);
uint16_t p2k_dcs_core_read_resp(void);
bool     p2k_dcs_core_has_resp(void);
uint8_t  p2k_dcs_core_flag_byte(void);
void     p2k_dcs_core_set_echo(uint8_t v);
uint8_t  p2k_dcs_core_get_echo(void);
/* Source tag for diagnostic classification: each frontend (BAR4 MMIO,
 * UART overlay) calls note_source() with a short literal tag right
 * BEFORE p2k_dcs_core_write_cmd().  The audio hooks read it via
 * p2k_dcs_core_source() to attribute every cmd to its frontend. */
void        p2k_dcs_core_note_source(const char *src);
const char *p2k_dcs_core_source(void);

/* DCS dispatch mode (Unicorn parity: --dcs-mode io-handled | bar4-patch).
 * Resolved once from env P2K_DCS_MODE on first call. */
bool        p2k_dcs_core_mode_is_io_handled(void);
const char *p2k_dcs_core_mode_name(void);

/* p2k-dcs.c: BAR4 MMIO frontend (0x13000000, 16 MiB). */
void p2k_install_dcs(void);
/* p2k-dcs-uart.c: I/O 0x138-0x13F UART/DCS frontend. */
void p2k_install_dcs_uart(void);
/* p2k-dcs-audio.c: QEMU audiodev backend with real pb2kslib sample
 * playback (8-voice software mixer). The wrapper enables it when it
 * auto-detects a host backend; P2K_NO_DCS_AUDIO forces it off. */
void p2k_install_dcs_audio(Pinball2000MachineState *s);

/* p2k-lpt-board.c: minimal LPT driver-board protocol on 0x378-0x37A
 * (STATUS=0x87 signature + edge-detect dispatch, all inputs idle). */
void p2k_install_lpt_board(void);

/* p2k-bar3-flash.c: BAR3 update flash @ 0x12000000 seeded from
 * savedata/<game>.flash (4 MiB). */
void p2k_install_bar3_flash(Pinball2000MachineState *s);

/* p2k-pic-fixup.c: keep IRQ0 + cascade force-unmasked once XINU is up
 * (mirrors unicorn.old/src/io.c:121-127). */
void p2k_install_pic_fixup(void);

void p2k_install_cyrix_ccr(void);
uint8_t p2k_cyrix_ccr_get(uint8_t index);
void p2k_install_superio(void);

/* p2k-mediagx-gate.c: runtime gate for Cyrix/MediaGX TCG opcode
 * extensions (0F 3A/3B/3C/3D and friends). pinball2000 init must call
 * p2k_mediagx_enable_extensions() so the helpers added by
 * qemu/upstream-patches/0001-i386-tcg-cyrix-mediagx-shim.patch take
 * effect; outside the pinball2000 machine the gate stays FALSE and the
 * helpers behave as plain #UD (preserving SSE4 dispatch on 0F 3A).
 * p2k_mediagx_note_opcode/get_opcode_count back the per-opcode hit
 * counters used by the diag panel. */
void     p2k_mediagx_enable_extensions(void);
unsigned p2k_mediagx_note_opcode(uint8_t op2);
unsigned p2k_mediagx_get_opcode_count(uint8_t op2);

/* p2k-vsync.c: ~57 Hz VBLANK ticker — writes BAR2_SRAM[4]=1 + DC_TIMING2
 * at end-of-frame, cycles DC_TIMING2 0..240 in between. */
void p2k_install_vsync(void);

/* p2k-plx-regs.c: PLX 9050 BAR0 register file + 93C46 SEEPROM model. */
void p2k_install_plx_regs(void);

/* p2k-mem-detect.c: BT-130 — patch XINU mem_detect() prologue to
 * return 14 MiB instead of the 4 MiB the stub controller reports. */
void p2k_install_mem_detect(void);

/* p2k-nic-dseg.c: BT-131 — seed SMC8216T LAN-ROM shadow at 0xD0008. */
void p2k_install_nic_dseg(void);

/* p2k-diag.c: read-only diagnostic sampler — periodically logs PIT
 * channel programming, PIC IMR/ISR/IRR, RTC index, and IDT[0x20]/[0x28].
 * OPT-IN: only active when env P2K_DIAG=1 (or `run-qemu.sh -v`).
 * No effect on guest execution. */
void p2k_install_diag(Pinball2000MachineState *s);

/* p2k-probe-cell-shim.c: STRICTLY GATED guest-data scribble for
 * --update none / P2K_NO_AUTO_UPDATE parity. Mirrors Unicorn's
 * "watchdog/probe-cell @ pci_watchdog_bone()" RAM_WR32 maintenance.
 * Active ONLY when P2K_NO_AUTO_UPDATE is set. NEVER active on normal
 * update boots. Documented as a temporary compatibility bridge. */
void p2k_install_probe_cell_shim(void);


/* p2k-timing-audit.c: single-line "is QEMU virtual time really driving
 * this run?" panel. Reports clock/icount/PIT/PIC/IDT/host-slow scale.
 * Default ON (initial line @3 s, exit line at shutdown). With P2K_DIAG=1
 * also emits one line every 5 s. Disable with P2K_NO_TIMING_AUDIT=1. */
void p2k_install_timing_audit(Pinball2000MachineState *s);

#endif /* HW_PINBALL2000_INTERNAL_H */
