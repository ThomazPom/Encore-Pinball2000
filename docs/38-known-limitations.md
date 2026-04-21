# 38 — Known Limitations

> *This list is based on emulator-only testing. Some entries may turn
> out to be cabinet-only issues — and other cabinet-only issues may
> surface that aren't listed here yet.*

Items that are incomplete, partially working, or require further
research. This is a truthful snapshot; it is not a promise of future
delivery.

---

## RFM v1.2 — pre-XINU crash (1999 build)

RFM v1.2 (`pin2000_50070_0120_06091999`) is the oldest known bundle,
predating the XINU port that all later builds use. It reaches a crash
state before XINU `sysinit` completes. Serial output is visible on the
virtual COM1 console but no graphics are produced.

**Root cause:** Unknown. The crash appears to be in the C++ static
constructor phase, which in v1.2 has a different layout from all later
bundles. Investigation was deferred in favour of the five working RFM
bundles.

**Impact:** 1 of 7 bundles does not boot fully. All other bundles are
unaffected.

---

## `io-handled` DCS mode — audio silent on most bundles

`--dcs-mode io-handled` lets the game run the unmodified PCI detect
probe at `0x1A2ABC`. The probe has inverted polarity (returns 1 when
the probed cell is **not** `0xFFFF`). In `io-handled` mode the emulator
scribbles `0x0000` into that cell so the probe returns 1 and the game
initialises DCS naturally.

In practice audio works in `io-handled` mode only for SWE1 v1.5. For
all other bundles the game takes the I/O-port DCS path (ports
`0x138`–`0x13F`) which requires a fuller handshake sequence that is not
yet fully implemented.

**Reference:** `src/io.c:432–449`, `src/encore.h` — `ENCORE_DCS_IO_HANDLED`.

---

## LPT IO DSP handshake — patchless start pending

The Start button on a real Pinball 2000 cabinet is wired through the
LPT driver board. Getting the Start button to work via the emulated LPT
(without hardcoded CPU patches) requires a complete patchless IO DSP
handshake sequence. This is partially implemented; the SPACE / S key
injection path works but relies on a direct switch injection rather than
a proper LPT protocol response.

---

## E0-prefixed PS/2 scancodes in capture mode

In `Alt+K` keyboard capture mode, keys that require E0-prefix extended
scancodes (right-side `Ctrl`, `Alt`, some navigation keys) are mapped
in the scancode table but the E0 prefix byte is not injected. Only the
bare scancode is sent. This means `RCtrl` and `RAlt` arrive as `LCtrl`
and `LAlt` on the guest side. This is noted in the `--help` text
(`src/main.c:381`).

---

## No graphical debugger or symbolic breakpoint panel

Encore has no built-in debugger UI. If you want to observe or modify
guest state while running, use `--serial-tcp` and the XINA shell
(`nc localhost 4444`). There is no breakpoint, watchpoint, or step
facility exposed to the user.

---

## `--keyboard-tcp` — experimental

The `--keyboard-tcp PORT` option bridges a TCP connection to the guest
PS/2 keyboard controller. It can inject arbitrary scan codes but the
exact encoding expected by the game's keyboard driver has not been
fully characterised. Works for ASCII text input to XINA; complex key
sequences are unreliable.

---

## NVRAM size mismatch on some bundles

The NVRAM image size written by one bundle version may not be accepted
by a different version on restart. If you switch bundle versions,
delete the corresponding `savedata/*.nvram` file to avoid a checksum
error on next boot. See [10-savedata.md](10-savedata.md).

---

## No `make install` target

Encore must be copied manually to its destination. A `make install`
target and a proper packaging recipe are absent. See
[28-build-system.md](28-build-system.md).

---

## Cross-references

* RFM vs SWE1: [35-rfm-vs-swe1.md](35-rfm-vs-swe1.md)
* DCS mode duality: [13-dcs-mode-duality.md](13-dcs-mode-duality.md)
* DCS probe polarity: [14-dcs-probe-polarity.md](14-dcs-probe-polarity.md)
* Future work: [39-future-work.md](39-future-work.md)

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
