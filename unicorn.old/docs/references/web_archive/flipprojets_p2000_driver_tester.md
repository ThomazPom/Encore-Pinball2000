# Web archive: FlipProjets — Tester for P2000 driver board

> **Mirror policy:** External pages may disappear without notice. This file
> reproduces the technically relevant text of the source page below for the
> sole purpose of preserving Pinball 2000 hardware-test know-how that
> Encore relies on. Original copyright remains with the page authors.
>
> - **Source:** <https://www.flipprojets.fr/TestP2000Driver_EN.php>
> - **Mirrored on:** 2026-04-24
> - **Authors:** FlipProjets (see source page)

## Tester for P2000 driver board

A simple PIC 16F452 based mounting to test the Pinball 2000 driver
boards. The µ-controller is a Microchip PIC 16F452 running at 20 MHz.

Electric power is autonomous and is used both for the driver card and the
main module. The main module is connected to the driver board by ribbon
cable. The user interface consists of an LCD screen and a few buttons.

Several additional turntables equipped with LEDs and buttons fit to the
standard connectors of the driver card. They visualize the state of the
outputs and control the various inputs (flippers, coins, contacts).

Distribution: currently unavailable; the authors mention possible future
free or paid distribution.

---

## Why this is preserved here

Encore's `--lpt-device` raw I/O backend talks the parallel-port protocol
to the Power Driver Board. The FlipProjets tester is independent evidence
that:

- A 20 MHz PIC microcontroller is sufficient hardware to drive the board
  in a useful way — exotic real-time silicon is not required.
- The board's external interface (DB25-style ribbon, fixed connector
  pinout) is stable enough that an MCU-side implementation can talk to it
  without the original Cyrix MediaGX PC.

Both facts support the design choices documented in
[`docs/48-lpt-protocol-references.md`](../../48-lpt-protocol-references.md).
