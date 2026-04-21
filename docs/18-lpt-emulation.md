# 18 — LPT Emulation

The Pinball 2000 cabinet communicates with its playfield driver board
through the standard PC parallel port at I/O `0x378` / `0x379` /
`0x37A`. The guest writes an "opcode" byte plus arguments through the
data port; the driver board latches the opcodes and either drives
coils / lamps or returns switch-matrix state on subsequent reads.

Encore emulates the entire LPT protocol in `src/io.c` so that you can
play the game without a real cabinet attached. Real-cabinet
passthrough is covered separately in
[19-real-lpt-passthrough.md](19-real-lpt-passthrough.md).

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## The protocol (as Williams implemented it)

The guest's PinIO driver talks to the driver board in a request/response
style:

```
1. Guest writes OPCODE byte to 0x378.
2. Guest strobes the control register bit (rising edge on STROBE).
3. For write-opcodes: guest writes argument byte(s) to 0x378,
   strobes each; the board latches.
4. For read-opcodes: guest writes OPCODE, then flips the port to
   input (via control register bit 5), reads 0x378; board drives
   the data lines with the response byte.
```

The opcode space is small (~32 distinct opcodes) and covers:

* Switch-matrix column reads (32 columns × 8 bits)
* Coil-bank writes (solenoids, flashers)
* Lamp-matrix writes
* Sound-board bridge (legacy)
* Initialisation / health-check handshakes

Encore implements every opcode the game actually issues during boot
and play. A small number of vendor-specific test opcodes return zero
and have never been observed in the field.

## The switch matrix

A Pinball 2000 playfield has up to 96 switches arranged in a 12-column,
8-bit matrix. The guest polls each column cyclically (usually every
~2 ms via a timer task). Encore maintains two parallel matrices:

```c
static uint8_t s_phys_matrix[12];     /* physical state from host keys */
static uint8_t s_logical_matrix[12];  /* debounced, latched for read */
```

SDL keydown events set bits in `s_phys_matrix`; the LPT opcode
handlers publish `s_logical_matrix` to the guest. See
[36-cli-keyboard-guide.md](36-cli-keyboard-guide.md) for the key
binding layout.

## Column polarity

Different matrix rows use different active-HIGH / active-LOW
conventions:

| Column | Polarity | Notes |
|---|---|---|
| 0 | active-LOW | Start button, buy-in, coin-slot electronics |
| 1 | active-HIGH | Cabinet flippers, plunger |
| 2..4 | active-LOW | Playfield switches |
| 5..8 | active-LOW | Playfield switches |
| 9 | active-LOW | Coin-door panel (service/test/menu up/down) |
| 10 | active-LOW | Cabinet buttons (flippers / action buttons) |
| 11 | reserved | — |

This is baked into `lpt_inject_switch()` in `io.c`. Bugs here cause
buttons to register as "held" at idle and vice versa — we had an
off-by-one in the switch decode that took weeks to track down
(commit `a925919 LPT: fix off-by-one in switch table decode`).

## Coin door interlock

Column 9 bit 7 is the coin-door closed interlock. The game enters
"service mode" when the door is open (bit cleared). F4 toggles this
bit via `lpt_toggle_coin_door()`, visible in the coin-door panel
behaviour:

* Door closed (default): buttons 1..4 = Service Credits, Volume −,
  Volume +, Begin Test.
* Door open (F4): buttons 1..4 = Escape, Menu Down, Menu Up, Enter.

This is the real hardware behaviour, not an Encore quirk; we just
expose the interlock toggle via a host key.

## Start button injection

The Start button sits at column 0 bit 2 (`Phys[0].b2`). SPACE / S
raise it via `lpt_set_start_button()`. The game expects a rising
edge — we hold the bit for one frame, then release. Mashing the key
queues multiple edges.

## Credit injection

F10 / C presses call `lpt_inject_switch(col=0, data=set-coin-bit)` for
`COIN_HIGH_FRAMES = 6` frames (≈ 100 ms at 60 FPS), matching the
minimum pulse width Williams validated for. Multiple presses are
queued so a fast-mash adds multiple credits.

## Service menu navigation

With the coin door closed:

* ESC / LEFT → `Phys[9].b0` (service credits)
* DOWN / KP_− → `Phys[9].b1` (volume down)
* UP / KP_+ → `Phys[9].b2` (volume up)
* RIGHT / ENTER / KP_ENTER → `Phys[9].b3` (begin test)

With the door open:

* same keys, different game-level meaning.

The bindings are identical because the real coin-door panel is four
physical buttons whose interpretation depends on interlock state.

## The trace window

`F11` (or `ENCORE_LPT_TRACE=1`) toggles an LPT-trace log window that
records every opcode and response for 10 seconds. Useful for figuring
out why a new playfield switch isn't recognised. Output goes to
stderr in a timestamped format.

## F12 — guest switch-state dump

Pressing F12 walks the guest's in-memory switch table (located via a
known pointer at `0x2EBFA0` for SWE1) and prints the byte at every
column. This lets you confirm that a switch you are holding on the
host is actually making it all the way to the game's internal state.

## Auto-activation gate

The LPT emulation starts *inactive* at boot:
`g_emu.lpt_active = false`. This is important — during early BIOS
probes, the LPT must read `0xFF` (absent), otherwise the guest takes a
different code path and never boots. When XINU boots (detected via a
BAR2 write at offset ≥ 0x1B14 or the UART "XINU" banner),
`lpt_activate()` flips the switch and the emulated port starts
responding.

In `--lpt-device` mode this gate still applies — the real port is
opened but kept guest-invisible until activation, to preserve the
exact same bring-up sequence.

## The "driver disassembly"

All of the above is extracted from a 1:1 study of the driver binary
shipped with the real hardware. Every opcode number, every polarity
decision, every timing threshold matches the driver's code. The
disassembly is not included in the repository (for clean-room
reasons), but its consequences are all faithfully reproduced.
