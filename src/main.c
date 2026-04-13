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
    strncpy(g_emu.roms_dir, "../deinterleaved_roms_with_update", sizeof(g_emu.roms_dir));
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
                   "  -h, --help            Show this help\n");
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

/* Zero DCS presence bytes in ROM banks — prevents NonFatal from missing DCS board.
 * ROM-agnostic: same offset (0x10000) and size (4KB) in all game ROMs. */
static void patch_dcs_presence(void)
{
    for (int b = 0; b < 4; b++) {
        if (!g_emu.rom_banks[b] || g_emu.rom_sizes[b] < DCS_PRES_OFF + DCS_PRES_BYTES)
            continue;
        int cleared = 0;
        for (uint32_t i = 0; i < DCS_PRES_BYTES; i++) {
            if (g_emu.rom_banks[b][DCS_PRES_OFF + i]) {
                g_emu.rom_banks[b][DCS_PRES_OFF + i] = 0;
                cleared++;
            }
        }
        if (cleared)
            LOG("boot", "Bank %d: zeroed %d DCS presence bytes at 0x%x\n",
                b, cleared, DCS_PRES_OFF);
    }
}

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

    struct itimerval itv;
    itv.it_interval.tv_sec  = 0;
    itv.it_interval.tv_usec = 10000;  /* 10ms = 100Hz */
    itv.it_value.tv_sec     = 0;
    itv.it_value.tv_usec    = 10000;
    setitimer(ITIMER_REAL, &itv, NULL);
    LOG("timer", "SIGALRM at 100Hz (10ms interval)\n");
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

    /* Initialize subsystems in order */
    if (rom_load_all() != 0) {
        fprintf(stderr, "Failed to load ROMs\n");
        return 1;
    }

    if (memory_init() != 0) {
        fprintf(stderr, "Failed to init memory\n");
        return 1;
    }

    if (cpu_init() != 0) {
        fprintf(stderr, "Failed to init CPU\n");
        return 1;
    }

    io_init();
    bar_init();

    /* Boot assistance (IVT stubs, DCS presence, PRISM ROM fix) */
    apply_boot_assist();
    patch_dcs_presence();
    patch_prism_rom();

    /* Load NVRAM/SEEPROM into guest memory after mapping */
    if (g_emu.bar2_sram[0] || g_emu.bar2_sram[1]) {
        uc_mem_write(g_emu.uc, WMS_BAR2, g_emu.bar2_sram, BAR2_SIZE);
        LOG("init", "NVRAM loaded into guest BAR2\n");
    }

    /* Write flash into guest BAR3 */
    if (g_emu.flash)
        uc_mem_write(g_emu.uc, WMS_BAR3, g_emu.flash, FLASH_SIZE);

    /* Display and sound (non-fatal if they fail) */
    if (display_init() != 0)
        LOG("warn", "Display init failed — running headless\n");

    if (sound_init() != 0)
        LOG("warn", "Sound init failed — running silent\n");

    /* Start timer and run */
    setup_timer();
    g_emu.running = true;

    LOG("cpu", "Starting from reset vector 0xFFFFFFF0...\n\n");
    fflush(stdout);

    cpu_run();

    cleanup_and_save();

    LOG("exit", "Encore finished (exec_count=%lu frames=%d)\n",
        (unsigned long)g_emu.exec_count, g_emu.frame_count);
    return 0;
}
