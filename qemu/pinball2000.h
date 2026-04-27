/*
 * Pinball 2000 hardware address map / register constants.
 *
 * Distilled from the Unicorn-era reverse-engineering work (see unicorn.old/).
 * These are *board* facts that survive the architecture switch — they describe
 * the silicon, not the emulator.
 */
#ifndef HW_PINBALL2000_H
#define HW_PINBALL2000_H

/* --- CPU / chipset --------------------------------------------------------
 * Cyrix MediaGX (i486-class core + integrated graphics + PCI host).
 * Custom opcode 0x0F 0x3C "BIST" — Cyrix-only. On a non-Cyrix CPU model
 * this faults #UD and the BIOS recovers via IRET (POP ESI; POP EAX;
 * ADD [ESP],1; IRET). NOT LEAVE+RET — that leaks 8 bytes of stack per
 * invocation and eventually trips the XINU IStack watchdog.
 * (Reference: unicorn.old commit 516210d.)
 *
 * South bridge: Cyrix CS5530 (ISA bridge + i8254 PIT + i8259 PIC).
 *
 * PCI bridge: PLX 9054
 *   BAR0  = ROM window (game ROM bank, paged)
 *   BAR2  = 256 KiB battery-backed SRAM + memory-mapped I/O
 *   BAR4  = PLX runtime registers
 */

/* --- BAR2 SRAM offsets ---------------------------------------------------- */
#define P2K_BAR2_SIZE              0x00040000   /* 256 KiB */
#define P2K_BAR2_WATCHDOG_HEALTH   0x00000420   /* must read 0xFFFF on BT-107 */
#define P2K_BAR2_DC_TIMING2        0x000003F4   /* VSYNC counter, ~57 Hz */

/* --- I/O ports ------------------------------------------------------------ */
#define P2K_IO_PIC_MASTER_CMD      0x0020
#define P2K_IO_PIC_MASTER_DATA     0x0021
#define P2K_IO_PIT_CH0             0x0040
#define P2K_IO_PIT_MODE            0x0043
#define P2K_IO_PIC_SLAVE_CMD       0x00A0
#define P2K_IO_PIC_SLAVE_DATA      0x00A1

#define P2K_IO_DCS2_PORT           0x013C       /* DCS sound byte stream */
#define P2K_IO_LPT_DATA            0x0378       /* LPT driver-board bus */
#define P2K_IO_LPT_STATUS          0x0379
#define P2K_IO_LPT_CTRL            0x037A

/* --- DCS protocol --------------------------------------------------------- *
 * Two consecutive byte writes form a 16-bit command (HIGH then LOW).
 * 0x00 reset byte → device must answer 0xF0.
 * Examples seen: 0x03D4/0x03D6 sample play, 0x003A "dong",
 * 0xACE1 boot init handshake.
 * (Reference: unicorn.old commit 0001de2 + io-handled DCS path.)
 */

/* --- LPT idle protocol ---------------------------------------------------- *
 * Opcode 0x00 → reply 0xF0 (NOT 0x00). Real silicon behavior.
 * (Reference: unicorn.old commit 817afac.)
 */

/* --- PIT timing ----------------------------------------------------------- *
 * SWE1 programs PIT channel 0 with divisor ~298 → ~4004 Hz IRQ0.
 * QEMU's i8254 already handles this correctly; do NOT override.
 */
#define P2K_IRQ0_DIVISOR_SWE1      298

/* --- Boot entry path ------------------------------------------------------ *
 * roms/bios.bin is NOT executed in the Unicorn project (and there is no
 * evidence the real BIOS POST is required for the game to run).  The actual
 * first instructions live in the PRISM option ROM, which is the first 32 KiB
 * of game ROM bank 0 (chips u100 + u101 interleaved).
 *
 * Unicorn boot recipe (cpu.c:200-227):
 *   1. Already in protected mode: CS=0x08, DS=ES=SS=FS=GS=0x10 from a
 *      hand-built GDT.
 *   2. Copy bank0[0x0..0x8000] (PRISM option ROM) to RAM 0x80000.
 *   3. ESP = 0x8B000.
 *   4. EFLAGS = 0x00000002 (only reserved bit; IF=0).
 *   5. EIP = 0x801D9 (PM entry; skips the real-mode call pair at
 *      0x801BF/0x801C4 that would otherwise be reached from a BIOS
 *      INT 19 boot path).
 *
 * The QEMU pinball2000 machine should reproduce the same final CPU state.
 * Two viable approaches:
 *   (a) Hand-init: do steps 1-5 in pinball2000_init() before TCG starts.
 *   (b) Real BIOS: load bios.bin at 0xFFFF0000 and let it run through POST
 *       and INT 19, but then the option ROM must hit its real-mode entry
 *       (0x801BF/0x801C4 path) and switch to PM itself.  Untested.
 * Recipe (a) is the proven path.
 */
#define P2K_BANK_SIZE              0x01000000   /* 16 MiB per ROM bank (2 chips x 8 MiB, 16-bit pair interleave step 4) */
#define P2K_CHIP_SIZE              0x00800000   /* 8 MiB per physical chip */
#define P2K_OPTROM_SIZE            0x00008000   /* 32 KiB PRISM option ROM */
#define P2K_OPTROM_LOAD_ADDR       0x00080000
#define P2K_PM_ENTRY_EIP           0x000801D9
#define P2K_INITIAL_ESP            0x0008B000
#define P2K_INITIAL_CS_SEL         0x0008
#define P2K_INITIAL_DS_SEL         0x0010
#define P2K_RAM_SIZE               0x01000000   /* 16 MiB DRAM */

#endif /* HW_PINBALL2000_H */
