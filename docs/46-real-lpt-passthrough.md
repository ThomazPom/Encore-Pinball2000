# 46 — Connecting a real cabinet through LPT

Encore can pass guest parallel-port traffic directly to a Linux `ppdev` device.
The implementation is in `qemu/p2k-lpt-board.c`.

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

`--cabinet-purist` refuses to start without a real parallel device, except for
the explicit `--lpt-device disconnected` ROM-diagnostic target. This avoids
silently running the keyboard-backed emulated board when cabinet control was
intended.

The cabinet installer discovers `/dev/parportN` without assuming a particular
port number. When a kernel parport exists but its ppdev device is absent, it
loads `ppdev` (offering the distribution's `kmod` package if the loader is
missing) and refuses to silently continue if no character device appears. It
selects a single detected port automatically, asks when several exist, enables
real cabinet I/O by default, and adds the unprivileged session account to `lp`.
When no kernel ppdev interface exists at all, it explicitly reports that Encore
will use the emulated demonstration board.

The runner owns that `lp` preparation as a persistent runtime prerequisite,
just like host packages. The installer supplies the selected device and user
to `run-qemu.sh --preflight`; it does not implement group management itself.
Consequently uninstalling the cabinet boot integration does not remove the
runtime account from `lp`.

The direct path uses `PPCLAIM`, negotiates compatibility mode, and forwards
DATA, STATUS and CONTROL accesses with `ppdev` ioctls. It does not need a QEMU
chardev or a separate helper process.

For a reproducible ROM-level cable-disconnection test that still traverses a
real Linux ppdev device, run `tools/test-disconnected-vport.sh`. The helper
temporarily registers an open ISA port at `0x278`, launches cabinet-purist
Encore through the resulting `/dev/parport0`, and removes the port when Encore
exits. It refuses to alter an existing parallel-port configuration.

## Validate before play

1. Run the same game and update with `--lpt-device emu --bench` on the cabinet
   host to verify guest timing.
2. Start real passthrough with `--lpt-trace` and the playfield power disabled.
3. Confirm expected register traffic and switch reads.
4. Enable playfield power only when the idle state is correct.
5. Test one switch, lamp and low-risk output at a time.

Details: [LPT driver-board interface](26-lpt-board.md) and
[CPU/timing measurement](12-cpu-and-timers.md).

---

← [Documentation index](README.md) · [Project README](../README.md)
