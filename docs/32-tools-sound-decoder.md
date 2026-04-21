# 32 — tools/extract_sounds.py

`tools/extract_sounds.py` decodes and extracts all sound samples from a
Pinball 2000 `*_P2K.bin` sound library file.

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## The pb2kslib container

The Pinball 2000 sound library (`swe1_P2K.bin` or `rfm_P2K.bin`) is a
custom container format, here referred to as **pb2kslib** ("P2K sound
lib"). It holds every OGG Vorbis sample used by the game as well as the
DCS boot-dong. All data in the index and blob sections is XOR-obfuscated
with key `0x3A`; the 16-byte file header is stored in plain form.

Detection is **shape-based**: `src/sound.c` locates the container by
matching the 16-byte header magic (`version=1, header_size=16,
entry_size=72`) rather than by file name. Any file in `./roms/` that
passes the magic check is accepted, regardless of its name.

## File format

```
[0x00–0x0F]   Header  (NOT XOR'd)  — 4 × u32 LE:
               version      = 1
               header_size  = 16
               count        = number of entries
               entry_size   = 72 (0x48)

[0x10 …]      Index table  (XOR'd × 0x3A)  — count × 72 bytes:
               [0 … ]   name   — NUL-terminated ASCII (e.g. "S0001-LP")
               [? …]    format — NUL-terminated ASCII (always "ogg")
               [64:68]  offset — u32 LE, absolute file offset of blob
               [68:72]  size   — u32 LE, byte length of blob

[offset …]    Data blobs  (XOR'd × 0x3A)  — OGG Vorbis audio:
               All samples: mono, 22 050 Hz, Vorbis codec.
               Naming conventions:
                 S[XXXX]-LP   — single looping sample (hex DCS sound ID)
                 S[XXXX]-LP1  — first segment of two-part loop
                 S[XXXX]-LP2  — second segment of two-part loop
                 dcs-bong     — DCS audio system startup sound (entry 0)
```

## Usage

```sh
python3 tools/extract_sounds.py <game>_P2K.bin [output_dir]
```

If `output_dir` is omitted, a subdirectory named after the input file
is created in the current directory.

### Example

```sh
python3 tools/extract_sounds.py roms/swe1_P2K.bin swe1_sounds/
```

Output:

```
File   : roms/swe1_P2K.bin (28.4 MB)
Version: 1  header_size: 16  entries: 312  entry_size: 72
Extracted: 312 files → swe1_sounds/
```

The extracted files are plain `.ogg` files, playable with any OGG-
capable player.

## DCS context

The original Pinball 2000 hardware used a dedicated DCS (Digital
Compression System) audio board with real DCS-format ROMs. The
`*_P2K.bin` library is an official software re-implementation: it
replaces the DCS hardware with OGG re-encodings of the same audio
content, loaded from disk at boot.

The `dcs-bong` sample (entry 0) is the startup sound confirming the
audio subsystem initialised correctly. Its playback is logged by
`src/sound.c` and is one of the pass criteria for the regression matrix
(see [26-testing-7-bundle-matrix.md](26-testing-7-bundle-matrix.md)).

## Cross-references

* Sound pipeline: [12-sound-pipeline.md](12-sound-pipeline.md)
* DCS mode duality: [13-dcs-mode-duality.md](13-dcs-mode-duality.md)
* Regression matrix: [26-testing-7-bundle-matrix.md](26-testing-7-bundle-matrix.md)
