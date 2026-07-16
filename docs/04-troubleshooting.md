# 04 — Troubleshooting

Start with the exact command that failed. Add `-v`, preserve the printed log
path, and avoid changing several timing, update, audio or savedata options at
once.

## Build fails

Run:

```sh
scripts/build-qemu.sh
```

Common causes:

| Symptom | Action |
|---|---|
| Missing compiler, Meson, Ninja, GLib, Pixman or SDL headers | Install the packages listed in [02 — Quickstart](02-quickstart.md). |
| `vorbis/vorbisfile.h` missing | Install `libvorbis-dev`. |
| Symlink or permission errors under a shared folder | Leave the default build cache under `$HOME/.cache`, or set `P2K_QEMU_BUILD_DIR` to a native Linux filesystem. |
| A source edit does not appear in the binary | Re-run `scripts/build-qemu.sh`; it recopies `qemu/` before invoking Ninja. |

Success ends with `[build-qemu] OK:` and the complete binary path.

## Launcher rejects an option

Use `scripts/run-qemu.sh --help`. The wrapper validates display/audio backends,
update tokens, paths and incompatible console combinations before QEMU starts.

Do not invoke a system `qemu-system-i386`; it does not contain Encore's
`pinball2000` machine.

## ROM or update failure

```sh
scripts/run-qemu.sh --game swe1 --update latest -v
```

- Bank0 requires the game's U100 and U101 files.
- Native ADSP additionally requires U109/U110 and a valid 1 MiB sound flash.
- An update directory must contain matching `bootdata`, `im_flsh0`, `game` and
  `symbols` ROM files.
- `--update none` intentionally selects base software and enables its isolated
  sound compatibility path.

See [15 — ROM and update loading](15-rom-loading.md).

## No graphics window

- Do not pass `--headless` or `--display none` for normal play.
- Ensure `DISPLAY` or `WAYLAND_DISPLAY` is available.
- Run `--display sdl`; if the backend is absent, rebuild after installing SDL2
  development packages.
- If the image is upside down, press `F2`.

See [23 — MediaGX display](23-mediagx-and-display.md).

## No sound

First confirm the host backend selected by the launcher. Then isolate the
content engine:

```sh
scripts/run-qemu.sh --dcs-engine pb2kslib -vv
scripts/run-qemu.sh --dcs-engine adsp -vv
```

`pb2kslib` needs `<roms>/<game>_sound.bin` unless overridden. Native ADSP needs
U109/U110 and sound flash. `--no-audio` disables output deliberately.

Open the coin door with `F4` before testing volume buttons. Insert a credit with
`F10` to trigger a predictable game sound. See [25 — DCS sound](25-dcs-sound.md).

## Controls do not respond

- The graphical window must have focus.
- `--display none` has no desktop keys.
- `--cabinet-purist` disables desktop switch injection.
- Service volume/menu buttons normally require the coin door to be open (`F4`).

Press `F12` to print current LPT/switch state. See
[41 — Desktop controls](41-cli-keyboard-guide.md).

## Serial console is unavailable

`--serial` needs `netcat-openbsd`; `rlwrap` is optional. It cannot be combined
with `--headless`, `--serial-tcp` or `--uart-tcp`.

For a separate client:

```sh
scripts/run-qemu.sh --uart-tcp 127.0.0.1:4444
nc 127.0.0.1 4444
```

If `%` appears only after the next command, that is guest prompt timing rather
than a lost command. See [06 — XINA console](06-xina-os-deep-dive.md).

## Game appears too slow or fast

Do not judge guest time from animation alone. Run:

```sh
scripts/run-qemu.sh --bench
```

Use steady-state `sleep 10`, delivery, jitter and PDB05 values. Boot totals are
reported separately. `--strict` can be substantially slower by design;
`--speed-target` deliberately changes guest speed. See
[12 — CPU and timing](12-cpu-and-timers.md).

## Saved settings disappear

Normal exit is required for atomic savedata flush. `--no-savedata` deliberately
loads and saves nothing. A hard kill cannot run QEMU exit notifiers. See
[09 — Savedata](09-savedata.md).

## Real LPT will not open

Verify `/dev/parport0`, load `parport_pc` and `ppdev`, release the printer
driver, and ensure the current login has `lp` group access. `EACCES` is a
permission problem; `EBUSY` means another process or driver owns the port.

Do not substitute `/dev/usb/lp0`; a printer-class USB adapter is not a
bidirectional register-level parallel port. See
[46 — Real LPT passthrough](46-real-lpt-passthrough.md).

## Report a new failure

Include the command, commit, host OS, game, update, DCS engine, savedata mode,
display/audio backends and the verbose log. For timing or LPT issues, include a
`--bench` result and trace artifact.

---

← [Documentation index](README.md) · [Project README](../README.md)
