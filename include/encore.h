/*
 * encore.h — Encore Pinball 2000 emulator.
 * Shared types, constants, memory map, and extern declarations.
 */
#ifndef ENCORE_H
#define ENCORE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unicorn/unicorn.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

/* =========================================================================
 * Physical memory map — Cyrix MediaGX + WMS PRISM
 * ========================================================================= */
/* Direct RAM access macros — g_emu.ram is uc_mem_map_ptr backing store,
 * so writing through it is identical to uc_mem_write but with zero API
 * overhead.  addr MUST be < RAM_SIZE. */
#define RAM_RD32(addr)       (*(uint32_t *)(g_emu.ram + (uint32_t)(addr)))
#define RAM_WR32(addr, val)  do { *(uint32_t *)(g_emu.ram + (uint32_t)(addr)) = (uint32_t)(val); } while(0)
#define RAM_RD8(addr)        (*(uint8_t  *)(g_emu.ram + (uint32_t)(addr)))

#define RAM_SIZE          0x01000000u   /* 16 MB guest RAM */
#define BIOS_SIZE         0x00010000u   /* 64 KB BIOS */
#define PRISM_ROM_SIZE    0x00008000u   /* 32 KB option ROM */
#define BANK_SIZE         0x01000000u   /* 16 MB per ROM bank */
#define DCS_BANK_SIZE     0x00800000u   /* 8 MB DCS sound ROM */
#define FLASH_SIZE        0x00400000u   /* 4 MB update flash */

/* Guest physical addresses */
#define GUEST_RAM         0x00000000u
#define GUEST_VGA_FB      0x000A0000u   /* VGA framebuffer area (128KB) */
#define GUEST_OPTION_ROM  0x000C0000u   /* PRISM option ROM */
#define GUEST_BIOS_SHADOW 0x000F0000u   /* BIOS shadow */

#define PLX_BANK0         0x08000000u   /* PLX chip-select bank 0 (LAS3BA) */
#define PLX_BANK1         0x08800000u   /* CS0BASE */
#define PLX_BANK2         0x09800000u   /* CS1BASE */
#define PLX_BANK3         0x0A800000u   /* CS2BASE */
#define PLX_CS3_DCS       0x0B800000u   /* CS3BASE — DCS2 sound ROM */

#define WMS_BAR0          0x10000000u   /* PLX 9050 register file */
#define WMS_BAR2          0x11000000u   /* DCS2 interface + char display */
#define WMS_BAR3          0x12000000u   /* Update flash (4MB) */
#define WMS_BAR4          0x13000000u   /* DCS audio board */
#define BAR5_BANK0        0x14000000u   /* ROM bank0 flash window */
#define BAR5_BANK1        0x15000000u
#define BAR5_BANK2        0x16000000u
#define BAR5_BANK3        0x17000000u
#define ROMBAR_DCS        0x18000000u   /* DCS2 sound ROM BAR5 */

#define GX_BASE           0x40000000u   /* MediaGX config registers (8MB) */
#define GX_FB             0x40800000u   /* Framebuffer (4MB) */

#define BIOS_RESET        0xFFFF0000u   /* BIOS reset vector mapping */

/* BAR sizes */
#define BAR0_SIZE         0x00000060u   /* 96 bytes PLX regs */
#define BAR2_SIZE         0x00020000u   /* 128 KB SRAM */
#define BAR3_SIZE         FLASH_SIZE    /* 4 MB flash */
#define BAR4_SIZE         0x01000000u   /* 16 MB DCS */
#define GX_BASE_SIZE      0x01000000u   /* 16 MB GX regs + FB */

/* DCS presence table */
#define DCS_PRES_OFF      0x10000u      /* offset in bank0 */
#define DCS_PRES_BYTES    0x01000u      /* 4KB presence table */

/* =========================================================================
 * Display constants
 * ========================================================================= */
#define SCREEN_W          640
#define SCREEN_H          480
#define FB_W              640
#define FB_H              240
#define FB_STRIDE         2048          /* bytes per row in guest FB */

/* =========================================================================
 * GX_BASE MMIO registers (offsets from GX_BASE)
 * ========================================================================= */
#define DC_UNLOCK         0x00008300u
#define DC_GENERAL_CFG    0x00008304u
#define DC_TIMING_CFG     0x00008308u
#define DC_OUTPUT_CFG     0x0000830Cu
#define DC_FB_ST_OFFSET   0x00008310u   /* framebuffer start offset */
#define DC_LINE_SIZE      0x00008314u
#define DC_GFX_PITCH      0x00008318u
#define DC_TIMING1        0x00008350u
#define DC_TIMING2        0x00008354u

/* GP (graphics processor) registers */
#define GP_DST_XCOOR      0x00008100u
#define GP_DST_YCOOR      0x00008104u
#define GP_WIDTH           0x00008108u
#define GP_HEIGHT          0x0000810Cu
#define GP_SRC_XCOOR      0x00008110u
#define GP_SRC_YCOOR      0x00008114u
#define GP_RASTER_MODE    0x00008200u
#define GP_VECTOR_MODE    0x00008204u
#define GP_BLT_MODE       0x00008208u
#define GP_BLT_STATUS     0x0000820Cu

/* =========================================================================
 * I/O port addresses
 * ========================================================================= */
#define PORT_PIC1_CMD     0x0020u
#define PORT_PIC1_DATA    0x0021u
#define PORT_PIT_CH0      0x0040u
#define PORT_PIT_CH1      0x0041u
#define PORT_PIT_CH2      0x0042u
#define PORT_PIT_CMD      0x0043u
#define PORT_KBC_DATA     0x0060u
#define PORT_KBC_CMD      0x0064u
#define PORT_CMOS_ADDR    0x0070u
#define PORT_CMOS_DATA    0x0071u
#define PORT_POST         0x0080u
#define PORT_A20          0x0092u
#define PORT_PIC2_CMD     0x00A0u
#define PORT_PIC2_DATA    0x00A1u
#define PORT_DMA_PAGE     0x0087u
#define PORT_PCI_ADDR     0x0CF8u
#define PORT_PCI_DATA     0x0CFCu
#define PORT_PRISM_IDX    0x0022u       /* PRISM/MediaGX config index */
#define PORT_PRISM_DATA   0x0023u       /* PRISM/MediaGX config data */
#define PORT_LPT_DATA     0x0378u
#define PORT_LPT_STATUS   0x0379u
#define PORT_LPT_CTRL     0x037Au
#define PORT_COM1_BASE    0x03F8u
#define PORT_VGA_MISC_W   0x03C2u
#define PORT_VGA_SEQ_IDX  0x03C4u
#define PORT_VGA_SEQ_DATA 0x03C5u
#define PORT_VGA_CRTC_IDX 0x03D4u
#define PORT_VGA_CRTC_DATA 0x03D5u
#define PORT_VGA_STATUS   0x03DAu
#define PORT_DCS2_STATUS  0x6F96u       /* DCS2 ready = 0x00 */

/* =========================================================================
 * PIC state machine
 * ========================================================================= */
typedef struct {
    uint8_t imr;        /* interrupt mask register */
    uint8_t irr;        /* interrupt request register */
    uint8_t isr;        /* in-service register */
    uint8_t icw_step;   /* ICW init state (0=ready, 1-4=expecting ICWn) */
    uint8_t icw1;
    uint8_t icw2;       /* vector base */
    uint8_t icw3;
    uint8_t icw4;
    uint8_t read_isr;   /* OCW3 read ISR mode */
    bool    init_mode;
} PICState;

/* =========================================================================
 * PIT state
 * ========================================================================= */
typedef struct {
    uint16_t count[3];
    uint16_t latch[3];
    uint8_t  mode[3];
    uint8_t  rw_mode[3];
    bool     latched[3];
    uint8_t  access_lo[3]; /* byte toggle for 16-bit access */
} PITState;

/* =========================================================================
 * DCS2 circular command buffer + response buffer
 * ========================================================================= */
#define DCS_CMD_BUF_SIZE  32
#define DCS_RESP_BUF_SIZE 64
typedef struct {
    uint16_t buf[DCS_CMD_BUF_SIZE];
    int      head;
    int      tail;
    int      count;
} DCSCmdBuf;

typedef struct {
    uint16_t buf[DCS_RESP_BUF_SIZE];
    int      head;
    int      tail;
    int      count;
} DCSRespBuf;

/* =========================================================================
 * Emulator state
 * ========================================================================= */
typedef struct {
    /* Unicorn engine */
    uc_engine *uc;

    /* Guest physical memory */
    uint8_t *ram;                       /* 16 MB guest RAM */
    uint8_t *rom_banks[4];              /* ROM bank data (pre-deinterleaved) */
    size_t   rom_sizes[4];
    uint8_t *dcs_rom;                   /* DCS sound ROM (bank4) */
    size_t   dcs_rom_size;
    uint8_t *bios;                      /* BIOS image */
    size_t   bios_size;
    uint8_t *flash;                     /* Update flash (4MB, starts 0xFF) */

    /* BAR2 SRAM echo memory */
    uint8_t  bar2_sram[BAR2_SIZE];
    uint32_t bar2_wr_count;

    /* PLX 9050 registers */
    uint32_t plx_regs[64];

    /* GX_BASE register space (GP 0x8200 + DC 0x8300 + BC 0x20000) */
    uint32_t dc_fb_offset;
    uint32_t dc_timing2;
    uint32_t gx_regs[0x9000];  /* covers offsets 0x0000-0x23FFF (>>2) */

    /* PCI */
    uint32_t pci_addr;

    /* PIC */
    PICState pic[2];

    /* PIT */
    PITState pit;

    /* CMOS/RTC */
    uint8_t  cmos_addr;
    uint8_t  cmos_data[128];

    /* Keyboard controller */
    uint8_t  kbc_status;
    uint8_t  kbc_outbuf;

    /* VGA state */
    uint8_t  vga_misc;
    uint8_t  vga_seq_idx;
    uint8_t  vga_seq[8];
    uint8_t  vga_crtc_idx;
    uint8_t  vga_crtc[25];
    bool     vga_flipflop;

    /* PRISM config */
    uint8_t  prism_idx;
    uint32_t gx_base_addr;

    /* UART */
    uint8_t  uart_regs[8];
    char     uart_buf[4096];
    int      uart_pos;
    int      uart_lsr_count;           /* LSR poll counter for drain (BT-88) */
    bool     monitor_active;           /* XINU monitor prompt detected */
    int      monitor_inject_pos;       /* position in "continue\r" injection */

    /* LPT — Pinball 2000 driver board interface (BT-120) */
    uint8_t  lpt_data;
    uint8_t  lpt_status;
    uint8_t  lpt_ctrl;
    bool     lpt_active;              /* LPT emulated port activated */

    /* SuperIO / CC5530 */
    uint8_t  superio_idx;              /* W83977EF index register (0x2E) */
    uint8_t  cc5530_idx;               /* CC5530 EEPROM index (0xEA) */

    /* DCS2 sound */
    DCSCmdBuf  dcs_cmds;
    DCSRespBuf dcs_resp;
    uint16_t   dcs_latch;
    uint16_t   dcs_flags;
    bool       dcs_active;   /* active/firmware-upload guard */
    bool       dcs_pending;  /* multi-word command in progress */
    int        dcs_remaining; /* remaining words for multi-word cmd */
    uint16_t   dcs_mixer[4]; /* multi-word accumulator */
    int        dcs_layer;    /* current mixer layer */

    /* Display */
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *texture;
    uint32_t      fb_pixels[SCREEN_W * SCREEN_H];
    uint32_t      rgb555_lut[32768];
    bool          display_ready;
    int           frame_count;

    /* Sound */
    bool          sound_ready;

    /* Runtime flags */
    bool          running;
    bool          xinu_booted;
    bool          xinu_ready;          /* clkint installed AND sysinit complete */
    uint64_t      clkint_ready_exec;   /* exec_count when clkint first detected in IDT[0x20] */
    bool          game_started;
    bool          is_v19_update;       /* running with update flash (V1.19) */
    volatile int timer_pending;       /* count of unprocessed SIGALRM ticks */
    volatile int timer_tick_queue;    /* queued IRQ0 ticks waiting for EOI */
    uint32_t      idt_base;            /* cached IDT base address */
    uint64_t      exec_count;

    /* Game info (ROM-agnostic detection) */
    char          game_prefix[16];      /* "swe1" or "rfm" */
    char          game_id_str[32];      /* "swe1_14" or "rfm_15" */
    uint32_t      game_id;              /* 50069=SWE1, 50070=RFM */
    char          roms_dir[256];
    char          savedata_dir[256];

    /* Savedata */
    uint16_t      seeprom[64];          /* 93C46 SEEPROM (128 bytes) */
    uint32_t      ems[4];              /* EMS state (16 bytes) */

    /* VSYNC counter */
    uint32_t      vsync_count;

    /* IRQ delivery counter (used to gate PIC enforcement) */
    int           irq_ok_count;

    /* IRQ pending */
    bool          irq_pending;
    uint8_t       irq_vector;

    /* POST code */
    uint8_t       post_code;

    /* A20 gate */
    bool          a20_enabled;

    /* ROM-agnostic watchdog health register address (found by scan, 0 if not
     * yet found). cpu.c writes 0xFFFF here each exec iteration so
     * pci_read_watchdog() always returns 0 (healthy), suppressing the
     * pci_watchdog_bone() False Alarm Fatal. Real 200MHz hardware completes
     * game init before the watchdog can expire; Unicorn emulation is slower. */
    uint32_t      watchdog_flag_addr;

    /* Host-driven C++ constructor calling phase (POC BT-64/BT-89).
     * XINU's cpp_call_ctors needs the symbol table at BAR3 flash, which may
     * fail under emulation. Host walks the ctor list at g_ctor_list_addr and
     * calls each entry via trampoline → HLT → next. */
    int           ctor_phase;           /* 0=pending, 1=running, 2=done */
    uint32_t      ctor_list_addr;       /* 0x24A928 for SWE1 V1.19 */
    int           ctor_total;
    int           ctor_idx;
    int           ctor_ok;
    int           ctor_skip;
    uint32_t      ctor_saved_eip;
} EncoreState;

/* Global emulator state */
extern EncoreState g_emu;

/* =========================================================================
 * Module init functions
 * ========================================================================= */
/* cpu.c */
int  cpu_init(void);
int  cpu_setup_protected_mode(void);
void cpu_run(void);
void cpu_inject_interrupt(uint8_t vector);
void cpu_timer_handler(int sig);

/* memory.c */
int  memory_init(void);

/* rom.c */
int  rom_load_all(void);
int  rom_detect_game(void);
int  savedata_load(void);
int  savedata_save(void);

/* pci.c */
uint32_t pci_read(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg);
void     pci_write(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg, uint32_t val);

/* io.c */
uint32_t io_port_read(uint16_t port, int size);
void     io_port_write(uint16_t port, uint32_t val, int size);
void     io_init(void);
void     nic_dseg_init(void);  /* populate NIC LAN ROM in D-segment guest RAM */
void     lpt_activate(void);   /* activate LPT emulated port for PinIO (BT-93) */
void     lpt_set_host_input(uint8_t buttons, uint8_t switches);
void     lpt_toggle_coin_door(void);
void     lpt_toggle_slam_tilt(void);
void     lpt_inject_switch(int col, uint8_t data);

/* bar.c */
void bar_mmio_read(uc_engine *uc, uc_mem_type type, uint64_t addr, int size, int64_t value, void *user_data);
void bar_mmio_write(uc_engine *uc, uc_mem_type type, uint64_t addr, int size, int64_t value, void *user_data);
void bar_init(void);
void bar_seeprom_reinit(void);

/* display.c */
int  display_init(void);
void display_update(void);
void display_handle_events(void);
void display_cleanup(void);

/* sound.c */
int  sound_init(void);
int  sound_is_ready(void);
void sound_play_boot_dong(void);
void sound_process_cmd(uint16_t cmd);
void sound_execute_mixer(int cmd, int data1, int data2);
void sound_set_global_volume(int vol);
int  sound_get_global_volume(void);
void sound_start_audio_init_thread(void);
void sound_cleanup(void);

/* =========================================================================
 * Logging
 * ========================================================================= */
#define LOG(tag, fmt, ...) fprintf(stdout, "[" tag "] " fmt, ##__VA_ARGS__)
#define LOG_ONCE(tag, fmt, ...) do { \
    static int _done = 0; \
    if (!_done) { _done = 1; LOG(tag, fmt, ##__VA_ARGS__); } \
} while(0)

#endif /* ENCORE_H */
