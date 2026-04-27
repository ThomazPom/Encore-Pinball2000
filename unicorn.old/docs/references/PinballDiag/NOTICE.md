# Vendored: PinballDiag

Mirror of selected source files from
[github.com/boilerbots/PinballDiag](https://github.com/boilerbots/PinballDiag)
(default branch `master`), kept here so that the documented Pinball 2000
Power-Driver-Board parallel-port protocol stays available to Encore even if
the upstream repository disappears.

- **Upstream repo:** https://github.com/boilerbots/PinballDiag
- **Upstream wiki:** https://github.com/boilerbots/PinballDiag/wiki
- **Upstream announcement:** https://pinside.com/pinball/forum/topic/pinball-2000-diagnostic-program-for-power-driver-board
- **License:** BSD 3-Clause "New" or "Revised" — see [`LICENSE.txt`](LICENSE.txt).
  Copyright (c) 2016, Curt Meyers. All rights reserved.
- **Mirrored on:** 2026-04-24
- **Files included:** `hw.cpp`, `hw.h`, `test.cpp`, `test.h`, `test_thread.cpp`,
  `test_thread.h`, `main.cpp`, `README.md`, `LICENSE.txt`.
- **Files excluded:** Qt UI sources (`mainwindow.*`, `*.ui`, `PinballDiag.pro`,
  `img/`) — not relevant for Encore reference.

This code is **not** built or linked into Encore. It is a documentation
artifact: the canonical worked example of how a real Pinball 2000 Power Driver
Board is driven from Linux user-space over the parallel port.

For the protocol summary that uses these files as reference, see
[`docs/48-lpt-protocol-references.md`](../../48-lpt-protocol-references.md).

## Why this matters for Encore

The single most actionable fact from this vendored code is in
[`test_thread.cpp`](test_thread.cpp) and [`hw.cpp`](hw.cpp): the watchdog
keepalive loop pulses index register `0x05` (Switch-Column) within ~2 ms,
because the on-board blanking circuit asserts after ~2.5 ms of silence and
disables every power output. That is the most likely root cause of the
`pci_watchdog_bone()` fatal observed when running Encore against a real
cabinet.

The `HW::control` / `HW::writeData` pair in [`hw.cpp`](hw.cpp) is also the
authoritative example of which PC parallel-port control bits are inverted at
the hardware level (`STROBE=0x01` and `SELECT=0x08` are inverted, `INIT=0x04`
is not — see [`hw.h`](hw.h)). Encore's `src/lpt_pass.c` should be checked
against these conventions before any real-cabinet protocol work.

## Local modifications

None. Files are byte-identical to the upstream master branch as of the mirror
date above. Re-fetch with:

```sh
for f in hw.cpp hw.h test.cpp test.h test_thread.cpp test_thread.h \
         main.cpp README.md LICENSE.txt; do
  curl -fsSL "https://raw.githubusercontent.com/boilerbots/PinballDiag/master/$f" \
    -o "docs/references/PinballDiag/$f"
done
```
