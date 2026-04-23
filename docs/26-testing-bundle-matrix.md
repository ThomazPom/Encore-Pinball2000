# 26 — Testing: Bundle Regression Matrix

Encore's primary correctness criterion is: *every bundled update boots
to attract mode with graphics and DCS audio*. This page documents the
reproducible headless test procedure and the latest results.

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## Bundled updates

Eleven update bundles ship pre-extracted under `updates/`. The original
self-extracting `.exe` archives (when available) sit alongside them in
`updates/exe-sources/` as the source-of-truth reference; the extracted
directories are produced by `unzip` of those archives.

> ### Scope policy: in-scope vs reference-only
>
> The chip ROMs in `roms/` are at a fixed baseline:
> **SWE1 v1.5** and **RFM v1.6 (r2)**.
> An on-cabinet update can only *upgrade* flash, never *downgrade* the
> base chip ROMs underneath. Bundles with a version *below* the chip-ROM
> baseline are therefore **out of scope** — they are kept on disk for
> historical/reference purposes (so you can see what shipped earlier),
> and they may even appear to start under emulation, but Encore makes
> no claims about whether they reach a usable state and will not
> investigate failures against them.
>
> | Game | In-scope updates |
> |------|------------------|
> | SWE1 | v1.3, v1.4, v1.5, v2.10 (community) |
> | RFM  | v1.2, v1.4, v1.5, v1.6, v1.8, v2.50 (community), v2.60 (community) |
>
> Community versions (mypinballs) are supported in Encore but not
> redistributed in the repo — see
> [47-community-updates.md](47-community-updates.md).

| # | Bundle directory pattern              | Title | Version | Scope |
|---|---------------------------------------|-------|---------|-------|
| 1 | `pin2000_50069_0130_*`                | SWE1  | v1.3    | **in-scope** |
| 2 | `pin2000_50069_0140_*`                | SWE1  | v1.4    | **in-scope** |
| 3 | `pin2000_50069_0150_*`                | SWE1  | v1.5    | **in-scope** (latest official Williams) |
| 4 | `pin2000_50069_0210_*`                | SWE1  | v2.10   | **in-scope** (community / mypinballs — see [47](47-community-updates.md)) |
| 5 | `pin2000_50070_0120_*`                | RFM   | v1.2    | **in-scope** (r1 chips, pre-XINU) |
| 6 | `pin2000_50070_0140_*`                | RFM   | v1.4    | **in-scope** |
| 7 | `pin2000_50070_0150_*`                | RFM   | v1.5    | **in-scope** |
| 8 | `pin2000_50070_0160_*`                | RFM   | v1.6    | **in-scope** (baseline, r2) |
| 9 | `pin2000_50070_0180_*`                | RFM   | v1.8    | **in-scope** (latest official Williams) |
|10 | `pin2000_50070_0250_*`                | RFM   | v2.50   | **in-scope** (community / mypinballs — see [47](47-community-updates.md)) |
|11 | `pin2000_50070_0260_*`                | RFM   | v2.60   | **in-scope** (community / mypinballs — see [47](47-community-updates.md)) |

The community/post-Williams updates (rows 4, 10 and 11) are not
shipped with the repo — grab the latest versions from
<https://mypinballs.com> and drop them into `./updates/` to run them
through the same regression matrix.

When `--update` is omitted, `--game swe1` and `--game rfm` automatically
select the newest bundle for that title (equivalent to `--update latest`).
Pass `--update none` to opt out — see [38-known-limitations.md](38-known-limitations.md)
for why base ROMs alone do not boot to a usable state.

## The two modes under test

Each bundle is run in both `--dcs-mode` variants:

| Mode            | Flag                      | Expected behaviour |
|-----------------|---------------------------|--------------------|
| `bar4-patch`    | `--dcs-mode bar4-patch`   | DCS audio via BAR4 patch (default) |
| `io-handled`    | `--dcs-mode io-handled`   | Natural PCI probe; audio when probe returns 1 |

`bar4-patch` is the default and is the primary delivery path.
`io-handled` is tested to verify the alternate path does not regress
the boot.

## Pass criteria

A bundle passes when, within ~15 seconds of headless launch with dummy
SDL drivers:

1. `exec_count` exceeds 50 000 (CPU is making real progress)
2. `vsync` count is at least 5 (display IRQ fires)
3. At least 5 `[dcs] WR …` log lines (sound pipeline active)

## Running the matrix

A reproducible headless run for every bundle in both modes:

```sh
mkdir -p /tmp/encore_reg && cd /tmp/encore_reg
ENC=/path/to/Encore-Pinball2000
for dir in $ENC/updates/pin2000_*; do
    bn=$(basename "$dir")
    ver=$(echo "$bn" | cut -d_ -f3)
    gid=$(echo "$bn" | cut -d_ -f2)
    [ "$gid" = "50069" ] && game=swe1 || game=rfm
    for mode in bar4-patch io-handled; do
        log=${game}_${ver}_${mode}.log
        SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
            timeout 15 "$ENC/build/encore" --headless --no-savedata \
            --game $game --update "$ver" --dcs-mode $mode > "$log" 2>&1
    done
done
```

Then summarise:

```sh
for log in *.log; do
    bn=$(basename "$log" .log)
    exec=$(grep -oE 'exec_count=[0-9]+' "$log" | tail -1 | cut -d= -f2)
    vsync=$(grep -oE 'vsync=[0-9]+'      "$log" | tail -1 | cut -d= -f2)
    dcs=$(grep -c '\[dcs\] WR' "$log")
    printf "%-40s exec=%-9s vsync=%-5s dcs_wr=%s\n" \
        "$bn" "${exec:-0}" "${vsync:-0}" "$dcs"
done | sort
```

## Latest observed results (headless, dummy SDL drivers, 18 s timeout)

> *Re-run command: see "Running the matrix" above. Captured against
> the current `main` build after the r2-ROM default fix (see
> [11-rom-loading.md](11-rom-loading.md) — Encore now prefers
> `rfm_u100r2.rom` / `rfm_u101r2.rom` for every RFM bundle because
> both r1 and r2 chip files ship together and r2 is the production
> revision).* Real-cabinet validation is still pending.
> Community bundles (rows marked *mypinballs*) require dropping the
> bundle into `./updates/` first — see
> [47-community-updates.md](47-community-updates.md).

| Bundle                      | bar4-patch                                   | io-handled                                  |
|-----------------------------|----------------------------------------------|---------------------------------------------|
| SWE1 v1.3                   | ⚠ boots video (vsync ≈ 782), **DCS pattern absent → silent** | ✗ `UC_ERR_INSN_INVALID` before XINU, no boot |
| SWE1 v1.4                   | ✓ pass (vsync ≈ 792, dcs_wr = 30)            | ✓ pass (vsync ≈ 817, dcs_wr = 30)           |
| SWE1 v1.5                   | ✓ pass                                       | ✓ pass                                      |
| SWE1 v2.10 *(mypinballs)*   | ✓ pass                                       | ✓ pass                                      |
| RFM v1.2                    | ✓ pass (with r2 chips)                       | ✗ stalls pre-XINU (EIP=0, dcs_mode stays 0) |
| RFM v1.4                    | ✓ pass                                       | ✓ pass                                      |
| RFM v1.5                    | ✓ pass                                       | ✓ pass                                      |
| RFM v1.6                    | ✓ pass                                       | ✓ pass                                      |
| RFM v1.8                    | ✓ pass                                       | ✓ pass                                      |
| RFM v2.50 *(mypinballs)*    | ✓ pass                                       | ✓ pass                                      |
| RFM v2.60 *(mypinballs)*    | ✓ pass                                       | ✓ pass                                      |

**Summary: 18 / 22 bundle×mode combinations pass cleanly.** The 4
deviations are all on the earliest pre-XINU bundles and are explained
below.

### `--update none` (no flash overlay) — separate axis

This row is not part of the bundle×mode matrix above, but worth
recording because it confirms the chip ROM set alone is healthy:

| Game | `--update none` result |
|------|------------------------|
| SWE1 | ⚠ video boots (exec ≈ 25 M, vsync ≈ 764), **no DCS writes** (pattern absent: the DCS driver code lives in the flash bundle, not in the base chips) |
| RFM  | ⚠ video boots (exec ≈ 50 M, vsync ≈ 759), **no DCS writes** (same reason)                                                                            |

Before the r2 default fix, `--game rfm --update none` produced
exec_count ≈ 2 000 and an immediate stall — the legacy r1 chips
don't own a modern PCI/XINU boot path. This is fixed now.
`--update none` is still not a supported "play the game" mode; its
purpose is to sanity-check the base chip ROM set. To actually play,
supply a bundle (or let the auto-picker choose the latest).

### Why the four deviations?

All four share the same mechanism: the BT-107 watchdog cell is
primed at SGC-scan time (`io.c:apply_sgc_patches`) with `0x0000`
under `io-handled` and `0x0000FFFF` under `bar4-patch`, and that
value is read by pre-XINU boot code — not just by the DCS probe.
On immature firmware (pre-XINU) or with no flash overlay, a read of
`0x0000` from that cell derails an early sanity check and the
guest dies before reaching `xinu_ready`. See
[13-dcs-mode-duality.md](13-dcs-mode-duality.md#what-actually-changes-between-the-two-modes)
for the full derivation.

* **SWE1 v1.3 / bar4-patch** — `[init] DCS-mode pattern absent`.
  The 5-byte `CMP eax,1 / JNE +0x21 / MOV [slot], eax` prologue
  does not exist in this pre-release build, so the CPU patch is a
  no-op. Boot still reaches attract because prime=0xFFFF lets the
  pre-XINU checks pass. DCS stays silent because the game's own
  probe returns 0 and DCS init is skipped.
* **SWE1 v1.3 / io-handled** — `UC_ERR_INSN_INVALID` at
  `EIP≈0x001f2de8` around `exec≈10 000`. The CPU patch is a no-op
  here too (pattern absent); the only variable left is the prime,
  and prime=0 sends a pre-XINU routine into unmapped memory.
* **RFM v1.2 / io-handled** — heartbeat shows `EIP=0x00000000`,
  `frames=0`, `dcs_mode=0`. XINU never fires. Prime=0 breaks an
  r1-era pre-XINU check; the CPU patch step is never reached.
  Under `bar4-patch` the same bundle reaches xinu_ready, hits the
  5-byte pattern at `0x001c0b9e` and boots with DCS.
* All other bundles pass in both modes with the same `[dcs] WR`
  activity (typically 30 writes during the 18 s window).

### Sound-pipeline failure modes (by symptom)

| Symptom in log                                             | Root cause                                                              | Fix |
|------------------------------------------------------------|-------------------------------------------------------------------------|-----|
| `[init] DCS-mode pattern absent`                           | 5-byte prologue not present in this build (SWE1 v1.3 only)              | None — DCS stays silent; video still works under `bar4-patch` |
| No `XINU ready` log line; `UC_ERR_INSN_INVALID` spam       | `io-handled` prime=0 derails a pre-XINU check                           | Switch to `--dcs-mode bar4-patch` |
| `XINU ready` fires but heartbeat shows `dcs_mode=0`        | Unmodified probe returned 0 after the scribble (not yet observed in the matrix) | Switch to `--dcs-mode bar4-patch` |
| `exec_count` < 100 K, no `vsync`                           | Base chips only (`--update none`) or the wrong `--dcs-mode` on pre-XINU | Supply `--update <version>` or use `bar4-patch` |
| XINU boots, pattern hit, `[dcs] WR` count = 0              | Not yet observed — would indicate a regression in `sound.c`             | Report a bug |

## Cross-references

* Known limitations: [38-known-limitations.md](38-known-limitations.md)
* DCS mode duality: [13-dcs-mode-duality.md](13-dcs-mode-duality.md)
* RFM vs SWE1 differences: [35-rfm-vs-swe1.md](35-rfm-vs-swe1.md)
* Update loader: [09-update-loader.md](09-update-loader.md)

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
