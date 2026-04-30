# `qemu/` — Pinball 2000 custom QEMU machine

Custom QEMU **machine type** `pinball2000`, built into a vendored,
patched, pinned `qemu-system-i386` (currently 10.0.8) by
`scripts/build-qemu.sh`. Exposes the Williams Pinball 2000 board (Cyrix
MediaGX + CS5530 + PLX9054 + DCS-2 sound + LPT driver board) so the
unmodified game ROMs run.

## Why custom — not stock `-bios`

`roms/bios.bin` is not a meaningful entry point on this hardware. The
boot recipe (`p2k-boot.c`) bypasses the BIOS, copies the PRISM option
ROM (first 32 KiB of game ROM bank 0 = `u100`+`u101` interleaved) to
physical `0x80000`, builds a flat-mode GDT at `0x1000`, and jumps the
CPU to `EIP=0x801D9` with `ESP=0x8B000` and `IF=0`.

## Architectural intent

QEMU owns CPU execution, PIT/PIC timing, virtual timers and interrupt
delivery. We do **not** rebuild any of that layer. Each Pinball-2000
peripheral should land here as a normal QEMU device (PCIDevice for
PCI-attached parts, `MemoryRegionOps` for MMIO, `isa_register_ioport`
for legacy I/O, `qemu_console` for video).

A handful of files in this directory are still **symptom patches**
(host-side RAM pokes, IDT pinning, code rewrites, fake cf8/cfc handler).
Each such file carries a `STATUS: TEMPORARY SYMPTOM PATCH` block at the
top stating the removal condition. Treat any new patch the same way:
ship it with a written sunset criterion or do not ship it.

## File layout — one concern per file

| File                  | Concern                                                     | Status         |
|-----------------------|-------------------------------------------------------------|----------------|
| `pinball2000.c`       | `MachineClass` registration; tiny init-order list           | machine        |
| `pinball2000.h`       | Address-map / register / boot-entry constants               | header         |
| `p2k-internal.h`      | Private declarations shared between p2k-*.c modules         | header         |
| `p2k-rom.c`           | Bank0..3 + DCS ROM deinterleave loader                      | device         |
| `p2k-boot.c`          | Post-reset PM-entry recipe (option-ROM copy + GDT + regs)   | machine glue   |
| `p2k-plx9054.c`       | PLX 9054 ROM/BIOS/bank0/BAR5 windows                        | device         |
| `p2k-plx-regs.c`      | PLX 9050 BAR0 registers + 93C46 SEEPROM model               | device         |
| `p2k-bars.c`          | BAR2 SRAM + 16 MiB sentinel (seeds from `savedata/*.nvram2`)| device         |
| `p2k-bar3-flash.c`    | BAR3 Intel 28F320 command protocol (seeds from `*.flash`)   | device         |
| `p2k-dcs.c`           | DCS-2 sound on BAR4 MMIO (state shared with dcs-uart)       | device (split) |
| `p2k-dcs-uart.c`      | DCS-2 sound legacy I/O view 0x138-0x13F (state split)       | device (split) |
| `p2k-lpt-board.c`     | LPT driver-board protocol on 0x378-0x37A                    | device         |
| `p2k-gx.c`            | Cyrix MediaGX 16 MiB MMIO + framebuffer alias to RAM 0x800k | device         |
| `p2k-display.c`       | 640×480 ARGB8888 console reading FB at RAM 0x800000         | device         |
| `p2k-vsync.c`         | ~57 Hz VSYNC ticker (BAR2[4]=1 + DC_TIMING2 walk)           | device         |
| `p2k-isa-stubs.c`     | i8042 / CMOS / POST / COM1 minimal stubs                    | device         |
| `p2k-superio.c`       | W83977EF (0x2E/0x2F) + Cyrix CC5530 (0xEA/0xEB) chip-IDs    | device         |
| `p2k-pci.c`           | cf8/cfc dispatcher with vendor-IDs (TEMPORARY)              | symptom patch  |
| `p2k-pic-fixup.c`     | Force-unmask IRQ0+cascade on the i8259 (TEMPORARY)          | symptom patch  |
| `p2k-mem-detect.c`    | BT-130: rewrite XINU `mem_detect` prologue (TEMPORARY)      | symptom patch  |
| `p2k-nic-dseg.c`      | BT-131: poke SMC8216T LAN-ROM shadow into D-seg (TEMPORARY) | symptom patch  |

## Boot recipe owned by `p2k-boot.c`

Run after every system reset (registered with `qemu_register_reset`
*after* QEMU's own device reset, so our register writes survive):

1. Copy first 32 KiB of bank0 to physical `0x80000` (PRISM option ROM).
2. Seed BT-131 NIC LAN ROM shadow at `0xD0008..0xD000F`.
3. Lay down the 4-entry GDT at `0x88000` (CS=0x9F, DS=0x93). The base
   used to be `0x1000` (mirroring unicorn) but swe1-default cold boot
   wild-jumps to `0x1008` (inside GDT entry 1, where the segment-limit
   bytes `FF FF` decode as undefined opcode `FF /7`); the old IDT[6]
   catchall hid this. New base sits just past the PRISM option ROM.
4. Reprogram CPU0 to PM entry: `CR0 |= PE|ET`, `CS:EIP = 0x08:0x801D9`,
   flat 4 GiB segments, `EFLAGS.IF = 0`, `ESP = 0x8B000`.

## Removal-condition discipline

When you change a `symptom patch` file:

* Keep it kill-switchable (env var) so it can be disabled while wiring
  up the real device.
* Update the `STATUS:` block if you change the removal condition.
* If the real-device path becomes available, **remove** the symptom
  patch in the same commit that lands the replacement — do not leave
  both running.

## Build

`bash scripts/build-qemu.sh` — downloads QEMU 10.0.8 to
`~/.cache/p2k-qemu-build/`, copies every `p2k-*.c` here into
`hw/i386/`, edits `meson.build`, builds `qemu-system-i386`. The script
is idempotent and cheap on incremental rebuilds.

## Run

The product wrapper is `scripts/run-qemu.sh` — Unicorn-Encore-style CLI
on top of the QEMU machine. `./scripts/run-qemu.sh --help` prints the
authoritative arg list and key bindings. Common invocations:

```sh
./scripts/run-qemu.sh --game swe1                      # auto-pick newest update
./scripts/run-qemu.sh --game swe1 --update none --no-savedata  # museum / base
./scripts/run-qemu.sh --game swe1 --update 0210        # short-code resolve
./scripts/run-qemu.sh --game rfm
./scripts/run-qemu.sh --headless --game swe1           # -display none + -serial stdio
./scripts/run-qemu.sh --game swe1 -vv                  # +DCS audio trace
```

Raw form (bypasses the wrapper) is still:

```sh
QEMU_BIN=~/.cache/p2k-qemu-build/qemu-10.0.8/build/qemu-system-i386
$QEMU_BIN -M pinball2000,game=swe1,roms-dir=$PWD/roms -m 16 -display sdl
```

Wrapper flag → behavior summary (see `--help` for full list):

| Flag                    | Effect                                                                  |
|-------------------------|-------------------------------------------------------------------------|
| `--game swe1\|rfm`      | Selects machine `game=` property (SWE1 default).                        |
| `--roms <dir>`          | Sets `roms-dir=` (default `./roms`).                                    |
| `--update <spec>`       | `auto` (machine picks newest), `latest` (wrapper resolves), `none` (museum, sets `P2K_NO_AUTO_UPDATE=1`), 4-digit `0210` / `2.10` short codes, or an explicit dir. |
| `--savedata <dir>` / `--no-savedata` | Working directory for flash/nvram persistence; `--no-savedata` runs in a throwaway tmpdir. |
| `--display sdl\|gtk\|none` / `--headless` / `--fullscreen` | Display backend; `--headless` implies `none` + serial stdio. |
| `--audio auto\|pa\|alsa\|sdl\|none` / `--no-audio`         | Audiodev selection (auto-detects PulseAudio/ALSA on host). |
| `--pb2kslib <path>`     | Override `P2K_PB2KSLIB` sample-cache path.                              |
| `--uart-quiet`          | Sets `P2K_NO_UART_STDERR=1` (UART mirror normally on).                  |
| `--uart-tcp host:port`  | Bind COM1 to a TCP server (mutually exclusive with `--headless`).       |
| `--monitor`             | Adds `-monitor stdio` (or TCP if also `--headless`).                    |
| `--diag` / `--trace-dcs` / `--trace-audio` / `--trace-timing` | Individual trace toggles. |
| `-v` / `-vv` / `-vvv`   | Tiered: `-v`=`P2K_DIAG`, `-vv`=+`P2K_DCS_AUDIO_TRACE`, `-vvv`=+`P2K_DCS_BYTE_TRACE`. |
| `--dcs-mode io-handled\|bar4-patch` | Documentation-only label (both modes route through BAR4 today). |
| `--serial-tcp <port>`   | Unicorn-compatible alias for `--uart-tcp 127.0.0.1:<port>`.            |
| `--screenshot-dir <dir>`| Where F3 writes screenshots (default `/tmp`); exported as `P2K_SCREENSHOT_DIR`. |
| `--bpp 16` | Native PIXMAN x1r5g5b5 display surface — pixels copied directly from the GX framebuffer with no ARGB conversion (`P2K_DISPLAY_BPP=16`). |
| `--splash-screen <path>` | Best-effort host viewer (`feh`/`eog`/`display`/`xdg-open`) shown while QEMU runs; killed on exit. `default`/`none` show no splash. |
| `--lpt-device none\|/dev/parportN\|0xNNN` | `none` → skip LPT install (P2K_LPT_DISABLE; game won't boot — diagnostic). `/dev/parportN` → host parport passthrough via Linux ppdev (P2K_LPT_PARPORT; needs `lp` group + `modprobe ppdev`). `0xNNN` → relocate emulated board to a custom I/O port (P2K_LPT_IOPORT). `emu`/`emulated` is the default. |
| `--lpt-trace <file>` | Append every LPT read/write event to `<file>` (`P2K_LPT_TRACE_FILE`) with µs timestamps. |
| `--cabinet` / `--cabinet-purist` | Records intent to trust the real driver-board protocol (`P2K_CABINET_PURIST=1`). Effective only when paired with `--lpt-device <hostdev>`; the emulated board still answers otherwise. |
| `--parport <device>` | Unicorn-compatible alias for `--lpt-device <device>`. |
| `--sound-loading preload` | Decode every pb2k sample at install time (`P2K_DCS_PRELOAD=1`); eliminates first-trigger decode hitch. `lazy` (default) decodes on demand. |
| `--bpp 24` / `--splash` (legacy alone) | Accepted for Unicorn parity: `--bpp 24` warns and falls back to 32; `--splash` keeps the silent default (no host splash). |
| `--` / passthrough      | Everything after `--` is forwarded verbatim to QEMU.                    |

Useful env vars (defaults reflect the post-bring-up state, see
`qemu/NOTES.next.v2.md` for status of each temporary patch):

| Var                            | Default | Effect                                                                |
|--------------------------------|---------|-----------------------------------------------------------------------|
| `P2K_NO_UART_STDERR=1`         | OFF     | Silence COM1/UART mirror on host stderr (mirror is ON by default so Fatal/NonFatal/monitor output is visible without env tweaks). |
| `P2K_UART_TO_STDERR=1`         | n/a     | Legacy explicit-on switch; same effect as the default — kept for compatibility. |
| `P2K_PIC_FIXUP=1`              | OFF     | Re-arm the legacy 250µs IRQ0/cascade unmask timer. The timer is OFF by default since `b20f39b`; opt-in only as a regression fallback. |
| `P2K_NO_PIC_FIXUP=1`           | OFF     | Force the legacy PIC fix-up timer off even if `P2K_PIC_FIXUP=1` is set (the override switch always wins). |
| `P2K_NO_MEM_DETECT_PATCH=1`    | OFF     | Disable BT-130 mem_detect prologue rewrite                            |
| `P2K_DIAG=1`                   | OFF     | Enable the read-only PIT/PIC/IDT/XINU-scheduler change-only sampler. The `run-qemu.sh -v` flag sets this automatically. Off by default to keep routine boots quiet. |
| `P2K_DCS_AUDIO=1`              | OFF     | Enable DCS audio backend (real samples from `<roms_dir>/<game>_sound.bin`, no synthesized fallback). Requires a host audiodev — pass `-audio driver=pa` (or `alsa`/`sdl`/...) so QEMU can bind a default audio device. The wrapper does this automatically with `--audio pa`. |
| `P2K_NO_DCS_AUDIO=1`           | OFF     | Force DCS audio off even if `P2K_DCS_AUDIO=1` is set.                |
| `P2K_DCS_BYTE_TRACE=1`         | OFF     | Trace 8-bit byte writes to DCS port 0x13C and reconstruct the latched 16-bit command. Used to validate the late-Unicorn `0001de2` clue. |
