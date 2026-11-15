# Web archive: FlipProjets — Tester for P2000 driver board

> **Mirror policy:** External pages may disappear without notice. This legacy
> extract reproduces the technically relevant text of the source page below.
> Original copyright remains with the page authors.
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

> [!CAUTION]
> This is descriptive evidence, not a reproducible design. The page did not
> publish firmware, a schematic, PCB files or a protocol description. A
> 2026-11-15 check of the site's download catalogue, the local preservation
> corpora and the authors' discoverable public GitHub repositories found no
> accompanying P2000 tester files. The controller therefore existed, but its
> implementation is not preserved here.

## What this establishes

The page is independent evidence that its authors built an autonomous fixture
around a 20 MHz PIC 16F452, an LCD and connector-specific LED/button panels to
exercise a Pinball 2000 power-driver board. It is useful as a historical design
lead and as evidence that the original PC was not required for a bench test.

It does **not** establish the exact bus timings, control-bit polarity, register
semantics or safety behavior. For those details, the BSD-licensed
[PinballDiag source](../PinballDiag/NOTICE.md) is the stronger reference: its
code exposes the parallel-port transactions, register accesses and watchdog
keepalive used against a real board.

Encore's current interpretation remains in the
[LPT driver-board guide](../../26-lpt-board.md).
