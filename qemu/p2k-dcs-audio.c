/*
 * p2k-dcs-audio.c — Real DCS sample playback via pb2kslib.
 *
 * Architecture (per user directive):
 *   - QEMU owns the audio backend (audiodev) and the output voice.
 *   - This module describes WHAT to play: decode the pb2kslib container
 *     once at install, look up samples by DCS command id, decode the
 *     Ogg Vorbis blob via libvorbisfile, mix up to N concurrent samples
 *     into the SWVoiceOut callback. No second SDL/audio app.
 *   - Reference behavior: unicorn.old/src/sound.c (SDL_mixer-based).
 *     We do NOT reuse SDL_mixer — we do the decode + mixing ourselves
 *     into the QEMU audio path.
 *
 * pb2kslib container format (see unicorn.old/src/sound.c:91-220):
 *   header = u32 ver(=1) | u32 hdr_size(=0x10) | u32 entry_cnt | u32 entry_sz(=0x48)
 *   each entry (0x48 bytes, XOR'd with 0x3A): name[32] + u32 fields[10]
 *     - name[32]                : "S####" hex track id, or "dcs-bong"
 *     - fields[8] = offset      : byte offset of OGG payload in container
 *     - fields[9] = size        : size of OGG payload
 *   payload bytes are XOR'd with 0x3A; after de-XOR, plain Ogg Vorbis.
 *
 * Defaults:
 *   - OFF unless P2K_DCS_AUDIO=1 is set (the wrapper does this when a
 *     host audiodev is auto-detected).
 *   - P2K_NO_DCS_AUDIO=1 forces off.
 *   - When the pb2kslib container can't be located/parsed, fall back to
 *     the synthesized-blip mode (proof-of-path), still hooked through
 *     the same voice/mixer.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "audio/audio.h"

#include "p2k-internal.h"

#include <math.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <strings.h>
#include <ctype.h>

#include <vorbis/vorbisfile.h>

#define DCS_OUT_RATE       44100
#define DCS_OUT_CHANS      1
#define DCS_VOICES         8
#define PB2K_MAX_ENTRIES   1024
#define SAMPLE_CACHE_SIZE  0x1000

typedef struct {
    char     name[33];
    uint16_t track_cmd;
    uint32_t offset;
    uint32_t size;
} Pb2kEntry;

typedef struct {
    int16_t *pcm;            /* mono S16 at DCS_OUT_RATE, malloc'd */
    size_t   frames;
} Sample;

typedef struct {
    const Sample *s;
    size_t        pos;       /* frame index into s->pcm */
    uint8_t       vol;       /* 0..255 per Unicorn */
    uint8_t       pan;       /* 0..255 (0x7F = center) */
    bool          active;
    uint16_t      cmd;       /* originating DCS cmd id (or lookup_cmd) */
    const char   *name;      /* pb2k entry name (interned, points into entries[]) */
    uint64_t      started_at_cb;  /* a->callbacks at start time (replacement age) */
} Voice;

extern void (*p2k_dcs_core_audio_process_cmd)(uint16_t cmd);
extern void (*p2k_dcs_core_audio_execute_mixer)(uint16_t cmd,
                                                uint16_t data1,
                                                uint16_t data2);

typedef struct DcsAudio {
    QEMUSoundCard card;
    SWVoiceOut   *voice;
    bool          enabled;
    bool          opened;
    uint64_t      cmd_count;
    uint64_t      played_count;
    uint64_t      missed_count;

    /* pb2kslib container */
    uint8_t      *pb2k_data;
    size_t        pb2k_size;
    Pb2kEntry     entries[PB2K_MAX_ENTRIES];
    int           entry_cnt;
    int           bong_idx;

    /* sample cache: indexed by track_cmd */
    Sample       *cache[SAMPLE_CACHE_SIZE];
    uint8_t       cache_tried[SAMPLE_CACHE_SIZE]; /* 1 = decode attempted */

    /* mixer */
    Voice         voices[DCS_VOICES];
    uint8_t       global_vol;     /* 0..255, applied to all channels */
    bool          trace;          /* P2K_DCS_AUDIO_TRACE=1 */

    /* Render/output proof.  Updated by the audio callback thread; we
     * sample from the QEMU main thread via the per-second timer.  Reads
     * are not strictly atomic but tearing is benign for diagnostics. */
    uint64_t      callbacks;          /* dcs_audio_callback() entries */
    uint64_t      aud_write_calls;    /* AUD_write() invocations */
    uint64_t      aud_write_bytes;    /* total bytes accepted by AUD_write */
    uint64_t      aud_write_zero;     /* AUD_write returning 0 */
    int32_t       peak_mix_abs;       /* max |sample| since last status log */
    uint64_t      frames_rendered;    /* total frames produced */

    /* Per-second status timer + source-tag attribution (raw cmd
     * histogram by source) — diagnostic only. */
    QEMUTimer    *status_timer;
    uint64_t      cmd_by_bar4;
    uint64_t      cmd_by_uart_w;
    uint64_t      cmd_by_uart_bp;
    uint64_t      cmd_by_other;

    /* startup hello + fallback blip */
    uint32_t      blip_freq;
    uint32_t      blip_samples_left;
    uint32_t      blip_amp;
    double        blip_phase;
} DcsAudio;

static DcsAudio s_dcs_audio;

/* ------------------------------------------------------------------ */
/* pb2kslib loader                                                     */
/* ------------------------------------------------------------------ */

static bool pb2k_validate(const uint8_t hdr[16], off_t fsize)
{
    uint32_t ver       = ldl_le_p(hdr + 0);
    uint32_t hdr_size  = ldl_le_p(hdr + 4);
    uint32_t entry_cnt = ldl_le_p(hdr + 8);
    uint32_t entry_sz  = ldl_le_p(hdr + 12);
    if (ver != 1u || hdr_size != 0x10u || entry_sz != 0x48u) return false;
    if (entry_cnt == 0 || entry_cnt > PB2K_MAX_ENTRIES) return false;
    uint64_t need = (uint64_t)hdr_size + (uint64_t)entry_cnt * entry_sz;
    if ((uint64_t)fsize < need) return false;
    return true;
}

static bool pb2k_find_container(const char *roms_dir, const char *game_prefix,
                                char *out, size_t out_sz)
{
    DIR *d = opendir(roms_dir);
    if (!d) return false;
    char best[512] = { 0 };
    int  best_score = -1;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", roms_dir, de->d_name);
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 64) continue;
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;
        uint8_t hdr[16];
        ssize_t n = read(fd, hdr, 16);
        close(fd);
        if (n != 16 || !pb2k_validate(hdr, st.st_size)) continue;
        int score = 0;
        size_t plen = game_prefix ? strlen(game_prefix) : 0;
        if (plen && strncasecmp(de->d_name, game_prefix, plen) == 0) score += 10;
        if (score > best_score) {
            best_score = score;
            strncpy(best, path, sizeof(best) - 1);
            best[sizeof(best) - 1] = '\0';
        }
    }
    closedir(d);
    if (best_score < 0) return false;
    strncpy(out, best, out_sz - 1);
    out[out_sz - 1] = '\0';
    return true;
}

static void pb2k_load(DcsAudio *a, const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return; }
    a->pb2k_size = st.st_size;
    a->pb2k_data = mmap(NULL, a->pb2k_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (a->pb2k_data == MAP_FAILED) {
        a->pb2k_data = NULL;
        a->pb2k_size = 0;
        return;
    }
    uint32_t hdr_size  = ldl_le_p(a->pb2k_data + 4);
    uint32_t entry_cnt = ldl_le_p(a->pb2k_data + 8);
    uint32_t entry_sz  = ldl_le_p(a->pb2k_data + 12);
    if (entry_cnt > PB2K_MAX_ENTRIES) entry_cnt = PB2K_MAX_ENTRIES;
    if (entry_sz < 72) return;

    uint8_t decoded[72];
    a->bong_idx = -1;
    for (uint32_t i = 0; i < entry_cnt; i++) {
        const uint8_t *raw = a->pb2k_data + hdr_size + i * entry_sz;
        for (int j = 0; j < 72; j++) decoded[j] = raw[j] ^ 0x3A;
        Pb2kEntry *e = &a->entries[i];
        memcpy(e->name, decoded, 32);
        e->name[32] = '\0';
        e->offset    = ldl_le_p(decoded + 32 + 8 * 4);
        e->size      = ldl_le_p(decoded + 32 + 9 * 4);
        e->track_cmd = 0xFFFF;
        if (e->name[0] == 'S') {
            char hex_buf[5] = { e->name[1], e->name[2], e->name[3], e->name[4], '\0' };
            unsigned long val = strtoul(hex_buf, NULL, 16);
            if (val <= 0xFFFF) e->track_cmd = (uint16_t)val;
        }
        if (strcmp(e->name, "dcs-bong") == 0) {
            a->bong_idx = (int)i;
            e->track_cmd = 0x003A;
        }
    }
    a->entry_cnt = (int)entry_cnt;
    info_report("dcs-audio: pb2kslib loaded %d entries from %s",
                a->entry_cnt, path);
}

/* ------------------------------------------------------------------ */
/* Vorbis decode                                                       */
/* ------------------------------------------------------------------ */

typedef struct OvMem {
    const uint8_t *data;
    size_t         size;
    size_t         pos;
} OvMem;

static size_t ov_mem_read(void *ptr, size_t sz, size_t nm, void *user)
{
    OvMem *m = user;
    size_t want = sz * nm;
    size_t avail = m->size - m->pos;
    if (want > avail) want = avail - (avail % sz);
    memcpy(ptr, m->data + m->pos, want);
    m->pos += want;
    return want / sz;
}
static int ov_mem_seek(void *user, ogg_int64_t off, int whence)
{
    OvMem *m = user;
    size_t np;
    switch (whence) {
        case SEEK_SET: np = (size_t)off; break;
        case SEEK_CUR: np = m->pos + (size_t)off; break;
        case SEEK_END: np = m->size + (size_t)off; break;
        default: return -1;
    }
    if (np > m->size) return -1;
    m->pos = np;
    return 0;
}
static long   ov_mem_tell (void *user) { return (long)((OvMem *)user)->pos; }
static int    ov_mem_close(void *user) { (void)user; return 0; }
static const ov_callbacks ov_mem_cb = {
    ov_mem_read, ov_mem_seek, ov_mem_close, ov_mem_tell,
};

/* Decode an Ogg Vorbis blob to mono S16 at DCS_OUT_RATE.
 * Returns NULL on failure. Caller frees ->pcm and the Sample. */
static Sample *decode_ogg_to_s16(const uint8_t *data, size_t size)
{
    OvMem mem = { .data = data, .size = size, .pos = 0 };
    OggVorbis_File vf;
    if (ov_open_callbacks(&mem, &vf, NULL, 0, ov_mem_cb) < 0) return NULL;

    vorbis_info *vi = ov_info(&vf, -1);
    if (!vi) { ov_clear(&vf); return NULL; }
    int src_chans = vi->channels;
    long src_rate = vi->rate;

    /* Decode all to interleaved S16 host-endian at native rate. */
    size_t cap = 1 << 15, got = 0;
    int16_t *raw = g_malloc(cap * sizeof(int16_t));
    char buf[4096];
    int bs = 0;
    for (;;) {
        long n = ov_read(&vf, buf, sizeof(buf), 0 /*LE*/, 2, 1, &bs);
        if (n <= 0) break;
        size_t samples = (size_t)n / 2;
        if (got + samples > cap) {
            while (got + samples > cap) cap *= 2;
            raw = g_realloc(raw, cap * sizeof(int16_t));
        }
        memcpy(raw + got, buf, n);
        got += samples;
    }
    ov_clear(&vf);

    if (got == 0) { g_free(raw); return NULL; }

    /* Down-mix to mono (avg L/R if stereo). */
    size_t src_frames = got / src_chans;
    int16_t *mono = g_malloc(src_frames * sizeof(int16_t));
    if (src_chans == 1) {
        memcpy(mono, raw, src_frames * sizeof(int16_t));
    } else {
        for (size_t i = 0; i < src_frames; i++) {
            int32_t acc = 0;
            for (int c = 0; c < src_chans; c++) acc += raw[i * src_chans + c];
            mono[i] = (int16_t)(acc / src_chans);
        }
    }
    g_free(raw);

    /* Linear-resample to DCS_OUT_RATE. */
    size_t dst_frames = (size_t)((double)src_frames * DCS_OUT_RATE / (double)src_rate);
    if (dst_frames == 0) { g_free(mono); return NULL; }
    int16_t *out = g_malloc(dst_frames * sizeof(int16_t));
    for (size_t i = 0; i < dst_frames; i++) {
        double t = (double)i * (double)src_rate / (double)DCS_OUT_RATE;
        size_t i0 = (size_t)t;
        if (i0 >= src_frames - 1) { out[i] = mono[src_frames - 1]; continue; }
        double frac = t - (double)i0;
        int32_t s = (int32_t)((1.0 - frac) * mono[i0] + frac * mono[i0 + 1]);
        if (s >  32767) s =  32767;
        if (s < -32768) s = -32768;
        out[i] = (int16_t)s;
    }
    g_free(mono);

    Sample *s = g_new0(Sample, 1);
    s->pcm    = out;
    s->frames = dst_frames;
    return s;
}

static Sample *get_sample_for_cmd(DcsAudio *a, uint16_t cmd,
                                  const char **out_name)
{
    if (out_name) *out_name = NULL;
    if (cmd >= SAMPLE_CACHE_SIZE) return NULL;

    /* Find entry by track_cmd (cached lookup attempted only once). */
    int idx = -1;
    for (int i = 0; i < a->entry_cnt; i++) {
        if (a->entries[i].track_cmd == cmd) { idx = i; break; }
    }
    if (idx < 0) return NULL;
    Pb2kEntry *e = &a->entries[idx];
    if (out_name) *out_name = e->name;

    if (a->cache[cmd]) return a->cache[cmd];
    if (a->cache_tried[cmd]) return NULL;
    a->cache_tried[cmd] = 1;

    if (e->size == 0 || e->offset + e->size > a->pb2k_size) return NULL;

    /* De-XOR payload into a temp buffer, then decode. */
    uint8_t *blob = g_malloc(e->size);
    const uint8_t *src = a->pb2k_data + e->offset;
    for (uint32_t i = 0; i < e->size; i++) blob[i] = src[i] ^ 0x3A;
    Sample *s = decode_ogg_to_s16(blob, e->size);
    g_free(blob);
    if (!s) return NULL;
    a->cache[cmd] = s;
    return s;
}

/* ------------------------------------------------------------------ */
/* Mixer + QEMU audio callback                                         */
/* ------------------------------------------------------------------ */

/* Start sample on a fixed channel (Unicorn semantics: re-trigger on
 * the same channel halts the previous voice).  Logs replacement so we
 * can see if a sample is being immediately stomped by another. */
static void start_voice_on_channel(DcsAudio *a, int ch, const Sample *s,
                                   int vol_8bit, int pan_8bit,
                                   uint16_t cmd, const char *name)
{
    if (ch < 0 || ch >= DCS_VOICES) ch = 0;
    if (vol_8bit < 0)   vol_8bit = 0;
    if (vol_8bit > 255) vol_8bit = 255;
    if (pan_8bit < 0)   pan_8bit = 0;
    if (pan_8bit > 255) pan_8bit = 255;
    Voice *vc = &a->voices[ch];
    if (vc->active && vc->s) {
        /* Replacement: the old voice is being preempted by a new one
         * on the same channel.  Emit a one-line bucket-5 trace. */
        info_report("dcs-audio: ch%d REPLACE old(cmd=0x%04x name=%s "
                    "pos=%zu/%zu vol=%u) ← new(cmd=0x%04x name=%s vol=%d)",
                    ch, vc->cmd, vc->name ? vc->name : "?",
                    vc->pos, vc->s->frames, vc->vol,
                    cmd, name ? name : "?", vol_8bit);
    }
    vc->s              = s;
    vc->pos            = 0;
    vc->vol            = (uint8_t)vol_8bit;
    vc->pan            = (uint8_t)pan_8bit;
    vc->active         = true;
    vc->cmd            = cmd;
    vc->name           = name;
    vc->started_at_cb  = a->callbacks;
}

static void stop_all_voices(DcsAudio *a)
{
    for (int i = 0; i < DCS_VOICES; i++) {
        Voice *vc = &a->voices[i];
        if (vc->active && vc->s) {
            info_report("dcs-audio: ch%d STOP-ALL was(cmd=0x%04x name=%s "
                        "pos=%zu/%zu)",
                        i, vc->cmd, vc->name ? vc->name : "?",
                        vc->pos, vc->s->frames);
        }
        vc->s = NULL;
        vc->active = false;
    }
}

static void render(DcsAudio *a, int16_t *out, int frames)
{
    /* Per-channel mix:
     *   amp_q15 = (vol * global_vol * 28000) / (255 * 255)
     * leaves headroom for summing 8 voices. */
    int32_t local_peak = 0;
    for (int i = 0; i < frames; i++) {
        int32_t mix = 0;
        if (a->blip_samples_left > 0) {
            const double dphi = 2.0 * M_PI * (double)a->blip_freq / DCS_OUT_RATE;
            uint32_t total = a->blip_samples_left;
            double env = (total < 256) ? (total / 256.0) : 1.0;
            mix += (int32_t)(sin(a->blip_phase) * a->blip_amp * env);
            a->blip_phase += dphi;
            if (a->blip_phase > 2.0 * M_PI) a->blip_phase -= 2.0 * M_PI;
            a->blip_samples_left--;
        }
        for (int v = 0; v < DCS_VOICES; v++) {
            Voice *vc = &a->voices[v];
            if (!vc->active || !vc->s) continue;
            if (vc->pos >= vc->s->frames) {
                info_report("dcs-audio: ch%d END  (cmd=0x%04x name=%s "
                            "frames=%zu)",
                            v, vc->cmd, vc->name ? vc->name : "?",
                            vc->s->frames);
                vc->s = NULL;
                vc->active = false;
                continue;
            }
            int32_t s = vc->s->pcm[vc->pos++];
            int32_t amp = (int32_t)vc->vol * (int32_t)a->global_vol * 28000;
            amp /= (255 * 255);
            mix += (s * amp) >> 15;
        }
        if (mix >  32767) mix =  32767;
        if (mix < -32768) mix = -32768;
        out[i] = (int16_t)mix;
        int32_t am = mix < 0 ? -mix : mix;
        if (am > local_peak) local_peak = am;
    }
    a->frames_rendered += (uint64_t)frames;
    if (local_peak > a->peak_mix_abs) a->peak_mix_abs = local_peak;
}

static void dcs_audio_callback(void *opaque, int avail_bytes)
{
    DcsAudio *a = opaque;
    if (!a->voice) return;
    a->callbacks++;
    int16_t buf[1024];
    while (avail_bytes >= (int)sizeof(buf)) {
        int frames = sizeof(buf) / sizeof(buf[0]);
        render(a, buf, frames);
        size_t w = AUD_write(a->voice, buf, sizeof(buf));
        a->aud_write_calls++;
        a->aud_write_bytes += (uint64_t)w;
        if (w == 0) { a->aud_write_zero++; break; }
        avail_bytes -= (int)w;
    }
    if (avail_bytes > 0) {
        int frames = avail_bytes / (int)sizeof(int16_t);
        if (frames > (int)(sizeof(buf) / sizeof(buf[0]))) {
            frames = sizeof(buf) / sizeof(buf[0]);
        }
        render(a, buf, frames);
        size_t w = AUD_write(a->voice, buf, frames * sizeof(buf[0]));
        a->aud_write_calls++;
        a->aud_write_bytes += (uint64_t)w;
        if (w == 0) a->aud_write_zero++;
    }
}

/* Fallback proof-of-path tone when no sample matches the cmd id. */
static uint32_t cmd_to_freq(uint16_t cmd)
{
    static const uint32_t scale[] = {
        262, 294, 330, 349, 392, 440, 494, 523,
        587, 659, 698, 784, 880, 988, 1046, 1175,
    };
    return scale[cmd & 0x0F];
}

/* One-line trace per audio event: shows enough to bucket the failure
 * (event source, full triple, computed channel/vol/pan, lookup key,
 * pb2k entry name if found, sample frames, voice slot). */
#define TRACE_EVT(a, fmt, ...) do {                            \
    if ((a)->trace || (a)->cmd_count <= 64) {                  \
        info_report("dcs-audio: " fmt, ##__VA_ARGS__);         \
    }                                                          \
} while (0)

/* Source-tag attribution: read once per cmd from the core. */
static const char *evt_source(DcsAudio *a)
{
    const char *src = p2k_dcs_core_source();
    if (!src) src = "?";
    if (!strcmp(src, "BAR4"))           a->cmd_by_bar4++;
    else if (!strcmp(src, "UART:0x13c.w"))  a->cmd_by_uart_w++;
    else if (!strcmp(src, "UART:0x13c.bp")) a->cmd_by_uart_bp++;
    else                                a->cmd_by_other++;
    return src;
}

/* Process-cmd hook: receives semantic direct-trigger events from
 * p2k-dcs-core (matches Unicorn sound.c sound_process_cmd contract). */
static void dcs_audio_on_process_cmd(uint16_t cmd)
{
    DcsAudio *a = &s_dcs_audio;
    if (!a->enabled || !a->opened) return;
    a->cmd_count++;
    const char *src = evt_source(a);

    /* Mirrors Unicorn sound.c sound_process_cmd:
     *   0x0000        -> halt all channels
     *   0x003A        -> boot dong on channel 0
     *   0x00AA        -> 0x0FFF special on channel 7
     *   cmd in [0x0100, 0x1000) -> normal track on channel (cmd & 7)
     *   else          -> ignore (protocol byte / out-of-range).
     * Unicorn SAMPLE_CACHE_SIZE = 0x1000 (sound.c:14). Anything >= 0x1000
     * is silently ignored — that filters mixer-ctrl raw words like
     * 0x55AB/0x55AC and the 0xFF7F/0x80XX/0x82XX protocol noise out
     * of the "missed" trace bucket. */
    if (cmd == 0x0000) {
        TRACE_EVT(a, "[%s] process_cmd 0x0000 → STOP ALL", src);
        stop_all_voices(a);
        return;
    }

    int channel;
    uint16_t lookup_cmd;
    if (cmd == 0x003A) {
        channel = 0;
        lookup_cmd = 0x003A;
    } else if (cmd == 0x00AA) {
        channel = 7;
        lookup_cmd = 0x0FFF;
    } else if (cmd >= 0x0100 && cmd < 0x1000) {
        channel = cmd & 7;
        lookup_cmd = cmd;
    } else {
        TRACE_EVT(a, "[%s] process_cmd 0x%04x → ignored "
                  "(out of cache range)", src, cmd);
        return;
    }

    const char *name = NULL;
    Sample *s = get_sample_for_cmd(a, lookup_cmd, &name);
    if (s) {
        start_voice_on_channel(a, channel, s, /*vol*/255, /*pan*/0x7F,
                               lookup_cmd, name);
        a->played_count++;
        TRACE_EVT(a,
                  "[%s] process_cmd 0x%04x → key=0x%04x name=%s frames=%zu "
                  "ch=%d vol=255 pan=127 (played #%llu)",
                  src, cmd, lookup_cmd, name ? name : "?", s->frames,
                  channel, (unsigned long long)a->played_count);
    } else {
        a->missed_count++;
        TRACE_EVT(a,
                  "[%s] process_cmd 0x%04x → key=0x%04x %s (missed #%llu)",
                  src, cmd, lookup_cmd,
                  name ? "decode-failed" : "no-pb2k-entry",
                  (unsigned long long)a->missed_count);
    }
}

/* Execute-mixer hook: ACE1 multi-word accumulator finished.
 * Mirrors Unicorn sound.c sound_execute_mixer:
 *   cmd >> 8 == 0x55  -> vol/pan/global control
 *   else              -> sound_play_track(cmd, ch=(d2 & 0x380)>>7,
 *                                         vol=(d1 >> 8) & 0xFF,
 *                                         pan=d1 & 0xFF). */
static void dcs_audio_on_execute_mixer(uint16_t cmd, uint16_t data1,
                                       uint16_t data2)
{
    DcsAudio *a = &s_dcs_audio;
    if (!a->enabled || !a->opened) return;
    a->cmd_count++;
    const char *src = evt_source(a);

    if ((cmd & 0xFF00) == 0x5500) {
        switch (cmd) {
        case 0x55AA:
            /* Unicorn sound_set_global_volume: data1 & 0xFF (low byte). */
            a->global_vol = (uint8_t)(data1 & 0xFF);
            TRACE_EVT(a,
                      "[%s] execute_mixer 0x%04x d1=0x%04x → GLOBAL_VOL=%u",
                      src, cmd, data1, a->global_vol);
            break;
        case 0x55AB:
        case 0x55AE: {
            int ch  = (data1 >> 8) & 0x07;
            int vol = data1 & 0xFF;
            a->voices[ch].vol = (uint8_t)vol;
            TRACE_EVT(a,
                      "[%s] execute_mixer 0x%04x d1=0x%04x → ch%d VOL=%d",
                      src, cmd, data1, ch, vol);
            break;
        }
        case 0x55AC: {
            int ch  = (data1 >> 8) & 0x07;
            int pan = data1 & 0xFF;
            a->voices[ch].pan = (uint8_t)pan;
            TRACE_EVT(a,
                      "[%s] execute_mixer 0x%04x d1=0x%04x → ch%d PAN=%d",
                      src, cmd, data1, ch, pan);
            break;
        }
        default:
            TRACE_EVT(a,
                      "[%s] execute_mixer 0x%04x d1=0x%04x d2=0x%04x → "
                      "unknown mixer-ctrl",
                      src, cmd, data1, data2);
            break;
        }
        return;
    }

    int channel = (data2 & 0x380) >> 7;
    int vol     = (data1 >> 8) & 0xFF;
    int pan     = data1 & 0xFF;
    /* Unicorn does NOT default vol=0 to full; vol=0 → silent voice
     * (Mix_Volume(track_idx, 0)). Match that. */

    const char *name = NULL;
    Sample *s = get_sample_for_cmd(a, cmd, &name);
    if (s) {
        start_voice_on_channel(a, channel, s, vol, pan, cmd, name);
        a->played_count++;
        TRACE_EVT(a,
                  "[%s] execute_mixer 0x%04x d1=0x%04x d2=0x%04x → "
                  "name=%s frames=%zu ch=%d vol=%d pan=%d (played #%llu)",
                  src, cmd, data1, data2, name ? name : "?", s->frames,
                  channel, vol, pan,
                  (unsigned long long)a->played_count);
    } else {
        a->missed_count++;
        TRACE_EVT(a,
                  "[%s] execute_mixer 0x%04x d1=0x%04x d2=0x%04x → "
                  "key=0x%04x %s (missed #%llu)",
                  src, cmd, data1, data2, cmd,
                  name ? "decode-failed" : "no-pb2k-entry",
                  (unsigned long long)a->missed_count);
    }
}

/* Per-second status report.  Logs render/output proof + active voices
 * + source histogram so we can bucket missing audio without rebuilding. */
static void dcs_audio_status_tick(void *opaque)
{
    DcsAudio *a = opaque;
    if (a->enabled) {
        int n_active = 0;
        for (int i = 0; i < DCS_VOICES; i++) {
            if (a->voices[i].active) n_active++;
        }
        info_report("dcs-audio status: cb=%llu frames=%llu "
                    "AUD_write calls=%llu bytes=%llu zero=%llu peak=%d "
                    "active=%d/%d global_vol=%u played=%llu missed=%llu "
                    "src{BAR4=%llu UART.w=%llu UART.bp=%llu other=%llu}",
                    (unsigned long long)a->callbacks,
                    (unsigned long long)a->frames_rendered,
                    (unsigned long long)a->aud_write_calls,
                    (unsigned long long)a->aud_write_bytes,
                    (unsigned long long)a->aud_write_zero,
                    (int)a->peak_mix_abs,
                    n_active, DCS_VOICES,
                    a->global_vol,
                    (unsigned long long)a->played_count,
                    (unsigned long long)a->missed_count,
                    (unsigned long long)a->cmd_by_bar4,
                    (unsigned long long)a->cmd_by_uart_w,
                    (unsigned long long)a->cmd_by_uart_bp,
                    (unsigned long long)a->cmd_by_other);
        for (int i = 0; i < DCS_VOICES; i++) {
            Voice *vc = &a->voices[i];
            if (!vc->active || !vc->s) continue;
            info_report("dcs-audio   ch%d cmd=0x%04x name=%s pos=%zu/%zu "
                        "vol=%u pan=%u",
                        i, vc->cmd, vc->name ? vc->name : "?",
                        vc->pos, vc->s->frames, vc->vol, vc->pan);
        }
        a->peak_mix_abs = 0;            /* reset window */
    }
    timer_mod(a->status_timer,
              qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1000);
}

/* ------------------------------------------------------------------ */
/* Install                                                             */
/* ------------------------------------------------------------------ */

void p2k_install_dcs_audio(Pinball2000MachineState *st)
{
    DcsAudio *a = &s_dcs_audio;

    if (getenv("P2K_NO_DCS_AUDIO")) {
        info_report("pinball2000: DCS audio disabled (P2K_NO_DCS_AUDIO)");
        return;
    }
    if (!getenv("P2K_DCS_AUDIO")) {
        return;                          /* off by default; wrapper opts in */
    }

    Error *local_err = NULL;
    if (!AUD_register_card("p2k-dcs-audio", &a->card, &local_err)) {
        warn_report("pinball2000: AUD_register_card failed: %s",
                    local_err ? error_get_pretty(local_err) : "?");
        error_free(local_err);
        return;
    }
    struct audsettings as = {
        .freq       = DCS_OUT_RATE,
        .nchannels  = DCS_OUT_CHANS,
        .fmt        = AUDIO_FORMAT_S16,
        .endianness = 0,
    };
    a->voice = AUD_open_out(&a->card, NULL, "p2k-dcs-out",
                            a, dcs_audio_callback, &as);
    if (!a->voice) {
        warn_report("pinball2000: AUD_open_out failed (no audiodev?). "
                    "Pass `-audio driver=pa` (or `--audio pa` via wrapper).");
        AUD_remove_card(&a->card);
        return;
    }
    AUD_set_active_out(a->voice, 1);
    a->enabled = true;
    a->opened  = true;
    a->bong_idx = -1;
    a->global_vol = 255;     /* full volume until guest sets 0x55AA */
    a->trace = (getenv("P2K_DCS_AUDIO_TRACE") != NULL);
    for (int i = 0; i < DCS_VOICES; i++) {
        a->voices[i].vol = 255;
        a->voices[i].pan = 0x7F;
        a->voices[i].active = false;
        a->voices[i].s = NULL;
    }

    /* Try to mount the pb2kslib container. Failure is non-fatal — the
     * fallback blip path keeps proof-of-path audible. */
    if (st && st->roms_dir) {
        char path[512];
        const char *prefix = st->game ? st->game : "";
        if (pb2k_find_container(st->roms_dir, prefix, path, sizeof(path))) {
            pb2k_load(a, path);
        } else {
            warn_report("dcs-audio: no pb2kslib container found in %s "
                        "(samples will fall back to blips)", st->roms_dir);
        }
    }

    /* 600 ms 660 Hz hello tone so the user knows audio is alive. */
    a->blip_freq         = 660;
    a->blip_samples_left = DCS_OUT_RATE * 600 / 1000;
    a->blip_amp          = 16000;
    a->blip_phase        = 0.0;

    p2k_dcs_core_audio_process_cmd   = dcs_audio_on_process_cmd;
    p2k_dcs_core_audio_execute_mixer = dcs_audio_on_execute_mixer;

    /* Per-second status timer when trace is on (render/output proof). */
    if (a->trace) {
        a->status_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL,
                                       dcs_audio_status_tick, a);
        timer_mod(a->status_timer,
                  qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1000);
    }

    info_report("pinball2000: DCS audio installed "
                "(%d Hz S16 mono, %d-voice mixer, %d pb2k entries; "
                "P2K_NO_DCS_AUDIO=1 to disable)",
                DCS_OUT_RATE, DCS_VOICES, a->entry_cnt);
}
