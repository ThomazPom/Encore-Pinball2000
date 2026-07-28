# 02 — Quickstart

## Install build dependencies

On Debian, Ubuntu or Kali:

```sh
sudo apt update
sudo apt install -y build-essential pkg-config git curl patch ninja-build \
  python3 python3-venv libsdl2-dev libglib2.0-dev libpixman-1-dev \
  zlib1g-dev libslirp-dev libvorbis-dev
```

Optional GTK display, interactive serial-console helpers and H.264 capture:

```sh
sudo apt install -y libgtk-3-dev netcat-openbsd rlwrap ffmpeg
```

> [!IMPORTANT]
> `libvorbis-dev` is required for DCS OGG playback. Install it before building.

## Provide ROMs

At minimum, put the selected game's `u100` and `u101` chip images in `roms/`:

```text
roms/swe1_u100.rom
roms/swe1_u101.rom
```

RFM uses the `rfm_` prefix. Native ADSP sound also needs `u109`, `u110` and a
matching sound-flash image. Encore does not distribute ROMs or update payloads.

Details: [ROM and update loading](15-rom-loading.md).

## Build and run

```sh
scripts/build-qemu.sh
scripts/run-qemu.sh --game swe1
# or
scripts/run-qemu.sh --game rfm
```

The build cache defaults to `$HOME/.cache/p2k-qemu-build`. The build excludes
QEMU's default hardware-device set and includes the i386/TCG core plus Encore's
explicit dependencies. Re-running the script copies only changed Encore files;
an unchanged build is normally a sub-second Ninja check.

> [!TIP]
> Press `F10` for a credit, `Space` to start and `F4` to open the coin door.
> Details: [desktop controls](41-cli-keyboard-guide.md).

## Common commands

| Task | Command |
|---|---|
| Select update 2.10 | `scripts/run-qemu.sh --update 0210` |
| Start fresh, then save new state | `scripts/run-qemu.sh --fresh` |
| Start fresh on update 2.00 | `scripts/run-qemu.sh --fresh --update 200` |
| Run base software | `scripts/run-qemu.sh --update none --no-savedata` |
| Interactive serial console | `scripts/run-qemu.sh --serial` |
| Replay the cabinet start demo | `scripts/run-qemu.sh --script scripts/demos/start-game.p2k` |
| Execute a XINU command file | `scripts/run-qemu.sh --script my-session.p2k` |
| TCP serial console | `scripts/run-qemu.sh --serial-tcp 4444` |
| Run without a window | `scripts/run-qemu.sh --display none` |
| Disable audio | `scripts/run-qemu.sh --no-audio` |
| Synchronous DSP diagnostic | `scripts/run-qemu.sh --dcs-engine adsp` |
| Extracted-sample compatibility audio | `scripts/run-qemu.sh --dcs-engine pb2kslib` |
| Timing self-diagnostic | `scripts/run-qemu.sh --bench` |
| Run at 75% game speed | `scripts/run-qemu.sh --speed-target 75` |
| Record H.264 video | `scripts/run-qemu.sh --record-video ./gameplay.mp4` |

> [!NOTE]
> `--bench` uses normal graphics and audio by default, separates boot from a
> steady-state window, and times the guest command `sleep 10`.

> [!WARNING]
> `--display none` has no playable window. `--serial` takes over the current
> terminal until the emulator exits.

All options: [command-line reference](03-cli-reference.md).
Script syntax: [console scripting](42-console-scripting.md).

## First diagnostic

If a run fails, repeat it with `-v` and keep the printed log path:

```sh
scripts/run-qemu.sh --game swe1 -v
```

Next: [troubleshooting](04-troubleshooting.md).
