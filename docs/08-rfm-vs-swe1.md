# 08 — Revenge from Mars vs Star Wars Episode I

Encore supports two Pinball 2000 titles. While the hardware platform
and XINU kernel are identical, there are meaningful per-title
differences in ROM content, chip-ROM revisions, sound libraries, and
boot behaviour.

> [!WARNING]
> The behaviour described here is based on QEMU emulator testing only. Real-cabinet validation is pending — see [docs/29-cabinet-testing-call.md](29-cabinet-testing-call.md).

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

The ROM loader handles both. When loading bank 0 for RFM the loader
always prefers r2 names (and falls back to r1 names only if the r2
file is missing).

> [!NOTE]
> The ROM loader implementation prioritizes r2 chips for RFM bank 0 unconditionally. Earlier builds conditioned this on savedata presence, which caused silent fallback to r1 and non-obvious boot failures on fresh installs.

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

Both titles use the same pb2kslib container format but the audio
content is entirely different. The `rfm_P2K.bin` file is approximately
the same size as `swe1_P2K.bin` but contains RFM-specific speech, music
and effects. Entry 0 in both containers is always `dcs-bong`. See
[docs/25-dcs-sound.md](25-dcs-sound.md) for DCS implementation details.

## Symbol table coverage

SWE1 v2.1 retains most XINU internals in its symbol table (`clkruns`,
`Fatal`, scheduler symbols). SWE1 v1.5 and all RFM versions ship
stripped tables that contain only exported API names. The runtime
`sym_lookup()` falls back to hardcoded addresses where the symbol is
absent from the table.

> [!IMPORTANT]
> Stripped symbol tables force pattern-scanning approaches for internal XINU symbols. This is why Encore's patches use byte patterns rather than symbol lookups — portability across stripped and non-stripped bundles.

## DCS-mode patch address

The `dcs_mode_select` function address differs between SWE1 and RFM
builds, but the pattern scan finds the CMP/JNE sequence
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
for the probe to succeed; see the documentation on the probe cell).

### RFM v1.2 (1999)

The oldest known bundle. It uses r1 chip ROMs and a pre-XINU codebase.
It reaches a crash state before XINU `sysinit` completes. Serial output
is visible; the crash is documented as a known limitation.

> [!CAUTION]
> RFM v1.2 (1999) predates the XINU scheduler redesign and crashes before `sysinit` completes. This bundle is preserved for historical research but is not expected to reach playable state without major architectural work.

## game_id auto-detection

Both titles expose their `game_id` at offset `0x3C` in the update
binary, which maps to guest address `0x803C` once the binary is loaded
at guest base `0x8000`. The ROM loader uses this to auto-discover which
game is being booted.

## Cross-references

* DCS sound: [25-dcs-sound.md](25-dcs-sound.md)
* ROM loading pipeline: [15-rom-loading.md](15-rom-loading.md)

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
