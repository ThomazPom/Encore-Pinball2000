/*
 * Experimental original-format DCS asset path.
 *
 * Loads U109/U110 plus a 1 MiB 28f800.rom, recognizes update *_sf.rom files,
 * and runs an ADSP-2104 core with SPORT1/autobuffer output. Keep this engine
 * isolated from the pb2kslib player.
 *
 * P2K_DCS_ENGINE=adsp boots the update flash, executes the original DSP
 * program, maps U109/U110 through the SDRC, and renders SPORT1 PCM. It never
 * consults pb2kslib after original-format asset preparation succeeds.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/notify.h"
#include "qemu/thread.h"
#include "system/system.h"

#include "p2k-internal.h"
#include "p2k-adsp2105-core.h"

#define P2K_DCS_SOUND_FLASH_SIZE (1024 * 1024)
#define P2K_DCS_REGION_WORDS     0x600000
#define P2K_DCS_U109_WORD_OFFSET 0x200000
#define P2K_DCS_U110_WORD_OFFSET 0x400000

static uint8_t *s_sound_flash;
static char s_sound_flash_path[1024];

typedef struct {
    uint16_t data[0x4000];
    uint32_t program[0x4000];
    const uint8_t *sound_rom;
    uint16_t *sound_data;
    size_t sound_words;
    uint16_t control[32];
    uint16_t rom_bank;
    uint16_t sdrc[4];
    uint8_t sdrc_seed;
    uint16_t sram[0x10000];
    uint16_t commands[65536];
    unsigned command_head;
    unsigned command_count;
    uint16_t output_data;
    uint16_t output_control;
    bool output_full;
    uint16_t host_ack[2];
    unsigned host_ack_head;
    unsigned host_ack_count;
    bool initialized;
    bool sport_enabled;
    int ireg;
    int increment;
    int length;
    int base;
    int play_pos;
    int next_irq_pos;
    double source_phase;
    double source_rate;
    double cycle_phase;
    uint64_t cycles;
    uint64_t pcm_frames;
    uint64_t pcm_nonzero;
    unsigned pcm_peak;
    int16_t last_sample[2];
    bool selftest_sent;
    bool selftest_ready;
    bool host_boot;
    unsigned host_boot_pos;
    unsigned host_boot_words;
    uint8_t host_boot_triplet[3];
    bool host_boot_compare_logged;
    QemuMutex lock;
    QemuMutex core_lock;
    QemuCond worker_cond;
    QemuThread worker;
    bool worker_started;
    bool worker_run;
    bool threaded_engine;
} P2KDcsAdsp;

static P2KDcsAdsp s_adsp;

static void *adsp_mailbox_worker(void *opaque);
static void adsp_worker_shutdown(Notifier *notifier, void *data);
static void adsp_worker_start(void);

static Notifier adsp_exit_notifier = {
    .notify = adsp_worker_shutdown,
};

/* Caller holds s_adsp.lock. Diagnostic commands retain their established
 * synchronous response loop and must never be stolen by the worker. */
static bool adsp_worker_has_command(void)
{
    if (!s_adsp.command_count) {
        return false;
    }
    uint16_t command = s_adsp.commands[s_adsp.command_head];
    return command != 0x003a && command != 0x001b && command != 0x00aa;
}

enum {
    S1_AUTOBUF_REG = 15,
    S1_RFSDIV_REG,
    S1_SCLKDIV_REG,
    S1_CONTROL_REG,
    S0_AUTOBUF_REG,
    S0_RFSDIV_REG,
    S0_SCLKDIV_REG,
    S0_CONTROL_REG,
    S0_MCTXLO_REG,
    S0_MCTXHI_REG,
    S0_MCRXLO_REG,
    S0_MCRXHI_REG,
    TIMER_SCALE_REG,
    TIMER_COUNT_REG,
    TIMER_PERIOD_REG,
    WAITSTATES_REG,
    SYSCONTROL_REG,
};

static uint16_t adsp_data_read(uint16_t address)
{
    if (address == 0x0400) {
        qemu_mutex_lock(&s_adsp.lock);
        uint16_t value = 0;
        if (s_adsp.command_count) {
            value = s_adsp.commands[s_adsp.command_head];
        }
        qemu_mutex_unlock(&s_adsp.lock);
        return value;
    }
    if (address == 0x0402) {
        return s_adsp.output_control;
    }
    if (address == 0x0403) {
        qemu_mutex_lock(&s_adsp.lock);
        bool full = s_adsp.command_count != 0;
        qemu_mutex_unlock(&s_adsp.lock);
        return (full ? 0x80 : 0) | (s_adsp.output_full ? 0 : 0x40);
    }
    if (address >= 0x0480 && address <= 0x0483) {
        unsigned reg = address - 0x0480;
        if (reg != 3) {
            return s_adsp.sdrc[reg];
        }
        static const uint16_t security[8] = {
            0x5a81, 0x5aa4, 0x5a00, 0x5ab9,
            0x5a03, 0x5a69, 0x5a20, 0x5aff,
        };
        unsigned mode = (s_adsp.sdrc[0] >> 13) & 7;
        return mode == 2 ? 0x5a00 | ((s_adsp.sdrc_seed & 0x3f) << 1)
                         : security[mode];
    }

    unsigned rom_st = s_adsp.sdrc[0] & 3;
    /* PB2K's flash bootloader reads the /BMS-selected page through DM while
     * ROM_MS is clear. The SDRC selects the unified external ROM region solely
     * with ROM_MS (bit 5). */
    bool rom_enabled = rom_st != 3;
    unsigned rom_base = rom_st == 0 ? 0x0000 : rom_st == 1 ? 0x3000 : 0x3400;
    unsigned page_words = (rom_st != 0 && !(s_adsp.sdrc[0] & 0x10))
                          ? 4096 : 1024;
    if (rom_enabled && address >= rom_base && address < rom_base + page_words) {
        /* The runtime mixer's sample fetch loop lives below PM 0x0800. */
        if (s_adsp.sdrc[0] & 0x20) {
            size_t word = ((size_t)(s_adsp.sdrc[2] & 0x1fff) * page_words +
                           address - rom_base) % s_adsp.sound_words;
            return s_adsp.sound_data[word];
        }
        unsigned page = (s_adsp.sdrc[0] >> 7) & 7;
        size_t word = (size_t)page * page_words + address - rom_base;
        return lduw_le_p(s_sound_flash + ((word * 2) &
                                          (P2K_DCS_SOUND_FLASH_SIZE - 1)));
    }

    unsigned dm_st = s_adsp.sdrc[1] & 3;
    unsigned dm_base = dm_st == 1 ? 0x0000 : dm_st == 2 ? 0x3000 : 0x3400;
    if (dm_st && address >= dm_base && address < dm_base + 0x400) {
        size_t word = ((size_t)(s_adsp.sdrc[2] & 0x7ff) * 1024 +
                       address - dm_base) % s_adsp.sound_words;
        return s_adsp.sound_data[word];
    }

    bool sm_enabled = (s_adsp.sdrc[0] & 0x0800) != 0;
    bool sm_bank = (s_adsp.sdrc[0] & 0x1000) != 0;
    if (sm_enabled) {
        if (!sm_bank && address >= 0x0800 && address <= 0x17ff) {
            return s_adsp.sram[address - 0x0800];
        }
        if (address >= 0x1800 && address <= 0x27ff) {
            return s_adsp.sram[(sm_bank ? 0x3000 : 0x1000) +
                               address - 0x1800];
        }
        if (address >= 0x2800 && address <= 0x37ff) {
            return s_adsp.sram[0x2000 + address - 0x2800];
        }
    }

    if (address >= 0x3800 && address <= 0x39ff) {
        return s_adsp.data[address];
    }
    if (address >= 0x2000 && address <= 0x2fff) {
        size_t offset = ((size_t)(s_adsp.rom_bank & 0x7ff) << 12) |
                        (address - 0x2000);
        return s_adsp.sound_rom[offset & (P2K_DCS_BANK_SIZE - 1)];
    }
    if (address >= 0x3fe0) {
        return s_adsp.control[address - 0x3fe0];
    }
    return 0xffff;
}

static void adsp_data_write(uint16_t address, uint16_t value)
{
    if (address == 0x0400) {
        qemu_mutex_lock(&s_adsp.lock);
        if (s_adsp.command_count) {
            s_adsp.command_head = (s_adsp.command_head + 1) & 65535;
            s_adsp.command_count--;
        }
        bool more = s_adsp.command_count != 0;
        qemu_mutex_unlock(&s_adsp.lock);
        p2k_adsp2105_set_irq_line(P2K_ADSP_IRQ2, 0);
        if (more) {
            p2k_adsp2105_set_irq_line(P2K_ADSP_IRQ2, 1);
        }
        return;
    }
    if (address == 0x0401) {
        static unsigned output_logs;
        qemu_mutex_lock(&s_adsp.lock);
        s_adsp.output_data = value;
        s_adsp.output_full = true;
        qemu_mutex_unlock(&s_adsp.lock);
        if (getenv("P2K_DCS_ADSP_TRACE") && output_logs++ < 64) {
            info_report("dcs-adsp: DSP->host %04x", value);
            if (value >= 0xee01 && value <= 0xee03) {
                info_report("dcs-adsp: SRAM failure detail %04x/%04x/%04x/"
                            "%04x/%04x/%04x", s_adsp.data[0x38e0],
                            s_adsp.data[0x38e1], s_adsp.data[0x38e2],
                            s_adsp.data[0x38e3], s_adsp.data[0x38e4],
                            s_adsp.data[0x38e5]);
            }
        }
        return;
    }
    if (address == 0x0402) {
        s_adsp.output_control = value;
        return;
    }
    if (address >= 0x0480 && address <= 0x0483) {
        static unsigned sdrc_logs;
        unsigned reg = address - 0x0480;
        if (getenv("P2K_DCS_ADSP_TRACE") && sdrc_logs++ < 128) {
            info_report("dcs-adsp: SDRC[%u] <- %04x pc=%04x", reg, value,
                        p2k_adsp2105_get_reg(P2K_ADSP_PC) & 0x3fff);
        }
        if (reg < 3) {
            s_adsp.sdrc[reg] = value;
        } else {
            switch ((s_adsp.sdrc[0] >> 13) & 7) {
            case 1: s_adsp.sdrc_seed = value; break;
            case 3: s_adsp.sdrc_seed = (s_adsp.sdrc_seed << 1) | 1; break;
            case 4: s_adsp.sdrc_seed += s_adsp.sdrc_seed >> 1; break;
            case 5: s_adsp.sdrc_seed ^= (s_adsp.sdrc_seed << 1) | 1; break;
            case 6:
                s_adsp.sdrc_seed = (((s_adsp.sdrc_seed << 7) ^
                    (s_adsp.sdrc_seed << 5) ^ (s_adsp.sdrc_seed << 4) ^
                    (s_adsp.sdrc_seed << 3)) & 0x80) |
                    (s_adsp.sdrc_seed >> 1);
                break;
            case 7: s_adsp.sdrc_seed = ~s_adsp.sdrc_seed; break;
            }
        }
        return;
    }

    unsigned dm_st = s_adsp.sdrc[1] & 3;
    unsigned dm_base = dm_st == 1 ? 0x0000 : dm_st == 2 ? 0x3000 : 0x3400;
    if (dm_st && address >= dm_base && address < dm_base + 0x400) {
        size_t word = ((size_t)(s_adsp.sdrc[2] & 0x7ff) * 1024 +
                       address - dm_base) % s_adsp.sound_words;
        s_adsp.sound_data[word] = value;
        return;
    }

    bool sm_enabled = (s_adsp.sdrc[0] & 0x0800) != 0;
    bool sm_bank = (s_adsp.sdrc[0] & 0x1000) != 0;
    if (sm_enabled) {
        if (!sm_bank && address >= 0x0800 && address <= 0x17ff) {
            s_adsp.sram[address - 0x0800] = value;
            return;
        }
        if (address >= 0x1800 && address <= 0x27ff) {
            s_adsp.sram[(sm_bank ? 0x3000 : 0x1000) +
                        address - 0x1800] = value;
            return;
        }
        if (address >= 0x2800 && address <= 0x37ff) {
            s_adsp.sram[0x2000 + address - 0x2800] = value;
            return;
        }
    }
    if (address >= 0x3800 && address <= 0x39ff) {
        s_adsp.data[address] = value;
        return;
    }
    if (address == 0x3000) {
        s_adsp.rom_bank = value & 0x7ff;
        return;
    }
    if (address >= 0x3fe0) {
        unsigned reg = address - 0x3fe0;
        s_adsp.control[reg] = value;
        if ((reg == SYSCONTROL_REG && !(value & 0x0800)) ||
            (reg == S1_AUTOBUF_REG && !(value & 0x0002))) {
            s_adsp.sport_enabled = false;
        }
    }
    /* 0x3403 is the DCS-to-host response latch.  The existing protocol
     * frontend remains authoritative for host responses; audio execution
     * intentionally does not synthesize a second response stream. */
}

static uint32_t adsp_program_read(uint16_t address)
{
    if (address >= 0x0800) {
        size_t offset = 0x4800 + (address - 0x0800) * 2;
        return (s_adsp.sram[offset] | ((uint32_t)s_adsp.sram[offset + 1] << 16))
               & 0xffffff;
    }
    return s_adsp.program[address] & 0xffffff;
}

static void adsp_program_write(uint16_t address, uint32_t value)
{
    if (address >= 0x0800) {
        size_t offset = 0x4800 + (address - 0x0800) * 2;
        if (!s_adsp.host_boot_compare_logged && address >= 0x3980 &&
            address < 0x3f80 && getenv("P2K_DCS_ADSP_TRACE")) {
            uint32_t old = (s_adsp.sram[offset] |
                            ((uint32_t)s_adsp.sram[offset + 1] << 16)) &
                           0xffffff;
            if (old != (value & 0xffffff)) {
                info_report("dcs-adsp: host copy mismatch pm[%04x] "
                            "old=%06x new=%06x", address, old,
                            value & 0xffffff);
                s_adsp.host_boot_compare_logged = true;
            }
        }
        s_adsp.sram[offset] = value;
        s_adsp.sram[offset + 1] = value >> 16;
        return;
    }
    s_adsp.program[address] = value & 0xffffff;
}

static void adsp_tx(int port, int32_t value)
{
    if (port != 1 || !(s_adsp.control[SYSCONTROL_REG] & 0x0800) ||
        !(s_adsp.control[S1_AUTOBUF_REG] & 0x0002)) {
        return;
    }

    s_adsp.ireg = (s_adsp.control[S1_AUTOBUF_REG] >> 9) & 7;
    int mreg = (s_adsp.control[S1_AUTOBUF_REG] >> 7) & 3;
    mreg |= s_adsp.ireg & 4;
    s_adsp.increment = (int16_t)p2k_adsp2105_get_reg(P2K_ADSP_M0 + mreg);
    s_adsp.length = p2k_adsp2105_get_reg(P2K_ADSP_L0 + s_adsp.ireg);
    int source = p2k_adsp2105_get_reg(P2K_ADSP_I0 + s_adsp.ireg);
    /* MAME's DCS SPORT callback aligns the autobuffer source to the 16-word
     * boundary selected by the board, rather than undoing one M increment. */
    source &= ~0xf;
    p2k_adsp2105_set_reg(P2K_ADSP_I0 + s_adsp.ireg, source);
    s_adsp.base = source & 0x3fff;
    s_adsp.play_pos = s_adsp.base;
    s_adsp.next_irq_pos = s_adsp.length / 2;
    /* With SCLKDIV=7 and 16-bit SPORT words, the source clock is 8 MHz. */
    s_adsp.source_rate = 8000000.0 /
        (2.0 * (s_adsp.control[S1_SCLKDIV_REG] + 1) * 16.0);
    s_adsp.sport_enabled = s_adsp.length > 0 && s_adsp.increment != 0;
    info_report("dcs-adsp: SPORT1 autobuffer %s I%d=%04x M=%d L=%d rate=%.2fHz",
                s_adsp.sport_enabled ? "started" : "invalid",
                s_adsp.ireg, s_adsp.base, s_adsp.increment,
                s_adsp.length, s_adsp.source_rate);
    (void)value;
}

static bool has_suffix(const char *name, const char *suffix)
{
    size_t n = strlen(name);
    size_t s = strlen(suffix);
    return n >= s && strcmp(name + n - s, suffix) == 0;
}

static bool find_update_sound_flash(const char *dir, char *out, size_t out_sz)
{
    if (!dir || !*dir) {
        return false;
    }

    GError *err = NULL;
    GDir *d = g_dir_open(dir, 0, &err);
    if (!d) {
        g_clear_error(&err);
        return false;
    }

    const char *name;
    bool found = false;
    while ((name = g_dir_read_name(d)) != NULL) {
        if (has_suffix(name, "_sf.rom")) {
            snprintf(out, out_sz, "%s/%s", dir, name);
            found = true;
            break;
        }
    }
    g_dir_close(d);
    return found;
}

static bool load_exact(const char *path, uint8_t **out)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return false;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }
    long size = ftell(fp);
    if (size != P2K_DCS_SOUND_FLASH_SIZE || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return false;
    }
    uint8_t *buf = g_malloc(P2K_DCS_SOUND_FLASH_SIZE);
    size_t got = fread(buf, 1, P2K_DCS_SOUND_FLASH_SIZE, fp);
    fclose(fp);
    if (got != P2K_DCS_SOUND_FLASH_SIZE) {
        g_free(buf);
        return false;
    }
    *out = buf;
    return true;
}

bool p2k_dcs_adsp_source_key(Pinball2000MachineState *s, char key[65])
{
    if (!s || !s->dcs_rom || !key) {
        return false;
    }
    char path[sizeof(s_sound_flash_path)] = {0};
    const char *override = getenv("P2K_DCS_SOUND_FLASH");
    if (override && *override) {
        snprintf(path, sizeof(path), "%s", override);
    } else if (!find_update_sound_flash(s->update_path, path, sizeof(path))) {
        snprintf(path, sizeof(path), "%s/%s_28f800.rom",
                 s->roms_dir ?: "roms", s->game ?: "swe1");
        if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
            snprintf(path, sizeof(path), "%s/%s/28f800.rom",
                     s->roms_dir ?: "roms", s->game ?: "swe1");
        }
    }
    uint8_t *flash = NULL;
    if (!load_exact(path, &flash)) {
        return false;
    }
    GChecksum *sum = g_checksum_new(G_CHECKSUM_SHA256);
    g_checksum_update(sum, s->dcs_rom, P2K_DCS_BANK_SIZE);
    g_checksum_update(sum, flash, P2K_DCS_SOUND_FLASH_SIZE);
    snprintf(key, 65, "%s", g_checksum_get_string(sum));
    g_checksum_free(sum);
    g_free(flash);
    return true;
}

bool p2k_dcs_adsp_prepare(Pinball2000MachineState *s)
{
    if (!s || !s->dcs_rom) {
        warn_report("dcs-adsp: original u109/u110 DCS ROM is unavailable");
        return false;
    }

    char path[sizeof(s_sound_flash_path)] = {0};
    const char *override = getenv("P2K_DCS_SOUND_FLASH");
    if (override && *override) {
        snprintf(path, sizeof(path), "%s", override);
        info_report("dcs-adsp: using explicit sound flash override %s", path);
    } else if (!find_update_sound_flash(s->update_path, path, sizeof(path))) {
        snprintf(path, sizeof(path), "%s/%s_28f800.rom",
                 s->roms_dir ?: "roms", s->game ?: "swe1");
        if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
            snprintf(path, sizeof(path), "%s/%s/28f800.rom",
                     s->roms_dir ?: "roms", s->game ?: "swe1");
        }
    }

    g_free(s_sound_flash);
    s_sound_flash = NULL;
    if (!load_exact(path, &s_sound_flash)) {
        warn_report("dcs-adsp: no valid 1 MiB sound flash at %s", path);
        return false;
    }

    if (!s_adsp.initialized) {
        qemu_mutex_init(&s_adsp.lock);
        qemu_mutex_init(&s_adsp.core_lock);
        qemu_cond_init(&s_adsp.worker_cond);
    }
    memset(s_adsp.data, 0, sizeof(s_adsp.data));
    memset(s_adsp.program, 0, sizeof(s_adsp.program));
    memset(s_adsp.control, 0, sizeof(s_adsp.control));
    memset(s_adsp.sdrc, 0, sizeof(s_adsp.sdrc));
    memset(s_adsp.sram, 0, sizeof(s_adsp.sram));
    s_adsp.sound_rom = s->dcs_rom;
    g_free(s_adsp.sound_data);
    /* The DCS address region is 0x600000 16-bit words: sound flash at word 0,
     * U109 at 0x200000, and U110 at 0x400000.  The gaps are real address-space
     * gaps, not concatenation padding, and file byte pairs are loaded LE. */
    size_t flash_words = P2K_DCS_SOUND_FLASH_SIZE / 2;
    size_t chip_words = P2K_DCS_BANK_SIZE / 4;
    s_adsp.sound_words = P2K_DCS_REGION_WORDS;
    s_adsp.sound_data = g_new0(uint16_t, s_adsp.sound_words);
    for (size_t i = 0; i < flash_words; i++) {
        s_adsp.sound_data[i] = lduw_le_p(s_sound_flash + i * 2);
    }
    for (size_t i = 0; i < chip_words; i++) {
        s_adsp.sound_data[P2K_DCS_U109_WORD_OFFSET + i] =
            lduw_le_p(s->dcs_rom + i * 4);
        s_adsp.sound_data[P2K_DCS_U110_WORD_OFFSET + i] =
            lduw_le_p(s->dcs_rom + i * 4 + 2);
    }
    s_adsp.rom_bank = 0;
    s_adsp.command_head = 0;
    s_adsp.command_count = 0;
    s_adsp.output_data = 0;
    s_adsp.output_control = 0;
    s_adsp.output_full = false;
    s_adsp.sport_enabled = false;
    s_adsp.source_phase = 0;
    s_adsp.cycle_phase = 0;
    s_adsp.cycles = 0;
    s_adsp.pcm_frames = 0;
    s_adsp.pcm_nonzero = 0;
    s_adsp.pcm_peak = 0;
    memset(s_adsp.last_sample, 0, sizeof(s_adsp.last_sample));
    s_adsp.selftest_sent = false;
    s_adsp.selftest_ready = false;

    /* The 28F800 is 16-bit. Read the low byte of each word into a 0x1000-byte
     * page before applying
     * the standard ADSP boot-page conversion. */
    uint8_t boot_page[0x1000];
    for (size_t i = 0; i < sizeof(boot_page); i++) {
        boot_page[i] = s_sound_flash[i * 2];
    }
    p2k_adsp2105_init(adsp_data_read, adsp_data_write,
                      adsp_program_read, adsp_program_write);
    p2k_adsp2105_set_tx_callback(adsp_tx);
    p2k_adsp2105_load_boot_data(boot_page, s_adsp.program);
    p2k_adsp2105_reset();
    s_adsp.initialized = true;
    const char *engine = getenv("P2K_DCS_ENGINE");
    s_adsp.threaded_engine = !engine || !*engine ||
                             !strcmp(engine, "adsp-thread") ||
                             !strcmp(engine, "pb2kslib-adsp");
    snprintf(s_sound_flash_path, sizeof(s_sound_flash_path), "%s", path);
    info_report("dcs-adsp: original assets ready (u109/u110=%u MiB, "
                "sound-flash=%s, %u KiB)",
                (unsigned)(P2K_DCS_BANK_SIZE / (1024 * 1024)),
                s_sound_flash_path,
                (unsigned)(P2K_DCS_SOUND_FLASH_SIZE / 1024));
    return true;
}

void p2k_dcs_adsp_write_cmd(uint16_t command)
{
    if (!s_adsp.initialized) {
        return;
    }
    /* After pci_reset(), BAR4 is the ADSP byte-wide boot input.  PB2K sends
     * page count followed by PM words high-to-low in byte order 2,0,1. */
    if (s_adsp.host_boot) {
        qemu_mutex_lock(&s_adsp.core_lock);
        uint8_t byte = command;
        if (s_adsp.host_boot_pos == 0) {
            s_adsp.host_boot_words = ((unsigned)byte + 1) * 8;
            if (s_adsp.host_boot_words > ARRAY_SIZE(s_adsp.program)) {
                warn_report("dcs-adsp: invalid host boot length %u words",
                            s_adsp.host_boot_words);
                s_adsp.host_boot = false;
                qemu_mutex_unlock(&s_adsp.core_lock);
                return;
            }
            s_adsp.host_boot_pos = 1;
            qemu_mutex_unlock(&s_adsp.core_lock);
            return;
        }
        unsigned data_pos = s_adsp.host_boot_pos - 1;
        if (data_pos < s_adsp.host_boot_words * 3) {
            unsigned phase = data_pos % 3;
            s_adsp.host_boot_triplet[phase] = byte;
            if (phase == 2) {
                unsigned address = s_adsp.host_boot_words - 1 - data_pos / 3;
                s_adsp.program[address] =
                    ((uint32_t)s_adsp.host_boot_triplet[0] << 16) |
                    ((uint32_t)s_adsp.host_boot_triplet[2] << 8) |
                    s_adsp.host_boot_triplet[1];
            }
            s_adsp.host_boot_pos++;
            qemu_mutex_unlock(&s_adsp.core_lock);
            return;
        }
        /* pci_dcs_host_boot() repeats its final byte after a 1us delay. */
        s_adsp.host_boot = false;
        s_adsp.command_head = 0;
        s_adsp.command_count = 0;
        s_adsp.output_full = false;
        p2k_adsp2105_reset();
        for (unsigned cycles = 0; cycles < 200000; cycles += 100) {
            qemu_mutex_lock(&s_adsp.lock);
            bool responded = s_adsp.output_full;
            qemu_mutex_unlock(&s_adsp.lock);
            if (responded) {
                break;
            }
            p2k_adsp2105_execute(100);
            s_adsp.cycles += 100;
        }
        info_report("dcs-adsp: accepted x86 host boot (%u PM words, "
                    "entry=%06x)", s_adsp.host_boot_words,
                    s_adsp.program[0] & 0xffffff);
        qemu_mutex_unlock(&s_adsp.core_lock);
        return;
    }
    qemu_mutex_lock(&s_adsp.lock);
    static unsigned command_logs;
    /* Last word of SWE1's ACE1/DCS runtime mixer initialization.  The
     * optional test trigger must follow it, never split an in-flight triple. */
    if (command == 0x8280) {
        s_adsp.selftest_ready = true;
    }
    if (command == 0xace1) {
        s_adsp.host_ack[0] = 0x0100;
        s_adsp.host_ack[1] = 0x000c;
        s_adsp.host_ack_head = 0;
        s_adsp.host_ack_count = 2;
    }
    unsigned queued_after = s_adsp.command_count;
    if (s_adsp.command_count < ARRAY_SIZE(s_adsp.commands)) {
        unsigned tail = (s_adsp.command_head + s_adsp.command_count) & 65535;
        s_adsp.commands[tail] = command;
        s_adsp.command_count++;
        queued_after = s_adsp.command_count;
        if (getenv("P2K_DCS_ADSP_TRACE") && command_logs++ < 64) {
            info_report("dcs-adsp: host->DSP %04x", command);
        }
    } else {
        warn_report("dcs-adsp: command FIFO overflow, dropping 0x%04x",
                    command);
    }
    qemu_mutex_unlock(&s_adsp.lock);
    p2k_adsp2105_set_irq_line(P2K_ADSP_IRQ2, 1);
    bool synchronous_diag = command == 0x003a || command == 0x001b ||
                            command == 0x00aa;
    bool start_worker = s_adsp.threaded_engine &&
                        !s_adsp.worker_started && command == 0x8280;
    if (s_adsp.threaded_engine && s_adsp.worker_started &&
        !synchronous_diag) {
        qemu_mutex_lock(&s_adsp.lock);
        qemu_cond_signal(&s_adsp.worker_cond);
        qemu_mutex_unlock(&s_adsp.lock);
    } else if (!synchronous_diag) {
        /* Advance the mailbox synchronously without introducing a second PCM clock.
         * The threaded engine deliberately follows this path through the
         * complete ACE1 initialization, keeping boot diagnostics identical. */
        qemu_mutex_lock(&s_adsp.core_lock);
        for (unsigned cycles = 0; cycles < 20000; cycles += 100) {
            qemu_mutex_lock(&s_adsp.lock);
            bool consumed = s_adsp.command_count < queued_after;
            qemu_mutex_unlock(&s_adsp.lock);
            if (consumed) {
                break;
            }
            p2k_adsp2105_execute(100);
            s_adsp.cycles += 100;
        }
        qemu_mutex_unlock(&s_adsp.core_lock);
    }
    if (start_worker) {
        adsp_worker_start();
        p2k_dcs_audio_adsp_runtime_ready();
    }
    if (command == 0x003a || command == 0x001b || command == 0x00aa) {
        unsigned limit = command == 0x001b ? 250000000 : 20000000;
        qemu_mutex_lock(&s_adsp.core_lock);
        for (unsigned cycles = 0; cycles < limit; cycles += 1000) {
            p2k_adsp2105_execute(1000);
            s_adsp.cycles += 1000;
            qemu_mutex_lock(&s_adsp.lock);
            bool responded = s_adsp.output_full;
            qemu_mutex_unlock(&s_adsp.lock);
            if (responded) {
                break;
            }
        }
        qemu_mutex_unlock(&s_adsp.core_lock);
    }
}

/* The condition-driven worker advances the DSP mailbox independently instead
 * of blocking the guest's MMIO
 * write or waiting for the next host-audio callback.  SPORT production stays
 * on the established audio clock until a lossless PCM handoff is proven. */
static void *adsp_mailbox_worker(void *opaque)
{
    (void)opaque;
#ifdef __linux__
    pthread_setname_np(pthread_self(), "dcs-pcm");
    const char *cpu_env = getenv("P2K_DCS_PCM_CPU");
    if (cpu_env && cpu_env[0]) {
        char *end = NULL;
        errno = 0;
        long cpu = strtol(cpu_env, &end, 10);
        if (errno || end == cpu_env || *end || cpu < 0 || cpu >= CPU_SETSIZE) {
            warn_report("dcs-adsp: invalid P2K_DCS_PCM_CPU=%s", cpu_env);
        } else {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(cpu, &cpuset);
            int err = pthread_setaffinity_np(pthread_self(), sizeof(cpuset),
                                             &cpuset);
            if (err) {
                warn_report("dcs-adsp: cannot pin dcs-pcm to CPU %ld: %s",
                            cpu, strerror(err));
            } else {
                info_report("dcs-adsp: dcs-pcm pinned to logical CPU %ld", cpu);
            }
        }
    }
#endif
    for (;;) {
        qemu_mutex_lock(&s_adsp.lock);
        while (s_adsp.worker_run && !adsp_worker_has_command()) {
            qemu_cond_wait(&s_adsp.worker_cond, &s_adsp.lock);
        }
        bool run = s_adsp.worker_run;
        qemu_mutex_unlock(&s_adsp.lock);
        if (!run) {
            break;
        }

        qemu_mutex_lock(&s_adsp.core_lock);
        for (unsigned cycles = 0; cycles < 20000; cycles += 100) {
            qemu_mutex_lock(&s_adsp.lock);
            bool empty = s_adsp.command_count == 0;
            qemu_mutex_unlock(&s_adsp.lock);
            if (empty) {
                break;
            }
            p2k_adsp2105_execute(100);
            s_adsp.cycles += 100;
        }
        qemu_mutex_unlock(&s_adsp.core_lock);
    }
    return NULL;
}

static void adsp_worker_start(void)
{
    qemu_mutex_lock(&s_adsp.lock);
    if (s_adsp.worker_started) {
        qemu_mutex_unlock(&s_adsp.lock);
        return;
    }
    s_adsp.worker_run = true;
    s_adsp.worker_started = true;
    qemu_mutex_unlock(&s_adsp.lock);
    qemu_thread_create(&s_adsp.worker, "dcs-mailbox",
                       adsp_mailbox_worker, NULL, QEMU_THREAD_JOINABLE);
    qemu_add_exit_notifier(&adsp_exit_notifier);
    info_report("dcs-adsp: runtime mailbox DSP worker started");
}

static void adsp_worker_shutdown(Notifier *notifier, void *data)
{
    (void)notifier;
    (void)data;
    qemu_mutex_lock(&s_adsp.lock);
    s_adsp.worker_run = false;
    qemu_cond_signal(&s_adsp.worker_cond);
    qemu_mutex_unlock(&s_adsp.lock);
    if (s_adsp.worker_started) {
        qemu_thread_join(&s_adsp.worker);
        s_adsp.worker_started = false;
    }
}

void p2k_dcs_adsp_host_reset(void)
{
    if (!s_adsp.initialized) {
        return;
    }
    qemu_mutex_lock(&s_adsp.core_lock);
    qemu_mutex_lock(&s_adsp.lock);
    qemu_mutex_unlock(&s_adsp.lock);
    /* The physical DSP has already run its flash bootstrap during the x86
     * reset delay.  Audio-driven scheduling can leave our DSP behind the
     * guest CPU, so catch it up before asserting the host reset. */
    for (unsigned cycles = 0; cycles < 2000000; cycles += 1000) {
        unsigned pc = p2k_adsp2105_get_reg(P2K_ADSP_PC) & 0x3fff;
        if (adsp_program_read(0x3980) != 0 &&
            adsp_program_read(0x3deb) != 0 && pc >= 0x3d9c && pc <= 0x3dc1) {
            break;
        }
        p2k_adsp2105_execute(1000);
        s_adsp.cycles += 1000;
    }
    /* The legacy flat-C shifter loses the upper sign bits for this all-ones
     * 24-bit PM word while the current MAME core preserves them. */
    if (adsp_program_read(0x3deb) == 0x0000ff) {
        adsp_program_write(0x3deb, 0xffffff);
    }
    qemu_mutex_lock(&s_adsp.lock);
    s_adsp.host_boot = true;
    s_adsp.host_boot_pos = 0;
    s_adsp.host_boot_words = 0;
    s_adsp.host_boot_compare_logged = false;
    s_adsp.command_head = 0;
    s_adsp.command_count = 0;
    s_adsp.output_full = false;
    qemu_mutex_unlock(&s_adsp.lock);
    qemu_mutex_unlock(&s_adsp.core_lock);
}

uint8_t p2k_dcs_adsp_flag_byte(void)
{
    qemu_mutex_lock(&s_adsp.lock);
    uint8_t flags = (s_adsp.command_count == 0 ? 0x40 : 0) |
                    ((s_adsp.output_full || s_adsp.host_ack_count) ? 0x80 : 0);
    qemu_mutex_unlock(&s_adsp.lock);
    return flags;
}

uint16_t p2k_dcs_adsp_read_response(void)
{
    static unsigned response_logs;
    qemu_mutex_lock(&s_adsp.core_lock);
    qemu_mutex_lock(&s_adsp.lock);
    uint16_t value;
    if (s_adsp.host_ack_count) {
        value = s_adsp.host_ack[s_adsp.host_ack_head++];
        s_adsp.host_ack_count--;
    } else {
        value = s_adsp.output_full ? s_adsp.output_data : 0;
        s_adsp.output_full = false;
    }
    qemu_mutex_unlock(&s_adsp.lock);
    if (value) {
        for (unsigned cycles = 0; cycles < 100000; cycles += 100) {
            p2k_adsp2105_execute(100);
            s_adsp.cycles += 100;
            qemu_mutex_lock(&s_adsp.lock);
            bool responded = s_adsp.output_full;
            qemu_mutex_unlock(&s_adsp.lock);
            if (responded) {
                break;
            }
        }
    }
    qemu_mutex_unlock(&s_adsp.core_lock);
    if (getenv("P2K_DCS_ADSP_TRACE") && response_logs++ < 64) {
        info_report("dcs-adsp: host read %04x", value);
    }
    return value;
}

void p2k_dcs_adsp_render(int16_t *samples, int frames, int output_rate)
{
    if (!s_adsp.initialized || output_rate <= 0) {
        memset(samples, 0, frames * 2 * sizeof(*samples));
        return;
    }

    qemu_mutex_lock(&s_adsp.core_lock);
    if (s_adsp.host_boot) {
        memset(samples, 0, frames * 2 * sizeof(*samples));
        qemu_mutex_unlock(&s_adsp.core_lock);
        return;
    }
    for (int n = 0; n < frames; n++) {
        s_adsp.cycle_phase += 10000000.0 / output_rate;
        int cycles = (int)s_adsp.cycle_phase;
        s_adsp.cycle_phase -= cycles;
        p2k_adsp2105_execute(cycles);
        s_adsp.cycles += cycles;

        /* The firmware first enables a 19.5 kHz/M=2 SPORT during board
         * diagnostics.  Wait for the final runtime SPORT and the end of the
         * guest's ACE1 init stream, then enqueue one complete sample triple. */
        if (!s_adsp.selftest_sent && s_adsp.selftest_ready &&
            s_adsp.sport_enabled &&
            s_adsp.increment == 1 && s_adsp.source_rate > 30000.0 &&
            getenv("P2K_DCS_ADSP_SELFTEST")) {
            s_adsp.selftest_sent = true;
            p2k_dcs_adsp_write_cmd(0x03ce);
            p2k_dcs_adsp_write_cmd(0xff7f);
            p2k_dcs_adsp_write_cmd(0x8180);
            info_report("dcs-adsp: queued runtime sample triple "
                        "03ce/ff7f/8180");
        }

        if (s_adsp.sport_enabled) {
            s_adsp.source_phase += s_adsp.source_rate;
            while (s_adsp.source_phase >= output_rate) {
                s_adsp.source_phase -= output_rate;
                for (int channel = 0; channel < 2; channel++) {
                    s_adsp.last_sample[channel] =
                        (int16_t)adsp_data_read(s_adsp.play_pos & 0x3fff);
                    s_adsp.play_pos += s_adsp.increment;
                }
                int relative = s_adsp.play_pos - s_adsp.base;
                bool half_elapsed = relative >= s_adsp.next_irq_pos;
                bool wrapped = relative >= s_adsp.length || relative < 0;
                if (wrapped) {
                    s_adsp.play_pos = s_adsp.base;
                    relative = 0;
                    s_adsp.next_irq_pos = s_adsp.length / 2;
                } else if (half_elapsed) {
                    s_adsp.next_irq_pos = s_adsp.length;
                }
                if (half_elapsed || wrapped) {
                    p2k_adsp2105_set_reg(P2K_ADSP_I0 + s_adsp.ireg,
                                         s_adsp.play_pos);
                    p2k_adsp2105_set_irq_line(P2K_ADSP_IRQ1, 1);
                    p2k_adsp2105_execute(100);
                    s_adsp.cycles += 100;
                    p2k_adsp2105_set_irq_line(P2K_ADSP_IRQ1, 0);
                }
            }
        }
        for (int channel = 0; channel < 2; channel++) {
            int16_t sample = s_adsp.sport_enabled ?
                             s_adsp.last_sample[channel] : 0;
            samples[n * 2 + channel] = sample;
            if (sample) {
                unsigned magnitude = sample == INT16_MIN ? 32768 :
                                     abs(sample);
                s_adsp.pcm_nonzero++;
                s_adsp.pcm_peak = MAX(s_adsp.pcm_peak, magnitude);
            }
        }
    }
    s_adsp.pcm_frames += frames;
    static uint64_t next_report = 44100;
    if (s_adsp.pcm_frames >= next_report) {
        qemu_mutex_lock(&s_adsp.lock);
        unsigned queued = s_adsp.command_count;
        qemu_mutex_unlock(&s_adsp.lock);
        unsigned pc = p2k_adsp2105_get_reg(P2K_ADSP_PC) & 0x3fff;
        info_report("dcs-adsp: run pc=%04x op=%06x cycles=%llu sport=%d "
                    "queued=%u boot3980=%06x bank=%03x sdrc=%04x/%04x/%04x/%04x "
                    "sys=%04x autobuf=%04x buf=%04x/%04x "
                    "pcm_nonzero=%llu peak=%u",
                    pc, adsp_program_read(pc),
                    (unsigned long long)s_adsp.cycles,
                    s_adsp.sport_enabled, queued, adsp_program_read(0x3980),
                    s_adsp.rom_bank,
                    s_adsp.sdrc[0], s_adsp.sdrc[1], s_adsp.sdrc[2],
                    s_adsp.sdrc[3],
                    s_adsp.control[SYSCONTROL_REG],
                    s_adsp.control[S1_AUTOBUF_REG],
                    adsp_data_read(0x3400), adsp_data_read(0x3401),
                    (unsigned long long)s_adsp.pcm_nonzero,
                    s_adsp.pcm_peak);
        next_report += 44100;
        static bool dumped_wait_loop;
        if (!dumped_wait_loop && getenv("P2K_DCS_ADSP_TRACE")) {
            dumped_wait_loop = true;
            for (unsigned address = 0x3a00; address < 0x3b80; address++) {
                info_report("dcs-adsp: pm[%04x]=%06x", address,
                            adsp_program_read(address));
            }
        }
    }
    qemu_mutex_unlock(&s_adsp.core_lock);
}

bool p2k_dcs_adsp_generate_track(uint16_t command, size_t hint_frames_44100,
                                 int16_t **pcm_44100, size_t *frames_44100,
                                 bool *loop)
{
    enum { NATIVE_RATE = 31250, BLOCK = 1024 };
    uint16_t voice_head_before[0x10];
    uint16_t voice_body_before[0x33];
    if (!s_adsp.initialized || !s_adsp.selftest_ready || !pcm_44100 ||
        !frames_44100 || !loop) {
        return false;
    }

    size_t target_native = hint_frames_44100 ?
        (hint_frames_44100 * NATIVE_RATE + 44099) / 44100 :
        30 * NATIVE_RATE;
    size_t cap = MIN(target_native, (size_t)BLOCK * 8);
    int16_t *native = g_new0(int16_t, cap * 2);
    size_t native_frames = 0, last_signal = 0, silence = 0;
    bool heard = false;

    /* The runtime resolves a valid track into channel-zero voice metadata.
     * Preserve the two track-specific regions so IDs absent from this
     * update can be rejected after one DSP block.  The intervening words
     * contain global command counters and are intentionally excluded. */
    qemu_mutex_lock(&s_adsp.core_lock);
    memcpy(voice_head_before, &s_adsp.sram[0x455],
           sizeof(voice_head_before));
    memcpy(voice_body_before, &s_adsp.sram[0x4b0],
           sizeof(voice_body_before));
    qemu_mutex_unlock(&s_adsp.core_lock);

    /* ACE1 play triple: track, full volume/centre pan, channel zero. */
    p2k_dcs_adsp_write_cmd(command);
    p2k_dcs_adsp_write_cmd(0xff7f);
    p2k_dcs_adsp_write_cmd(0x8180);
    while (native_frames < target_native) {
        size_t count = MIN((size_t)BLOCK, target_native - native_frames);
        if (native_frames + count > cap) {
            cap = MIN(target_native, MAX(cap * 2, native_frames + count));
            native = g_renew(int16_t, native, cap * 2);
        }
        p2k_dcs_adsp_render(native + native_frames * 2, count, NATIVE_RATE);
        for (size_t i = 0; i < count; i++) {
            int a = abs(native[(native_frames + i) * 2]);
            int b = abs(native[(native_frames + i) * 2 + 1]);
            if (a > 8 || b > 8) {
                heard = true;
                last_signal = native_frames + i + 1;
                silence = 0;
            } else if (heard) {
                silence++;
            }
        }
        native_frames += count;
        if (!hint_frames_44100 && native_frames == count) {
            bool indexed;
            qemu_mutex_lock(&s_adsp.core_lock);
            indexed = memcmp(voice_head_before, &s_adsp.sram[0x455],
                             sizeof(voice_head_before)) != 0 ||
                      memcmp(voice_body_before, &s_adsp.sram[0x4b0],
                             sizeof(voice_body_before)) != 0;
            qemu_mutex_unlock(&s_adsp.core_lock);
            if (!indexed && !heard)
                break;
        }
        if (!hint_frames_44100 && !heard && native_frames >= NATIVE_RATE / 4)
            break;
        if (!hint_frames_44100 && heard && silence >= NATIVE_RATE * 2 / 5)
            break;
    }

    *loop = !hint_frames_44100 && heard &&
            native_frames >= target_native && silence < NATIVE_RATE * 2 / 5;
    if (!heard) {
        g_free(native);
        native = NULL;
    } else if (!hint_frames_44100 && !*loop) {
        native_frames = MIN(native_frames, last_signal + NATIVE_RATE / 20);
    }

    size_t output_frames = hint_frames_44100 ? hint_frames_44100 :
        (native_frames * 44100 + NATIVE_RATE - 1) / NATIVE_RATE;
    int16_t *output = heard ? g_new(int16_t, output_frames) : NULL;

    /* Convert the board's stereo 31.25 kHz stream to the natural pb2kslib
     * mixer's mono 44.1 kHz PCM. */
    for (size_t i = 0; heard && i < output_frames; i++) {
        double pos = (double)i * NATIVE_RATE / 44100.0;
        size_t p0 = MIN((size_t)pos, native_frames - 1);
        size_t p1 = MIN(p0 + 1, native_frames - 1);
        double frac = pos - (double)p0;
        int32_t m0 = ((int32_t)native[p0 * 2] + native[p0 * 2 + 1]) / 2;
        int32_t m1 = ((int32_t)native[p1 * 2] + native[p1 * 2 + 1]) / 2;
        output[i] = (int16_t)((1.0 - frac) * m0 + frac * m1);
    }
    g_free(native);

    /* Quiet all mixer tracks before generating the next isolated entry. */
    p2k_dcs_adsp_write_cmd(0x55ae);
    p2k_dcs_adsp_write_cmd(0x3f00);
    p2k_dcs_adsp_write_cmd(0x8180);
    int16_t settle[1024 * 2];
    for (int i = 0; i < 8; i++) {
        p2k_dcs_adsp_render(settle, 1024, NATIVE_RATE);
    }

    if (!heard) return false;
    *pcm_44100 = output;
    *frames_44100 = output_frames;
    return true;
}
