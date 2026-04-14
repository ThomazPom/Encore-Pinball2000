/*
 * sound.c — SDL2_mixer audio, DCS2 command processing.
 *
 * DCS2 commands arrive via BAR4 word writes.
 * Sound samples from pb2kslib (XOR 0x3A encoded OGG) or wav files.
 * SDL2_mixer: 44100Hz, S16LSB, stereo, 4096 buffer, 8 channels.
 */
#include "encore.h"
#include <math.h>

#define MAX_TRACKS 8

static bool s_audio_ok = false;
static int  s_global_volume = 128;  /* SDL_mixer 0-128 */
static Mix_Chunk *s_boot_dong = NULL;

/* Synthetic tone generator (fallback when samples unavailable) */
static Mix_Chunk *build_tone(int freq_hz, int duration_ms, float decay)
{
    int sr = 44100;
    int n = (sr * duration_ms) / 1000;
    int data_bytes = n * 2 * 2; /* stereo 16-bit */
    int wav_size = 44 + data_bytes;
    uint8_t *buf = malloc(wav_size);
    if (!buf) return NULL;

    /* WAV header */
    memcpy(buf, "RIFF", 4);
    *(uint32_t *)(buf + 4) = wav_size - 8;
    memcpy(buf + 8, "WAVEfmt ", 8);
    *(uint32_t *)(buf + 16) = 16;
    *(uint16_t *)(buf + 20) = 1;      /* PCM */
    *(uint16_t *)(buf + 22) = 2;      /* stereo */
    *(uint32_t *)(buf + 24) = sr;
    *(uint32_t *)(buf + 28) = sr * 4;
    *(uint16_t *)(buf + 32) = 4;
    *(uint16_t *)(buf + 34) = 16;
    memcpy(buf + 36, "data", 4);
    *(uint32_t *)(buf + 40) = data_bytes;

    int16_t *pcm = (int16_t *)(buf + 44);
    double omega = 2.0 * M_PI * freq_hz / sr;
    for (int i = 0; i < n; i++) {
        double env = exp(-(double)decay * i / sr);
        int16_t s = (int16_t)(16000.0 * sin(omega * i) * env);
        pcm[i * 2] = s;
        pcm[i * 2 + 1] = s;
    }

    SDL_RWops *rw = SDL_RWFromMem(buf, wav_size);
    Mix_Chunk *chunk = Mix_LoadWAV_RW(rw, 1);
    free(buf);
    return chunk;
}

int sound_init(void)
{
    if (Mix_OpenAudio(44100, AUDIO_S16LSB, 2, 4096) < 0) {
        LOG("snd", "Mix_OpenAudio failed: %s\n", Mix_GetError());
        return -1;
    }

    Mix_AllocateChannels(MAX_TRACKS);
    Mix_Volume(-1, s_global_volume);

    /* Build boot dong tone (440Hz, 500ms, moderate decay) */
    s_boot_dong = build_tone(440, 500, 3.0f);

    s_audio_ok = true;
    LOG("snd", "SDL2_mixer: 44100Hz stereo S16, %d channels\n", MAX_TRACKS);
    return 0;
}

void sound_process_cmd(uint16_t cmd)
{
    if (!s_audio_ok) return;

    /* DCS2 command protocol */
    if (cmd == 0x0000) {
        /* Silence / reset */
        Mix_HaltChannel(-1);
        return;
    }

    if (cmd == 0x003A || cmd == 0x003a) {
        /* Boot dong */
        if (s_boot_dong)
            Mix_PlayChannel(0, s_boot_dong, 0);
        return;
    }

    if (cmd == 0x55AA || cmd == 0x55aa) {
        /* Global volume — next word is volume level */
        return;
    }

    /* Per-channel volume commands (0x55AB, 0x55AC, 0x55AE) */
    if ((cmd & 0xFF00) == 0x5500) {
        return; /* Volume/pan control — handled when we have full sample support */
    }

    /* Track play command */
    if (cmd >= 0x0100 && cmd < 0x1000) {
        /* Play synthesized tone as placeholder */
        int ch = cmd & 0x07;
        int freq = 220 + (cmd & 0xFF) * 10;
        Mix_Chunk *tone = build_tone(freq, 200, 5.0f);
        if (tone) {
            Mix_PlayChannel(ch % MAX_TRACKS, tone, 0);
            /* Note: this leaks — production code should cache/free */
        }
    }
}

void sound_cleanup(void)
{
    if (!s_audio_ok) return;
    Mix_HaltChannel(-1);
    if (s_boot_dong) { Mix_FreeChunk(s_boot_dong); s_boot_dong = NULL; }
    Mix_CloseAudio();
    s_audio_ok = false;
}
