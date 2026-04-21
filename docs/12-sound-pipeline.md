# 12 — Sound Pipeline

Encore's audio subsystem has two jobs:

1. Answer the game's DCS-2 sound-board handshake well enough that the
   game does not turn itself off.
2. When a command-stream request arrives, play back a matching
   WAV-encoded sample through SDL2_mixer.

Source: `src/sound.c` (537 lines).

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## The DCS-2 board, briefly

On real Pinball 2000 hardware, the DCS-2 board is a separate ADSP-2105
DSP with its own ROM banks and serial link to the main CPU. The CPU
writes commands into a BAR4 MMIO register; the DSP executes them and
plays a sample from its bank. Encore does not emulate the ADSP —
instead, we decode the command stream and play pre-extracted WAV
samples through the host audio device.

## Sample container (pb2kslib)

The samples were extracted from the DSP ROMs into a single container
file by an offline tool (see
[32-tools-sound-decoder.md](32-tools-sound-decoder.md)). The
container uses a 16-byte header with a known magic and follows the
layout:

```
+0x00  magic  (see tool)
+0x04  u32   header size      (usually 0x20)
+0x08  u32   entry count
+0x0C  u32   entry stride      (usually 16 B)
+hdr+i*s   per-entry record:
               u32  id
               u32  offset     (absolute in container)
               u32  size
               u32  flags
```

After the header+entry table, the raw WAV blobs are concatenated with
no padding.

## Shape-based container detection

A historical sore spot: the container file's name used to be
`swe1_<tag>.bin` or `rfm_<tag>.bin`, with `<tag>` free-text and
inconsistent. Name-based discovery kept breaking whenever a new bundle
added or removed samples.

The current approach is **shape-first**:

```c
static bool pb2k_validate_header(const uint8_t hdr[16], off_t size);
static bool pb2k_find_container(char *out, size_t out_sz);
```

`pb2k_find_container()` walks `roms_dir`, `stat()`s every file,
reads the first 16 bytes, and runs `pb2k_validate_header()`. Valid
candidates are scored by a shape-quality metric (monotonic entry
offsets, sensible entry count, total size fits in file). The highest
scorer is picked. A name-prefix match (`game_prefix == "swe1"` →
files starting with `swe1`) is used only as a tie-breaker.

This means you can rename the file anything, add dozens of candidates
to the same folder, and the right one still gets picked.

## Mapping DCS commands to samples

When the DCS command-stream dispatcher in `sound.c` receives a
play-sample request, it calls `sound_execute_mixer(cmd, data1,
data2)` which:

1. Assembles a 16-bit sample id from the operands.
2. Looks up the id in the `pb2k_entries[]` table.
3. Resolves a `Mix_Chunk` via `pb2k_load_track()` (lazily mmapping the
   container on first touch).
4. Calls `Mix_PlayChannel(-1, chunk, 0)` to queue playback.

A per-id chunk cache (`s_track_chunks[]`, `MAX_TRACKS` entries) avoids
re-parsing the WAV header on every hit. The LRU size is chosen to
cover the worst-case polyphony: typical playfield has up to 8 calls
active, plus music layers — we size for 64.

## The boot "dong"

The very first DCS command after reset is always a small ping — the
sound board answering "I'm alive". On real hardware this is a 220 Hz
tone with a short decay envelope. Encore synthesises it on the fly in
`build_tone(220, 300, 0.95)` so audio output is present even when the
pb2kslib container is missing.

This is deliberately the only synthesised asset in the entire audio
path; everything else must come from the game's own ROM data.

## SDL2_mixer init

```c
Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024);
Mix_AllocateChannels(32);
```

* 44.1 kHz output because that is what most PC sound hardware
  natively supports; the source samples are 22.05 kHz or 11 kHz but
  SDL_mixer resamples cheaply.
* Stereo output. The game uses hard-panned stereo cues for
  left/right-side playfield sounds; `sound_execute_mixer()` sets
  per-channel panning via `Mix_SetPanning()` based on the DCS mixer
  layer id.
* 32 simultaneous channels. Typical peak use is 12–16 during a busy
  play ball.

## Global volume

The service-menu volume setting writes into the SEEPROM; at boot we
read it out (`g_emu.seeprom[…]`) and call
`Mix_Volume(-1, global_vol)`. There is also a `sound_set_global_volume`
helper exposed for runtime tweaks (not wired to any CLI flag today).

## Headless / sound-disabled mode

When `sound_init()` fails (no audio device, no SDL_mixer) or
`--headless` is set, every API in this module becomes a no-op. The
`sound_ready` flag gates all playback.

## Relationship to `--dcs-mode`

The sound subsystem is fed by *either* of two producers:

* In `--dcs-mode bar4-patch`, the game writes DCS commands to the
  BAR4 MMIO window and the hook in `bar.c` hands them straight to
  `sound_process_cmd()`.
* In `--dcs-mode io-handled`, the game writes to I/O ports
  `0x138..0x13F` and `io.c`'s UART handlers feed `sound_process_cmd()`
  the same way.

Either way, `sound.c` is downstream and does not care which producer
handed it the byte. See [13-dcs-mode-duality.md](13-dcs-mode-duality.md).

## Known weakness

The pb2kslib container only ships with the sample bank Williams used
for SWE1 and RFM v1.5+. On RFM v1.2, the bank is incomplete and many
calls fall through with an "unknown sample" log line. This is a data
problem (missing extraction work), not a code problem.

## Debug logs

`[snd]` tag, with useful lines:

```
[snd] pb2kslib detected by shape: ./roms/swe1_bong.bin (score=42)
[snd] pb2kslib loaded: 1024 entries
[snd] dcs cmd 0x0140 0x0230 0x0000 → sample id 0x0140
[snd] playback chan=5 chunk=… len=8820 B
```
