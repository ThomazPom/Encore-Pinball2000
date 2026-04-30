/*
 * p2k-dcs-audio.c — Real DCS sample playback via pb2kslib.
 *
 * Architecture:
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
 * Container path resolution (host-agnostic):
 *   1. $P2K_PB2KSLIB if set
 *   2. <roms_dir>/<game>_sound.bin (e.g. roms/swe1_sound.bin)
 *   No directory walks, no name-prefix scoring, no host-specific paths.
 *
 * Defaults:
 *   - OFF unless P2K_DCS_AUDIO=1 is set (the wrapper does this when a
 *     host audiodev is auto-detected).
 *   - P2K_NO_DCS_AUDIO=1 forces off.
 *   - When the pb2kslib container can't be located/parsed, audio stays
 *     installed but cmd lookups will all miss (counted, not synthesised).
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "audio/audio.h"

#include "p2k-internal.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

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
    int32_t  peak;           /* max |pcm[]| (decoded loudness) */
    int32_t  rms;            /* RMS of pcm[] */
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
    int32_t       voice_peak[DCS_VOICES]; /* per-voice contribution peak (since last status) */
    uint64_t      voice_frames[DCS_VOICES]; /* frames mixed in since last status */
    uint64_t      frames_rendered;    /* total frames produced */

    /* Optional raw-PCM dump for diagnosis. Set P2K_DCS_AUDIO_DUMP=path.
     * Writes whatever we hand to AUD_write(), so a wav header
     * (44.1k mono S16) over the saved file lets you hear exactly
     * what the host backend received. */
    FILE         *wav_dump;
    uint64_t      wav_dump_bytes;

    /* Per-second status timer + source-tag attribution (raw cmd
     * histogram by source) — diagnostic only. */
    QEMUTimer    *status_timer;
    uint64_t      cmd_by_bar4;
    uint64_t      cmd_by_uart_w;
    uint64_t      cmd_by_uart_bp;
    uint64_t      cmd_by_compat;       /* "compat:*" museum-mode bridges */
    uint64_t      cmd_by_other;
    uint64_t      dong_played_count;   /* 0x003A on ch0 successful starts */

    /* "played-but-never-rendered" detector.  We snapshot the played
     * counter + the last play info on each successful start, and on
     * the per-second status tick we check whether new plays appeared
     * while peak stayed zero and active dropped to zero — meaning the
     * voice was started and then disappeared before any callback
     * actually saw it. */
    uint64_t      last_played_count;
    uint16_t      last_played_cmd;
    const char   *last_played_name;
    int           last_played_ch;
    uint64_t      last_played_start_cb;
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

/* Deterministic container resolution.  Tries, in order:
 *   1. $P2K_PB2KSLIB
 *   2. <roms_dir>/<game>_sound.bin
 * The selected path must exist, be regular, and have a valid header.
 * No directory walks, no scoring — host-agnostic by construction. */
static bool pb2k_path_is_valid(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 64) {
        close(fd);
        return false;
    }
    uint8_t hdr[16];
    ssize_t n = read(fd, hdr, 16);
    close(fd);
    return (n == 16 && pb2k_validate(hdr, st.st_size));
}

static bool pb2k_resolve_path(const char *roms_dir, const char *game,
                              char *out, size_t out_sz)
{
    const char *override = getenv("P2K_PB2KSLIB");
    if (override && *override) {
        if (pb2k_path_is_valid(override)) {
            snprintf(out, out_sz, "%s", override);
            return true;
        }
        warn_report("dcs-audio: P2K_PB2KSLIB=%s is not a valid pb2kslib "
                    "container; falling back to default lookup", override);
    }
    if (!roms_dir || !game) return false;
    snprintf(out, out_sz, "%s/%s_sound.bin", roms_dir, game);
    return pb2k_path_is_valid(out);
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

static uint32_t isqrt64(uint64_t v)
{
    uint64_t r = 0, b = 1ULL << 32;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else            { r >>= 1; }
        b >>= 2;
    }
    return (uint32_t)r;
}

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
    info_report("dcs-audio: ogg src_chans=%d src_rate=%ld", src_chans, (long)src_rate);

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
    int32_t pk = 0;
    int64_t sq = 0;
    for (size_t i = 0; i < dst_frames; i++) {
        int32_t v = out[i];
        int32_t a = v < 0 ? -v : v;
        if (a > pk) pk = a;
        sq += (int64_t)v * v;
    }
    s->peak = pk;
    s->rms  = dst_frames ? (int32_t)isqrt64((uint64_t)(sq / (int64_t)dst_frames)) : 0;
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
    info_report("dcs-audio: decoded cmd=0x%04x name=%s frames=%zu peak=%d rms=%d",
                cmd, e->name, s->frames, s->peak, s->rms);
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
                    "pos=%zu/%zu vol=%u cb_elapsed=%llu) ← "
                    "new(cmd=0x%04x name=%s vol=%d)",
                    ch, vc->cmd, vc->name ? vc->name : "?",
                    vc->pos, vc->s->frames, vc->vol,
                    (unsigned long long)(a->callbacks - vc->started_at_cb),
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
    /* Remember last successful play for the played-but-never-rendered
     * detector in the status tick. */
    a->last_played_cmd      = cmd;
    a->last_played_name     = name;
    a->last_played_ch       = ch;
    a->last_played_start_cb = a->callbacks;
}

static void stop_all_voices(DcsAudio *a)
{
    for (int i = 0; i < DCS_VOICES; i++) {
        Voice *vc = &a->voices[i];
        if (vc->active && vc->s) {
            info_report("dcs-audio: ch%d STOP-ALL was(cmd=0x%04x name=%s "
                        "pos=%zu/%zu cb_elapsed=%llu)",
                        i, vc->cmd, vc->name ? vc->name : "?",
                        vc->pos, vc->s->frames,
                        (unsigned long long)(a->callbacks - vc->started_at_cb));
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
        for (int v = 0; v < DCS_VOICES; v++) {
            Voice *vc = &a->voices[v];
            if (!vc->active || !vc->s) continue;
            if (vc->pos >= vc->s->frames) {
                info_report("dcs-audio: ch%d END  (cmd=0x%04x name=%s "
                            "frames=%zu cb_elapsed=%llu)",
                            v, vc->cmd, vc->name ? vc->name : "?",
                            vc->s->frames,
                            (unsigned long long)(a->callbacks - vc->started_at_cb));
                vc->s = NULL;
                vc->active = false;
                continue;
            }
            int32_t s = vc->s->pcm[vc->pos++];
            /* Unicorn parity: SDL2_mixer applies channel_vol/MIX_MAX_VOLUME
             * with MIX_MAX_VOLUME=128, and dcs_vol_to_sdl(255)=128, so
             * vol=255 → unity-gain pass-through. Match that exactly:
             *   contrib = s * vol / 255
             * Multi-voice overflow is handled by the saturating clip
             * after the per-frame sum — same as SDL_mixer's behaviour. */
            int32_t contrib = (s * (int32_t)vc->vol) / 255;
            mix += contrib;
            int32_t ac = contrib < 0 ? -contrib : contrib;
            if (ac > a->voice_peak[v]) a->voice_peak[v] = ac;
            a->voice_frames[v]++;
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
        if (a->wav_dump) {
            fwrite(buf, 1, sizeof(buf), a->wav_dump);
            a->wav_dump_bytes += sizeof(buf);
        }
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
        if (a->wav_dump) {
            fwrite(buf, 1, frames * sizeof(buf[0]), a->wav_dump);
            a->wav_dump_bytes += frames * sizeof(buf[0]);
        }
        if (w == 0) a->aud_write_zero++;
    }
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
    else if (!strncmp(src, "compat:", 7))   a->cmd_by_compat++;
    else                                a->cmd_by_other++;
    return src;
}

/* Public accessor: has the boot dong (0x003A on ch0) been heard yet?
 * Used by the museum-mode shim to suppress its synthetic dong injection
 * if the guest's DCS path already produced one naturally. */
bool p2k_dcs_audio_dong_observed(void)
{
    return s_dcs_audio.dong_played_count > 0;
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
        if (lookup_cmd == 0x003A) {
            a->dong_played_count++;
        }
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
            /* Unicorn sound_set_global_volume (sound.c:302-308):
             *   Mix_Volume(-1, dcs_vol_to_sdl(global)) — sets ALL
             *   channels' current volume in one shot. Subsequent
             *   plays will reset their channel's volume to the
             *   play's own `volume` parameter (sound_play_track at
             *   sound.c:346), so this broadcast only affects voices
             *   that are currently active or that play with no
             *   per-track volume override. Mirror that here by
             *   broadcasting the value to every voice's vc->vol. */
            a->global_vol = (uint8_t)(data1 & 0xFF);
            for (int v = 0; v < DCS_VOICES; v++) {
                a->voices[v].vol = a->global_vol;
            }
            TRACE_EVT(a,
                      "[%s] execute_mixer 0x%04x d1=0x%04x → GLOBAL_VOL=%u "
                      "(broadcast to all 8 voices, per Unicorn Mix_Volume(-1))",
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
                    "mode=%s "
                    "src{BAR4=%llu UART.w=%llu UART.bp=%llu compat=%llu other=%llu}",
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
                    p2k_dcs_core_mode_name(),
                    (unsigned long long)a->cmd_by_bar4,
                    (unsigned long long)a->cmd_by_uart_w,
                    (unsigned long long)a->cmd_by_uart_bp,
                    (unsigned long long)a->cmd_by_compat,
                    (unsigned long long)a->cmd_by_other);
        for (int i = 0; i < DCS_VOICES; i++) {
            if (a->voice_peak[i] || a->voice_frames[i]) {
                Voice *vc = &a->voices[i];
                info_report("dcs-audio   ch%d window: peak=%d frames=%llu "
                            "(now: active=%d cmd=0x%04x name=%s vol=%u pan=%u)",
                            i, (int)a->voice_peak[i],
                            (unsigned long long)a->voice_frames[i],
                            vc->active ? 1 : 0,
                            vc->cmd, vc->name ? vc->name : "?",
                            vc->vol, vc->pan);
            }
            a->voice_peak[i] = 0;
            a->voice_frames[i] = 0;
        }
        for (int i = 0; i < DCS_VOICES; i++) {
            Voice *vc = &a->voices[i];
            if (!vc->active || !vc->s) continue;
            info_report("dcs-audio   ch%d cmd=0x%04x name=%s pos=%zu/%zu "
                        "vol=%u pan=%u",
                        i, vc->cmd, vc->name ? vc->name : "?",
                        vc->pos, vc->s->frames, vc->vol, vc->pan);
        }
        /* Played-but-never-rendered: new plays appeared in the last
         * second but the mixer never produced a non-zero sample and
         * has no active voices.  Strong signal that a voice started
         * and was killed/ended before the audio callback saw it. */
        if (a->played_count > a->last_played_count
            && n_active == 0
            && a->peak_mix_abs == 0) {
            info_report("dcs-audio: WARN played-but-never-rendered "
                        "delta=%llu last=ch%d cmd=0x%04x name=%s "
                        "start_cb=%llu now_cb=%llu",
                        (unsigned long long)(a->played_count
                                             - a->last_played_count),
                        a->last_played_ch,
                        a->last_played_cmd,
                        a->last_played_name ? a->last_played_name : "?",
                        (unsigned long long)a->last_played_start_cb,
                        (unsigned long long)a->callbacks);
        }
        a->last_played_count = a->played_count;
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

    const char *dump = getenv("P2K_DCS_AUDIO_DUMP");
    if (dump && *dump) {
        a->wav_dump = fopen(dump, "wb");
        if (a->wav_dump) {
            info_report("dcs-audio: dumping raw PCM (mono S16 LE @ %d Hz, "
                        "no wav header) to %s", DCS_OUT_RATE, dump);
        } else {
            warn_report("dcs-audio: could not open P2K_DCS_AUDIO_DUMP=%s",
                        dump);
        }
    }
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

    /* Mount the pb2kslib container.  Failure is non-fatal but loud:
     * audio stays installed (so command tracing keeps working) and
     * every cmd lookup will count as a miss. */
    if (st && st->roms_dir && st->game) {
        char path[512];
        if (pb2k_resolve_path(st->roms_dir, st->game, path, sizeof(path))) {
            pb2k_load(a, path);
        } else {
            warn_report("dcs-audio: pb2kslib not found "
                        "(P2K_PB2KSLIB unset or invalid, "
                        "%s/%s_sound.bin missing) — every sample lookup "
                        "will miss", st->roms_dir, st->game);
        }
    }

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
