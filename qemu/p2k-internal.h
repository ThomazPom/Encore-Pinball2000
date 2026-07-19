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

static inline bool p2k_no_savedata_enabled(void)
{
    const char *v = getenv("P2K_NO_SAVEDATA");
    return v && *v && strcmp(v, "0") != 0;
}

static inline bool p2k_fresh_savedata_enabled(void)
{
    const char *v = getenv("P2K_FRESH_SAVEDATA");
    return v && *v && strcmp(v, "0") != 0;
}

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
bool p2k_dcs_adsp_source_key(Pinball2000MachineState *s, char key[65]);

typedef struct P2KDcsProfileSnapshot {
    bool available;
    bool state_lock_busy;
    bool core_lock_busy;
    bool worker_started;
    bool worker_run;
    bool host_boot;
    bool hybrid_engine;
    unsigned command_count;
    unsigned ring_frames;
    int output_rate;
    uint64_t cycles;
    uint64_t pcm_frames;
} P2KDcsProfileSnapshot;

/* Non-blocking snapshot used only by the opt-in rare-gap profiler. */
void p2k_dcs_adsp_profile_snapshot(P2KDcsProfileSnapshot *snapshot);

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
void *p2k_gx_regs_host(void);
void p2k_install_gp_blt(void);
void p2k_install_gfxlist_watch(Pinball2000MachineState *s);

/* p2k-display.c: 640×480 SDL/QEMU display reading FB at RAM 0x800000. */
void p2k_install_display(void);
/* F2 Y-flip toggle. Default on (matches the default framebuffer orientation). */
void p2k_display_toggle_flip_y(void);
void p2k_display_set_status(const char *status);
void p2k_display_refresh_status(void);

/* p2k-dcs-core.c: single shared DCS-2 state machine.  Both p2k-dcs.c
 * (BAR4 MMIO) and p2k-dcs-uart.c (I/O 0x138-0x13F) MUST be thin views
 * over this core; do not introduce a parallel queue/handshake again. */
void     p2k_dcs_core_reset(void);
void     p2k_dcs_core_write_cmd(uint16_t cmd);
uint16_t p2k_dcs_core_read_resp(void);
bool     p2k_dcs_core_has_resp(void);
uint8_t  p2k_dcs_core_flag_byte(void);
void     p2k_dcs_core_set_flag(uint16_t v);
void     p2k_dcs_core_set_echo(uint8_t v);
uint8_t  p2k_dcs_core_get_echo(void);
/* Source tag for diagnostic classification: each frontend (BAR4 MMIO,
 * UART overlay) calls note_source() with a short literal tag right
 * BEFORE p2k_dcs_core_write_cmd().  The audio hooks read it via
 * p2k_dcs_core_source() to attribute every cmd to its frontend. */
void        p2k_dcs_core_note_source(const char *src);
const char *p2k_dcs_core_source(void);

/* DCS dispatch mode selected by --dcs-mode io-handled | bar4-patch.
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
bool p2k_dcs_adsp_prepare(Pinball2000MachineState *s);
void p2k_dcs_adsp_write_cmd(uint16_t command);
void p2k_dcs_adsp_host_reset(void);
void p2k_dcs_adsp_render(int16_t *samples, int frames, int output_rate);
bool p2k_dcs_adsp_generate_track(uint16_t command, size_t hint_frames_44100,
                                 int16_t **pcm_44100, size_t *frames_44100,
                                 bool *loop);
void p2k_dcs_audio_adsp_runtime_ready(void);
uint8_t p2k_dcs_adsp_flag_byte(void);
uint16_t p2k_dcs_adsp_read_response(void);

/* Audio dispatch hooks owned by p2k-dcs-core.c, populated by
 * p2k-dcs-audio.c at install time. NULL when the audio backend is
 * disabled. */
extern void (*p2k_dcs_core_audio_process_cmd)(uint16_t cmd);
extern void (*p2k_dcs_core_audio_execute_mixer)(uint16_t cmd,
                                                uint16_t data1,
                                                uint16_t data2);
extern void (*p2k_dcs_core_audio_raw_cmd)(uint16_t cmd);

/* p2k-lpt-board.c: minimal LPT driver-board protocol on 0x378-0x37A
 * (STATUS=0x87 signature + edge-detect dispatch, all inputs idle). */
void p2k_install_lpt_board(void);
void p2k_lpt_host_key(int qcode, bool down);

/* p2k-bar3-flash.c: BAR3 update flash @ 0x12000000 seeded from
 * savedata/<game>.flash (4 MiB). */
void p2k_install_bar3_flash(Pinball2000MachineState *s);

void p2k_install_cyrix_ccr(void);
uint8_t p2k_cyrix_ccr_get(uint8_t index);
void p2k_install_superio(void);

/* p2k-mediagx-gate.c: runtime gate for Cyrix/MediaGX TCG opcode
 * extensions (0F 3A/3B/3C/3D and friends). pinball2000 init must call
 * p2k_mediagx_enable_extensions() so the helpers added by
 * qemu/upstream-patches/mediagx-instructions/ take
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
void p2k_install_plx_regs(Pinball2000MachineState *s);

/* p2k-mem-detect.c: opt-in XINU sizmem() 4 MiB -> 14 MiB override. */
void p2k_install_mem_detect(void);

/* p2k-nic-dseg.c: BT-131 — seed SMC8216T LAN-ROM shadow at 0xD0008. */
void p2k_install_nic_dseg(void);

/* p2k-diag.c: read-only diagnostic sampler — periodically logs PIT
 * channel programming, PIC IMR/ISR/IRR, RTC index, and IDT[0x20]/[0x28].
 * OPT-IN: only active when env P2K_DIAG=1 (or `run-qemu.sh -v`).
 * No effect on guest execution. */
void p2k_install_diag(Pinball2000MachineState *s);

/* p2k-probe-cell-shim.c: STRICTLY GATED guest-data scribble for
 * --update none / P2K_NO_AUTO_UPDATE parity. Implements the required
 * "watchdog/probe-cell @ pci_watchdog_bone()" RAM_WR32 maintenance.
 * Active ONLY when P2K_NO_AUTO_UPDATE is set. NEVER active on normal
 * update boots. Documented as a temporary compatibility bridge. */
void p2k_install_probe_cell_shim(void);


/* p2k-timing-audit.c: single-line timing panel. Reports expected PIT
 * cadence separately from observed IRQ0 line, clkint-entry, and PIC EOI
 * counters. Default ON (initial line @3 s, exit line at shutdown). With
 * P2K_DIAG=1 also emits one line every 5 s. */
void p2k_install_timing_audit(Pinball2000MachineState *s);
void p2k_timing_audit_note_irq0_raised(void);
uint64_t p2k_timing_audit_get_irq0_raised(void);
uint64_t p2k_timing_audit_get_irq0_serviced(void);
void p2k_timing_audit_note_clkint_enter(uint64_t eip);
void p2k_timing_audit_note_pic_eoi(bool master, int irq, uint8_t ocw2);
bool p2k_clkint_tcg_match_pc(uint64_t pc, uint64_t cs_base);

/* p2k-stall-profile.c: opt-in EIP/IF/halted ring-buffer profiler that
 * samples CPU state every PIT-tap rising edge whose unservice gap
 * (raised − serviced) exceeds a threshold. Histogram emits
 * once per audit snapshot. Off unless P2K_PROFILE_STALLS=1. Costs one
 * integer compare per PIT raise when disabled. */
void p2k_stall_profile_init(void);
bool p2k_stall_profile_active(void);
void p2k_stall_profile_sample(uint32_t deficit);
void p2k_stall_profile_dump(void);

/* clkint segment-dwell observers (Phase 2 audit). The CPU-side intack
 * and IRET observers live in target/i386/tcg/seg_helper.c (added by
 * upstream patch 0002 as weak stubs that the audit module overrides
 * here). The PIC-side EOI observer is the same hook used for delivery
 * accounting. Together with the existing TCG IDT-entry helper they
 * decompose every IRQ0 cycle into four named segments:
 *   raise   -> intack       (PIC + CPU dispatch latency)
 *   intack  -> handler entry (frame push + CS:EIP load)
 *   entry   -> EOI           (handler work before EOI)
 *   EOI     -> IRET          (handler work after EOI + return)
 * and one cross-cycle gap (IRET -> next raise = idle / non-handler).
 * Implemented with single timestamps (no rings) so the lookup is never
 * stale even when the i8259 IRR coalesces edges. */
void p2k_timing_audit_note_intack(int intno);
void p2k_timing_audit_note_iret(uint32_t eip);

/* p2k-timing-audit.c additions: PIT raise -> clkint latency histogram
 * (per-snapshot p50/p95/p99/max in microseconds) + PDB 0x05 max
 * refresh gap reporter. Both are ON by default and cheap; gate off
 * with P2K_NO_TIMING_AUDIT=1 (same gate as the audit panel itself). */
void p2k_timing_audit_note_clkint_latency_sample(void);
void p2k_timing_audit_note_pdb05(void);

/* p2k-lpt-board.c: driver-board activity counters. Per Erikie's pinside
 * msg #36, "in the end it matters if you get 16khz to driverboard ...
 * that is the clock it expects". These let the audit report what the
 * guest is actually writing to the LPT output (data port writes, ctrl
 * port writes, and successful host dispatches). */
uint64_t p2k_lpt_get_data_writes(void);
uint64_t p2k_lpt_get_ctrl_writes(void);
uint64_t p2k_lpt_get_dispatches(void);

/* p2k-display.c: monotonic count of full frame submissions to the QEMU
 * display backend (one increment per dpy_gfx_update_full). The audit
 * derives per-interval FPS from the delta. */
uint64_t p2k_display_get_frames(void);

/* pinball2000.c: deliberate game-clock scaling shared by PIT, HOTLOOP,
 * diagnostics and the CLI. 100.0 is physical cabinet timing. */
double p2k_speed_target_percent(void);

/* p2k-clkint-hotloop.c: HOTLOOP-based
 * IRQ0 delivery. See qemu/p2k-clkint-hotloop.c and
 * docs/12-cpu-and-timers.md for the mechanism. */

/* Accessors used by sibling p2k modules (defined in p2k-timing-audit.c). */
bool     p2k_audit_in_clkint(void);
uint64_t p2k_audit_pit_period_ns(void);
uint64_t p2k_audit_clkint_entered_count(void);

/* HOTLOOP IRQ0 delivery API. */
bool     p2k_clkint_hotloop_enabled(void);
bool     p2k_clkint_hotloop_no_pit(void);
bool     p2k_clkint_hotloop_uses_host_timer(void);
void     p2k_clkint_hotloop_connect_irq(qemu_irq irq0);
bool     p2k_clkint_hotloop_uses_pit_stub(void);
void     p2k_clkint_hotloop_pit_write(hwaddr addr, uint64_t value);
void     p2k_clkint_hotloop_maybe_raise(CPUState *cs);
uint64_t p2k_clkint_hotloop_count_reraises(void);
uint64_t p2k_clkint_hotloop_count_skipped_pending(void);
uint64_t p2k_clkint_hotloop_count_skipped_isr(void);
uint64_t p2k_clkint_hotloop_count_skipped_imr(void);
uint64_t p2k_clkint_hotloop_count_skipped_if0(void);
uint64_t p2k_clkint_hotloop_count_skipped_shadow(void);
uint64_t p2k_clkint_hotloop_count_skipped_in_clkint(void);
uint64_t p2k_clkint_hotloop_count_skipped_min_gap(void);
int64_t  p2k_clkint_hotloop_current_gap_ns(void);
double   p2k_clkint_hotloop_measured_hz(void);
bool     p2k_clkint_hotloop_adaptive_enabled(void);
void     p2k_hotloop_note_swallowed_edge(void);
uint64_t p2k_hotloop_swallowed_edges_count(void);
int64_t  p2k_clkint_hotloop_jitter_min_ns(void);
int64_t  p2k_clkint_hotloop_jitter_max_ns(void);
uint64_t p2k_clkint_hotloop_jitter_count(void);
uint64_t p2k_clkint_hotloop_jitter_mean_ns(void);
uint64_t p2k_clkint_hotloop_jitter_stddev_us(void);

#endif /* HW_PINBALL2000_INTERNAL_H */
