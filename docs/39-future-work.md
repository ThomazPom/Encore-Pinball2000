# 39 — Future Work

A prioritised list of open investigations and improvements. Items marked
**high** are blocking for broader usability; **medium** items improve
correctness or convenience; **low** and *maybe-fun* items are
speculative.

---

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## High priority

### Patchless IO DSP handshake

The `io-handled` DCS mode path needs a complete implementation of the
I/O-port DCS handshake (ports `0x138`–`0x13F`). The current stub
answers reads with `0xFF` which causes the game to decide no DCS board
is present. Reverse-engineering the exact handshake sequence from the
game's DCS init code would make `io-handled` the primary audio path and
eliminate the byte-patch dependency in `bar4-patch` mode.

**Reference:** `src/io.c` — DCS2 port handler; `src/encore.h` —
`ENCORE_DCS_IO_HANDLED`.

### RFM v1.2 pre-XINU boot investigation

The 1999 RFM v1.2 build crashes before XINU `sysinit`. A UART trace
of the crash site, combined with the symbol table for that bundle,
should pinpoint the failing constructor or initialisation routine.
See [38-known-limitations.md](38-known-limitations.md).

### DCS-mode pattern scan for all versions

The `bar4-patch` CMP/JNE scan is currently working for SWE1 v1.5 and
v2.1. A scan that works for every RFM and SWE1 version without relying
on the address `0x001931e4` being stable would be preferable. The
pattern is in the SYMBOL TABLE as `dcs_mode_select`; a sym-lookup based
approach would be cleaner.

---

## Medium priority

### E0-prefixed scancode injection in capture mode

Implement the E0-prefix injection for extended keys in `Alt+K` capture
mode so that `RCtrl`, `RAlt`, and extended navigation keys arrive
correctly on the guest KBC. Affects XINA usability for users who rely on
right-hand modifiers.

### `make install` and packaging

Add a `make install DESTDIR=/usr/local` target and a minimal
`debian/` packaging recipe so Encore can be distributed as a `.deb`.

### Automated regression script

The 7-bundle × 2-mode regression matrix (see
[26-testing-7-bundle-matrix.md](26-testing-7-bundle-matrix.md)) is
currently a manual procedure. A shell script that launches each bundle
with `--headless`, waits for the `FPS:` log line, and exits 0/1 would
make CI feasible.

### Symbol-based patch address resolution

Replace any remaining hardcoded guest addresses in `src/cpu.c` and
`src/io.c` with `sym_lookup()` calls so they survive binary layout
changes across bundle versions.

---

## Low priority

### MAME-style configuration save

Persist `--fullscreen`, `--flipscreen`, and `--bpp` settings to a per-
game config file so they do not need to be re-specified on every launch.

### Snapshot / restore

Save and restore full emulator state (RAM + register file) so a running
session can be resumed without a full re-boot. Unicorn supports context
save/restore via `uc_context_save` / `uc_context_restore`.

### Net bridge for RFM internet leaderboard

`--net-bridge tap0` is wired in the help text but not implemented. RFM
originally supported an internet high-score submission feature via a
modem. Emulating the modem interface and routing to a modern HTTPS
endpoint would restore this functionality.

---

## Maybe-fun

* **Audio visualiser** — overlay the DCS command stream as an on-screen
  histogram, useful for debugging DCS command routing.
* **XINA web terminal** — expose the serial console as WebSocket so
  it can be accessed from a browser without `nc`.
* **Frame recorder** — dump each rendered framebuffer to PNG using the
  already-linked `stb_image_write` so a game session can be reviewed
  offline.
* **Multi-game ROM auto-discover** — scan a directory tree and auto-
  classify ROMs by magic bytes rather than relying on naming conventions.

---

## Cross-references

* Known limitations: [38-known-limitations.md](38-known-limitations.md)
* DCS probe polarity: [14-dcs-probe-polarity.md](14-dcs-probe-polarity.md)
* Patching philosophy: [21-patching-philosophy.md](21-patching-philosophy.md)
* Regression matrix: [26-testing-7-bundle-matrix.md](26-testing-7-bundle-matrix.md)

## Historical research notes

The following items remain open from earlier P2K reverse-engineering
work and would benefit from further investigation. None are blocking
for current Encore functionality, but each would expand what the
emulator can faithfully reproduce.

* **DCS-2 sound-DSP ROM format (`u109` / `u110`).** The on-card ADSP
  ROMs that drive DCS-2 sample playback use a layout that is not yet
  fully documented. Encore bypasses ADSP emulation entirely and
  serves samples from a pre-extracted container, so the ROM format
  is not required for current playback — but documenting it would
  enable a true DCS-2 emulator path.
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

← [Back to documentation index](README.md) · [Back to project README](../README.md)
