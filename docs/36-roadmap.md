# 36 — Roadmap

Encore boots SWE1 and RFM, renders graphics, plays DCS audio, provides desktop
controls, persists machine state, loads updates, and exposes Linux LPT
passthrough. The remaining goal is physical Pinball 2000 cabinet validation.

## Before cabinet power-on

1. Run the locally installed update and DCS-engine validation matrix.
2. Run `scripts/run-qemu.sh --bench` on the cabinet host and record steady-state
   guest clock, IRQ0, LPT rate and PDB05 gaps.
3. Exercise coins, start, flippers, service buttons, volume and door state with
   the emulated LPT board.
4. Verify `--lpt-device required` refuses to start without a recognized board.
5. Capture real-port traffic with playfield power disabled.

## Physical validation

Test switch reads, lamps, low-risk outputs, service controls, DCS audio and
sustained gameplay one class at a time. Record the host, parallel-port card,
game, update, DCS engine, timing mode and trace artifact.

Record failures with the corresponding LPT trace so they can be tied to a
specific device behavior.

---

← [Documentation index](README.md) · [Project README](../README.md)
