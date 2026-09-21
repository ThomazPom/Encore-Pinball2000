# 25 — DCS sound

Encore separates the Pinball 2000 sound path into four layers: the guest's
two hardware-facing transports, a shared DCS command protocol, one selected
content engine, and a QEMU host-audio backend. A working handshake proves only
the first two layers; audible output requires all four.

```text
guest BAR4 MMIO ─┐
                 ├─→ shared DCS protocol ─→ selected content engine
guest UART 0138h ┘          │                         │
                            │                         ▼
                 responses / flags            signed 16-bit PCM
                                                      │
                                                      ▼
                                            QEMU audiodev backend
```

This distinction is central to troubleshooting. `--dcs-mode` selects a
frontend label, `--dcs-engine` selects how sound content is produced, and
`--audio` selects where the host sends the resulting PCM. They are not three
names for the same switch.

Sound ROM and update-flash discovery is covered by
[ROM and update loading](15-rom-loading.md). The BAR and I/O addresses are in
the [memory map](13-memory-map.md).

### Implementation owners

| Concern | Primary source |
|---|---|
| shared protocol and response ring | `qemu/p2k-dcs-core.c` |
| PLX BAR4 frontend | `qemu/p2k-dcs.c` |
| UART/byte-pair frontend | `qemu/p2k-dcs-uart.c` |
| sample/cache mixer and QEMU audiodev | `qemu/p2k-dcs-audio.c` |
| original-board assets, mailbox, SDRC and SPORT | `qemu/p2k-dcs-adsp.c` |
| ADSP-21xx instruction core | `qemu/p2k-adsp2105-core.c` |
| cache key and parallel generator | `scripts/internal/pb2k-sound-key.py`, `scripts/internal/build-pcm-cache.py` |
| public selection and host-backend policy | `scripts/run-qemu.sh` |

## One protocol, two guest transports

The BAR4 window at `0x13000000` and the DCS overlay at I/O
`0x138–0x13f` are thin views of one protocol state. They share the response
queue, echo byte, flag latch, suspend state and ACE1 command accumulator; they
must not be treated as independent sound boards.

| Transport operation | Shared-core effect |
|---|---|
| BAR4 offset 0 byte read/write | read or replace the probe echo byte |
| BAR4 offset 0 word read/write | pop a response or submit a command |
| BAR4 offset 2 read | ready/output flags, including the stored latch |
| BAR4 offset 2 write | replace the flag latch used by later reads |
| UART `0x13c` word read/write | pop a response or submit a command |
| UART `0x13c` high-byte then low-byte writes | assemble and submit one 16-bit command |
| UART `0x13e` read | return the same ready/output state |
| other UART bytes | minimal local 16550 state, with transmit empty/ready |

High/low byte-pair assembly is enabled by default because the guest uses that
access shape. `P2K_DCS_NO_BYTE_PAIR=1` exists only for forensic A/B work; it
turns those byte writes into non-command UART traffic.

The public labels `--dcs-mode io-handled` and `--dcs-mode bar4-patch` currently
run this same natural BAR4-plus-UART implementation. Neither label patches
guest text or data. `io-handled` is the default; the second name is retained
for diagnostic continuity, not as a different delivery path.

> [!NOTE]
> With a live original-ADSP engine, commands, response words and flags are
> delegated to the emulated board firmware. With the two sample-based runtime
> engines, the shared core supplies the protocol responses below and calls the
> host mixer for sound triggers.

## Synthetic protocol used by sample playback

The shared core has a 64-word response ring. Its main command contract is:

| Command | Response or state change |
|---:|---|
| `0x5800`, `0x5a00` | enqueue `0x1000` |
| `0x003a` | enqueue `0xcc01`, `10`; trigger the boot bong |
| `0x001b` | enqueue `0xcc09`, `10` |
| `0x00aa` | enqueue `0xcc04`, `10`; trigger its sample mapping |
| `0x000e` | enter suspend; only another `0x000e` exits and enqueues `10` |
| `0xace1` | enter the persistent mixer stream; enqueue `0x0100`, `0x000c` |
| other word outside ACE1 | direct sample command, normally without a response |

After `0xace1`, a `0x55xx` header consumes one data word and other headers
consume two. The accumulator deliberately remains armed after a complete
request so subsequent requests retain their mixer/channel interpretation; a
suspend command clears it. Headers 999 and 1000 receive the special
`0x0100`, `0x0010` acknowledgement.

The response-available flag is bit 7 and ready-to-accept is bit 6. A full
synthetic response ring drops additional words. The live ADSP path instead
reports the firmware's output-control state.

## Choosing a content engine

`adsp-hybrid-thread` is the default. The six accepted values intentionally
span three use cases: current live emulation, controlled scheduler comparisons,
and pre-rendered sample playback.

| Engine | Runtime source and scheduling | PCM presented to QEMU | Intended use |
|---|---|---|---|
| `adsp-hybrid-thread` | original firmware; event-driven worker fills an audio ring in 8-frame slices, refilling below 512 frames toward 1,024 | 31,250 Hz stereo | normal default |
| `adsp-clock-thread` | original firmware; worker advances fixed 2-frame slices into a 4,096-frame ring | 31,250 Hz stereo | fixed-clock comparison |
| `adsp-thread` | original firmware; condition-driven worker services mailbox work while SPORT production remains callback-driven | 31,250 Hz stereo | mailbox-thread comparison |
| `adsp` | original firmware advanced synchronously from writes/audio callbacks | 31,250 Hz stereo | simplest live reference |
| `pb2kslib-adsp` | persistent PCM library previously rendered by the selected update's native DSP; normal play uses the sample mixer | 44,100 Hz mono | update-derived fast startup after first generation |
| `pb2kslib` | fixed extracted Ogg/PCM library in `roms/<game>_sound.bin` | 44,100 Hz mono | compatibility and forensic comparison |

`--dcs-pcm-cpu N` can pin the live ADSP worker for the three threaded live
engines. It is an experiment, not a generally faster setting; host topology
and competing work determine the result.

> [!IMPORTANT]
> Plain `pb2kslib` can omit tracks introduced by a newer update. The default
> live engine executes the selected sound flash and therefore does not depend
> on the extracted library's coverage.

### Why the hybrid engine is the default

Live sound has two scheduling jobs: firmware must react promptly to mailbox
commands, while PCM must reach the host at its steady audio clock. The hybrid
engine separates them with one 4,096-frame stereo ring:

1. the host callback drains ready PCM;
2. a pending command or the 512-frame low-water mark wakes the worker;
3. the worker advances the single DSP state in eight-frame batches until
   commands are handled and the ring reaches 1,024 frames;
4. the worker sleeps again while the host continues consuming.

There are not two concurrent DSP clocks. The one core state is serialized by
its core lock; the bounded ring hands PCM from its producer to the audio
consumer, which emits silence if it ever drains the ring completely. Compared
with `adsp-thread`, firmware execution no longer has to wait for SPORT work in
a host callback. Compared with
`adsp-clock-thread`, the event/low-water gate avoids continuous two-frame
production and amortizes locking and scheduler wakeups.

The selection was supported by a matched, AC-powered SWE1 2.00 comparison
with 30 seconds of warmup and 60 seconds of framebuffer-plus-live-audio
measurement:

| Engine | Delivery | IRQ σ | IRQ worst | PDB p99 | PDB worst |
|---|---:|---:|---:|---:|---:|
| `adsp-clock-thread` | 100.03% | 64 µs | 5.31 ms | 298 µs | 1.98 ms |
| `adsp-hybrid-thread` | 100.04% | 12 µs | 604 µs | 304 µs | 1.74 ms |

> [!NOTE]
> This is historical evidence for the design choice, not a portable current
> guarantee. Host load, power policy and audio backend change tail latency;
> rerun the current benchmark when scheduler behavior changes.

## Live original-board execution

The live engines require three original-format inputs:

- U109 and U110, interleaved into the 8 MiB DCS ROM image during machine
  construction;
- one sound flash that is exactly 1 MiB;
- the selected engine's ADSP scheduling policy.

Sound-flash lookup is deterministic, in this order:

1. `--dcs-sound-flash PATH` / `P2K_DCS_SOUND_FLASH`;
2. the lexicographically first `*_sf.rom` in the selected update directory;
3. `roms/<game>_28f800.rom`;
4. `roms/<game>/28f800.rom`.

An invalid or wrong-sized flash makes original-format preparation fail and
logs a fallback to `pb2kslib`. Check the engine-selection line rather than
assuming the requested engine was installed.

The board layer boots the flash, maps the flash and interleaved sound ROM
through the SDRC view, queues host commands through the mailbox/IRQ2 path and
collects SPORT1 autobuffer output driven by IRQ1. The standalone ADSP-21xx
core executes the instruction stream; it is an adaptation following the
current MAME core and carries its own BSD licensing notice.

The live producer emits signed 16-bit stereo at 31,250 Hz. The early firmware
may configure a diagnostic SPORT rate first; audio delivery waits for the
final runtime SPORT rather than exposing that transient configuration as the
game stream.

## Extracted-library mixer

The fixed library defaults to `roms/<game>_sound.bin`; `--pb2kslib PATH`
overrides it. Resolution never scans a directory for a near match. The loader
accepts the pb2kslib version-1 header and its XOR-`0x3a`, 72-byte entry table,
with at most 4,096 entries.

Ogg entries are decoded lazily, downmixed to mono and linearly resampled to
44,100 Hz. `--sound-loading preload` walks and decodes the library during
startup instead. Generated-cache entries use the same table but carry signed
PCM and a stored sample rate.

The runtime mixer provides eight fixed channels:

- a new trigger replaces the current voice on its selected channel;
- `-LP` samples loop, while `-LP1` may hand off to the matching `-LP2` tail;
- missing related commands may fall back to the command with its low two bits
  cleared;
- `0x55aa`, `0x55ab`, `0x55ac` and `0x55ae` update global volume, channel
  volume, pan state and channel quieting;
- an ACE1 track request derives its channel from the second data word and its
  volume/pan from the first;
- accumulation saturates to signed 16-bit output.

Pan is parsed, stored and traced, but both sample-library engines currently
produce mono output. It therefore does not create left/right spatial movement.

## Update-derived PCM cache

`pb2kslib-adsp` uses the actual ADSP implementation to render tracks once,
then mounts the result as a normal 44.1 kHz sample library. Its identity is
`v2-<sha256>.pcm.pb2k`, where the digest covers the interleaved U109/U110 image
followed by the exact 1 MiB sound flash. Changing either input selects a new
cache instead of silently reusing stale PCM.

The default root is:

```text
~/.cache/encore-pb2k/pb2kslib-adsp/<game>/
```

When a cache is missing, the launcher defaults to six isolated, headless QEMU
workers. Work is divided over IDs 1–4095, with a boundary after six hinted
tracks or 256 candidate IDs; each process owns independent firmware and SRAM
state. Completed parts are validated, sorted, deduplicated and atomically
renamed into place. Parallel progress is printed in the terminal.
`--pb2kslib-cache-workers 1` retains the original single-process in-window
generator and its window progress display; values 1–32 are accepted.

The fixed pb2kslib, when present, supplies only duration and loop hints during
generation. It is not the PCM source and does not restrict which IDs are
probed. For an unhinted ID, the generator renders one block and compares the
firmware's resolved voice metadata; an unchanged, silent voice is rejected
without manufacturing an empty entry.

`--clear-pb2kslib-cache` deletes the configured generated-cache root before
launch. It does not affect `roms/<game>_sound.bin`, but it does force an
expensive regeneration on the next cache-engine run.

> [!WARNING]
> Do not clear the cache merely to troubleshoot host audio. Cache generation
> changes content preparation; it cannot repair a missing PulseAudio, SDL or
> ALSA output service.

## Host audio and silent operation

The wrapper's `--audio auto` checks both what the QEMU binary compiled and
what the host appears able to serve. Its preference is SDL, PulseAudio, ALSA,
OSS, sndio, then D-Bus. If none is usable it warns and runs silently. An
explicit backend must be advertised by `qemu-system-i386 -audio help` or the
launcher fails before boot.

`--audio none` and `--no-audio` disable PCM production. The guest-facing DCS
protocol remains installed, so a game can still pass the shared core's
synthetic handshake without any audible renderer, regardless of the requested
engine label. Conversely, choosing a host backend does not alter the guest
protocol or content engine.

Use `--audio wav` for a deterministic file-oriented host backend during
diagnosis. The QEMU backend may write its own WAV file in the launch working
directory; Encore's scripted capture below is a separate, bounded facility.

## Capture, tracing and health checks

`--trace-audio` logs command/mixer events and a per-second render summary:
callbacks, accepted host writes, peaks, active voices and source transport.
`--trace-dcs` adds individual UART byte traffic. `-vv` and `-vvv` enable those
levels cumulatively. They are investigative and can perturb timing through
extra logging.

A console script can request a bounded WAV capture:

```text
@record-audio 5 attract-sound
```

The launcher prepares a raw S16LE stream and a rate/channel sidecar, then the
console runner gates capture with F11 and writes the requested portion as a
proper WAV. This works for both 44.1 kHz mono sample engines and 31.25 kHz
stereo live engines. See [Console scripting](42-console-scripting.md) for the
complete directive contract.

For live ADSP engines, `--bench` enables a shutdown health record on both its
IRQ and LPT passes. A pass requires:

- no queued command, runtime host reset or dropped command;
- `enqueued == consumed`;
- at least one PCM frame and one non-zero PCM sample.

That proves command drainage and real PCM activity; it does not judge whether
every game sound is subjectively correct.

Maintainer-only variables include `P2K_DCS_ADSP_TRACE`, continuous raw capture
through `P2K_DCS_AUDIO_DUMP`, and raw-55/byte-pair A/B switches. They should
not become recommended user launch profiles.

Some guest builds also expose a XINA command with the reported syntax
`dcs N [track [pan]]`. That is a revision-specific guest diagnostic, not an
Encore CLI or a stable DCS API. Run guest `help` first and keep it separate
from the emulator-side engine and backend selectors.

## Verification checklist

For a sound-path change:

1. confirm the log names the requested engine rather than a fallback;
2. test a real boot bong and later gameplay/attract sounds, not only DCS
   handshake responses;
3. capture PCM and verify non-zero samples, duration, rate and channel count;
4. for a live engine, run the normal two-pass benchmark and require DCS health
   `PASS/PASS` as well as the timing verdict;
5. for `pb2kslib-adsp`, prove both a current cache hit and first-generation
   behavior without deleting unrelated cache trees;
6. compare update-derived and fixed-library track coverage when a command is
   silent;
7. keep transport, engine and host-backend changes separate during A/B tests.

For a quick human trigger, open the coin door with F4, exercise Up/Down volume,
then close a coin contact with F10 or C. This checks more of the command/mixer
path than the boot bong alone, but listening still cannot replace captured PCM
and health counters.

A normal read-only desktop validation is:

```bash
scripts/run-qemu.sh --bench --game swe1 --update 2.10 --no-savedata
```

For missing backends, fallback messages and first-response triage, see
[Troubleshooting](04-troubleshooting.md). Timing effects of logging and worker
activity belong to [CPU, PIT and IRQ0 timing](12-cpu-and-timers.md).

---

← [MediaGX and display](23-mediagx-and-display.md) ·
[Documentation index](README.md) · [LPT driver board](26-lpt-board.md)
