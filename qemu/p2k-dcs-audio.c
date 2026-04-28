/*
 * p2k-dcs-audio.c — Minimal DCS audio backend (proof-of-path).
 *
 * Goal: prove the QEMU audio pipeline is wired and the host can hear
 * the guest by emitting a short audible "blip" each time the guest
 * issues a DCS command.  This is a bring-up bridge — the real DCS-2
 * sample dispatch (pb2kslib, see unicorn.old/src/sound.c) is a much
 * bigger port.  Once the wiring is proven, we can grow this module
 * to feed real ADPCM/PCM samples without touching command-side code.
 *
 * Architecture rules (per user directive):
 *   - Use QEMU's native audio backend (audiodev), no second SDL app.
 *   - Pinball2000-specific code only describes what tone to play,
 *     QEMU owns mixing / device output / sample rate conversion.
 *   - Disable cleanly with P2K_NO_DCS_AUDIO=1 — defaults to OFF for
 *     now (P2K_DCS_AUDIO=1 to enable) so we don't surprise users
 *     before the proper sample path lands.
 *
 * Hook point: p2k_dcs_core_write_cmd() is the single shared DCS
 * command sink (p2k-dcs.c MMIO + p2k-dcs-uart.c I/O both funnel
 * through it).  We register a callback there.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "audio/audio.h"

#include "p2k-internal.h"

#include <math.h>

#define DCS_AUDIO_RATE     22050        /* low rate is fine for blips */
#define DCS_AUDIO_CHANS    1
#define DCS_AUDIO_BUF_MS   200
#define DCS_AUDIO_BUF_FRAMES (DCS_AUDIO_RATE * DCS_AUDIO_BUF_MS / 1000)

/* Registered (weak) hook used by p2k_dcs_core_write_cmd in the core. */
extern void (*p2k_dcs_core_audio_hook)(uint16_t cmd);

typedef struct DcsAudio {
    QEMUSoundCard card;
    SWVoiceOut   *voice;

    /* Tiny ring of pending sine-blips. Each blip = freq + samples_left. */
    uint32_t      blip_freq;
    uint32_t      blip_samples_left;
    double        blip_phase;
    uint32_t      blip_amp;             /* 0..32767 */

    bool          enabled;
    bool          opened;

    uint64_t      cmd_count;
} DcsAudio;

static DcsAudio s_dcs_audio;

/* Render up to `frames` 16-bit signed mono samples into out[] */
static int dcs_audio_render(int16_t *out, int frames)
{
    DcsAudio *a = &s_dcs_audio;
    int produced = 0;

    while (produced < frames) {
        if (a->blip_samples_left == 0) {
            break;
        }
        const double dphi = 2.0 * M_PI * (double)a->blip_freq /
                            (double)DCS_AUDIO_RATE;
        int batch = MIN(frames - produced, (int)a->blip_samples_left);
        for (int i = 0; i < batch; i++) {
            /* simple linear envelope to avoid clicks */
            uint32_t total = a->blip_samples_left;
            double env = (total < 256) ? (total / 256.0) : 1.0;
            int16_t s = (int16_t)(sin(a->blip_phase) * a->blip_amp * env);
            out[produced + i] = s;
            a->blip_phase += dphi;
            if (a->blip_phase > 2.0 * M_PI) a->blip_phase -= 2.0 * M_PI;
            a->blip_samples_left--;
        }
        produced += batch;
    }
    /* zero-fill remainder so QEMU mixer always has clean silence */
    for (int i = produced; i < frames; i++) {
        out[i] = 0;
    }
    return frames;
}

static void dcs_audio_callback(void *opaque, int avail_bytes)
{
    DcsAudio *a = opaque;
    if (!a->voice) return;

    /* avail_bytes is in bytes of host PCM; we emit S16 mono. */
    int16_t buf[512];
    while (avail_bytes >= (int)sizeof(buf)) {
        int frames = sizeof(buf) / sizeof(buf[0]);
        dcs_audio_render(buf, frames);
        size_t written = AUD_write(a->voice, buf, sizeof(buf));
        if (written == 0) break;
        avail_bytes -= (int)written;
    }
    if (avail_bytes > 0) {
        int frames = avail_bytes / (int)sizeof(int16_t);
        if (frames > (int)(sizeof(buf) / sizeof(buf[0]))) {
            frames = sizeof(buf) / sizeof(buf[0]);
        }
        dcs_audio_render(buf, frames);
        AUD_write(a->voice, buf, frames * sizeof(buf[0]));
    }
}

/* Cheap mapping: hash the 16-bit DCS cmd to an audible musical pitch
 * so distinct commands are distinguishable by ear during bring-up. */
static uint32_t cmd_to_freq(uint16_t cmd)
{
    static const uint32_t scale[] = {
        262, 294, 330, 349, 392, 440, 494, 523,
        587, 659, 698, 784, 880, 988, 1046, 1175,
    };
    return scale[cmd & 0x0F];
}

static void dcs_audio_on_cmd(uint16_t cmd)
{
    DcsAudio *a = &s_dcs_audio;
    if (!a->enabled || !a->opened) return;

    a->cmd_count++;

    /* Some commands are protocol housekeeping (0x0E suspend, etc.).
     * Still emit a blip — we can refine later. Keep blips short so
     * back-to-back commands don't fuse into a drone. */
    a->blip_freq         = cmd_to_freq(cmd);
    a->blip_samples_left = DCS_AUDIO_RATE / 20;     /* 50 ms */
    a->blip_amp          = 6000;                    /* low to avoid clipping */
    a->blip_phase        = 0.0;

    if (a->cmd_count <= 8) {
        info_report("dcs-audio: cmd 0x%04x -> %u Hz blip (#%llu)",
                    cmd, a->blip_freq,
                    (unsigned long long)a->cmd_count);
    }
}

void p2k_install_dcs_audio(void)
{
    DcsAudio *a = &s_dcs_audio;

    if (getenv("P2K_NO_DCS_AUDIO")) {
        info_report("pinball2000: DCS audio disabled (P2K_NO_DCS_AUDIO)");
        return;
    }
    /* Off by default during bring-up; opt-in with P2K_DCS_AUDIO=1 */
    if (!getenv("P2K_DCS_AUDIO")) {
        return;
    }

    Error *local_err = NULL;
    if (!AUD_register_card("p2k-dcs-audio", &a->card, &local_err)) {
        warn_report("pinball2000: AUD_register_card failed: %s",
                    local_err ? error_get_pretty(local_err) : "?");
        error_free(local_err);
        return;
    }

    struct audsettings as = {
        .freq       = DCS_AUDIO_RATE,
        .nchannels  = DCS_AUDIO_CHANS,
        .fmt        = AUDIO_FORMAT_S16,
        .endianness = 0,                /* host */
    };
    a->voice = AUD_open_out(&a->card, NULL, "p2k-dcs-out",
                            a, dcs_audio_callback, &as);
    if (!a->voice) {
        warn_report("pinball2000: AUD_open_out failed (no audiodev?). "
                    "Pass `-audiodev pa,id=spk -machine audiodev=spk` "
                    "or similar to enable.");
        AUD_remove_card(&a->card);
        return;
    }
    AUD_set_active_out(a->voice, 1);
    a->enabled = true;
    a->opened  = true;

    p2k_dcs_core_audio_hook = dcs_audio_on_cmd;

    info_report("pinball2000: DCS audio installed "
                "(%d Hz S16 mono proof-of-path; P2K_NO_DCS_AUDIO=1 to disable)",
                DCS_AUDIO_RATE);
}
