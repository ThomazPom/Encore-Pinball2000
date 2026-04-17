/*
 * sound.c — SDL2_mixer audio for DCS2 sound.
 *
 * Follows the i386 POC path exactly:
 *   1. Load pb2k sound library ({game}_P2K-runtime.bin) at init
 *   2. Pre-load ALL samples before execution loop starts
 *   3. Synthesized 220 Hz fallback tone for boot dong (matching i386)
 */
#include "encore.h"
#include <math.h>
#include <pthread.h>

#define MAX_TRACKS 8
#define SAMPLE_CACHE_SIZE 0x1000
#define PB2K_MAX_ENTRIES 1024

typedef struct {
    char     name[33];
    uint16_t track_cmd;
    uint32_t offset;
    uint32_t size;
} Pb2kEntry;

static bool      s_audio_ok = false;
static int       s_global_volume = 0x10;
static int       s_audio_thread_level = 0;
static pthread_t s_audio_thread;
static Mix_Chunk *s_boot_dong = NULL;
static Mix_Chunk *s_track_chunks[MAX_TRACKS];
static uint32_t  s_track_cmd[MAX_TRACKS];
static int       s_track_vol[MAX_TRACKS];
static int       s_track_pan[MAX_TRACKS];
static int       s_track_active[MAX_TRACKS];
static Mix_Chunk *s_sample_cache[SAMPLE_CACHE_SIZE];
static int       s_sample_loaded[SAMPLE_CACHE_SIZE];
static uint8_t  *s_pb2k_data = NULL;
static size_t    s_pb2k_size = 0;
static Pb2kEntry s_pb2k_entries[PB2K_MAX_ENTRIES];
static int       s_pb2k_count = 0;
static int       s_pb2k_bong_idx = -1;

static Mix_Chunk *build_tone(int freq_hz, int duration_ms, float decay_rate)
{
    int sample_rate = 44100;
    int num_samples = (sample_rate * duration_ms) / 1000;
    int data_bytes  = num_samples * 2 * 2;
    int wav_size    = 44 + data_bytes;
    uint8_t *buf    = malloc(wav_size);
    if (!buf) return NULL;

    memcpy(buf, "RIFF", 4);
    *(uint32_t *)(buf + 4)  = wav_size - 8;
    memcpy(buf + 8, "WAVEfmt ", 8);
    *(uint32_t *)(buf + 16) = 16;
    *(uint16_t *)(buf + 20) = 1;
    *(uint16_t *)(buf + 22) = 2;
    *(uint32_t *)(buf + 24) = sample_rate;
    *(uint32_t *)(buf + 28) = sample_rate * 4;
    *(uint16_t *)(buf + 32) = 4;
    *(uint16_t *)(buf + 34) = 16;
    memcpy(buf + 36, "data", 4);
    *(uint32_t *)(buf + 40) = data_bytes;

    int16_t *pcm = (int16_t *)(buf + 44);
    double omega = 2.0 * M_PI * freq_hz / sample_rate;
    for (int i = 0; i < num_samples; i++) {
        double t = (double)i / sample_rate;
        double envelope = exp(-decay_rate * t);
        int16_t sample = (int16_t)(16000.0 * sin(omega * i) * envelope);
        pcm[i * 2] = sample;
        pcm[i * 2 + 1] = sample;
    }

    SDL_RWops *rw = SDL_RWFromMem(buf, wav_size);
    Mix_Chunk *chunk = Mix_LoadWAV_RW(rw, 1);
    free(buf);
    return chunk;
}

static int dcs_vol_to_sdl(int dcs_vol)
{
    return (dcs_vol * MIX_MAX_VOLUME) / 255;
}

static void pb2k_load(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;

    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return;
    }

    s_pb2k_size = (size_t)st.st_size;
    s_pb2k_data = mmap(NULL, s_pb2k_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (s_pb2k_data == MAP_FAILED) {
        s_pb2k_data = NULL;
        s_pb2k_size = 0;
        return;
    }

    uint32_t hdr_size  = *(uint32_t *)(s_pb2k_data + 4);
    uint32_t entry_cnt = *(uint32_t *)(s_pb2k_data + 8);
    uint32_t entry_sz  = *(uint32_t *)(s_pb2k_data + 12);
    if (entry_cnt > PB2K_MAX_ENTRIES) entry_cnt = PB2K_MAX_ENTRIES;
    if (entry_sz < 72) return;

    uint8_t decoded[72];
    for (uint32_t i = 0; i < entry_cnt; i++) {
        const uint8_t *raw = s_pb2k_data + hdr_size + i * entry_sz;
        for (int j = 0; j < 72; j++)
            decoded[j] = raw[j] ^ 0x3A;

        Pb2kEntry *e = &s_pb2k_entries[i];
        memcpy(e->name, decoded, 32);
        e->name[32] = '\0';
        uint32_t *fields = (uint32_t *)(decoded + 32);
        e->offset = fields[8];
        e->size   = fields[9];
        e->track_cmd = 0xFFFF;

        if (e->name[0] == 'S') {
            char hex_buf[5] = { e->name[1], e->name[2], e->name[3], e->name[4], '\0' };
            unsigned long val = strtoul(hex_buf, NULL, 16);
            if (val <= 0xFFFF) e->track_cmd = (uint16_t)val;
        }
        if (strcmp(e->name, "dcs-bong") == 0) {
            s_pb2k_bong_idx = (int)i;
            e->track_cmd = 0x003A;
        }
    }
    s_pb2k_count = (int)entry_cnt;
    LOG("snd", "pb2kslib loaded: %d entries\n", s_pb2k_count);
}

static Mix_Chunk *pb2k_load_track(int entry_idx)
{
    if (!s_pb2k_data || entry_idx < 0 || entry_idx >= s_pb2k_count)
        return NULL;
    Pb2kEntry *e = &s_pb2k_entries[entry_idx];
    if (e->size == 0 || e->offset + e->size > s_pb2k_size)
        return NULL;

    uint8_t *ogg_buf = malloc(e->size);
    if (!ogg_buf) return NULL;
    const uint8_t *src = s_pb2k_data + e->offset;
    for (uint32_t i = 0; i < e->size; i++)
        ogg_buf[i] = src[i] ^ 0x3A;

    SDL_RWops *rw = SDL_RWFromMem(ogg_buf, e->size);
    Mix_Chunk *chunk = Mix_LoadWAV_RW(rw, 1);
    free(ogg_buf);
    return chunk;
}

static int pb2k_find_track(uint16_t track_cmd)
{
    for (int i = 0; i < s_pb2k_count; i++) {
        if (s_pb2k_entries[i].track_cmd == track_cmd)
            return i;
    }
    return -1;
}

static void pb2k_preload_samples(void)
{
    int ok = 0;
    int fail = 0;

    for (int i = 0; i < s_pb2k_count; i++) {
        Pb2kEntry *e = &s_pb2k_entries[i];
        Mix_Chunk *chunk;

        if (e->track_cmd == 0xFFFF || e->track_cmd >= SAMPLE_CACHE_SIZE || e->size == 0)
            continue;
        if (s_sample_loaded[e->track_cmd])
            continue;

        chunk = pb2k_load_track(i);
        if (chunk) {
            s_sample_loaded[e->track_cmd] = 1;
            s_sample_cache[e->track_cmd] = chunk;
            ok++;
        } else {
            fail++;
        }
    }

    LOG("snd", "pb2k preloaded: ok=%d fail=%d\n", ok, fail);
}

/* load_special_sample / load_sample_from_dir removed — i386 POC pre-loads
 * all samples from pb2kslib at init. No fallback directory needed. */

static Mix_Chunk *get_or_load_sample(uint16_t track_cmd)
{
    if (track_cmd >= SAMPLE_CACHE_SIZE) return NULL;
    if (s_sample_loaded[track_cmd])
        return s_sample_cache[track_cmd];

    /* Late-load from pb2k (backup for samples missed during pre-load) */
    s_sample_loaded[track_cmd] = 1;
    int idx = pb2k_find_track(track_cmd);
    if (idx >= 0) {
        Mix_Chunk *chunk = pb2k_load_track(idx);
        s_sample_cache[track_cmd] = chunk;
        if (chunk)
            LOG("snd", "late-loaded sample 0x%04x\n", track_cmd);
        return chunk;
    }
    return NULL;
}

void sound_set_global_volume(int vol)
{
    s_global_volume = vol & 0xFF;
    if (!s_audio_ok) return;
    Mix_Volume(-1, dcs_vol_to_sdl(s_global_volume));
    LOG("snd", "global volume=0x%02x\n", s_global_volume);
}

int sound_get_global_volume(void)
{
    return s_global_volume;
}

static void sound_stop_track(int track_idx)
{
    if (!s_audio_ok || track_idx < 0 || track_idx >= MAX_TRACKS) return;
    if (s_track_active[track_idx]) {
        Mix_HaltChannel(track_idx);
        s_track_active[track_idx] = 0;
    }
}

static void sound_play_track(uint16_t track_cmd, int track_idx, int volume, int pan)
{
    if (!s_audio_ok || track_idx < 0 || track_idx >= MAX_TRACKS) return;

    Mix_Chunk *chunk = get_or_load_sample(track_cmd);
    if (!chunk) {
        LOG("snd", "no sample for 0x%04x\n", track_cmd);
        return;
    }

    sound_stop_track(track_idx);
    if (Mix_PlayChannelTimed(track_idx, chunk, 0, -1) < 0) {
        LOG("snd", "Mix_PlayChannelTimed failed for 0x%04x: %s\n", track_cmd, Mix_GetError());
        return;
    }

    s_track_chunks[track_idx] = chunk;
    s_track_cmd[track_idx] = track_cmd;
    s_track_vol[track_idx] = volume;
    s_track_pan[track_idx] = pan;
    s_track_active[track_idx] = 1;

    Mix_Volume(track_idx, dcs_vol_to_sdl(volume));
    int left  = (pan <= 0x7F) ? 255 : (255 - (pan - 0x7F) * 2);
    int right = (pan >= 0x7F) ? 255 : (255 - (0x7F - pan) * 2);
    if (left < 0) left = 0;
    if (right < 0) right = 0;
    Mix_SetPanning(track_idx, (uint8_t)left, (uint8_t)right);
    LOG("snd", "play 0x%04x ch=%d vol=%d pan=%d\n", track_cmd, track_idx, volume, pan);
}

static void *audio_init_thread_func(void *arg)
{
    (void)arg;
    LOG("snd", "audio init thread started (level=%d)\n", s_audio_thread_level);

    if (s_audio_thread_level >= 1) {
        usleep(450000);
        LOG("snd", "audio init: stage 0x3ca\n");
        s_audio_thread_level = 2;
    }
    if (s_audio_thread_level >= 2) {
        usleep(200000);
        LOG("snd", "audio init: stage 0x3cb\n");
        s_audio_thread_level = 3;
    }
    if (s_audio_thread_level >= 3) {
        usleep(300000);
        LOG("snd", "audio init: stage 0x3cc\n");
        s_audio_thread_level = 4;
    }

    s_audio_thread_level = 5;
    LOG("snd", "audio init thread complete\n");
    return NULL;
}

void sound_start_audio_init_thread(void)
{
    if (!s_audio_ok || s_audio_thread_level != 0)
        return;

    s_audio_thread_level = 1;
    pthread_create(&s_audio_thread, NULL, audio_init_thread_func, NULL);
    pthread_detach(s_audio_thread);
    LOG("snd", "launched audio init thread\n");
}

int sound_init(void)
{
    for (int attempts = 0; attempts < 5; attempts++) {
        if (Mix_OpenAudio(44100, AUDIO_S16LSB, 2, 4096) == 0)
            break;
        if (attempts == 4) {
            LOG("snd", "Mix_OpenAudio failed after 5 attempts: %s\n", Mix_GetError());
            return -1;
        }
        usleep(500000);
    }

    Mix_AllocateChannels(MAX_TRACKS);

    char pb2k_path[512];
    snprintf(pb2k_path, sizeof(pb2k_path),
             "%s/%s_P2K-runtime.bin", g_emu.roms_dir, g_emu.game_prefix);
    if (access(pb2k_path, R_OK) == 0) {
        pb2k_load(pb2k_path);
        pb2k_preload_samples();
    } else {
        LOG("snd", "pb2kslib not found: %s\n", pb2k_path);
    }

    /* Boot dong: pb2k first (i386 POC path), synthetic fallback */
    if (s_pb2k_bong_idx >= 0) {
        s_boot_dong = pb2k_load_track(s_pb2k_bong_idx);
        LOG("snd", "boot dong from pb2k entry %d: %s\n",
            s_pb2k_bong_idx, s_boot_dong ? "OK" : "FAIL");
    }
    if (!s_boot_dong) {
        s_boot_dong = build_tone(220, 500, 3.0f);
        LOG("snd", "boot dong synthetic fallback: %s\n", s_boot_dong ? "OK" : "FAIL");
    }

    s_audio_ok = true;
    sound_set_global_volume(s_global_volume);
    g_emu.sound_ready = true;
    LOG("snd", "SDL2_mixer ready, pb2k=%s\n", s_pb2k_data ? pb2k_path : "(none)");
    return 0;
}

void sound_process_cmd(uint16_t cmd)
{
    if (!s_audio_ok) return;

    switch (cmd) {
    case 0x0000:
        Mix_HaltChannel(-1);
        return;
    case 0x003A:
        if (s_boot_dong)
            Mix_PlayChannelTimed(0, s_boot_dong, 0, -1);
        return;
    case 0x00AA:
        sound_play_track(0x0FFF, 7, 0xFF, 0x7F);
        return;
    default:
        if (cmd >= 0x0100 && cmd < SAMPLE_CACHE_SIZE)
            sound_play_track(cmd, cmd & 0x07, 0xFF, 0x7F);
        return;
    }
}

int sound_is_ready(void)
{
    return s_audio_ok;
}

void sound_play_boot_dong(void)
{
    if (s_audio_ok && s_boot_dong) {
        Mix_Volume(0, MIX_MAX_VOLUME);
        Mix_PlayChannelTimed(0, s_boot_dong, 0, -1);
    }
}

void sound_execute_mixer(int cmd, int data1, int data2)
{
    if (!s_audio_ok) return;

    if ((cmd >> 8) == 0x55) {
        switch (cmd) {
        case 0x55AA:
            sound_set_global_volume(data1);
            break;
        case 0x55AB:
        case 0x55AE: {
            int ch = (data1 >> 8) & 0x07;
            int vol = data1 & 0xFF;
            if (ch < MAX_TRACKS) {
                s_track_vol[ch] = vol;
                Mix_Volume(ch, dcs_vol_to_sdl(vol));
                LOG("snd", "mixer 0x%04x: ch%d vol=%d\n", cmd, ch, vol);
            }
            break;
        }
        case 0x55AC: {
            int ch = (data1 >> 8) & 0x07;
            int pan = data1 & 0xFF;
            if (ch < MAX_TRACKS) {
                s_track_pan[ch] = pan;
                int left  = (pan <= 0x7F) ? 255 : (255 - (pan - 0x7F) * 2);
                int right = (pan >= 0x7F) ? 255 : (255 - (0x7F - pan) * 2);
                if (left < 0) left = 0;
                if (right < 0) right = 0;
                Mix_SetPanning(ch, (uint8_t)left, (uint8_t)right);
                LOG("snd", "mixer 0x55ac: ch%d pan=%d\n", ch, pan);
            }
            break;
        }
        default:
            LOG("snd", "unknown mixer cmd 0x%04x data=0x%04x\n", cmd, data1);
            break;
        }
        return;
    }

    sound_play_track((uint16_t)cmd, (data2 & 0x380) >> 7, (data1 >> 8) & 0xFF, data1 & 0xFF);
}

void sound_cleanup(void)
{
    if (!s_audio_ok) return;

    Mix_HaltChannel(-1);
    for (int i = 0; i < SAMPLE_CACHE_SIZE; i++) {
        if (s_sample_cache[i]) {
            Mix_FreeChunk(s_sample_cache[i]);
            s_sample_cache[i] = NULL;
        }
        s_sample_loaded[i] = 0;
    }
    if (s_boot_dong) { Mix_FreeChunk(s_boot_dong); s_boot_dong = NULL; }
    if (s_pb2k_data) {
        munmap(s_pb2k_data, s_pb2k_size);
        s_pb2k_data = NULL;
        s_pb2k_size = 0;
    }
    Mix_CloseAudio();
    g_emu.sound_ready = false;
    s_audio_ok = false;
    s_audio_thread_level = 0;
}
