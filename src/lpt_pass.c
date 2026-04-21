/*
 * lpt_pass.c — Real LPT passthrough via Linux ppdev.
 *
 * When active, all guest accesses to ports 0x378/0x379/0x37A are forwarded
 * to a physical parallel port (e.g. /dev/parport0). The emulated driver-board
 * state machine in io.c is bypassed for response generation, so the real
 * Pinball 2000 cabinet sees genuine guest-driven LPT traffic.
 *
 * Direction policy — matches P2K-driver ground truth (binary RE):
 *   P2K-driver's LPT data-port read handler (sub @ 0x8059688) gates
 *   the response on renderingFlags bits 0+3 only — it never inspects
 *   control register bit 5. The P2K protocol is implicit-direction:
 *   the board drives data lines back when the rendering opcode is
 *   armed, otherwise the host's data latch holds the bus.
 *
 *   To match this on real hardware, encore unconditionally flips
 *   PPDATADIR to input around every data-port read and back to output
 *   afterwards — without ever consulting guest control bit 5. This
 *   prevents bus contention while the cabinet board is driving and
 *   restores the latch immediately so the next opcode write goes out.
 *
 * Other design points (validated by rubber-duck review):
 *   - Hardware port is opened/claimed at startup, but `g_emu.lpt_active`
 *     stays controlled by the existing UART-driven activation gate so
 *     pre-XINU probes still see 0xFF (avoids surprising the guest).
 *   - PPGETMODES verifies bidirectional capability up front.
 *   - PPEXCL prevents lp/CUPS sneaking in.
 *   - Control reads return the cached guest-visible byte (PPRCONTROL
 *     semantics vary across kernels; cache is what the guest just wrote).
 *   - Cleanup releases + closes the device. Signal handlers set
 *     g_emu.running=false so main loop exits and runs cleanup normally.
 */
#include "encore.h"

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>

#ifdef __linux__
#include <linux/ppdev.h>
#include <linux/parport.h>
#endif

static int     s_fd          = -1;
static uint8_t s_ctrl_cached = 0;       /* mirrors guest's last 0x37A write */
static int     s_dir_input   = 0;       /* current PPDATADIR state          */

static void set_dir(int input)
{
#ifdef __linux__
    if (s_fd < 0) return;
    if (s_dir_input == !!input) return;
    int dir = input ? 1 : 0;
    if (ioctl(s_fd, PPDATADIR, &dir) == 0)
        s_dir_input = !!input;
#else
    (void)input;
#endif
}

bool lpt_passthrough_active(void)
{
    return s_fd >= 0;
}

/* Open + claim the device. Returns 0 on success, -1 on failure.
 * Caller decides whether failure is fatal (explicit --lpt-device) or
 * a silent fallback (default path attempted, no device present). */
int lpt_passthrough_open(const char *device, bool quiet_if_missing)
{
#ifndef __linux__
    (void)device; (void)quiet_if_missing;
    return -1;
#else
    if (!device || !*device) return -1;
    if (!strcmp(device, "none") || !strcmp(device, "off") ||
        !strcmp(device, "emu")  || !strcmp(device, "emulate"))
        return -1;

    int fd = open(device, O_RDWR);
    if (fd < 0) {
        /* Default-mode probe: stay quiet on ENOENT/EACCES so users without
         * a cabinet don't see scary-looking errors at boot. Anything else
         * (or any failure when --lpt-device was given explicitly) is logged. */
        if (!quiet_if_missing || (errno != ENOENT && errno != EACCES &&
                                  errno != ENXIO  && errno != ENODEV))
            LOG("lpt", "passthrough: open(%s) failed: %s\n",
                device, strerror(errno));
        return -1;
    }

    /* Verify bidirectional capability — P2K reads driver-board responses
     * via the data port, so reverse mode is mandatory. */
    int modes = 0;
    if (ioctl(fd, PPGETMODES, &modes) == 0) {
        const int needed = PARPORT_MODE_TRISTATE | PARPORT_MODE_PCSPP;
        if ((modes & needed) != needed) {
            LOG("lpt", "passthrough: %s lacks bidirectional support "
                       "(modes=0x%x, need TRISTATE|PCSPP) — refusing\n",
                device, modes);
            close(fd);
            return -1;
        }
    }
    /* PPEXCL must precede PPCLAIM to prevent lp/CUPS interference.
     * Non-fatal if it fails (older kernels): we still PPCLAIM. */
    (void)ioctl(fd, PPEXCL);

    if (ioctl(fd, PPCLAIM) < 0) {
        LOG("lpt", "passthrough: PPCLAIM(%s) failed: %s "
                   "(rmmod lp ? user in 'lp' group ?)\n",
            device, strerror(errno));
        close(fd);
        return -1;
    }

    s_fd = fd;
    s_ctrl_cached = 0;
    s_dir_input = -1;          /* force first set_dir() to take effect */
    set_dir(0);                /* idle = output (data latch holds bus) */

    LOG("lpt", "*** real cabinet detected on %s — keyboard/F-key button "
               "emulation DISABLED ***\n", device);
    LOG("lpt", "    guest LPT traffic forwarded to hardware; cabinet drives "
               "switch matrix\n");
    return 0;
#endif
}

void lpt_passthrough_close(void)
{
#ifdef __linux__
    if (s_fd < 0) return;
    (void)ioctl(s_fd, PPRELEASE);
    close(s_fd);
    s_fd = -1;
    LOG("lpt", "passthrough released\n");
#endif
}

/* === Guest I/O forwarding === */

uint8_t lpt_passthrough_read(uint8_t reg)
{
#ifdef __linux__
    unsigned char v = 0xFF;
    if (s_fd < 0) return v;

    switch (reg) {
    case 0: /* data port — flip to input so cabinet board can drive bus */
        set_dir(1);
        if (ioctl(s_fd, PPRDATA, &v) < 0)
            LOG("lpt", "PPRDATA failed: %s\n", strerror(errno));
        set_dir(0);                            /* restore output for latch */
        return v;
    case 1: /* status — always input on real LPT */
        if (ioctl(s_fd, PPRSTATUS, &v) < 0)
            LOG("lpt", "PPRSTATUS failed: %s\n", strerror(errno));
        return v;
    case 2: /* control — return cached guest-visible value */
        return s_ctrl_cached;
    default:
        return 0xFF;
    }
#else
    (void)reg;
    return 0xFF;
#endif
}

void lpt_passthrough_write(uint8_t reg, uint8_t val)
{
#ifdef __linux__
    if (s_fd < 0) return;
    unsigned char v = val;

    switch (reg) {
    case 0: /* data port — must be in output mode for the latch to hold */
        set_dir(0);
        if (ioctl(s_fd, PPWDATA, &v) < 0)
            LOG("lpt", "PPWDATA failed: %s\n", strerror(errno));
        return;
    case 2: /* control — cache and forward verbatim. Direction (bit 5) is
             * managed by encore around data-port accesses; P2K-driver
             * confirms the P2K driver never writes bit 5 itself. */
        s_ctrl_cached = val;
        if (ioctl(s_fd, PPWCONTROL, &v) < 0)
            LOG("lpt", "PPWCONTROL failed: %s\n", strerror(errno));
        return;
    default:
        return;
    }
#else
    (void)reg; (void)val;
#endif
}

/* ─────────────────────────────────────────────────────────────────────
 * Game auto-detect over the LPT cabinet board.
 *
 * Ported 1:1 from P2K-driver @ 0x804fe56 (disasm in build/scratch/
 * driver disassembly around line 5120). The driver-board exposes a custom
 * register bus bit-banged through the parallel port:
 *
 *   bus_write(addr, data)                         bus_read(addr)
 *     DATA  ← addr                                  DATA  ← addr
 *     CTRL  ← 0                                     CTRL  ← 0
 *     usleep(100)                                   usleep(100)
 *     CTRL  ← 4      (latch address)                CTRL  ← 4    (latch)
 *     DATA  ← data                                  CTRL  ← 0x2D (bus → read)
 *     CTRL  ← 5      (latch data)                   usleep(100)
 *     usleep(100)                                   value = inb(DATA)
 *     CTRL  ← 4      (done)                         CTRL  ← 4    (done)
 *
 * The probe walks 3 board-ID registers (0x04, 0x10, 0x11) in 8 selector
 * steps each, sets a precharge flag at 0xD, then computes two weighted
 * popcount sums across specific bytes of the captured arrays. The
 * decision thresholds (sum_20 vs sum_1c ranges) discriminate SWE1 from
 * RFM from an empty/unknown board.
 *
 * This is identity-matched to what the original binary does — so
 * plugging the same playfield into us yields the same string. Nothing
 * is touched on the cabinet-board register state that lpt_activate()
 * will later reinit, so the probe leaves no trace. */

#ifdef __linux__
static void pp_usleep(unsigned us) { usleep(us); }

static void bus_write(uint8_t addr, uint8_t data)
{
    lpt_passthrough_write(0, addr);
    lpt_passthrough_write(2, 0);
    pp_usleep(100);
    lpt_passthrough_write(2, 4);
    lpt_passthrough_write(0, data);
    lpt_passthrough_write(2, 5);
    pp_usleep(100);
    lpt_passthrough_write(2, 4);
}

static uint8_t bus_read(uint8_t addr)
{
    lpt_passthrough_write(0, addr);
    lpt_passthrough_write(2, 0);
    pp_usleep(100);
    lpt_passthrough_write(2, 4);
    lpt_passthrough_write(2, 0x2D);   /* bus → input, enable read strobe */
    pp_usleep(100);
    uint8_t v = lpt_passthrough_read(0);
    lpt_passthrough_write(2, 4);
    return v;
}

/* popcount() of (0xFF - val) & mask — matches the helper at
 * P2K-driver+0x804fe09: counts the LOW (0) bits of `val` inside `mask`. */
static int popcount_low_bits(uint8_t val, uint8_t mask)
{
    uint8_t bits = (uint8_t)(~val) & mask;
    int c = 0;
    for (int i = 0; i < 8; i++) { c += (bits >> i) & 1; }
    return c;
}
#endif /* __linux__ */

int lpt_passthrough_detect_game(char *out, size_t out_sz)
{
#ifndef __linux__
    (void)out; (void)out_sz;
    return -1;
#else
    if (s_fd < 0) return -1;

    uint8_t a1[8] = {0}, a2[8] = {0}, a3[8] = {0};

    /* Phase 1: walk 8 selector bits, read board-ID register 0x04 each time. */
    for (int i = 0; i < 8; i++) {
        bus_write(0x05, (uint8_t)(1u << i));
        pp_usleep(150);
        a1[i] = bus_read(0x04);
        bus_write(0x05, 0);
    }
    bus_write(0x0D, 0x80);  /* arm precharge latch */

    /* Phase 2: walk 8 selector bits on two lines, read reg 0x10. */
    for (int i = 0; i < 8; i++) {
        bus_write(0x05, (uint8_t)(1u << i));
        bus_write(0x08, (uint8_t)(1u << i));
        bus_write(0x06, 0xFF);
        bus_write(0x05, 0);
        pp_usleep(150);
        a2[i] = bus_read(0x10);
        bus_write(0x08, 0);
        bus_write(0x06, 0);
    }

    /* Phase 3: same walk, different read register (0x11). */
    for (int i = 0; i < 8; i++) {
        bus_write(0x05, (uint8_t)(1u << i));
        bus_write(0x08, (uint8_t)(1u << i));
        bus_write(0x07, 0xFF);
        bus_write(0x05, 0);
        pp_usleep(150);
        a3[i] = bus_read(0x11);
        bus_write(0x08, 0);
        bus_write(0x07, 0);
    }
    bus_write(0x0D, 0x00);  /* release precharge */

    /* Weighted popcounts across specific indices + masks — byte-for-byte
     * identical to P2K-driver+0x80500f6..0x80501c4. */
    int s20 = 0, s1c = 0;
    s20 += popcount_low_bits(a2[1], 0x04);   /* 0xe993ed5 */
    s20 += popcount_low_bits(a2[5], 0xC0);   /* 0xe993ed9 */
    s20 += popcount_low_bits(a3[3], 0x06);   /* 0x8170273 */
    s20 += popcount_low_bits(a3[5], 0xFF);   /* 0x8170275 */
    s1c += popcount_low_bits(a2[4], 0x80);   /* 0xe993ed8 */
    s1c += popcount_low_bits(a3[2], 0x80);   /* 0x8170272 */
    s1c += popcount_low_bits(a3[3], 0x80);   /* 0x8170273 */

    LOG("lpt", "board probe: s20=%d s1c=%d "
               "a1=%02x%02x%02x%02x%02x%02x%02x%02x\n",
        s20, s1c,
        a1[0], a1[1], a1[2], a1[3], a1[4], a1[5], a1[6], a1[7]);

    const char *game;
    if (s20 > 1 && s1c > 1) {
        game = "swe1";
    } else if (s20 <= 11 && s1c <= 1) {
        game = "rfm";
    } else {
        LOG("lpt", "board probe: no clear match (s20=%d s1c=%d)\n", s20, s1c);
        return -1;
    }

    strncpy(out, game, out_sz - 1);
    out[out_sz - 1] = '\0';
    LOG("lpt", "board auto-detect → %s\n", out);
    return 0;
#endif
}
