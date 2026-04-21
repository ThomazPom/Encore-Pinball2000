# 19 — Real LPT Passthrough

Encore can drive an actual Pinball 2000 cabinet driver board through
the host machine's parallel port. This doc covers everything needed
to set it up and the design choices behind the implementation.

Source: `src/lpt_pass.c` (343 lines), also the `--lpt-device` CLI flag.

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## Why real-cabinet mode at all?

Two reasons:

1. **Restoration.** Owners of a silent head cabinet (no working
   emulator) can connect a host PC's LPT port to the playfield and
   play the game on original hardware with original audio response,
   without the rest of the original CPU board surviving.
2. **Testing.** When debugging playfield protocols, you need a real
   driver board to verify coil timings and switch-matrix reads. The
   emulated matrix is great for gameplay but by definition cannot
   tell you about real-world bus contention.

## Setup

One-time host configuration:

```sh
sudo modprobe ppdev parport parport_pc
sudo usermod -a -G lp $USER    # then log out and back in
sudo rmmod lp                  # if the printer driver claimed the port

ls -l /dev/parport0            # verify existence + lp-group ownership
```

Once `/dev/parport0` is available, Encore can open it.

## How `--lpt-device` affects behaviour

Default (no flag): Encore silently tries `/dev/parport0`. If it
succeeds, the emulated matrix is bypassed and the real board drives
the game. If it fails, emulation continues without complaint.

`--lpt-device /dev/parport0` (explicit): a failure to open is now
fatal:

```
encore: --lpt-device /dev/parport0 requested but unavailable. Aborting
(use --lpt-device none to force emulation).
```

This prevents the "I thought I was driving my cabinet but Encore
silently fell back to emulation" class of bugs.

`--lpt-device none` / `--lpt-device emu`: skip the probe entirely,
force emulation. Useful on a laptop that has a `/dev/parport0` for
unrelated reasons.

## Implementation: direction management

The real-hardware LPT protocol is "implicit-direction": the driver
board decides when to drive the data lines back based on the
currently-latched opcode. The host CPU must flip its own port to
input *before* reading, otherwise there is bus contention.

Encore unconditionally flips `PPDATADIR` to input around every data-
port *read* and back to output immediately afterwards:

```c
static void set_dir(int input);  /* tracks current state in s_dir_input */

uint8_t lpt_passthrough_read(uint8_t reg)
{
    if (reg == 0 /* data port 0x378 */) {
        set_dir(1);                   // input
        read from /dev/parport0
        set_dir(0);                   // back to output
    } else if (reg == 1 /* status 0x379 */) { /* no dir flip */ }
    else if (reg == 2 /* ctrl  0x37A */) { /* return cached */ }
}
```

This policy matches what the real driver binary does — consistent with what
reverse engineering of its own LPT handler shows, which gates the data read
on a pair of `renderingFlags` bits and never touches control-register
bit 5 (the canonical PC "direction bit"). The P2K protocol is
**implicit-direction**.

## Control register mirroring

The real hardware ignores most control bits: only `STROBE` matters,
and it is implicit in the protocol timing. Encore caches the guest's
last control-register write in `s_ctrl_cached` and returns it on
reads, so any code that does a read-modify-write of the control
register sees its own last write.

Reads are explicitly **not** routed through `PPRCONTROL` because the
ioctl's semantics vary across kernels. The cached byte is always
correct.

## Exclusive mode

`PPEXCL` is requested so that `lp` / CUPS cannot open the same port
while Encore holds it. If CUPS has claimed the port, `PPEXCL` will
fail and Encore falls back to emulation (or aborts in explicit mode).

## Bidirectional capability check

`PPGETMODES` is called to verify the port supports bidirectional
transfers. If it does not (some ultra-old hardware), Encore refuses
to enter passthrough mode — no point, the protocol requires reading
back.

## Alt+K — raw keyboard capture

Unrelated to `--lpt-device` but usually wanted in the same session.
`ALT+K` toggles a mode where every key event bypasses Encore's F-key
gameplay bindings and goes straight to the emulated PS/2 keyboard
controller as make/break scancodes. This lets you type at the XINA
service shell while the cabinet runs a playfield test.

`F1` still exits. `ALT+K` toggles back.

See also `--keyboard-tcp` for the network equivalent.

## Game auto-detection via LPT probe

`lpt_passthrough_detect_game()` is called when `--game auto` is
combined with an active real-cabinet LPT. It bit-bangs a 3-register
probe:

1. Writes three opcodes that request playfield-identity registers.
2. Reads three response bytes.
3. Runs a weighted popcount over the three bytes and classifies:
   SWE1 vs RFM via a hardcoded signature table.

Signatures are derived from the real driver binary's same auto-detect
function. If the probe is ambiguous (neither signature matches cleanly),
`--game auto` aborts:

```
encore: --game auto: LPT board did not return a recognizable playfield
signature. Pass --game swe1 or --game rfm explicitly.
```

This is deliberate. Using the wrong chip-ROM bank against the wrong
coil driver map risks driving coils with the wrong voltage / duration,
which can damage them. Better to fail loud.

## Cleanup

`atexit(lpt_passthrough_close)` releases and closes the device. On
clean exit this runs. On SIGKILL or segfault it does not — the port
may remain claimed. A re-run of Encore re-claims it successfully; a
reboot or `rmmod ppdev ; modprobe ppdev` is a harder reset.

## Coexistence with `--serial-tcp`

Both are fine at the same time. The serial path is UART/TCP; the LPT
path is parallel port. They share nothing. A common session pattern:

```sh
./build/encore --game swe1 --lpt-device /dev/parport0 \
               --serial-tcp 4444 --keyboard-tcp 4445
```

→ real cabinet plays the game, serial-console shell is reachable over
TCP for service-menu operations.

## Known hardware caveats

* Laptops with a USB-to-parallel dongle: support varies widely. The
  dongle must expose a real `/dev/parport*` (i.e. kernel driver-level
  emulation), not a user-space libusb stack.
* PCI parallel cards: usually work fine if the kernel detects them.
* Old ISA parallel cards: work fine on systems old enough to still
  have ISA.
* On-board parallel ports of modern (post-2015) consumer motherboards:
  usually require BIOS to be set to `ECP+EPP` mode.
