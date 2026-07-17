# 26 — LPT driver-board interface

The Pinball 2000 software talks to the playfield through parallel-port registers
at `0x378–0x37a`. Encore implements those registers in
`qemu/p2k-lpt-board.c`.

## Modes

| Command | Behavior |
|---|---|
| `scripts/run-qemu.sh` | Emulated board with keyboard-controlled switches. |
| `scripts/run-qemu.sh --lpt-device none` | Disable the board interface. |
| `scripts/run-qemu.sh --lpt-device /dev/parport0 --cabinet-purist` | Forward accesses to a real Linux `ppdev` port. |

The emulated path keeps switch inputs separate from lamp and output latches.
Writes to output rows therefore cannot feed back as phantom switches.

## Keyboard switches

Use `scripts/run-qemu.sh --help` for the complete current mapping. Important
service controls include the coin-door switch, volume down, volume up and enter.
They update the emulated switch matrix; they are not PS/2 keys delivered to the
guest.

## Start input and game-start checks

The desktop Start key is mapped to Williams switch `0x02`: logical column 0,
bit 2. In Encore this is stored in `s_switch_matrix[1]` because the board
protocol's one-hot column decoder uses slot 1 for column 0. That one-based slot
is easy to misread; moving Start to matrix index 0 would be a regression, not a
fix.

Receiving the Start switch does not guarantee that the running game accepts
it. The game software also checks that:

- the pricing system has awarded at least one credit, or free play is enabled;
- the audio board reports ready;
- Slam Tilt is inactive;
- the trough and other ball devices pass their start audit;
- tournament and game-specific start hooks allow the game.

> [!IMPORTANT]
> A coin-switch pulse is not itself a credit. Pricing adjustments decide how
> many pulses award one credit, and closely repeated host keys can overlap the
> active emulated pulse.

A successful transition can be confirmed from the XINU console with
`game info`: `m_game_over False` means the game left attract mode. A later ball
search is a separate limitation. The emulated board does not yet move a ball
from the trough to the shooter lane in response to coils, so that behavior does
not indicate a broken Start switch.

## What to measure

Run a normal graphical benchmark:

```sh
scripts/run-qemu.sh --bench
```

The report includes:

- guest-clock speed from `sleep 10`;
- current IRQ0 delivery and jitter;
- LPT DATA writes per second;
- PDB05 median, p95, p99 and worst gaps.

Use steady-state values, not boot totals. Compare the guest clock with the
requested `--speed-target`; compare PDB05 tail gaps with physical-port traces.

`--lpt-trace <file>` records register accesses for protocol diagnosis. Timing
audit panels do not count the direct `ppdev` early-return path, so physical-port
validation must use the trace and external observation.

## Real cabinet

Real passthrough exists but still requires physical validation.

> [!WARNING]
> Start with playfield power disabled and capture real-port traffic before
> enabling coils or lamps.

Details: [real LPT passthrough](46-real-lpt-passthrough.md).

---

← [Documentation index](README.md) · [Project README](../README.md)
