# 38 — Known Limitations

> *This list is based on emulator-only testing. Some entries may turn
> out to be cabinet-only issues — and other cabinet-only issues may
> surface that aren't listed here yet.*

Items that are incomplete, partially working, or require further
research. This is a truthful snapshot; it is not a promise of future
delivery.

---

## Base ROMs alone do not boot

The chip ROMs under `roms/` do not reach a usable state without a
flashed update overlay. Launching `--game rfm` with `--update none`
halts at a XINA panic:

```
sysinit: game code overlaps 640k - 1024k hole
```

followed by an infinite loop (`jmp $`). `--game swe1 --update none`
reaches attract-mode code paths but never wires up DCS sound.

This mirrors real-hardware behaviour: no shipping cabinet ran with
just the chip ROMs — every cabinet had at least one update flashed.
Encore therefore auto-selects `--update latest` when `--game` is used
without an explicit `--update`. Pass `--update none` to opt out.

---

## Reference-only update bundles (below chip-ROM baseline)

The bundled chip ROMs are at a fixed baseline: **SWE1 v1.5** and
**RFM v1.6 (r2)**. An on-cabinet update can only *upgrade* flash,
never downgrade the underlying chip ROMs. Bundles below the baseline
— SWE1 v1.3 / v1.4 and RFM v1.2 / v1.4 / v1.5 — are therefore
**reference-only**:

* They ship on disk for historical interest (so you can see what
  shipped earlier and inspect the `.exe` self-extractors under
  `updates/exe-sources/`).
* Encore does not claim they reach a usable state.
* Failures under these versions are not tracked as bugs and will
  not be investigated.

RFM v1.2 in particular predates the XINU port and uses r1 chip ROMs
(`rfm_u100.rom` / `rfm_u101.rom`) with a different memory layout.

See [26-testing-bundle-matrix.md](26-testing-bundle-matrix.md#scope-policy-in-scope-vs-reference-only)
for the full scope table.

---

## `io-handled` DCS mode — alternate audio path

`--dcs-mode io-handled` lets the game run the unmodified PCI detect
probe at `0x1A2ABC`. The probe has inverted polarity (returns 1 when
the probed cell is **not** `0xFFFF`). In `io-handled` mode the emulator
scribbles `0x0000` into that cell so the probe returns 1 and the game
initialises DCS naturally, then routes commands through the emulated
I/O ports `0x138`–`0x13F` rather than a BAR4 byte-patch.

Under in-scope bundles (SWE1 v1.5, v2.1; RFM v1.6, v1.8, v2.5, v2.6)
both `--dcs-mode bar4-patch` (default) and `--dcs-mode io-handled`
pass the headless regression with DCS command writes observed. The
`bar4-patch` path remains the default and the primary delivery
because it has accumulated the most testing hours.

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

## v2.1 community-update attract clock shows wrong date

The community v2.1 firmware updates (SWE1 0210, RFM 0210) draw a
wall-clock in the lower-right of the attract screen. The clock display
is implemented entirely in those community ROMs — earlier official
Williams releases (SWE1 ≤ 0150, RFM ≤ 0180) do not show it.

The displayed year, day-of-week and AM/PM do **not** match the host
system clock that Encore feeds through the CMOS/RTC registers. Empirical
probing of CMOS register `0x09` (year) shows v2.1 maps it through an
internal table that is not a simple offset:

| `cmos[0x09]` (BCD) | displayed year |
| --- | --- |
| `0x01` | 2001 |
| `0x13` | 2051 |
| `0x14` | 2025 |
| `0x25` | 2027 |
| `0x26` | 2001 |

The day-of-week is recomputed from the (mis-read) year by the firmware,
so feeding the correct day register has no effect, and AM/PM is
similarly inverted in some boots. Because the relationship is not
monotonic, no single CMOS-side compensation makes the v2.1 clock honest
for an arbitrary host year.

Encore feeds the true host time to the CMOS registers exactly as
Williams' BIOS expected; this is a community-firmware quirk, not an
emulator bug. Run any official update (e.g. `--update 0150` for SWE1,
`--update 0180` for RFM) and the attract clock disappears entirely.

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
