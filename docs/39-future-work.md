# 39 — Future Work

Open investigations and improvements, ordered by how much they would
actually move the project. **High** = users hit it today; **medium** =
correctness/convenience polish; **low / maybe-fun** = speculative.

This list trails the code; if you see an item here that is already
landed, send a patch to delete it.

---

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [42-cabinet-testing-call.md](42-cabinet-testing-call.md) for how to
> help verify.

## High priority

### Guest CPU pacing / target-IPS throttle — coarse version done, refine it

`--cpu-stats[=N]` (measurement) and `--realtime` + `--cpu-target-mhz`
(opt-in once-per-vblank nanosleep throttle) now exist — see
[50-cpu-clock-mismatch.md](50-cpu-clock-mismatch.md). They are coarse
and off by default; `--lpt-bus-pace` ([19-real-lpt-passthrough.md](19-real-lpt-passthrough.md))
remains the finer-grained band-aid for the LPT wire boundary
specifically. Remaining work: verify `--realtime` actually removes the
need for `--lpt-bus-pace` on a real cabinet (untested — no hardware
run logged yet), then retire the band-aid if so. If `--realtime`'s
per-vblank granularity proves too coarse, move to the icount-style
per-block throttle described as step 3 in doc 50.

### LPT cabinet validation on real hardware

`--lpt-device /dev/parportN` is implemented and traceable, but no
end-to-end run on a real Pinball 2000 cabinet has been logged here.
Until that happens, every claim in [19-real-lpt-passthrough.md](19-real-lpt-passthrough.md)
is plausible-but-unverified. See
[42-cabinet-testing-call.md](42-cabinet-testing-call.md).

---

## Medium priority

### `make install` and packaging

Add a `make install DESTDIR=/usr/local` target and a minimal
`debian/` packaging recipe so Encore can be distributed as a `.deb`
(or `.rpm`/`.AppImage`).

### Automated regression script — done, wire into CI

`tools/run-bundle-matrix.py` launches every bundle under `updates/`
(or the base ROMs with `--update none`) `--headless`, checks the log
for `[gp] BLT` (video) and `[dcs] WR`/`[dcs-io] cmd=` (DCS audio)
activity, and exits 0/1. See
[26-testing-bundle-matrix.md](26-testing-bundle-matrix.md#running-it-automated)
for usage. Remaining work: wire it into CI, and decide whether to also
assert on the in-scope/reference-only split documented there.

### Symbol-based patch address resolution everywhere

`sym_lookup()` is already used in most places, but a few hardcoded
guest addresses remain in `src/cpu.c` and `src/io.c`. Replacing them
with sym-table lookups would let the same source survive arbitrary
binary layout drift across bundle versions.

### Per-game persistent settings

`--config` already loads `encore.yaml` at startup. The next step is
saving runtime-changed flags (`--fullscreen`, `--flipscreen`, `--bpp`)
back to a per-game config so they don't need to be re-specified every
launch.

---

## Low priority

### Snapshot / restore

Save and restore full emulator state (RAM + register file) so a
running session can be resumed without a full re-boot. Unicorn supports
context save/restore via `uc_context_save` / `uc_context_restore`;
the harder part is freezing PCI/IO peripheral state coherently.

### Net bridge for RFM internet leaderboard

`--net-bridge tap0` appears in the help text (`src/main.c`) but no
code path consumes it. RFM originally supported internet high-score
submission via a modem; emulating the modem interface and routing to
a modern HTTPS endpoint would restore this. Either implement it or
delete the help line.

---

## Maybe-fun

* **Audio visualiser** — overlay the DCS command stream as an on-screen
  histogram, useful for debugging DCS routing.
* **XINA web terminal** — expose the serial console as WebSocket so
  it can be accessed from a browser without `nc`.
* **Frame recorder** — dump each rendered framebuffer to PNG using the
  already-linked `stb_image_write` so a session can be reviewed offline.
* **Multi-game ROM auto-discover** — scan a directory tree and
  classify ROMs by magic bytes rather than relying on filenames.

---

## Historical research notes

These predate Encore and would expand what the emulator can faithfully
reproduce. None are blocking.

* **DCS-2 sound-DSP ROM format (`u109` / `u110`).** The on-card ADSP
  ROMs that drive DCS-2 sample playback use a layout that is not yet
  fully documented. Encore bypasses ADSP emulation entirely and serves
  samples from a pre-extracted container, so the ROM format is not
  required for current playback — but documenting it would enable a
  true DCS-2 emulator path.
* **EMS file format.** The 16-byte `*.ems` savedata file holds
  service-menu flags. The encoding is partially understood from
  observed bit changes after menu interactions, but a complete field
  table has not been written down.
* **PRISM I/O port map (`0x22` / `0x23` and others).** Several
  PRISM-side ISA I/O registers are recognised by the option ROM but
  their full register table has not been enumerated.
* **SEEPROM data layout.** Words 38–45 are known to hold the
  CS0BASE–CS3BASE PLX values. The remainder of the 64-word, 16-bit
  SEEPROM is mostly understood from observed reads but is not yet
  documented as a single field map.
* **Video BIOS identity.** Confirmed: the first 32 KB of ROM bank 0
  serves as the option ROM. No separate video BIOS image exists on
  the card.

---

## Cross-references

* CPU clock-rate mismatch: [50-cpu-clock-mismatch.md](50-cpu-clock-mismatch.md)
* LPT passthrough: [19-real-lpt-passthrough.md](19-real-lpt-passthrough.md)
* Cabinet testing: [42-cabinet-testing-call.md](42-cabinet-testing-call.md)
* Known limitations: [38-known-limitations.md](38-known-limitations.md)
* Patching philosophy: [21-patching-philosophy.md](21-patching-philosophy.md)
* Regression matrix: [26-testing-bundle-matrix.md](26-testing-bundle-matrix.md)

← [Back to documentation index](README.md) · [Back to project README](../README.md)
