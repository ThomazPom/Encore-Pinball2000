# 27 — Troubleshooting

Quick diagnosis guide for the most common failure modes.

---

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## Black SDL window — no graphics

**Symptom:** The window opens but stays black indefinitely.

**Checklist:**

1. Look for `[disp] first non-zero framebuffer detected` in the log. If
   it never appears, the guest is not writing any pixels.
2. Check `[irq] XINU ready` — if this line never appears, the scheduler
   did not start (see [23-boot-scheduler-fix.md](23-boot-scheduler-fix.md)).
3. Ensure BT-74 fired:
   ```
   [sgc] BT-74: nulluser idle JMP$ → HLT+JMP at 0x00xxxxxx
   ```
   If missing, the nulluser-idle pattern scan failed — possibly the ROM
   is corrupted or the chip-ROM interleave was not applied correctly.
4. Verify `--dcs-mode bar4-patch` is active (default). If you passed
   `--dcs-mode io-handled`, audio is silent and the display may stall
   waiting for a DCS handshake on bundles that require the BAR4 path.

---

## No sound / DCS silent

**Symptom:** Graphics appear but the boot dong never plays.

**Checklist:**

1. Confirm `[init] DCS-mode pattern hit` is logged. If not, the CMP/JNE
   scan at the DCS-mode branch failed; try re-running with the same
   bundle and verify there are no ROM load errors.
2. Check that the sound container was found:
   ```
   [sound] shape-detected sound lib: roms/swe1_P2K.bin
   ```
   If the container is not found, the `*_P2K.bin` file is missing from
   `./roms/`. Its location is shape-detected (magic bytes), not
   filename-based, so a renamed file will still work as long as it is
   in `./roms/`.
3. Verify SDL2_mixer initialised:
   ```
   [sound] SDL_mixer initialised
   ```
   If missing, the audio subsystem failed to open (common in headless
   CI). Use `--headless` to skip audio entirely in that environment.
4. Check that `--dcs-mode bar4-patch` is active (the default). The
   `io-handled` path relies on the natural PCI probe which is only
   reproducible on SWE1 v1.5 under specific conditions.

---

## Wrong game — RFM boots as SWE1 (or vice-versa)

**Symptom:** The wrong attract-mode sequence appears, or ROM load fails
with size mismatches.

**Fix:** Pass `--game rfm` or `--game swe1` explicitly, or use
`--update VERSION` which auto-detects game_id from offset `0x3C` in the
bundle. See [25-game-detection-auto.md](25-game-detection-auto.md).

---

## `--update: could not resolve '2.1'`

**Cause:** No directory named `pin2000_50069_0210_*` under `./updates/`.

**Fix:** Place the dearchived bundle under `./updates/` using the
standard naming convention, or pass the full path:

```sh
./build/encore --update /path/to/my_update.bin
```

---

## `Failed to load ROM bank`

**Cause:** The chip-ROM files are missing, have the wrong names, or are
not interleaved correctly.

**Fix:** Verify that the files are named according to the convention in
[08-rom-loading-pipeline.md](08-rom-loading-pipeline.md) and that the
2-byte interleave was applied with
`tools/deinterleave_rebuild.sh` (see
[31-tools-deinterleave.md](31-tools-deinterleave.md)).

---

## `SDL_Init failed` / immediate exit

**Cause:** No display server available (headless CI, SSH without X).

**Fix:** Add `--headless` to skip SDL entirely:

```sh
./build/encore --game swe1 --update 2.1 --headless --serial-tcp 4444
```

Then attach the serial console with `nc localhost 4444`.

---

## Hang after XINU banner, no `[irq] XINU ready`

**Cause:** The `clkint` interrupt handler was not detected in IDT[0x20].

**Checklist:**

1. Check for `[irq] clkint detected` — if present but `XINU ready`
   never follows, the 50-batch grace window did not elapse; the log
   would show `waiting for clkint` repeating. This is benign; wait
   longer.
2. If `clkint detected` never appears: the PIT divisor was not
   programmed by the game. Check that the update binary loaded
   (`[rom] update flash loaded`).

---

## Excessive CPU usage / emulator slower than expected

**Cause:** The `nulluser` idle-loop patch (BT-74) failed to apply, or
the SIGALRM handler is delivering ticks faster than the host can
consume them.

**Check:** Look for `BT-74: nulluser idle JMP$ → HLT+JMP` in the log.
If absent, the pattern scan found nothing — the ROM may be a non-
standard build. File an issue with the bundle version.

---

## Savedata corruption on restart

**Symptom:** Game credits or settings reset every launch.

**Fix:** Do not kill the process with `kill -9`; use `F1`, `Ctrl-C`,
or `SIGTERM` which triggers a clean savedata flush. See
[10-savedata.md](10-savedata.md).

---

## Real cabinet: LPT device not opening

**Cause:** `/dev/parport0` is not accessible (no `lp` module, wrong
permissions, or device node does not exist).

**Fix:**

```sh
sudo modprobe parport_pc
sudo chmod a+rw /dev/parport0
# or run encore as root for testing
```

If no real cabinet is present, force emulated LPT:

```sh
./build/encore --lpt-device none
```

See [19-real-lpt-passthrough.md](19-real-lpt-passthrough.md).

---

## Cross-references

* Boot scheduler: [23-boot-scheduler-fix.md](23-boot-scheduler-fix.md)
* DCS mode: [13-dcs-mode-duality.md](13-dcs-mode-duality.md)
* ROM loading: [08-rom-loading-pipeline.md](08-rom-loading-pipeline.md)
* Keyboard guide: [36-cli-keyboard-guide.md](36-cli-keyboard-guide.md)

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
