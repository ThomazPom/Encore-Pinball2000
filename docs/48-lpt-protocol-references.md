# 48 — Real Power-Driver-Board LPT protocol: public references

Status: research notes, **not yet validated against a real Pinball 2000 cabinet**.
This page collects the public documentation for the real PB2K Power Driver Board
parallel-port protocol so that future work on Encore's `--lpt-device` raw backend
(see [19-real-lpt-passthrough.md](19-real-lpt-passthrough.md)) can be aligned with
what the hardware actually expects.

The current emulated decoder in `src/io.c` and the raw passthrough in
`src/lpt_pass.c` were built without access to a real driver board. They are
expected to need revision once tested on hardware. Treat everything below as the
documented target, not as a description of Encore's current behavior.

## 1. Primary protocol sources

| Source | What it gives |
| --- | --- |
| ["Making Your Own Pin2000 Game" excerpt on VPUniverse](https://vpuniverse.com/forums/topic/1631-what-could-we-do-in-theory-with-pin2k-with-pinbox-and-vp/) | English text of the protocol: DB25 pinout, register map, write & read sequences, blanking/watchdog behavior, zero-cross. |
| [Bible Marvin — Williams/Bally P2000 (FR)](https://flipjuke.fr/redactor/27827/Bible_Marvin_-_Williams-Bally_P2000_-_FR.pdf) (≈ pp. 85–89) | The original French version of the same protocol text. |
| [Williams Pinball 2000 Schematics](https://arcarc.xmission.com/Pinball/PDF%20Pinball%20Misc/Williams%20Pinball%202000%20Schematics.pdf) | Driver-board schematics, J100 / DB25 wiring, signal names. |

## 2. Public reference implementation

[`boilerbots/PinballDiag`](https://github.com/boilerbots/PinballDiag)
([wiki](https://github.com/boilerbots/PinballDiag/wiki)) is a Linux C++
diagnostic that drives a real Power Driver Board over the parallel port:

- Uses `ioperm()` + raw `inb`/`outb`. Also writes the extended mode register at
  `BASE + 0x402` to put the port into the right mode.
- Treats `STROBE` (`0x01`) and `SELECT` (`0x08`) as **inverted** PC control
  bits — be careful when comparing pseudocode below to the actual outb values.
- Uses a busy-wait on `CLOCK_MONOTONIC_RAW` for sub-microsecond strobe holds.
- Includes a watchdog-keepalive loop that hammers register `0x05` and a lamp
  test that uses ~100 µs column dwell.

Most useful files: [`hw.cpp`](https://raw.githubusercontent.com/boilerbots/PinballDiag/master/hw.cpp),
[`hw.h`](https://raw.githubusercontent.com/boilerbots/PinballDiag/master/hw.h),
[`test.cpp`](https://raw.githubusercontent.com/boilerbots/PinballDiag/master/test.cpp),
[`test_thread.cpp`](https://raw.githubusercontent.com/boilerbots/PinballDiag/master/test_thread.cpp).

## 3. Parallel-port mapping (logical view)

| LPT offset | Direction | P2K usage |
| --- | --- | --- |
| `base + 0` | read / write, **bidirectional** | D0–D7 data to/from the I/O board |
| `base + 1` | read | Board ID bits (status lines repurposed) |
| `base + 2` | write | Control: index latch clock, decode enable, data direction |

Control-register bits (logical, before PC inversion):

| Bit | DB25 | P2K meaning |
| --- | --- | --- |
| D0 | Strobe (pin 1) | Index Register Decode Output Enable |
| D2 | Init   (pin 16) | Index Register Latch Clock |
| D3 | Select (pin 17) | Driver-Board Data Buffer Direction |
| D5 | (PC internal) | PC parallel-port direction on most chipsets |

The PC parallel-port standard inverts `Strobe` and `Select` in hardware, so the
actual outb values you write differ from the logical pseudocode. PinballDiag
keeps this straight via a `ctl` shadow with named bits.

## 4. Documented sequences (logical pseudocode)

Write `value` into board register `index`:

```
outb(index, base+0)
outb(0x04,  base+2)   # Init high  → latch index clock high
outb(0x00,  base+2)   # Init low   → latch index
outb(value, base+0)
outb(0x01,  base+2)   # Strobe → enable selected register decode
outb(0x00,  base+2)   # Strobe off
```

Read board register `index`:

```
outb(index, base+0)
outb(0x04,  base+2)
outb(0x00,  base+2)
outb(0x29,  base+2)   # 0b00101001: decode enable + buffer-dir + PC dir
value = inb(base+0)
outb(0x00,  base+2)
```

`0x29` flips both the host parallel-port direction (bit 5) and the driver-board
data buffer direction (bit 3) plus enables decode (bit 0), in one write.

## 5. Index register map

| Idx | Dir | Name |
| --- | --- | --- |
| `0x00` | R | Switch — Coin door |
| `0x01` | R | Switch — Flipper |
| `0x02` | R | Switch — Dip |
| `0x03` | R | Switch — EOS / Diagnostic |
| `0x04` | R | Switch — Row |
| `0x05` | W | **Switch — Column** (also blanking/watchdog refresh) |
| `0x06` | W | Lamp Row A |
| `0x07` | W | Lamp Row B |
| `0x08` | W | Lamp Column |
| `0x09` | W | Solenoid C group |
| `0x0A` | W | Solenoid B group |
| `0x0B` | W | Solenoid A group |
| `0x0C` | W | Solenoid Flipper |
| `0x0D` | W | Solenoid D group (D4 = Health LED, D5 = Power Relay) |
| `0x0E` | W | Solenoid Logic |
| `0x0F` | R | Switch — System (D6 = Blanking-OK, D7 = Zero-Cross latch) |
| `0x10` | R | Lamp Test A |
| `0x11` | R | Lamp Test B |
| `0x12` | R | Fuse Test A |
| `0x13` | R | Fuse Test B |
| `0x14`–`0x1F` | — | Unused (reads return all-1, writes ignored) |

## 6. Blanking / watchdog (the most important timing constraint)

Writing index `0x05` strobes the switch-matrix columns. Each strobe also
refreshes the blanking/watchdog circuit on the driver board.

If column strobes stop for **~2.5 ms**, blanking is asserted and **all power
I/O drivers are disabled** — lamps go dark, solenoids stop firing, and reads
of `0x0F` show `D6 = 0`.

This is the single most likely explanation for `pci_watchdog_bone()` firing on
a real cabinet: the guest's switch-column strobing is reaching the host LPT
too slowly or in the wrong shape to keep blanking cleared.

The PinballDiag watchdog thread shows the minimum keepalive pattern:
periodically write index `0x05` plus any column data within the 2.5 ms window.

Other documented timing notes:

- Lamp-missing/burned-out test: read lamp-test register **150 µs** after
  asserting the column/row.
- Shorted-lamp diagnostic: same path with **300 µs** dwell.
- Zero-cross pulse from the line-AC detector is ~1.5 ms wide; reading `0x0F`
  clears the latch.

## 7. Suggested first-contact tests on real hardware

In rough order of safety and diagnostic value:

1. **Board ID** via `inb(base+1)` — confirms wiring.
2. **DIP switches** via read of index `0x02` — confirms latch + direction
   change + bidirectional data path.
3. **Switch-System** read of `0x0F` — confirms `D6` blanking flag and `D7`
   zero-cross behavior.
4. **Blanking heartbeat** — repeatedly write `0x05` within 2 ms; verify `0x0F`
   bit 6 stays high.
5. **Health LED / relay** — write `0x30` to register `0x0D` (PinballDiag does
   exactly this). Visible feedback on the board.
6. **Lamps** via `0x06`/`0x07`/`0x08`. Use the PinballDiag lamp test as
   reference.
7. **Solenoids last.** Coil drive can damage hardware if mis-sequenced; do not
   touch `0x09`–`0x0E` (except the LED/relay bits of `0x0D`) until the
   read-back path and blanking are confirmed.

## 8. What this means for Encore today

- The current raw backend (`--lpt-device 0xBASE`) successfully opens on a
  tester's PCIe card at `0x3100`. That part is no longer the bottleneck.
- The current code path generates LPT writes from the guest's port I/O. It
  does **not** yet implement an explicit blanking heartbeat from the host
  side. Whether the guest's natural XINA traffic produces fast-enough `0x05`
  strobes through the host's LPT card is unknown until measured on real
  hardware (the µs sampler added on the experimental branch is a first step).
- The `--cabinet-purist` flag (experimental branch) disables the in-emulator
  watchdog suppression so the original code path is exercised end-to-end on
  real hardware, instead of being short-circuited by the boot patch.

Implementing a host-side `0x05` keepalive will probably be needed before a
real cabinet can boot past `pci_watchdog_bone()`. That work is intentionally
deferred until someone with a spare driver board can validate it on a logic
analyzer, as recommended in the PinballDiag wiki.

## 9. Other useful pages

- [PinballDiag announcement on Pinside](https://pinside.com/pinball/forum/topic/pinball-2000-diagnostic-program-for-power-driver-board)
- [PinWiki — Pinball 2000 Repair](https://www.pinwiki.com/wiki/index.php/Pinball_2000_Repair)
- [Gerwiki — XINA / P2K monitor commands](https://www.gerwiki.de/xina/p2k)
- [EEVblog — fixing a P2K Power Driver Board](https://www.eevblog.com/forum/repair/fixing-a-pinball-2000-power-driver-board-and-did-i-just-discover-a-design-flaw/)
- [FlipProjets — P2000 driver-board tester (PIC)](https://www.flipprojets.fr/TestP2000Driver_EN.php)
- RFM operations manual:
  [arcarc PDF](https://arcarc.xmission.com/Pinball/PDF%20Pinball%20Manuals%20and%20Schematics/Revenge%20from%20Mars%20Operations%20Manual.pdf)
- SWE1 on ManualsLib:
  [link](https://www.manualslib.com/manual/1915294/Williams-Pinball-2000-Star-Wars-Episode-I.html)

Credit: the existence and shape of this register map / sequence text is
not original to this project; it was located by community research and is
reproduced here only as a working reference for hardware testing.
