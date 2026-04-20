/*
 * main.c — Encore Pinball 2000 emulator entry point.
 *
 * Encore (French for "again") — a clean x64 Pinball 2000 emulator using:
 *   - Unicorn Engine for CPU emulation (i386 guest on x64 host)
 *   - SDL2 for display
 *   - SDL2_mixer for audio
 *   - Pre-deinterleaved ROM banks
 *   - ROM-agnostic: auto-detects game from ROM content
 *   - Savedata persistence between sessions
 */
#include "encore.h"

EncoreState g_emu;

static void print_banner(void)
{
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║  Encore — Pinball 2000 Emulator                 ║\n");
    printf("║  CPU: Unicorn Engine (i386)                     ║\n");
    printf("║  Video: SDL2 | Audio: SDL2_mixer                ║\n");
    printf("╚══════════════════════════════════════════════════╝\n\n");
}

static void parse_args(int argc, char **argv)
{
    /* Defaults: auto-detect game, standard paths */
    strncpy(g_emu.game_prefix, "auto", sizeof(g_emu.game_prefix));
    strncpy(g_emu.roms_dir, "../emulator/P2K-runtime/roms", sizeof(g_emu.roms_dir));
    strncpy(g_emu.savedata_dir, "../emulator/P2K-runtime/roms/savedata", sizeof(g_emu.savedata_dir));

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--game") == 0 && i + 1 < argc) {
            strncpy(g_emu.game_prefix, argv[++i], sizeof(g_emu.game_prefix) - 1);
        } else if (strcmp(argv[i], "--roms") == 0 && i + 1 < argc) {
            strncpy(g_emu.roms_dir, argv[++i], sizeof(g_emu.roms_dir) - 1);
        } else if (strcmp(argv[i], "--savedata") == 0 && i + 1 < argc) {
            strncpy(g_emu.savedata_dir, argv[++i], sizeof(g_emu.savedata_dir) - 1);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: encore [OPTIONS]\n\n"
                   "Options:\n"
                   "  --game swe1|rfm|auto  Game selection (default: auto-detect)\n"
                   "  --roms /path          ROM directory\n"
                   "  --savedata /path      Save data directory\n"
                   "  -h, --help            Show this help\n"
                   "\n"
                   "Key bindings (F-row keys are positionally identical on every\n"
                   "keyboard layout — works on QWERTY, AZERTY, DVORAK, etc.):\n"
                   "\n"
                   "  F1            Quit\n"
                   "  F2            Flip display vertically\n"
                   "  F3            Screenshot\n"
                   "  F4            Toggle COIN DOOR (closed/open interlock)\n"
                   "  F6            LEFT  action button   (Phys[10].b7)\n"
                   "  F7            LEFT  flipper         (Phys[10].b5)\n"
                   "  F8            RIGHT flipper         (Phys[10].b4)\n"
                   "  F9            RIGHT action button   (Phys[10].b6)\n"
                   "  F10 / C       Insert credit (queueable; mash for multi)\n"
                   "  F11           Toggle FULLSCREEN\n"
                   "  F12           Dump guest switch state to stderr\n"
                   "  SPACE         START button (sw=2 / Phys[0].b2)\n"
                   "\n"
                   "Coin-door panel (4 buttons, dual-function by mode):\n"
                   "                       attract        |   service / test\n"
                   "                       --------------- + -----------------\n"
                   "  ESC / LEFT      btn1: Service Credits | Escape     (Phys[9].b0)\n"
                   "  DOWN  - KP_-    btn2: Volume −        | Menu Down  (Phys[9].b1)\n"
                   "  UP    = KP_+    btn3: Volume +        | Menu Up    (Phys[9].b2)\n"
                   "  RIGHT ENTER\n"
                   "        KP_ENT    btn4: Begin Test      | Enter      (Phys[9].b3)\n");
            exit(0);
        }
    }
}

/*
 * ROM-agnostic pattern scanning: find a 4-byte value in guest RAM.
 * Returns offset or 0 if not found.
 */
__attribute__((unused))
static uint32_t scan_ram_u32(uint32_t start, uint32_t end, uint32_t target)
{
    if (!g_emu.ram || end > RAM_SIZE) return 0;
    for (uint32_t off = start; off + 4 <= end; off += 4) {
        if (*(uint32_t *)(g_emu.ram + off) == target)
            return off;
    }
    return 0;
}

/*
 * Minimal boot assistance — applied only when correct emulation alone
 * doesn't suffice. ROM-agnostic: uses pattern scanning, not hardcoded addresses.
 */
static void apply_boot_assist(void)
{
    uint8_t *ram = g_emu.ram;
    if (!ram) return;

    /* 1. IVT safety — fill IVT[0..255] with IRET+EOI stub at 0x20000.
     *    Required because Unicorn doesn't have default interrupt handlers. */
    {
        uint8_t *stub = ram + 0x20000u;
        stub[0] = 0x50;                     /* PUSH AX */
        stub[1] = 0xB0; stub[2] = 0x20;     /* MOV AL, 0x20 */
        stub[3] = 0xE6; stub[4] = 0x20;     /* OUT 0x20, AL (EOI) */
        stub[5] = 0x58;                     /* POP AX */
        stub[6] = 0xCF;                     /* IRET */
        uint32_t *ivt = (uint32_t *)ram;
        for (int i = 0; i < 256; i++) ivt[i] = 0x20000000u;
        LOG("boot", "IVT[0..255] → IRET+EOI stub at 0x20000\n");
    }

    /* 2. Safety stub at 0x400000: MOV EAX,1; RET — catches null vtable calls.
     *    This is emulator infrastructure, not a guest patch. */
    {
        uint8_t *stub = ram + 0x400000u;
        stub[0] = 0xB8; stub[1] = 0x01; stub[2] = 0x00;
        stub[3] = 0x00; stub[4] = 0x00; stub[5] = 0xC3;
    }

    fflush(stdout);
}

/*
 * Apply post-ROM-copy patches. Called AFTER option ROM is copied to 0x80000
 * (by cpu_setup_protected_mode). These patches are in-RAM, not in ROM files.
 */
static void apply_ram_patches(void)
{
    /* Safety halt at option ROM offset 0x1F7 (0x801F7):
     * This is garbled 16-bit PM switch code that shouldn't execute.
     * If Init2 returns instead of jumping to game code, we catch it cleanly. */
    uint8_t halt_loop[] = { 0xF4, 0xEB, 0xFD };  /* HLT; JMP $-3 */
    uc_mem_write(g_emu.uc, 0x801F7, halt_loop, sizeof(halt_loop));
    LOG("boot", "Safety halt at 0x801F7 (post-Init2 fallthrough catcher)\n");
}

/* DCS presence table — no longer zeroed. Game needs it to detect DCS2
 * hardware and create the DCS2 driver task. NonFatal from missing DCS
 * board is caught by the NonFatal→XOR EAX,EAX;RET patch in cpu.c. */

/* PRISM option ROM framebuffer write NOP + checksum fix.
 * ROM-agnostic: scan for the MOV [EDI+...], EAX pattern. */
static void patch_prism_rom(void)
{
    if (!g_emu.rom_banks[0] || g_emu.rom_sizes[0] < PRISM_ROM_SIZE) return;
    uint8_t *rom = g_emu.rom_banks[0];

    if (rom[0] != 0x55 || rom[1] != 0xAA) return;

    uint32_t rom_len = rom[2] * 512u;
    if (rom_len > PRISM_ROM_SIZE) rom_len = PRISM_ROM_SIZE;

    /* Scan for framebuffer write pattern: 89 87 xx xx xx xx (MOV [EDI+disp32], EAX)
     * where disp32 is a high address (framebuffer). Look for writes to 0x40xxxxxx. */
    for (uint32_t off = 0x100; off < rom_len - 6; off++) {
        if (rom[off] == 0x89 && rom[off+1] == 0x87) {
            uint32_t disp = *(uint32_t *)&rom[off+2];
            if ((disp & 0xFF000000u) == 0x40000000u) {
                for (int i = 0; i < 6; i++) rom[off + i] = 0x90; /* NOP */
                LOG("boot", "PRISM ROM: NOP'd framebuf write at 0x%x (disp=0x%08x)\n", off, disp);
                break;
            }
        }
    }

    /* Fix checksum */
    uint8_t sum = 0;
    for (uint32_t i = 0; i < rom_len; i++) sum += rom[i];
    if (sum != 0) {
        rom[rom_len - 1] -= sum;
        LOG("boot", "PRISM ROM: checksum fixed\n");
    }
}

static void setup_timer(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = cpu_timer_handler;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &sa, NULL);

    /* SIGALRM disabled — tick injection is iteration-count based.
     * SIGALRM signal delivery was interfering with Unicorn JIT execution
     * (signal masking, cpu_exit from signal handler context).
     * HLT wakeup handled by busy-wait with nanosleep. */
    LOG("timer", "SIGALRM disabled — iteration-count ticks only\n");
}

static void cleanup_and_save(void)
{
    /* Save state before exit */
    savedata_save();

    display_cleanup();
    sound_cleanup();
}

int main(int argc, char **argv)
{
    memset(&g_emu, 0, sizeof(g_emu));
    print_banner();
    parse_args(argc, argv);

    LOG("init", "Game: %s | ROMs: %s | Savedata: %s\n",
        g_emu.game_prefix, g_emu.roms_dir, g_emu.savedata_dir);

    /* Initialize subsystems in order:
     * 1. ROMs (load files from disk)
     * 2. CPU (create Unicorn engine)
     * 3. Memory (map guest physical address space)
     */
    if (rom_load_all() != 0) {
        fprintf(stderr, "Failed to load ROMs\n");
        return 1;
    }

    if (cpu_init() != 0) {
        fprintf(stderr, "Failed to init CPU\n");
        return 1;
    }

    if (memory_init() != 0) {
        fprintf(stderr, "Failed to init memory\n");
        return 1;
    }

    io_init();
    bar_init();  /* sets EEPROM defaults, but .see file already loaded by rom_init */

    /* Re-apply EEPROM defaults — .see file may contain stale data from prior POC */
    bar_seeprom_reinit();

    /* Populate NIC LAN ROM data in D-segment guest RAM (BT-131) */
    nic_dseg_init();

    /* Boot assistance (IVT stubs, PRISM ROM fix).
     * DCS presence table left intact so game detects DCS2 hardware. */
    apply_boot_assist();
    patch_prism_rom();

    /* Load NVRAM/SEEPROM into guest memory after mapping */
    if (g_emu.bar2_sram[0] || g_emu.bar2_sram[1]) {
        uc_mem_write(g_emu.uc, WMS_BAR2, g_emu.bar2_sram, BAR2_SIZE);
        LOG("init", "NVRAM loaded into guest BAR2\n");
    }

    /* Write flash into guest BAR3 */
    if (g_emu.flash)
        uc_mem_write(g_emu.uc, WMS_BAR3, g_emu.flash, FLASH_SIZE);

    /* Pre-fill BAR4 with 0xFF (i386 POC copycat: absent DCS2 hardware reads 0xFF).
     * Guest DCS2 driver checks BAR4 initial state during init. */
    {
        uint8_t *bar4_fill = calloc(1, BAR4_SIZE);
        if (bar4_fill) {
            memset(bar4_fill, 0xFF, BAR4_SIZE);
            uc_mem_write(g_emu.uc, WMS_BAR4, bar4_fill, BAR4_SIZE);
            free(bar4_fill);
            LOG("init", "BAR4 pre-filled with 0xFF (%u MB)\n", BAR4_SIZE >> 20);
        }
    }

    /* Set up protected mode (skip BIOS, start at option ROM PM code) */
    if (cpu_setup_protected_mode() != 0) {
        fprintf(stderr, "Failed to setup protected mode\n");
        return 1;
    }

    /* Apply RAM patches after ROM is in guest memory */
    apply_ram_patches();

    /* Display and sound (non-fatal if they fail) */
    if (display_init() != 0)
        LOG("warn", "Display init failed — running headless\n");

    if (sound_init() != 0)
        LOG("warn", "Sound init failed — running silent\n");

    /* Start timer and run */
    setup_timer();
    g_emu.running = true;

    LOG("cpu", "Starting Encore in protected mode...\n\n");
    fflush(stdout);

    cpu_run();

    cleanup_and_save();

    LOG("exit", "Encore finished (exec_count=%lu frames=%d)\n",
        (unsigned long)g_emu.exec_count, g_emu.frame_count);
    return 0;
}
