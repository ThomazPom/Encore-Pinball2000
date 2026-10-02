/*
 * Pinball 2000 battery-backed RTC persistence.
 *
 * XINA does not treat MC146818 register 9 as an absolute two-digit year.
 * It persists an absolute base year in BAR2 (RTCBaseY) and keeps register 9
 * as the number of year boundaries crossed since that base was chosen.  A
 * fresh QEMU MC146818 is initialized from the host on every process start;
 * feeding host_year - 1999 to an established XINA profile therefore doubles
 * the offset (for example 2026 + 27 = 2053).
 *
 * Preserve the first 64 CMOS bytes and the host save time.  On restore use
 * today's local wall-clock fields, retain the guest's saved year counter, and
 * add only calendar-year boundaries crossed while the emulator was off.  The
 * public port interface is used to program A/B and the clock, so upstream
 * QEMU recalculates its internal base time and timer scheduling normally.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/notify.h"
#include "exec/ioport.h"
#include "hw/rtc/mc146818rtc.h"
#include "system/system.h"
#include <errno.h>

#include "p2k-internal.h"

#define P2K_RTC_CMOS_SIZE 64
#define P2K_RTC_FILE_SIZE (8 + 8 + P2K_RTC_CMOS_SIZE)
#define P2K_RTC_INDEX_PORT 0x70
#define P2K_RTC_DATA_PORT  0x71

#define RTC_SECONDS       0x00
#define RTC_MINUTES       0x02
#define RTC_HOURS         0x04
#define RTC_DAY_OF_WEEK   0x06
#define RTC_DAY_OF_MONTH  0x07
#define RTC_MONTH         0x08
#define RTC_YEAR          0x09
#define RTC_REG_A         0x0a
#define RTC_REG_B         0x0b
#define RTC_REG_C         0x0c
#define RTC_REG_D         0x0d
#define RTC_CENTURY       0x32

#define REG_B_24H         0x02
#define REG_B_DM          0x04
#define REG_B_SET         0x80

static const uint8_t p2k_rtc_magic[8] = {
    'P', '2', 'K', 'R', 'T', 'C', '1', '\0'
};

static MC146818RtcState *s_rtc;
static char s_rtc_save_path[1024];

static uint64_t p2k_rtc_load_le64(const uint8_t *p)
{
    uint64_t value = 0;

    for (unsigned i = 0; i < 8; i++) {
        value |= (uint64_t)p[i] << (i * 8);
    }
    return value;
}

static void p2k_rtc_store_le64(uint8_t *p, uint64_t value)
{
    for (unsigned i = 0; i < 8; i++) {
        p[i] = value >> (i * 8);
    }
}

static uint8_t p2k_rtc_encode(unsigned value, uint8_t reg_b)
{
    if (reg_b & REG_B_DM) {
        return value;
    }
    return ((value / 10) << 4) | (value % 10);
}

static int p2k_rtc_decode(uint8_t value, uint8_t reg_b)
{
    if (reg_b & REG_B_DM) {
        return value;
    }
    if ((value & 0x0f) > 9 || ((value >> 4) & 0x0f) > 9) {
        return -1;
    }
    return ((value >> 4) * 10) + (value & 0x0f);
}

static void p2k_rtc_write(unsigned reg, uint8_t value)
{
    cpu_outb(P2K_RTC_INDEX_PORT, reg);
    cpu_outb(P2K_RTC_DATA_PORT, value);
}

static uint8_t p2k_rtc_read(unsigned reg)
{
    cpu_outb(P2K_RTC_INDEX_PORT, reg);
    return cpu_inb(P2K_RTC_DATA_PORT);
}

static void p2k_rtc_program_host_time(const struct tm *now_tm,
                                      uint8_t reg_b, unsigned year_count)
{
    unsigned hour = now_tm->tm_hour;

    p2k_rtc_write(RTC_SECONDS, p2k_rtc_encode(now_tm->tm_sec, reg_b));
    p2k_rtc_write(RTC_MINUTES, p2k_rtc_encode(now_tm->tm_min, reg_b));
    if (reg_b & REG_B_24H) {
        p2k_rtc_write(RTC_HOURS, p2k_rtc_encode(hour, reg_b));
    } else {
        bool pm = hour >= 12;
        hour %= 12;
        if (hour == 0) {
            hour = 12;
        }
        p2k_rtc_write(RTC_HOURS,
                      p2k_rtc_encode(hour, reg_b) | (pm ? 0x80 : 0));
    }
    p2k_rtc_write(RTC_DAY_OF_WEEK,
                  p2k_rtc_encode(now_tm->tm_wday + 1, reg_b));
    p2k_rtc_write(RTC_DAY_OF_MONTH,
                  p2k_rtc_encode(now_tm->tm_mday, reg_b));
    p2k_rtc_write(RTC_MONTH,
                  p2k_rtc_encode(now_tm->tm_mon + 1, reg_b));
    p2k_rtc_write(RTC_YEAR, p2k_rtc_encode(year_count % 100, reg_b));
    p2k_rtc_write(RTC_CENTURY, p2k_rtc_encode(0, reg_b));
}

static bool p2k_rtc_restore_file(const uint8_t file[P2K_RTC_FILE_SIZE])
{
    uint8_t cmos[P2K_RTC_CMOS_SIZE];
    time_t saved_time = (time_t)p2k_rtc_load_le64(file + 8);
    time_t now = time(NULL);
    struct tm saved_tm;
    struct tm now_tm;
    int saved_year;
    int crossed_years;
    uint8_t reg_a;
    uint8_t reg_b;

    memcpy(cmos, file + 16, sizeof(cmos));
    reg_a = cmos[RTC_REG_A] & 0x7f;
    reg_b = cmos[RTC_REG_B] & ~REG_B_SET;
    saved_year = p2k_rtc_decode(cmos[RTC_YEAR], reg_b);

    if (saved_time <= 0 || now == (time_t)-1 ||
        !localtime_r(&saved_time, &saved_tm) ||
        !localtime_r(&now, &now_tm) ||
        saved_year < 0 || saved_year > 99) {
        return false;
    }

    crossed_years = now_tm.tm_year - saved_tm.tm_year;
    if (crossed_years < 0) {
        crossed_years = 0;
    }

    /*
     * Freeze first.  Copy battery-backed bytes without restoring a stale
     * pending interrupt, then use normal I/O writes for timing registers.
     */
    p2k_rtc_write(RTC_REG_B, reg_b | REG_B_SET);
    for (unsigned i = 0; i < P2K_RTC_CMOS_SIZE; i++) {
        if (i == RTC_REG_A || i == RTC_REG_B ||
            i == RTC_REG_C || i == RTC_REG_D) {
            continue;
        }
        mc146818rtc_set_cmos_data(s_rtc, i, cmos[i]);
    }
    mc146818rtc_set_cmos_data(s_rtc, RTC_REG_C, 0);
    mc146818rtc_set_cmos_data(s_rtc, RTC_REG_D, 0x80);
    p2k_rtc_write(RTC_REG_A, reg_a);
    p2k_rtc_program_host_time(&now_tm, reg_b,
                              saved_year + crossed_years);
    p2k_rtc_write(RTC_REG_B, reg_b);

    info_report("pinball2000: RTC restored from %s "
                "(year counter %d + %d off-time rollover%s)",
                s_rtc_save_path, saved_year, crossed_years,
                crossed_years == 1 ? "" : "s");
    return true;
}

static void p2k_rtc_migrate_existing_savedata(Pinball2000MachineState *s)
{
    g_autofree char *nvram_path = NULL;
    struct tm now_tm;
    time_t now = time(NULL);
    uint8_t reg_a;
    uint8_t reg_b;

    if (p2k_fresh_savedata_enabled()) {
        return;
    }
    nvram_path = g_strdup_printf("%s/%s.nvram2", s->savedata_dir, s->game);
    if (!g_file_test(nvram_path, G_FILE_TEST_IS_REGULAR) ||
        now == (time_t)-1 || !localtime_r(&now, &now_tm)) {
        return;
    }

    /*
     * One-time migration for profiles created before RTC persistence.  Such
     * a profile already owns RTCBaseY in BAR2, so its companion RTC counter
     * starts at zero.  A genuinely fresh profile has no nvram2 seed and keeps
     * upstream QEMU's host-relative initialization.
     */
    reg_a = p2k_rtc_read(RTC_REG_A) & 0x7f;
    reg_b = p2k_rtc_read(RTC_REG_B) & ~REG_B_SET;
    p2k_rtc_write(RTC_REG_B, reg_b | REG_B_SET);
    p2k_rtc_write(RTC_REG_A, reg_a);
    p2k_rtc_program_host_time(&now_tm, reg_b, 0);
    p2k_rtc_write(RTC_REG_B, reg_b);
    info_report("pinball2000: initialized RTC persistence for existing "
                "savedata (year counter 0)");
}

static void p2k_rtc_save_cb(Notifier *notifier, void *data)
{
    uint8_t file[P2K_RTC_FILE_SIZE] = { 0 };
    char tmp[1100];
    FILE *fp;
    size_t written;
    time_t now;

    if (!s_rtc || !s_rtc_save_path[0] || p2k_no_savedata_enabled()) {
        return;
    }

    /*
     * A clock-register read first asks upstream QEMU to materialize the
     * current guest time into cmos_data.  Read C only through the non-
     * destructive accessor so saving cannot acknowledge a guest IRQ.
     */
    (void)p2k_rtc_read(RTC_YEAR);
    memcpy(file, p2k_rtc_magic, sizeof(p2k_rtc_magic));
    now = time(NULL);
    p2k_rtc_store_le64(file + 8, now == (time_t)-1 ? 0 : (uint64_t)now);
    for (unsigned i = 0; i < P2K_RTC_CMOS_SIZE; i++) {
        file[16 + i] = mc146818rtc_get_cmos_data(s_rtc, i);
    }

    snprintf(tmp, sizeof(tmp), "%s.tmp", s_rtc_save_path);
    fp = fopen(tmp, "wb");
    if (!fp) {
        warn_report("pinball2000: RTC save: fopen(%s) failed: %s",
                    tmp, strerror(errno));
        return;
    }
    written = fwrite(file, 1, sizeof(file), fp);
    if (fclose(fp) != 0 || written != sizeof(file)) {
        warn_report("pinball2000: RTC save: short/failed write %zu/%zu",
                    written, sizeof(file));
        unlink(tmp);
        return;
    }
    if (rename(tmp, s_rtc_save_path) != 0) {
        warn_report("pinball2000: RTC save: rename failed: %s",
                    strerror(errno));
        unlink(tmp);
        return;
    }
    info_report("pinball2000: RTC saved to %s", s_rtc_save_path);
}

static Notifier p2k_rtc_exit_notifier = {
    .notify = p2k_rtc_save_cb,
};

void p2k_install_rtc_persistence(Pinball2000MachineState *s,
                                 MC146818RtcState *rtc)
{
    uint8_t file[P2K_RTC_FILE_SIZE];
    FILE *fp;
    size_t count;

    s_rtc = rtc;
    snprintf(s_rtc_save_path, sizeof(s_rtc_save_path), "%s/%s.rtc",
             s->savedata_dir, s->game);

    if (!p2k_no_savedata_enabled() && !p2k_fresh_savedata_enabled()) {
        fp = fopen(s_rtc_save_path, "rb");
        if (fp) {
            count = fread(file, 1, sizeof(file), fp);
            if (fgetc(fp) != EOF) {
                count++;
            }
            fclose(fp);
            if (count != sizeof(file) ||
                memcmp(file, p2k_rtc_magic, sizeof(p2k_rtc_magic)) != 0 ||
                !p2k_rtc_restore_file(file)) {
                warn_report("pinball2000: ignoring invalid RTC state %s",
                            s_rtc_save_path);
                p2k_rtc_migrate_existing_savedata(s);
            }
        } else if (errno != ENOENT) {
            warn_report("pinball2000: RTC load: fopen(%s) failed: %s",
                        s_rtc_save_path, strerror(errno));
        } else {
            p2k_rtc_migrate_existing_savedata(s);
        }
    } else if (p2k_fresh_savedata_enabled()) {
        info_report("pinball2000: fresh run — ignoring saved RTC state");
    }

    qemu_add_exit_notifier(&p2k_rtc_exit_notifier);
}
