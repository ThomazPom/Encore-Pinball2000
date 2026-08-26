# 46 — Connecting a real cabinet through LPT

Encore can pass guest parallel-port traffic directly to a Linux `ppdev` device.
The implementation is in `qemu/p2k-lpt-board.c`.

Normal use requires no port number:

```bash
scripts/run-qemu.sh --lpt-device auto --game auto
```

Encore scans `/dev/parportN` inside the binary, accepts only a recognized
Pinball 2000 response, then identifies SWE1 or RFM. With the cable disconnected
it returns to the emulated board. To test keyboard controls while hardware
remains connected, use `--lpt-device emulated`. For strict cabinet validation,
use `--lpt-device required`.

> [!WARNING]
> Real Pinball 2000 cabinet validation is still pending. Begin with playfield
> power disabled and stop immediately if outputs behave incorrectly.

## Required hardware

Use a bidirectional parallel port exposed through Linux `ppdev` as
`/dev/parportN`. `/dev/usb/lpN` is a printer interface, not the `ppdev`
register interface used by Encore.

## Linux setup

```sh
sudo modprobe parport_pc ppdev
sudo rmmod lp 2>/dev/null || true
sudo usermod -aG lp "$USER"
```

The runner activates newly granted `lp` membership for its current process, so
an immediate manual launch does not require a new login. Future sessions inherit
the group normally. Verify the device with:

```sh
ls -l /dev/parport0
```

## Run

```sh
scripts/run-qemu.sh \
  --lpt-device /dev/parport0 \
  --lpt-trace /tmp/encore-lpt.csv
```

An explicit device is authoritative and automatically disables emulated
cabinet keys while retaining host-only F1 quit, F2 flip and F3 screenshot.
Use `--lpt-device disconnected` for an artificial open-bus
ROM diagnostic.

## Experimental hybrid input

This branch can add keyboard switch closures to the input bytes read from a
physical board:

```sh
scripts/run-qemu.sh \
  --lpt-device /dev/parport0 \
  --lpt-input hybrid
```

The overlay is additive: it can set an input bit but never clear a bit returned
by the cabinet. Output writes, status replies and auxiliary synchronization
remain physical. The coin-door interlock is deliberately excluded because an
additive overlay cannot safely represent both its open and closed states.

This path is experimental until its input polarity and timing have been
validated on a real cabinet. `--lpt-input physical` remains the default.

The cabinet installer stores only the selected `auto`, `emulated`, or
`required` policy. It does not enumerate ports. The runner prepares generic
`lp` access; the Encore binary performs live enumeration and recognition on
every launch. Connecting or disconnecting the cabinet therefore changes the
next `auto` launch without reinstalling Encore.

The runner owns that `lp` preparation as a persistent runtime prerequisite,
just like host packages. The installer supplies the policy and user to
`run-qemu.sh --preflight`; it does not implement detection or group management.
Consequently uninstalling the cabinet boot integration does not remove the
runtime account from `lp`.

The direct path uses `PPCLAIM`, negotiates compatibility mode, and forwards
DATA, STATUS and CONTROL accesses with `ppdev` ioctls. It does not need a QEMU
chardev or a separate helper process.

For a reproducible ROM-level cable-disconnection test that still traverses a
real Linux ppdev device, run `tools/test-disconnected-vport.sh`. The helper
temporarily registers an open ISA port at `0x278`, launches explicit ppdev
Encore through the resulting `/dev/parport0`, and removes the port when Encore
exits. It refuses to alter an existing parallel-port configuration.

## Validate before play

1. Run the same game and update with `--lpt-device emulated --bench` on the cabinet
   host to verify guest timing.
2. Start real passthrough with `--lpt-trace` and the playfield power disabled.
3. Confirm expected register traffic and switch reads.
4. Enable playfield power only when the idle state is correct.
5. Test one switch, lamp and low-risk output at a time.

Details: [LPT driver-board interface](26-lpt-board.md) and
[CPU/timing measurement](12-cpu-and-timers.md).

---

← [Documentation index](README.md) · [Project README](../README.md)
