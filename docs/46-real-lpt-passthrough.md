# 46 — Connecting a real cabinet through LPT

Encore can pass guest parallel-port traffic directly to a Linux `ppdev` device.
The implementation is in `qemu/p2k-lpt-board.c`.

> Real Pinball 2000 cabinet validation is still pending. Use the emulated LPT
> mode until you are prepared to observe the physical board and stop immediately
> if coils, lamps, or relays behave incorrectly.

## Required hardware

Use a bidirectional parallel port exposed as `/dev/parportN`. An onboard LPT
port or a PCI/PCIe parallel card normally works. A USB printer adapter exposed
as `/dev/usb/lpN` does not provide the register-level bidirectional interface
required by the cabinet.

## Linux setup

```sh
sudo modprobe parport_pc ppdev
sudo rmmod lp 2>/dev/null || true
sudo usermod -aG lp "$USER"
```

Log in again after changing group membership, then verify:

```sh
ls -l /dev/parport0
```

## Run

```sh
scripts/run-qemu.sh \
  --lpt-device /dev/parport0 \
  --cabinet-purist \
  --lpt-trace /tmp/encore-lpt.csv
```

`--cabinet-purist` refuses to start without a real parallel device. This avoids
silently running the keyboard-backed emulated board when cabinet control was
intended.

The direct path uses `PPCLAIM`, negotiates compatibility mode, and forwards
DATA, STATUS and CONTROL accesses with `ppdev` ioctls. It does not need a QEMU
chardev or a separate helper process.

## Validate before play

1. Run the same game and update with `--lpt-device emu --bench` on the cabinet
   host to verify guest timing.
2. Start real passthrough with `--lpt-trace` and the playfield power disabled.
3. Confirm expected register traffic and switch reads.
4. Enable playfield power only when the idle state is correct.
5. Test one switch, lamp and low-risk output at a time.

Do not add artificial LPT delays merely because host execution is fast. Add
pacing only if a physical trace proves a setup/hold or cadence failure.

See [29 — Cabinet testing](29-cabinet-testing-call.md), [37 — Driver-board
protocol](37-power-driver-board-protocol.md), and [47 — Recommended
configuration](47-recommended-configuration.md).

---

← [Documentation index](README.md) · [Project README](../README.md)
