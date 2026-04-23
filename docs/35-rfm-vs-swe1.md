# 35 — RFM vs SWE1 Per-Title Differences

Encore supports two Pinball 2000 titles. While the hardware platform
and XINU kernel are identical, there are meaningful per-title
differences in ROM content, chip-ROM revisions, sound libraries, and
boot behaviour.

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## Identity

| Field              | Star Wars Episode I | Revenge From Mars |
|--------------------|--------------------|--------------------|
| `game_prefix`      | `swe1`             | `rfm`              |
| `game_id`          | 50069              | 50070              |
| Bundle GID field   | `pin2000_50069_*`  | `pin2000_50070_*`  |
| Sound library      | `swe1_P2K.bin`     | `rfm_P2K.bin`      |

## Chip-ROM revisions

SWE1 uses a single chip-ROM set (`u100`–`u107`, `u109`/`u110`). There
is no r2 revision for SWE1.

RFM shipped in two chip-ROM revisions:

* **r1** — original 1999 chips, used with RFM v1.2. These are labelled
  `rfm_u100.rom` … `rfm_u107.rom`.
* **r2** — revised chips, used with RFM v1.6 and later. These are
  labelled `rfm_u100r2.rom` … `rfm_u107r2.rom`.

`src/rom.c` handles both. When loading bank 0 for RFM the loader
always prefers r2 names (and falls back to r1 names only if the r2
file is missing):

```c
/* src/rom.c — bank 0 for RFM always prefers r2 */
bool prefer_r2 = (b == 0 &&
                  strcmp(g_emu.game_prefix, "rfm") == 0);
```

Older builds of Encore gated the r2 preference on the savedata
`game_id_str` containing `_15`, which meant RFM would silently fall
back to the r1 chips whenever no savedata existed yet (fresh
install, `--no-savedata`, `--update none`). The symptoms were
non-obvious — black screen on `--update none`, and occasional boot
flakiness on newer bundles that expect the r2 revision. r2 is now
preferred unconditionally for RFM.

The `allow_r2` flag is set for bank 0 only, where the revision
difference matters; banks 1–3 only ship as `rfm_u102.rom` … `rfm_u107.rom`.

## DCS sound content

Both titles use the same pb2kslib container format (see
[32-tools-sound-decoder.md](32-tools-sound-decoder.md)) but the audio
content is entirely different. The `rfm_P2K.bin` file is approximately
the same size as `swe1_P2K.bin` but contains RFM-specific speech, music
and effects. Entry 0 in both containers is always `dcs-bong`.

## Symbol table coverage

SWE1 v2.1 retains most XINU internals in its symbol table (`clkruns`,
`Fatal`, scheduler symbols). SWE1 v1.5 and all RFM versions ship
stripped tables that contain only exported API names. The runtime
`sym_lookup()` falls back to hardcoded addresses where the symbol is
absent from the table.

## DCS-mode patch address

The `dcs_mode_select` function address differs between SWE1 and RFM
builds, but the pattern scan (`src/cpu.c`) finds the CMP/JNE sequence
regardless:

```
[init] DCS-mode pattern hit @0x001931e4 slot=0x0034a714 — patched
```

The logged address will differ across bundles. No per-title gating is
needed.

## Boot behaviour differences

### SWE1 v1.5 and v2.1

Both boot to attract mode in `bar4-patch` mode. SWE1 v1.5 is also the
only bundle observed to reach DCS audio under emulation via `io-handled` mode (natural
probe path).

### RFM v1.6, v1.8, v2.5, v2.6

All four reach attract mode in `bar4-patch` mode. The `io-handled` path
boots graphics but is audio-silent due to the natural probe returning 0
on these bundles (the watchdog scribble value must be carefully aligned
for the probe to succeed; see [13-dcs-mode-duality.md](13-dcs-mode-duality.md)).

### RFM v1.2 (1999)

The oldest known bundle. It uses r1 chip ROMs and a pre-XINU codebase.
It reaches a crash state before XINU `sysinit` completes. Serial output
is visible; the crash is documented in
[38-known-limitations.md](38-known-limitations.md).

## game_id auto-detection

Both titles expose their `game_id` at offset `0x3C` in the update
binary, which maps to guest address `0x803C` once the binary is loaded
at guest base `0x8000`. See [25-game-detection-auto.md](25-game-detection-auto.md).

## Cross-references

* Game detection: [25-game-detection-auto.md](25-game-detection-auto.md)
* Sound decoder tool: [32-tools-sound-decoder.md](32-tools-sound-decoder.md)
* DCS mode duality: [13-dcs-mode-duality.md](13-dcs-mode-duality.md)
* Known limitations: [38-known-limitations.md](38-known-limitations.md)
* ROM loading pipeline: [08-rom-loading-pipeline.md](08-rom-loading-pipeline.md)

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
