# 38 — Known Limitations

> *This list is based on emulator-only testing. Some entries may turn
> out to be cabinet-only issues — and other cabinet-only issues may
> surface that aren't listed here yet.*

Items that are incomplete, partially working, or require further
research. This is a truthful snapshot; it is not a promise of future
delivery.

---

## Base ROMs alone do not boot to a usable game

The chip ROMs under `roms/` do not reach a usable *game* state
without a flashed update overlay. They still boot far enough for
the BIOS/XINU to come up and draw frames — what they can't do is
start the attract-mode application or DCS audio, because those
live in the flash overlay.

Current behaviour (with the r2-ROM default fix **and** the staged-scribble fix in place):

| Invocation                                           | Observed over 15 s headless                                 | Why |
|------------------------------------------------------|-------------------------------------------------------------|-----|
| `--game swe1 --update none` (default `bar4-patch`)   | video boots, **no DCS writes**, no attract                  | `DCS-mode pattern absent` — the 5-byte prologue lives only in the update bundle |
| `--game rfm --update none` (default `bar4-patch`)    | video boots, **no DCS writes**, no attract                  | Same |
| `--game swe1 --update none --dcs-mode io-handled`    | video boots **and** DCS writes fire (30 in 15 s window)     | No prologue needed — natural probe activates DCS post-xinu_ready |
| `--game rfm  --update none --dcs-mode io-handled`    | video boots **and** DCS writes fire                         | Same |

This still mirrors real-hardware behaviour in that no shipping
cabinet ran with just the chip ROMs; Encore therefore auto-selects
`--update latest` when `--game` is used without an explicit
`--update`. But `--update none --dcs-mode io-handled` is no longer
the dead-end it used to be — the base chip ROM set contains enough
DCS driver code to answer the probe and emit commands.

> **Historical note:** prior to the r2-ROM default fix,
> `--game rfm --update none` produced `exec_count ≈ 2 000` and an
> immediate stall. The loader was selecting the legacy r1 chip ROMs
> because the r2 preference required a `_15` substring in the
> savedata id, which is never present on `--no-savedata` fresh
> boots. That bug is fixed — RFM bank 0 now prefers r2 chips
> unconditionally, which is what shipped hardware actually uses.
> See [35-rfm-vs-swe1.md](35-rfm-vs-swe1.md#chip-rom-revisions).

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

RFM v1.2 in particular predates the XINU port. It targets the r1
chip hardware revision (`rfm_u100.rom` / `rfm_u101.rom`), but under
Encore it is loaded against the r2 chips (the unconditional
bank-0 preference) and boots cleanly under **both** `--dcs-mode
bar4-patch` and `--dcs-mode io-handled` now that the scribble is
staged around `xinu_ready` — see
[13-dcs-mode-duality.md](13-dcs-mode-duality.md).

See [26-testing-bundle-matrix.md](26-testing-bundle-matrix.md#scope-policy-in-scope-vs-reference-only)
for the full scope table.

---

## `io-handled` DCS mode — alternate audio path

`--dcs-mode io-handled` lets the game run the unmodified PCI detect
probe at `0x1A2ABC`. The probe has inverted polarity (returns 1 when
the probed cell is **not** `0xFFFF`). In `io-handled` mode the
emulator scribbles `0x0000FFFF` into that cell pre-`xinu_ready`
(to keep early-boot sentinel checks happy) and flips to `0x0000`
after `xinu_ready` (so the natural DCS probe returns 1 and the
game initialises DCS). Commands then route through the emulated
I/O ports `0x138`–`0x13F` rather than a BAR4 byte-patch.

Under every in-scope bundle **both** modes pass the headless
regression with the same `[dcs] WR` count. `io-handled` is
strictly more capable on the three "pattern absent" cases
(`swe1 --update 1.3`, `swe1/rfm --update none`) where
`bar4-patch` has no prologue to rewrite. `bar4-patch` remains
the default for historical compatibility; switching the default
to `io-handled` is under consideration. See the full matrix in
[26-testing-bundle-matrix.md](26-testing-bundle-matrix.md#latest-observed-results-headless-dummy-sdl-drivers-15-s-timeout).

**Reference:** `src/io.c` `apply_sgc_patches` (prime), `src/cpu.c`
staged scribble (post-xinu_ready flip), `src/encore.h` —
`ENCORE_DCS_IO_HANDLED`.

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

This limitation only applies if you load a community-built update
(notably mypinballs's SWE1 v2.10 / RFM v2.10 — see
[47-community-updates.md](47-community-updates.md) for how to install
them) — the original Williams update bundles shipped with the repo do
not exhibit it.

Those community firmwares draw a wall-clock in the lower-right of the
attract screen. The clock display is implemented entirely in those
ROMs — earlier official Williams releases (SWE1 ≤ 0150, RFM ≤ 0180)
do not show it.

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
`--update 0180` for RFM — both shipped with the repo) and the attract
clock disappears entirely.

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
