# 42 — Cabinet Testing Call-to-Action

> **You can help make Encore better.** If you own or have access to a
> real Pinball 2000 cabinet (Revenge From Mars or Star Wars Episode I),
> you are in a unique position to verify something no emulator run can
> confirm: whether Encore's behaviour matches the real machine.

## Why cabinet testing matters

Encore has been developed and tested entirely in software — every result
in these docs comes from running the game binary inside the Unicorn
Engine emulator on a modern Linux host. That is a rigorous environment,
but it is not the same as a real Pinball 2000 cabinet.

Real hardware validation would tell us:

* Whether the NVRAM image Encore writes is accepted by the original firmware.
* Whether the LPT protocol (`--lpt-device`) correctly drives real lamps, coils
  and switches.
* Whether the DCS audio timing matches what the original DCS-2 board expects
  when commanded over the parallel port.
* Whether the serial console (`--serial-tcp`) behaves as the XINA shell does
  on a real machine.
* Whether attract mode, single-ball play, multi-ball, and game-over sequences
  all progress correctly on actual hardware.

## What to test

Work through the following sequence in order. Stop and report at any
point where something diverges from normal cabinet behaviour.

### 1. Boot
- Power on the cabinet with Encore providing the software.
- Confirm the WMS logo appears on the playfield projector.
- Confirm attract mode begins within 60 seconds.
- Note any error messages on the XINA serial console.

### 2. Attract mode
- Let attract mode run for at least 5 minutes.
- Confirm video is cycling normally.
- Confirm DCS audio plays background music and stings.
- Confirm no software watchdog resets occur.

### 3. Single-ball game
- Insert a credit and start a one-player game.
- Confirm ball launch, switch detection, scoring, and ball drain all
  function normally through to game-over.

### 4. Full game (three balls)
- Play a complete three-ball game.
- Check that bonus counting, NVRAM high-score registration, and
  match sequence work end-to-end.

### 5. Multi-ball
- Trigger a multi-ball mode (game-specific; consult the operator manual).
- Confirm that multiple balls are tracked and the appropriate lighting
  and audio cues fire.

### 6. NVRAM persistence
- After a full game, power-cycle the cabinet.
- Confirm that the high-score table and adjustment settings survive the
  power cycle (i.e. the `savedata/` NVRAM image is reloaded correctly).

### 7. Audio
- Confirm speech, music, and sound effects are timed and pitched
  correctly compared to a known-good cabinet recording.
- Note any samples that are missing, distorted, or play at the wrong time.

### 8. LPT switches and lamps (if `--lpt-device` is available)
- With `--lpt-device /dev/parport0` and a real driver board connected,
  confirm that switch closures are detected and that lamps and
  flashers respond to game events.

### 9. Coil drive
- With the driver board connected, verify that flippers, slingshots,
  and pop bumpers fire on the correct switch activations.

## How to set up

1. **Build Encore** — follow [02-quickstart.md](02-quickstart.md).
2. **Prepare your ROM folder** — follow [08-rom-loading-pipeline.md](08-rom-loading-pipeline.md)
   and [09-update-loader.md](09-update-loader.md) to lay out the chip ROMs and
   one update bundle.
3. **Optional LPT passthrough** — if a real driver board is available,
   add `--lpt-device /dev/parport0` (or whichever device node your system
   exposes). See [19-real-lpt-passthrough.md](19-real-lpt-passthrough.md) for
   kernel module requirements.
4. **Serial console** — connect a terminal to `--serial-tcp 4444` or to
   the real RS-232 port of the machine to see XINA output.
5. **Run** — a typical launch command:
   ```sh
   ./build/encore --game rfm --update 260 --lpt-device /dev/parport0
   ```

## Report template

When you have results to share, please include the following
information. A brief log excerpt is especially valuable.

```
Game:           [Revenge From Mars | Star Wars Episode I]
Bundle version: [e.g. RFM v2.6]
Host OS:        [e.g. Debian 12 x86_64]
Encore version: [git describe output or commit hash]
LPT device:     [/dev/parport0 | none]

What worked:
  [list each test section above: ✓ pass / ✗ fail / ~ partial]

What failed or behaved differently from a stock cabinet:
  [describe in detail — screenshots, video, and log excerpts welcome]

XINA console log excerpt (first 50 lines or so):
  [paste here]
```

## Where to send reports

Open an issue against this repository or contact the maintainers via
the channel listed in the top-level README.

## Background: emulator regression baseline

Encore already passes an emulator-only regression matrix covering all
seven known update bundles. See [26-testing-7-bundle-matrix.md](26-testing-7-bundle-matrix.md)
for the pass criteria and current status. Cabinet testing would extend
that baseline with real-hardware confirmation.

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
