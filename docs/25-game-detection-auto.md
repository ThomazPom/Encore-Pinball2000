# 25 — Game Detection and Auto-Selection

Encore supports two titles — **Star Wars Episode I (SWE1)** and
**Revenge From Mars (RFM)** — identified by integer game IDs embedded
in the update bundle. Detecting the correct title automatically avoids
the need for the user to pass `--game` on every invocation.

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## game_id values

| Decimal | Hex        | Title                  |
|--------:|-----------|------------------------|
|   50069 | 0x0000C3D5 | Star Wars Episode I    |
|   50070 | 0x0000C3D6 | Revenge From Mars      |

These are the Williams production game IDs written into every official
update bundle and replicated in the chip ROMs.

## The three detection sources

### 1. Bundle file offset 0x803C (primary)

When `--update FILE` is given, `main.c` reads four bytes at offset
`0x3C` inside the update binary (equivalent to guest address `0x803C`
once the binary is mapped at guest base `0x8000`):

```c
/* src/main.c */
if (fseek(f, 0x3C, SEEK_SET) == 0 && fread(&gid, 4, 1, f) == 1) {
    if (gid == 50069u)       → prefix = "swe1"
    else if (gid == 50070u)  → prefix = "rfm"
}
```

The bundle naming convention also encodes the game ID:
`pin2000_<game_id>_<4-digit-version>_<date>_B_10000000`.
When resolving a version token like `1.5` the directory scan looks for
`pin2000_50069_0150_*` (SWE1) or `pin2000_50070_0150_*` (RFM).

```c
/* src/main.c */
if (strcmp(g_emu.game_prefix, "swe1") == 0) want_gid = "50069";
else if (strcmp(g_emu.game_prefix, "rfm") == 0) want_gid = "50070";
```

### 2. LPT board auto-detect (real-cabinet mode)

When `--lpt-device` opens a physical parallel-port device,
`lpt_passthrough_detect_game()` (`src/lpt_pass.c`) bit-bangs a
three-register probe sequence ported from the original P2K driver. The
board responds differently depending on which title's playfield is
connected, and the function returns `"swe1"` or `"rfm"` accordingly.

```c
/* src/main.c */
if (lpt_passthrough_detect_game(detected, sizeof(detected)) == 0) {
    LOG("init", "LPT board auto-detect → %s\n", detected);
    strncpy(g_emu.game_prefix, detected, ...);
}
```

This path is preferred over bundle inspection when a real cabinet is
present because the cabinet wiring is authoritative.

### 3. Default fallback

If neither a bundle file nor a real LPT board is available, the prefix
defaults to `"swe1"` (`src/main.c`). SWE1 is the more common
installation at the time of writing and is used as the developer's
daily-driver title.

## Prefix table

The `game_prefix` string drives all downstream file lookups:

| `game_prefix` | game_id | ROM subdirectory | Savedata prefix |
|---|---|---|---|
| `swe1` | 50069 | `roms/swe1_*/` | `swe1_<ver>` |
| `rfm`  | 50070 | `roms/rfm_*/`  | `rfm_<ver>`  |

The full ROM-loading path is documented in
[08-rom-loading-pipeline.md](08-rom-loading-pipeline.md).

## Detection precedence

```
--game swe1 / rfm  (explicit CLI flag)
    │ highest priority — skips all detection
    ▼
--update FILE / DIR / .exe / VERSION
    │ reads game_id at offset 0x3C; updates prefix on match
    ▼
LPT board auto-detect
    │ only runs if prefix is still "auto" after --update step
    ▼
Default: "swe1"
```

`--game auto` (the default) means "let detection run". Once the prefix
is known it is logged:

```
[main] --update: detected SWE1 (game_id=50069) → forcing prefix=swe1
```
or
```
[init] LPT board auto-detect → rfm
```

## Cross-references

* ROM loading: [08-rom-loading-pipeline.md](08-rom-loading-pipeline.md)
* Update loader: [09-update-loader.md](09-update-loader.md)
* Title differences: [35-rfm-vs-swe1.md](35-rfm-vs-swe1.md)
* Real LPT passthrough: [19-real-lpt-passthrough.md](19-real-lpt-passthrough.md)

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
