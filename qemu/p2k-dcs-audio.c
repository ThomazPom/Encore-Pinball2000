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
    int           amp_q15;   /* 0..32767, simple per-voice volume */
} Voice;

extern void (*p2k_dcs_core_audio_hook)(uint16_t cmd);

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

static Sample *get_sample_for_cmd(DcsAudio *a, uint16_t cmd)
{
    if (cmd >= SAMPLE_CACHE_SIZE) return NULL;
    if (a->cache[cmd]) return a->cache[cmd];
    if (a->cache_tried[cmd]) return NULL;
    a->cache_tried[cmd] = 1;

    /* Find entry by track_cmd. */
    int idx = -1;
    for (int i = 0; i < a->entry_cnt; i++) {
        if (a->entries[i].track_cmd == cmd) { idx = i; break; }
    }
    if (idx < 0) return NULL;
    Pb2kEntry *e = &a->entries[idx];
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

static void start_voice(DcsAudio *a, const Sample *s)
{
    /* Pick a free slot, else replace the most-advanced (oldest) one. */
    int best = -1;
    size_t best_pos = 0;
    for (int i = 0; i < DCS_VOICES; i++) {
        if (!a->voices[i].s) { best = i; break; }
        if (a->voices[i].pos > best_pos) {
            best_pos = a->voices[i].pos;
            best = i;
        }
    }
    if (best < 0) best = 0;
    a->voices[best].s       = s;
    a->voices[best].pos     = 0;
    a->voices[best].amp_q15 = 28000;        /* leave headroom for mix */
}

static void render(DcsAudio *a, int16_t *out, int frames)
{
    /* Start with silence + the fallback blip (or hello tone) so the
     * voice is always producing some audible sample stream. */
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
            if (!vc->s) continue;
            if (vc->pos >= vc->s->frames) { vc->s = NULL; continue; }
            int32_t s = vc->s->pcm[vc->pos++];
            mix += (s * vc->amp_q15) >> 15;
        }
        if (mix >  32767) mix =  32767;
        if (mix < -32768) mix = -32768;
        out[i] = (int16_t)mix;
    }
}

static void dcs_audio_callback(void *opaque, int avail_bytes)
{
    DcsAudio *a = opaque;
    if (!a->voice) return;
    int16_t buf[1024];
    while (avail_bytes >= (int)sizeof(buf)) {
        int frames = sizeof(buf) / sizeof(buf[0]);
        render(a, buf, frames);
        size_t w = AUD_write(a->voice, buf, sizeof(buf));
        if (w == 0) break;
        avail_bytes -= (int)w;
    }
    if (avail_bytes > 0) {
        int frames = avail_bytes / (int)sizeof(int16_t);
        if (frames > (int)(sizeof(buf) / sizeof(buf[0]))) {
            frames = sizeof(buf) / sizeof(buf[0]);
        }
        render(a, buf, frames);
        AUD_write(a->voice, buf, frames * sizeof(buf[0]));
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

static void dcs_audio_on_cmd(uint16_t cmd)
{
    DcsAudio *a = &s_dcs_audio;
    if (!a->enabled || !a->opened) return;
    a->cmd_count++;

    /* Unicorn dispatch (sound.c sound_process_cmd / sound_execute_mixer):
     *   0x0000        -> halt all
     *   0x003A        -> boot dong
     *   0x00AA        -> special 0x0FFF sample
     *   0x55XX        -> mixer control (vol/pan); data word follows
     *   cmd >= 0x0100 -> normal track trigger (lookup by cmd)
     *   else          -> protocol byte (volume/echo/handshake), ignore.
     * We do not currently parse the (cmd, data1, data2) triples that
     * Unicorn's sound_execute_mixer handles, so 0x55XX commands and
     * their following data words simply do not produce audio yet.
     * Real triggers without a matching pb2k entry are logged at low
     * verbosity; we no longer emit a synthetic blip for them. */
    uint16_t lookup_cmd = cmd;
    bool is_trigger = false;
    if (cmd == 0x003A) {
        is_trigger = true;
    } else if (cmd == 0x00AA) {
        lookup_cmd = 0x0FFF;
        is_trigger = true;
    } else if ((cmd & 0xFF00) == 0x5500) {
        /* mixer control: vol/pan, no sample to start */
        return;
    } else if (cmd >= 0x0100) {
        is_trigger = true;
    }

    if (!is_trigger) {
        return;
    }

    Sample *s = get_sample_for_cmd(a, lookup_cmd);
    if (s) {
        start_voice(a, s);
        a->played_count++;
        if (a->played_count <= 12) {
            info_report("dcs-audio: cmd 0x%04x → sample %zu frames "
                        "(played #%llu)",
                        cmd, s->frames,
                        (unsigned long long)a->played_count);
        }
    } else {
        a->missed_count++;
        if (a->missed_count <= 8) {
            info_report("dcs-audio: cmd 0x%04x → trigger w/o pb2k entry "
                        "(missed #%llu, silent)",
                        cmd, (unsigned long long)a->missed_count);
        }
    }
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

    p2k_dcs_core_audio_hook = dcs_audio_on_cmd;

    info_report("pinball2000: DCS audio installed "
                "(%d Hz S16 mono, %d-voice mixer, %d pb2k entries; "
                "P2K_NO_DCS_AUDIO=1 to disable)",
                DCS_OUT_RATE, DCS_VOICES, a->entry_cnt);
}
