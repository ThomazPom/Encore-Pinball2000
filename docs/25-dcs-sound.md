# 25 — DCS Sound

This doc covers the QEMU DCS-2 sound implementation in `qemu/p2k-dcs.c`, `qemu/p2k-dcs-core.c`, `qemu/p2k-dcs-uart.c`, and `qemu/p2k-dcs-audio.c`.  It documents the BAR4 MMIO view, the legacy I/O-port UART view, the shared protocol core, and the optional QEMU audio backend.

## Architecture at HEAD

The QEMU build has one DCS protocol owner: `p2k-dcs-core.c`.  BAR4 and legacy I/O are thin frontends and are not allowed to keep parallel FIFO/handshake state (`qemu/p2k-dcs-core.c:1-11`).

> [!NOTE]
> BAR4 and legacy UART share the same protocol core. Both routes decode commands identically; the only difference is the source attribution used for diagnostics.

```
BAR4 MMIO 0x13000000  ----+
                           +--> p2k-dcs-core.c --> optional p2k-dcs-audio.c
I/O 0x138..0x13F     ----+
```

The shared core owns response queue, echo byte, ready/output flags, `0x0E` suspend state, and ACE1 multi-word mixer capture (`qemu/p2k-dcs-core.c:4-8`, `qemu/p2k-dcs-core.c:37-74`).  Both installers call `p2k_dcs_core_reset()`, which is idempotent after first initialization (`qemu/p2k-dcs.c:95-104`, `qemu/p2k-dcs-uart.c:240-249`, `qemu/p2k-dcs-core.c:182-189`).

## BAR4 MMIO view

`p2k-dcs.c` maps a 16 MiB MemoryRegion at `0x13000000` (`qemu/p2k-dcs.c:1-3`, `qemu/p2k-dcs.c:25-26`, `qemu/p2k-dcs.c:95-108`).

| BAR4 offset | Access | Meaning |
|---:|---|---|
| `0x0` | byte read | Return the echo byte from the shared core (`qemu/p2k-dcs.c:28-35`). |
| `0x0` | byte write | Store echo byte; used by PCI-side liveness probes (`qemu/p2k-dcs.c:42-51`). |
| `0x0` | word/dword read | Pop next DCS response word from shared core (`qemu/p2k-dcs.c:28-35`). |
| `0x0` | word/dword write | Submit a 16-bit DCS command word and attribute source as `BAR4` (`qemu/p2k-dcs.c:52-60`). |
| `0x2` | read | Return flag byte: ready/output-available/latch bits (`qemu/p2k-dcs.c:36-38`, `qemu/p2k-dcs-core.c:206-225`). |
| `0x2` | write | Store a flag latch so write-then-read probes see a live device (`qemu/p2k-dcs.c:62-70`, `qemu/p2k-dcs-core.c:228-231`). |
| other | read/write | Reads return 0; writes are ignored except for optional trace warnings (`qemu/p2k-dcs.c:39`, `qemu/p2k-dcs.c:71-77`). |

BAR4 accepts 1..4 byte unaligned accesses (`qemu/p2k-dcs.c:79-93`).

> [!IMPORTANT]
> BAR4 must be mapped at `0x13000000` before PRISM probes DCS. If BAR4 is unmapped or returns all-zeros, the game falls back to legacy UART mode at I/O port `0x13C`.

## Legacy I/O view: `0x138..0x13F`

`p2k-dcs-uart.c` maps eight I/O bytes starting at `0x138` (`qemu/p2k-dcs-uart.c:1-18`, `qemu/p2k-dcs-uart.c:27-29`, `qemu/p2k-dcs-uart.c:240-254`).  It combines the DCS narrow-route protocol with a minimal 16550 register simulation.

| Port | Offset | Access | Meaning |
|---:|---:|---|---|
| `0x138` | 0 | byte R/W | 16550 RBR/THR/DLL shape; echo byte when DLAB is clear (`qemu/p2k-dcs-uart.c:104-110`, `qemu/p2k-dcs-uart.c:204-212`). |
| `0x139` | 1 | byte R/W | IER/DLM subset (`qemu/p2k-dcs-uart.c:109-110`, `qemu/p2k-dcs-uart.c:213-217`). |
| `0x13A` | 2 | byte R | IIR returns `0x01` (`qemu/p2k-dcs-uart.c:111-113`). |
| `0x13B` | 3 | byte R/W | LCR stored, including DLAB bit (`qemu/p2k-dcs-uart.c:113-115`, `qemu/p2k-dcs-uart.c:218-220`). |
| `0x13C` | 4 | word R | Pop DCS response (`qemu/p2k-dcs-uart.c:86-93`). |
| `0x13C` | 4 | word W | Submit DCS command and source `UART:0x13c.w` (`qemu/p2k-dcs-uart.c:135-145`). |
| `0x13C` | 4 | byte W | Assemble high-then-low byte pairs into a DCS command unless `P2K_DCS_NO_BYTE_PAIR=1` (`qemu/p2k-dcs-uart.c:148-200`). |
| `0x13D` | 5 | byte R | LSR `0x60` transmit empty (`qemu/p2k-dcs-uart.c:36-38`, `qemu/p2k-dcs-uart.c:117-119`). |
| `0x13E` | 6 | read | DCS flag byte or modem-status loopback bits (`qemu/p2k-dcs-uart.c:95-102`, `qemu/p2k-dcs-uart.c:119-122`). |
| `0x13F` | 7 | byte R/W | Scratch register (`qemu/p2k-dcs-uart.c:123-125`, `qemu/p2k-dcs-uart.c:224-226`). |

The historically important hardware fact is preserved: DCS command words travel through port `0x13C` in the legacy view (`qemu/p2k-dcs-uart.c:10-18`, `qemu/p2k-dcs-uart.c:135-145`).

> [!NOTE]
> Legacy UART mode at `0x138..0x13F` exists for compatibility. Byte writes at `0x13C` are assembled into 16-bit command words unless `P2K_DCS_NO_BYTE_PAIR=1`.

## Shared core response protocol

| Command | Core response / effect | Citation |
|---:|---|---|
| `0x5800` / `0x5A00` | Push `0x1000` reset/alive ACK. | `qemu/p2k-dcs-core.c:13-18`, `qemu/p2k-dcs-core.c:302-306` |
| `0x003A` | Push `0xCC01`, `10`; trigger boot-dong audio hook. | `qemu/p2k-dcs-core.c:16-18`, `qemu/p2k-dcs-core.c:307-314` |
| `0x001B` | Push `0xCC09`, `10`. | `qemu/p2k-dcs-core.c:315-318` |
| `0x00AA` | Push `0xCC04`, `10`; trigger audio init hook. | `qemu/p2k-dcs-core.c:319-326` |
| `0x000E` | Enter suspend/active mode; the next `0x000E` exits and pushes `10`. | `qemu/p2k-dcs-core.c:19-20`, `qemu/p2k-dcs-core.c:249-258`, `qemu/p2k-dcs-core.c:327-331` |
| `0xACE1` | Begin ACE1 mixer accumulator; push `0x0100`, `0x0C`. | `qemu/p2k-dcs-core.c:21-24`, `qemu/p2k-dcs-core.c:261-300`, `qemu/p2k-dcs-core.c:332-337` |
| other | Forward to audio process hook if installed; otherwise consume silently. | `qemu/p2k-dcs-core.c:25-30`, `qemu/p2k-dcs-core.c:338-343` |

The response ring holds 64 words and drops on overflow (`qemu/p2k-dcs-core.c:37-40`, `qemu/p2k-dcs-core.c:172-180`).  `p2k_dcs_core_flag_byte()` always sets bit 6 ready and sets bit 7 when responses are queued (`qemu/p2k-dcs-core.c:206-225`).

> [!NOTE]
> The response queue is 64 words. Overflow silently drops new responses. Bit 7 of the flag byte signals when responses are available; bit 6 is always set to indicate DCS ready.

## Mode configuration

In this QEMU build, `P2K_DCS_MODE` is resolved only as a diagnostic label (`qemu/p2k-dcs-core.c:96-124`, `qemu/p2k-dcs-core.c:133-160`).

| Label | QEMU behavior at HEAD |
|---|---|
| `io-handled` | Default label.  No CPU text patch.  BAR4 and legacy UART both share the same core (`qemu/p2k-dcs-core.c:106-112`, `qemu/p2k-dcs-core.c:121-124`). |
| `bar4-patch` | Accepted label if `P2K_DCS_MODE=bar4-patch`, but QEMU still performs no CPU-side patch (`qemu/p2k-dcs-core.c:113-124`, `qemu/p2k-dcs-core.c:136-145`). |

> [!NOTE]
> Both mode labels use the same BAR4 + UART shared core. QEMU does not implement CPU text patching because the BAR4 device responds correctly to natural probes (`qemu/p2k-dcs-core.c:113-124`, `qemu/p2k-dcs.c:52-58`).

## DCS audio backend

Three content engines are intentionally separated. `pb2kslib` remains the
stable default. The experimental `--dcs-engine adsp` path boots the selected
update's 1 MiB `sf.rom` (the 28F800 sound flash), executes the ADSP-2104
offsets `0x200000` and `0x400000`. SPORT1 autobuffer output enters QEMU audio
asset preparation succeeds this mode does not load or play samples from
pb2kslib.

The physical U109/U110 pair holds the main DCS sound corpus. The independently
updateable 28F800/`sf.rom` sound-flash channel is mapped alongside that corpus;
it is not a replacement for U109/U110. The genuine SWE1 2.00 community update
includes its own 1 MiB `sf.rom`, so Encore selects native ADSP automatically:

```sh
scripts/run-qemu.sh --game swe1 --update 0200 --dcs-engine adsp
```

`--dcs-sound-flash PATH` remains available for deliberate A/B tests with another
exactly 1 MiB image. Encore does not silently borrow sound flash from an
unrelated update.

`--dcs-engine adsp-thread` uses the same original assets and SPORT renderer,
but services the host-command mailbox on a dedicated condition-driven worker.
It is experimental; SPORT remains callback-clocked so this stage can validate
threaded command progress without simultaneously replacing the PCM clock.

`p2k-dcs-audio.c` is optional.  It installs only when `P2K_DCS_AUDIO=1` and is forced off by `P2K_NO_DCS_AUDIO` (`qemu/p2k-dcs-audio.c:27-33`, `qemu/p2k-dcs-audio.c:824-834`).

> [!TIP]
> Enable audio with `P2K_DCS_AUDIO=1` or the wrapper's `--dcs-audio` flag. Audio requires a `_sound.bin` container in the ROM directory or at `$P2K_PB2KSLIB`.

| Component | Behavior | Citation |
|---|---|---|
| QEMU audio card | Registers `p2k-dcs-audio`, opens `p2k-dcs-out`, activates output voice. | `qemu/p2k-dcs-audio.c:836-858` |
| Output format | 44.1 kHz, mono, signed 16-bit. | `qemu/p2k-dcs-audio.c:51-53`, `qemu/p2k-dcs-audio.c:843-850` |
| Polyphony | 8 voices. | `qemu/p2k-dcs-audio.c:51-55`, `qemu/p2k-dcs-audio.c:71-80` |
| Container path | `$P2K_PB2KSLIB`, then `<roms_dir>/<game>_sound.bin`; no directory walks. | `qemu/p2k-dcs-audio.c:22-25`, `qemu/p2k-dcs-audio.c:192-207` |
| Container format | Header plus 0x48-byte XOR-`0x3A` entries; payload Ogg Vorbis bytes are also XORed. | `qemu/p2k-dcs-audio.c:14-21`, `qemu/p2k-dcs-audio.c:223-253`, `qemu/p2k-dcs-audio.c:408-413` |
| Decode path | Ogg Vorbis to mono S16, linear-resampled to 44.1 kHz. | `qemu/p2k-dcs-audio.c:307-384` |
| Hooks | Installs `process_cmd` and `execute_mixer` callbacks into the core. | `qemu/p2k-dcs-audio.c:82-90`, `qemu/p2k-dcs-audio.c:897-899` |
| Preload | `P2K_DCS_PRELOAD` decodes all known entries during install. | `qemu/p2k-dcs-audio.c:900-913` |

The audio callback mixes active voices with saturating mono S16 output and writes to QEMU with `AUD_write()` (`qemu/p2k-dcs-audio.c:481-563`).  `P2K_DCS_AUDIO_TRACE` enables per-second render/source histograms (`qemu/p2k-dcs-audio.c:742-818`, `qemu/p2k-dcs-audio.c:915-921`).

## Debug and A/B environment variables

| Variable | Effect |
|---|---|
| `P2K_DCS_MODE` | Selects diagnostic label `io-handled` or `bar4-patch` (`qemu/p2k-dcs-core.c:133-160`). |
| `P2K_DCS_BYTE_TRACE` | Logs legacy UART byte-pair activity (`qemu/p2k-dcs-uart.c:40-60`). |
| `P2K_DCS_NO_BYTE_PAIR` | Disables byte-pair assembly at `0x13C` for A/B testing (`qemu/p2k-dcs-uart.c:62-82`, `qemu/p2k-dcs-uart.c:172-184`). |
| `P2K_DCS_RAW_55_PAIR` | Experimental raw `0x55XX+data1` mixer route, off by default (`qemu/p2k-dcs-core.c:346-380`). |
| `P2K_DCS_AUDIO` | Enables QEMU audio install (`qemu/p2k-dcs-audio.c:824-834`). |
| `P2K_NO_DCS_AUDIO` | Forces audio off (`qemu/p2k-dcs-audio.c:828-830`). |
| `P2K_PB2KSLIB` | Overrides sample-container path (`qemu/p2k-dcs-audio.c:192-203`). |
| `P2K_DCS_AUDIO_DUMP` | Dumps raw PCM bytes to a host file (`qemu/p2k-dcs-audio.c:124-129`, `qemu/p2k-dcs-audio.c:859-869`). |
| `P2K_DCS_AUDIO_TRACE` | Enables audio trace/status and BAR4 dropped-write warnings (`qemu/p2k-dcs.c:71-77`, `qemu/p2k-dcs-audio.c:742-818`). |
| `P2K_DCS_PRELOAD` | Preloads decoded samples (`qemu/p2k-dcs-audio.c:900-913`). |


## Source attribution

Before every command write, the frontend records a source tag in the core (`qemu/p2k-dcs.c:58-59`, `qemu/p2k-dcs-uart.c:143-145`, `qemu/p2k-dcs-uart.c:198-199`).

The audio backend reads that tag to maintain BAR4 / UART word / UART byte-pair / compatibility histograms (`qemu/p2k-dcs-audio.c:574-584`, `qemu/p2k-dcs-audio.c:752-772`).

This does not affect protocol behavior; it is diagnostic accounting.

## ACE1 mixer persistence

When an ACE1 mixer triple completes, the core intentionally leaves `pending` set (`qemu/p2k-dcs-core.c:288-299`).

The code preserves the behavior that later mixer triples remain routed through `execute_mixer` until a suspend command clears the state (`qemu/p2k-dcs-core.c:288-295`).

Resetting this flag too early would make later mixer commands fall through as direct `process_cmd` calls and use the wrong channel mapping.

> [!WARNING]
> ACE1 mixer state persists after the first triple completes. If the suspend command (`0x000E`) is never sent, all subsequent mixer commands route through `execute_mixer` instead of `process_cmd`. This is intentional.

## Audio command mapping

`process_cmd` treats `0x0000` as stop-all, `0x003A` as the boot dong on channel 0, `0x00AA` as lookup key `0x0FFF` on channel 7, and `0x0100..0x0FFF` as sample IDs with channel `cmd & 7` (`qemu/p2k-dcs-audio.c:587-648`).

`execute_mixer` treats `0x55AA`, `0x55AB`/`0x55AE`, and `0x55AC` as global volume, channel volume, and channel pan controls (`qemu/p2k-dcs-audio.c:650-711`).

For `0x55AA`, `data1` is a value/complement pair (`0x609f`, `0xc837`,
etc.). The high byte is the actual global volume. Using the low complement
reverses the cabinet volume buttons.

Other mixer triples compute channel from `(data2 & 0x380) >> 7`, volume from `data1 >> 8`, and pan from `data1 & 0xFF` (`qemu/p2k-dcs-audio.c:714-739`).

## Sample cache behavior

Sample lookup is keyed by 16-bit DCS command, bounded by `SAMPLE_CACHE_SIZE = 0x1000` (`qemu/p2k-dcs-audio.c:51-55`, `qemu/p2k-dcs-audio.c:387-391`).

Each failed decode is remembered in `cache_tried[]` so the backend does not repeatedly parse a bad Ogg payload (`qemu/p2k-dcs-audio.c:402-418`).

pb2kslib entries whose names contain `-LP` are continuous assets. The
software mixer distinguishes single-loop tails (`-LP`), two-part loop heads
(`-LP1`), and two-part loop tails (`-LP2`). `-LP1` hands off to the matching
`-LP2` entry when present; `-LP2` and plain `-LP` entries loop at EOF.

A pb2k entry named `dcs-bong` is explicitly mapped to command `0x003A` (`qemu/p2k-dcs-audio.c:245-248`).

## Silent operation

If no audio backend is enabled, DCS protocol still works.

The core function pointers default to NULL (`qemu/p2k-dcs-core.c:78-90`).

Protocol responses are queued regardless of whether the audio sink exists (`qemu/p2k-dcs-core.c:302-343`).

This lets headless or no-audio runs boot without making sound output a hard dependency.


## See also

- [20 — PLX / PCI configuration](20-plx-pci.md)
- [21 — Flash BAR3](21-flash-bar3.md)
- [22 — SRAM BAR2](22-sram-bar2.md)
- [30 — Symptom patches](30-symptom-patches.md)
