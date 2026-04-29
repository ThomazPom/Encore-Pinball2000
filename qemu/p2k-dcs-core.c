/*
 * pinball2000 DCS-2 shared core.
 *
 * Single owner of the DCS-2 protocol state (response queue, echo byte,
 * 0x0E suspend, ACE1 multi-word accumulator).  Both the BAR4 MMIO view
 * (p2k-dcs.c) and the I/O 0x138-0x13F UART overlay (p2k-dcs-uart.c)
 * must be thin frontends that call into here.  They are NOT allowed to
 * keep their own copies of the FIFO/handshake state.
 *
 * Removal of the parallel DcsState/p2k_dcs duplicates was the pending
 * 'dcs-shared-core' item in qemu/NOTES.next.md.
 *
 * Behavior is the union of the two pre-refactor implementations, which
 * already agreed on:
 *   - 5800/5A00  -> push 0x1000 (RESET ACK / "I'm alive")
 *   - 0x3A       -> push 0xCC01, 10 (boot dong)
 *   - 0x1B       -> push 0xCC09, 10
 *   - 0xAA       -> push 0xCC04, 10 (audio init handshake)
 *   - 0x0E       -> enter active/suspend mode (next 0x0E exits and
 *                   pushes 10 ack)
 *   - 0xACE1     -> begin multi-word capture, push 0x0100, 0x0C
 *   - inside ACE1: 1 word header (count = (hdr>>8)==0x55 ? 1 : 2)
 *                  followed by `count` data words; if header == 999/1000
 *                  push 0x100, 0x10 when complete
 *   - everything else -> consumed silently (sound playback opcodes have
 *                        no protocol response)
 *
 * Reference: unicorn.old/src/io.c:1427-1655 (legacy state machine).
 * No timing / no IRQs here.  Sound output is deliberately deferred.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"

#include "p2k-internal.h"

#define DCS_RESP_RING        64

typedef struct {
    uint16_t buf[DCS_RESP_RING];
    int      head;
    int      count;
    uint8_t  echo;
    int      active;          /* 0x0E suspend */
    int      pending;         /* ACE1 in progress */
    int      remaining;       /* ACE1 data words still expected */
    int      layer;           /* index into mixer[] */
    int      mixer[8];
    uint32_t cnt_cmd;
    uint32_t cnt_resp;
    uint32_t cnt_flag;
    bool     ready;
} DcsCore;

static DcsCore s_core;

/* Optional audio sink registered by p2k-dcs-audio.c. NULL = silent. */
/* Semantic audio hooks. Replaces the old single raw-cmd hook so the
 * audio backend sees the same shape Unicorn's sound.c saw:
 *   process_cmd(cmd)               -- direct sound triggers (Unicorn
 *                                     sound_process_cmd path)
 *   execute_mixer(cmd, d1, d2)     -- result of an ACE1 multi-word
 *                                     accumulator (Unicorn
 *                                     sound_execute_mixer path)
 * NULL by default; p2k-dcs-audio.c installs sinks when audio is on. */
void (*p2k_dcs_core_audio_process_cmd)(uint16_t cmd) = NULL;
void (*p2k_dcs_core_audio_execute_mixer)(uint16_t cmd,
                                         uint16_t data1,
                                         uint16_t data2) = NULL;

/* Source tag (frontend attribution).  Set by p2k_dcs_core_note_source()
 * just before each write_cmd; read by audio sinks for tracing. */
static const char *s_dcs_source_tag = "?";

/* DCS dispatch mode (Unicorn parity: --dcs-mode bar4 | io-handled).
 *  - bar4 (default):     game uses BAR4 MMIO at 0x13000000 for DCS commands.
 *                        Both frontends accept writes; that's how it has
 *                        always run.
 *  - io-handled:         BAR4 frontend MUST NOT carry DCS commands.  The
 *                        UART frontend at 0x138-0x13F is the only legal
 *                        DCS data-path.  Selected via env P2K_DCS_MODE
 *                        or run-qemu.sh --dcs-mode.
 * Acceptance for io-handled: UART.w or UART.bp counter must grow during
 * the run; sound must work; boot must remain stable. */
typedef enum {
    P2K_DCS_MODE_BAR4 = 0,
    P2K_DCS_MODE_IO_HANDLED = 1,
} P2KDcsMode;

static P2KDcsMode s_dcs_mode = P2K_DCS_MODE_BAR4;
static bool       s_dcs_mode_resolved = false;

P2KDcsMode p2k_dcs_core_mode(void)
{
    if (!s_dcs_mode_resolved) {
        const char *e = getenv("P2K_DCS_MODE");
        if (e && *e) {
            if (!strcmp(e, "io-handled") || !strcmp(e, "io_handled") ||
                !strcmp(e, "io")) {
                s_dcs_mode = P2K_DCS_MODE_IO_HANDLED;
            } else {
                s_dcs_mode = P2K_DCS_MODE_BAR4;
            }
        }
        s_dcs_mode_resolved = true;
    }
    return s_dcs_mode;
}

bool p2k_dcs_core_mode_is_io_handled(void)
{
    return p2k_dcs_core_mode() == P2K_DCS_MODE_IO_HANDLED;
}

const char *p2k_dcs_core_mode_name(void)
{
    return p2k_dcs_core_mode_is_io_handled() ? "io-handled" : "bar4";
}

void p2k_dcs_core_note_source(const char *src)
{
    s_dcs_source_tag = src ? src : "?";
}

const char *p2k_dcs_core_source(void)
{
    return s_dcs_source_tag;
}

static void core_push(uint16_t v)
{
    if (s_core.count >= DCS_RESP_RING) {
        return;                     /* drop on overflow */
    }
    int tail = (s_core.head + s_core.count) % DCS_RESP_RING;
    s_core.buf[tail] = v;
    s_core.count++;
}

void p2k_dcs_core_reset(void)
{
    if (s_core.ready) {
        return;                     /* both installers may call us; once is enough */
    }
    memset(&s_core, 0, sizeof(s_core));
    s_core.ready = true;
}

void p2k_dcs_core_set_echo(uint8_t v)
{
    s_core.echo = v;
}

uint8_t p2k_dcs_core_get_echo(void)
{
    return s_core.echo;
}

bool p2k_dcs_core_has_resp(void)
{
    return s_core.count > 0;
}

uint8_t p2k_dcs_core_flag_byte(void)
{
    s_core.cnt_flag++;
    uint8_t f = 0x40u;              /* always ready to accept */
    if (s_core.count > 0) {
        f |= 0x80u;                 /* response available */
    }
    return f;
}

uint16_t p2k_dcs_core_read_resp(void)
{
    if (s_core.count == 0) {
        return 0;
    }
    s_core.cnt_resp++;
    uint16_t v = s_core.buf[s_core.head];
    s_core.head = (s_core.head + 1) % DCS_RESP_RING;
    s_core.count--;
    return v;
}

void p2k_dcs_core_write_cmd(uint16_t cmd)
{
    s_core.cnt_cmd++;

    /* Active (0x0E) suspend: only another 0x0E exits, then ack */
    if (s_core.active) {
        if (cmd == 0x0E) {
            s_core.active    = 0;
            s_core.pending   = 0;
            s_core.remaining = 0;
            s_core.layer     = 0;
            core_push(10);
        }
        return;
    }

    /* ACE1 multi-word accumulator */
    if (s_core.pending) {
        if (s_core.remaining == 0) {
            /* This word is the header that announces the data count. */
            s_core.remaining = ((cmd >> 8) == 0x55) ? 1 : 2;
            s_core.mixer[0]  = cmd;
            s_core.layer     = 1;
            return;
        }
        if (s_core.layer < (int)ARRAY_SIZE(s_core.mixer)) {
            s_core.mixer[s_core.layer++] = cmd;
        }
        if (--s_core.remaining != 0) {
            return;
        }
        if (s_core.mixer[0] == 999 || s_core.mixer[0] == 1000) {
            core_push(0x100);
            core_push(0x10);
        } else if (p2k_dcs_core_audio_execute_mixer) {
            uint16_t m0 = (uint16_t)s_core.mixer[0];
            uint16_t m1 = (uint16_t)(s_core.layer > 1 ? s_core.mixer[1] : 0);
            uint16_t m2 = (uint16_t)(s_core.layer > 2 ? s_core.mixer[2] : 0);
            p2k_dcs_core_audio_execute_mixer(m0, m1, m2);
        }
        /* IMPORTANT: keep s_core.pending = 1.  Unicorn bar.c:946-948
         * resets only layer/remaining/mixer after a triple — dcs_pending
         * stays true, so every subsequent triple is also routed through
         * execute_mixer (with channel = (data2 & 0x380) >> 7).  Resetting
         * pending here was the bug that made post-ACE1 cmds fall through
         * to process_cmd's "ch = cmd & 7" branch — wrong channel,
         * stomped voices, ship/diag never audible.  0x0E (suspend) clears
         * pending; another 0xACE1 simply re-arms (idempotent). */
        s_core.layer     = 0;
        s_core.remaining = 0;
        memset(s_core.mixer, 0, sizeof(s_core.mixer));
        return;
    }

    switch (cmd) {
    case 0x5800:
    case 0x5A00:
        core_push(0x1000);
        break;
    case 0x3A:
        core_push(0xCC01);
        core_push(10);
        if (p2k_dcs_core_audio_process_cmd) {
            p2k_dcs_core_audio_process_cmd(0x003A);
        }
        break;
    case 0x1B:
        core_push(0xCC09);
        core_push(10);
        break;
    case 0xAA:
        core_push(0xCC04);
        core_push(10);
        if (p2k_dcs_core_audio_process_cmd) {
            p2k_dcs_core_audio_process_cmd(0x00AA);
        }
        break;
    case 0x0E:
        s_core.active  = 1;
        s_core.pending = 0;
        break;
    case 0xACE1:
        s_core.pending = 1;
        core_push(0x0100);
        core_push(0x0C);
        break;
    default:
        /* Direct sound trigger (matches Unicorn sound_process_cmd default). */
        if (p2k_dcs_core_audio_process_cmd) {
            p2k_dcs_core_audio_process_cmd(cmd);
        }
        break;
    }
}
