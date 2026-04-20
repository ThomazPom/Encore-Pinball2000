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
int lpt_passthrough_open(const char *device)
{
#ifndef __linux__
    (void)device;
    return -1;
#else
    if (!device || !*device) return -1;
    if (!strcmp(device, "none") || !strcmp(device, "off") ||
        !strcmp(device, "emu")  || !strcmp(device, "emulate"))
        return -1;

    int fd = open(device, O_RDWR);
    if (fd < 0) {
        LOG("lpt", "passthrough: open(%s) failed: %s\n", device, strerror(errno));
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

    LOG("lpt", "passthrough ENABLED on %s — emulated state machine bypassed\n",
        device);
    LOG("lpt", "  cabinet board drives switch matrix; F-key injection inert\n");
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
