# 42 — Cabinet Testing Call-to-Action

> **You can help make Encore better.** Encore has been written and
> verified entirely in software, against the Unicorn Engine on a
> modern Linux host. The next big unknown is whether the same binary
> behaves sensibly when it is asked to take the place of the original
> CPU board on a real Pinball 2000 machine.

## What we are realistically asking for

We are **not** asking anyone to commit to a full bring-up of an
unfamiliar emulator on a working cabinet. The first useful step is
much simpler:

> Install Encore on a stock **Debian 13 x86_64** PC, run it the way
> you would run any other pinball emulator (no special hardware, no
> serial cable, no LPT board), and report back what happened.

In particular, what would already be a huge insight for the project:

* Whether the Debian 13 build instructions in
  [02-quickstart.md](02-quickstart.md) and
  [28-build-system.md](28-build-system.md) actually work end-to-end on
  a clean install (apt packages, build, first launch).
* Whether the boot timing on a typical desktop PC is in the same ball
  park as other emulators that the community already runs (i.e. does
  it boot in something like the same number of seconds? does it stall?
  does it consume reasonable CPU?).
* Whether attract mode looks and sounds correct on your machine
  compared to what you remember from a real cabinet, or from a video
  capture of one.
* Whether the SDL window, audio output and basic key bindings (F-keys,
  start, flippers, credit) behave as expected without any extra setup.

That alone — a "yes it built and ran on Debian 13 like a normal
emulator" or "no, here is where it went wrong" — would tell us
exactly which next steps are worth investing in.

## If you do have access to real hardware

Anything beyond the Debian 13 first-run report is **bonus** at this
stage. If you happen to own a Pinball 2000 cabinet (Revenge From Mars
or Star Wars Episode I), or a spare driver board on the bench, the
following extra tests would be enormously valuable, but please treat
them as optional:

* **LPT passthrough on a bench driver board.** Connect a spare driver
  board via a parallel port and run with
  `--lpt-device /dev/parport0`. Confirm whether switch closures are
  read back and whether lamps and flashers respond to attract-mode
  events. See [19-real-lpt-passthrough.md](19-real-lpt-passthrough.md)
  for the kernel module setup.
* **NVRAM round-trip.** Boot, play a game, exit cleanly, boot again,
  and confirm the high-score table and adjustments survived.
* **DCS audio path.** Compare the audio against a known-good cabinet
  recording — same music tracks, same speech samples, same timing.
* **Full attract-to-game-over loop on a complete cabinet.** Only worth
  attempting once the bench-level tests look healthy.

## How to set up the simple Debian 13 test

1. **Install build dependencies** (everything is in Debian 13's
   official repositories):
   ```sh
   sudo apt install build-essential libsdl2-dev libsdl2-mixer-dev \
                    libunicorn-dev unzip git
   ```
2. **Clone and build:**
   ```sh
   git clone <repo-url> Encore-Pinball2000
   cd Encore-Pinball2000
   make
   ```
3. **Drop your ROMs into `roms/swe1/` or `roms/rfm/`** as described in
   [08-rom-loading-pipeline.md](08-rom-loading-pipeline.md).
4. **Launch:**
   ```sh
   ./build/encore --game swe1
   ./build/encore --game rfm
   ```
5. **Watch the first 60 seconds.** Note when the WMS/Williams logo
   appears, when attract mode starts, and whether audio plays.

## Report template

A short report is more useful than no report. The minimum we would
love to see:

```
Host OS         : Debian 13 x86_64 (or other)
Encore version  : git describe / commit hash
Game / version  : SWE1 v1.5 / RFM v1.8 / etc.
CLI invoked     : ./build/encore --game swe1

Build           : ✓ ok / ✗ failed (paste error)
First launch    : ✓ ok / ✗ failed (paste last 30 lines of stdout)
Time to attract : roughly N seconds from launch to attract video
Video           : looks right / looks wrong (describe)
Audio           : plays music + effects / silent / distorted
CPU usage       : reasonable / pegged at 100 % / other

Anything that surprised you:
```

## Where to send reports

Open an issue on the project repository, or use whichever
contact channel is listed in the top-level
[README.md](../README.md).

## Background — what is already known

Encore already passes its own software regression matrix on every
shipping bundle (see [26-testing-bundle-matrix.md](26-testing-bundle-matrix.md)).
A successful Debian 13 desktop test would extend that baseline
outside the developer's own machine, and a successful bench-level
LPT/audio test would extend it outside emulation entirely.

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
