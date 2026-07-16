# 25 — DCS sound

Encore has one DCS command/protocol core and three selectable content engines.

## Protocol path

`qemu/p2k-dcs-core.c` owns command, response and mixer state. Two guest-facing
frontends feed it:

- `qemu/p2k-dcs.c`: PRISM BAR4 MMIO at `0x13000000`;
- `qemu/p2k-dcs-uart.c`: the DCS I/O-port view at `0x138–0x13f`.

Both frontends use the same response queue, echo byte, mixer state and command
dispatcher. The two `--dcs-mode` labels select that shared implementation.

## Audio engines

| Engine | Input | Status |
|---|---|---|
| `pb2kslib` | Extracted `<roms>/<game>_sound.bin` sample container | Default sample player |
| `adsp` | Original U109/U110 plus 1 MiB sound flash | Experimental synchronous native DSP |
| `adsp-thread` | Same original assets | Experimental condition-driven mailbox worker; SPORT remains audio-clocked |

Select one with:

```sh
scripts/run-qemu.sh --dcs-engine pb2kslib
scripts/run-qemu.sh --dcs-engine adsp
scripts/run-qemu.sh --dcs-engine adsp-thread
```

`pb2kslib` decodes samples through libvorbisfile and mixes voices into QEMU's
audio backend. `--sound-loading preload` decodes the full container at startup;
lazy loading is the default.

The native engines execute the ADSP-2105 core in
`qemu/p2k-adsp2105-core.c`, map original U109/U110 data and render SPORT PCM.
Sound flash is selected in this order:

1. `--dcs-sound-flash <path>`;
2. `*_sf.rom` in the selected update;
3. `<roms>/<game>_28f800.rom`;
4. `<roms>/<game>/28f800.rom`.

## Host output

The launcher chooses a supported host audio backend in `auto` mode. Use an
explicit backend to diagnose host selection, or `--no-audio` to disable output.
This is separate from the DCS engine choice.

## Manual test

Open the coin door with `F4`, exercise volume up/down, then insert a credit with
`F10`. Credit music provides a repeatable manual trigger. Use `-vv` or
`--trace-audio` to record DCS commands, voices and PCM progress.

Automated validation checks boot and engine progress. Song selection, loops,
stops and volume behavior require listening tests.

Details: [validation matrix](26-testing-validation-matrix.md).

---

← [Documentation index](README.md) · [Project README](../README.md)
