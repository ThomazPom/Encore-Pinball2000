# 51 — Power Driver Board: physical primer

This page documents the Pinball 2000 **Power Driver Board** (PDB) as a
physical PCB — its power rails, its parallel-port front-end, the decode
chain that turns a control byte into a register write, the 555-based
blanking watchdog, and a bench bring-up procedure. It complements
[48-lpt-protocol-references.md](48-lpt-protocol-references.md) (which
covers the LPT *protocol*) and [43-cabinet-hardware-primer.md](43-cabinet-hardware-primer.md)
§6 (which briefly mentions the LPT in the context of the whole cabinet).

The intended reader is a developer hooking Encore up to a real cabinet
or a bench-mounted PDB — the level of detail you need to interpret a
logic-analyzer trace or to reason about why a given write fails to
produce a visible output.

## 1. Overview

The PDB is the only piece of the cabinet head's electronics that sits
*outside* the standard PC. The PC motherboard hosts the CPU, the PRISM
PCI card, audio and video; everything that touches the playfield —
lamps, switches, flippers, slingshots, bumpers, popper coils, flashers,
diverters, the knocker, the relay that gates +50 V coil power, and the
zero-cross detector — lives on the PDB.

The PC talks to the PDB through a single DB-25 parallel-port cable
(connector **J100** on the PDB). Bidirectional data, control strobes
and board-ID bits all flow over that one cable. There is no second
out-of-band link.

Power for the PDB does **not** come from the PC supply. The cabinet's
main transformer feeds the PDB directly with the high-voltage rails
(+50 V coil, +18 V AC lamp, +20 V AC flasher) and a +12 V/+5 V logic
rail derived from the same transformer. The PC's AT/ATX supply only
powers the head electronics. The two domains are connected purely
through the LPT cable; there is no shared ground beyond what the cable
carries on pins 18–25.

## 2. LPT front-end and decode chain

The DB-25 cable lands on J100. From there the signals fan out through
a small group of TTL chips that constitute the entire PDB-side
front-end — everything else on the board (lamp drivers, MOSFETs, fuse
sense, the watchdog, the relay) hangs off the outputs of these few
chips.

The chip-level identifiers below come from the published Williams
schematic and are the same designators used by the
[`boilerbots/PinballDiag`](https://github.com/boilerbots/PinballDiag) wiki
when it tells you where to attach a logic-analyzer probe.

| Chip | Role |
| --- | --- |
| **U58** | Buffers the three control strobes coming in from J100 — `Init`, `Strobe`, `Select`. Outputs are the canonical "what the PC just wrote to LPT base+2" view, after PC-side hardware inversion. The wiki recommends probing **U58 pin 8 (Init)**, **pin 4 (Strobe)** and **pin 12 (Select)** as your primary three logic channels. |
| **U3** | Bidirectional data buffer on D0–D7. The "input side" of U3 is what the PC drove during a write; the "output side" goes onto the board's internal data bus. Direction is controlled by `Select` (DB-25 pin 17). The wiki recommends a chip clip on U3 to scope the data lines while running a test. |
| **U7** | The **index register**. Latches the data byte into a 5-bit register on the rising edge of `Init` (DB-25 pin 16). The latched value selects which on-board register the next data write or read will hit. |
| **U2 / U4 / U5 / U6** | Decoder fan-out. The 5-bit index from U7 feeds a one-of-N decoder whose 32 outputs each enable one of the on-board registers — `0x05` selects `SW_COL`, `0x08` selects `LAMP_COL`, `0x0D` selects `SOL_D`, and so on. The decode is gated by `Strobe`; the strobe pulse is what actually clocks the data into the selected register. |

Because U7 is a *latch*, the PC never has to re-send the index value
between successive accesses to the same register — this is what makes
the 6-step write sequence (and the 6-step read sequence) in
[doc 48 §4](48-lpt-protocol-references.md) economical. The index
persists until the PC overwrites it.

The control bits coming in from the PC are inverted by the PC's
parallel-port hardware on the way out the DB-25:

| Logical | PC LPT base+2 bit | Inverted? | PDB-side meaning (after U58) |
| --- | --- | --- | --- |
| `Strobe` | bit 0 | yes | Index Register Decode Output Enable |
| `Init`   | bit 2 | no  | Index Register Latch Clock |
| `Select` | bit 3 | yes | Driver-Board Data Buffer Direction |

So a PC-side `outb(0x29, base+2)` lands on the PDB as "Strobe asserted,
Init de-asserted, Select asserted" — which is exactly the magic value
the documented read sequence uses to flip both the PC's port direction
(bit 5) and the PDB's data buffer direction in a single write.

`boilerbots/PinballDiag`'s `hw.h` codifies this with named constants
(`STROBE = 0x01 // inverted`, `INIT = 0x04`, `SELECT = 0x08 // inverted`,
`DIRECTION = 0x20`) and a shadow `ctl` byte to keep the inversion
arithmetic from leaking into the call sites — see the vendored mirror
at [`docs/references/PinballDiag/hw.h`](references/PinballDiag/hw.h)
and the matching implementation
[`hw.cpp`](references/PinballDiag/hw.cpp).

## 3. The 555 blanking watchdog

The single most important analog circuit on the PDB is a 555-based
watchdog. Its output gates **all** power outputs — lamps, flashers,
solenoids — through a global `BLANKING` line. When `BLANKING` is
asserted, every high-current driver on the board is forced off,
regardless of what the PC writes to the lamp/coil registers.

The 555 is retriggered by **any low-going transition on the decoded
output for index register `0x05` (`SW_COL`, the switch-column
register)**. As long as the PC keeps writing to register `0x05`
faster than the 555's RC time constant (~2.5 ms in the original
design), `BLANKING` stays clear and the output drivers remain enabled.

This is by design and not a bug: it means a hung PC, a crashed game,
or a stalled emulator cannot leave coils energised. If the column
strobe stops, the next column never fires, the 555 times out, every
coil drops within a few milliseconds.

Three direct consequences for emulator work:

1. **The switch-matrix scan is the keepalive.** Any P2K firmware that
   actually runs the playfield is, by definition, hammering register
   `0x05` continuously as part of its switch-matrix scan. There is no
   separate "kick the watchdog" call site to find — the scan *is* the
   kick. If you suppress, slow down or coalesce switch-column writes
   anywhere in the path between guest and wire, you risk asserting
   `BLANKING`.

2. **Bench tools must generate the keepalive themselves.** PinballDiag
   is a standalone diagnostic with no game running, so it ships an
   explicit watchdog thread (vendored:
   [`test_thread.cpp`](references/PinballDiag/test_thread.cpp)) that
   does nothing but pulse `0x05` on a configurable period. The wiki's
   own description: *"If the blanking signal is set nothing else in
   the circuit will work, so this is essential. You can adjust the
   speed of the watchdog to see where your circuit will start to drop
   out."* The author measured drop-out around 1.7 s on his particular
   board, which is much longer than the 2.5 ms documented elsewhere
   — suggesting board-to-board variation in the 555's RC values, or a
   different timing constant in some board revisions.

3. **You can probe the watchdog state from software.** Index register
   `0x0F` (`SW_SYS`) returns a status byte whose **bit 6** mirrors
   `BLANKING-OK`. Reading `0x0F` periodically and asserting
   `(value & 0x40)` is the cheapest possible health check — if it
   ever goes false during normal play, the watchdog has tripped and
   you have a timing problem upstream.

Bit 7 of the same `0x0F` register is the latched **zero-cross**
indicator. The PDB's mains-detect circuit pulses for ~1.5 ms at every
zero crossing of the AC line (so 100 Hz on 50 Hz mains, 120 Hz on
60 Hz). That pulse sets a latch; reading `0x0F` clears it. Coil-firing
code uses zero-cross to schedule lamp dimming and to coordinate
solenoid pulses with the AC waveform, but it is not part of the
watchdog path.

## 4. Power topology and test points

The PDB has named test points for bench bring-up, again per the
PinballDiag wiki. The two most important for first-power-on are:

| Test point | Net | Use |
| --- | --- | --- |
| **TP5** | +12 V logic | Inject +12 V from a current-limited bench supply for board-only testing without the cabinet transformer. PinballDiag's author limits to **0.7 A**; healthy idle draw is **< 0.25 A**. Anything above that is a short-to-find-first situation. |
| **TP6** | GND | Ground reference for TP5 and for all logic-analyzer probes. Also the safe ground reference for measuring coil/lamp/flasher rails when those are present (with appropriate care — the +50 V rail is enough to damage equipment but not normally lethal; the +18 V/+20 V AC rails are not isolated from mains). |

The high-voltage rails (+50 V coil, +18 V lamp, +20 V flasher) are
generated from the cabinet transformer's secondaries through bridge
rectifiers and bulk caps on the PDB itself. They are **not** present
when the board is bench-powered through TP5/TP6 — and that is exactly
the point: the entire control path (LPT decode, index register,
register decode, watchdog, status read-back) can be debugged with
nothing more dangerous than a +12 V bench supply. Coils, lamps and
flashers will not fire because the rails that drive them are absent,
but the digital side will respond correctly to all reads and writes.

## 5. Bench bring-up procedure

This is a condensed procedure synthesised from the PinballDiag wiki
and the `mainwindow.cpp` button order in the upstream repo. It
assumes the board sits on a bench with a bench supply on TP5/TP6
and a logic analyzer with at least 4 digital channels and 1 analog
channel, the parallel cable plugged into J100 and the PC end into a
known-working LPT port (default `0x378`).

1. **Power the board.** Bench supply set to +12 V, current limit
   ~0.7 A. Healthy idle draw is < 0.25 A. If draw spikes against the
   limit immediately, do not proceed — there is a short.
2. **Open the parallel port.** From PC user-space:
   `ioperm(BASE, 3, 1)` for the standard registers and
   `ioperm(BASE+0x402, 1, 1)` for the extended-mode/ECR register at
   that offset. Set the ECR mode to **bidirectional SPP** (write
   `(old & 0x1F) | 0x20` to `BASE+0x402`). The vendored
   [`hw.cpp`](references/PinballDiag/hw.cpp) `HW::init()` is the
   canonical sequence.
3. **Probe the strobes.** Logic-analyzer channels on U58 pins 8
   (`Init`), 4 (`Strobe`), 12 (`Select`). Trigger on `Init` rising
   edge. Toggle each strobe individually from software and confirm
   each one moves on its channel only. PinballDiag's "Test INIT",
   "Test STROBE", "Test SELECT" buttons exist precisely for this
   step.
4. **Probe the data lines.** Chip clip on U3, channels D0–D7. Walk a
   single bit across the data lines (PinballDiag's "Test DATA"
   slider does `outb(1 << n, BASE)` for `n=0..7`). Confirm one bit
   moves at a time. This tests both the cable's data lines and U3's
   input side.
5. **Probe the decode chain.** Trigger control writes 0x00 through
   0x14 ("Test Control" in PinballDiag) and confirm the corresponding
   decoder output (one of U2/U4/U5/U6's 32 outputs) pulses for each
   index value. This is where most subtle PDB faults show up: a bad
   solder joint on U7 will cause the index latch to take wrong
   values; a damaged decoder chip will leave one or more index slots
   permanently dead.
6. **Start the watchdog keepalive.** Write `0x05` continuously at
   sub-2 ms intervals. Read `0x0F` after each write and confirm
   bit 6 (`BLANKING-OK`) stays high. This is the "Watchdog" button
   in PinballDiag and the inner loop of
   [`test_thread.cpp`](references/PinballDiag/test_thread.cpp).
7. **Read the DIP switches.** `read(0x02)` returns the cabinet DIP
   switch settings. This is the cheapest end-to-end test of the
   bidirectional path: data goes out, direction flips (PC and U3),
   data comes back, direction flips back. If this works, every
   register read on the board will work.
8. **Toggle the Health LED and the +50 V relay.** Write `0x30` to
   register `0x0D` (`SOL_D`): bit 4 is the Health LED, bit 5 is the
   relay that gates +50 V to the coil drivers. Both are visible
   feedback (the LED lights up; the relay clicks audibly), and
   neither requires the high-voltage rails to be present.
9. **Lamps.** `0x06`/`0x07` are the lamp row registers, `0x08` is
   the lamp column. PinballDiag's `testLamps()` walks columns,
   writes the row pattern, asserts the column for ~100 µs, then
   moves on. With +18 V AC absent on the bench, the lamp matrix
   will not light, but you can still scope the row/column outputs
   on the lamp-driver side of the board to confirm the scan
   pattern.
10. **Solenoids last.** Registers `0x09`–`0x0E` (excluding the LED
    and relay bits of `0x0D`) drive the coil MOSFETs. **Do not** fire
    these without the +50 V rail intentionally absent or without
    fully understanding which solenoid each bit drives — a coil
    mis-fired against a stuck flipper or a popper at rest can
    damage parts and physically eject pinballs.

## 6. What this means for Encore

Encore's cabinet path (`--lpt-device 0xBASE`) lands at the bottom of
the PDB's decode chain via the same DB-25 cable as anything else.
There is no privileged backdoor: every guest LPT write becomes a
PC-side `outb`, which becomes a J100 transition, which becomes a U58
output, which (eventually) latches a register through the U7 → U2/4/5/6
chain. Anything Encore does that perturbs the *shape* of those wire
transitions — host-side pacing, CTL byte rewriting, batched writes,
direction-flip insertion — is visible on the wire and visible to the
watchdog 555.

A few specific implications, with pointers into the codebase and into
adjacent docs:

* **The XINA driver's switch-matrix scan is the watchdog keepalive.**
  Encore must let it through unmangled. There is no separate keepalive
  thread in the cabinet path (and there should not be one — the
  PinballDiag thread exists only because PinballDiag has no game
  running). See [doc 48 §8 "On a host-side 0x05 keepalive"](48-lpt-protocol-references.md)
  for the full reasoning.
* **Host-side bus pacing is opt-in.** `--lpt-bus-pace` defaults to
  `0` because the guest's own `iodelay` loops already pace the bus
  correctly. Stacking host-side busywait on top of that pacing can
  starve the JIT badly enough to slip the watchdog window. See
  [`src/lpt_pass.c`](../src/lpt_pass.c)'s `lpt_bus_pace()` and the
  rationale in [doc 19 "Bus pacing"](19-real-lpt-passthrough.md).
* **CTL bytes are forwarded verbatim by default.** The guest's
  documented `0x29 → read → 0x00` sequence (the magic that flips both
  the PC port direction and the PDB data buffer direction in a single
  write) reaches the wire intact, matching the documented protocol.
  `--lpt-managed-dir` is a back-compat opt-in for boards where that
  sequence is suspect.
* **`SW_SYS` (`0x0F`) bit 6 is the cheapest possible health check.**
  Reading it periodically and logging when it drops gives an
  unambiguous "the watchdog tripped" signal that does not depend on
  any guess about timing budgets.
* **`SOL_D` (`0x0D`) bits 4 and 5** (Health LED and +50 V relay) are
  the safest visible-feedback writes for a real-cabinet smoke test.
  They produce immediate audible/visible feedback without firing any
  playfield mechanism.

## 7. Cross-references

* [19-real-lpt-passthrough.md](19-real-lpt-passthrough.md) — Encore's
  raw / `ppdev` backends, bus pacing, managed-dir flag.
* [43-cabinet-hardware-primer.md](43-cabinet-hardware-primer.md) — the
  rest of the cabinet (motherboard, PRISM, DCS, CRT, optional NIC).
* [48-lpt-protocol-references.md](48-lpt-protocol-references.md) — the
  LPT protocol, register map, write/read sequences, `0x29` magic and
  the public source documents.
* [50-cpu-clock-mismatch.md](50-cpu-clock-mismatch.md) — why guest
  iodelay loops alone may not produce the wall-clock budgets the PDB
  expects on a fast host.
* [`docs/references/PinballDiag/`](references/PinballDiag/NOTICE.md) —
  vendored BSD-licensed mirror of the diagnostic's source.

## 8. External documentation

* **PinballDiag** —
  [repo](https://github.com/boilerbots/PinballDiag),
  [wiki](https://github.com/boilerbots/PinballDiag/wiki). The wiki's
  "Using the Diagnostic tool" section is the primary source for the
  bench-bring-up procedure summarised in §5; the "Clicking buttons"
  section describes the exact tool stimuli that map onto each
  procedure step.
* **Williams Pinball 2000 Schematics** — archived as a PDF at
  [arcarc.xmission.com](https://arcarc.xmission.com/Pinball/PDF%20Pinball%20Misc/Williams%20Pinball%202000%20Schematics.pdf).
  Williams document number 16-10882, February 1999. Source for the
  J100 connector wiring, the U58/U3/U7/U2/U4/U5/U6 designators used
  in §2, and the analog detail of the 555 blanking watchdog circuit
  in §3.
* **Williams Pinball 2000 Safety Manual** — Williams document
  number 16-10878. Required reading before any work that involves
  re-energising the +50 V coil rail or the +18 V/+20 V AC lamp/flasher
  rails on the bench.
* **Game-specific operator manuals** —
  [Revenge from Mars (PDF)](https://arcarc.xmission.com/Pinball/PDF%20Pinball%20Manuals%20and%20Schematics/Revenge%20from%20Mars%20Operations%20Manual.pdf)
  and the SWE1 manual on
  [ManualsLib](https://www.manualslib.com/manual/1915294/Williams-Pinball-2000-Star-Wars-Episode-I.html).
  Source for switch matrices, lamp matrices, fuse charts and
  per-game playfield part numbers.
* **PinWiki — Pinball 2000 Repair** —
  [pinwiki.com](https://www.pinwiki.com/wiki/index.php/Pinball_2000_Repair).
  Index of community repair notes. Useful for "this capacitor leaked
  and damaged that trace" pattern matching.
* **EEVblog — fixing a P2K Power Driver Board** —
  [eevblog.com forum](https://www.eevblog.com/forum/repair/fixing-a-pinball-2000-power-driver-board-and-did-i-just-discover-a-design-flaw/).
  Long electronics-side repair thread; useful for understanding
  failure modes around the MOSFET drivers.
* **FlipProjets — P2000 driver-board tester (PIC)** —
  [flipprojets.fr](https://www.flipprojets.fr/TestP2000Driver_EN.php).
  Independent hardware tester (PIC 16F452) — useful as a confirmation
  that the public protocol is also implementable in microcontroller
  firmware, not only over LPT from a PC.

Credit: the chip-level identifiers, bench-debug procedure and
watchdog behaviour described here come from public community work
(primarily the PinballDiag wiki and the Williams schematic).
This document collects and contextualises that material for Encore
contributors.

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
