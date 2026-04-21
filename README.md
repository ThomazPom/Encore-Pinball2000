# Encore — a Pinball 2000 Emulator

Encore is a clean-room, single-binary x86 emulator targeting the
Williams **Pinball 2000** platform (the games **Star Wars: Episode I**
and **Revenge from Mars**). It bundles everything needed to boot the
included update packages and exercise the full display + audio + LPT
pipeline from a single self-contained folder.

---

## ⚠️  Project status — read this first

**Encore has never been tested on a real Pinball 2000 cabinet.**

Every behavioural claim in this repository — graphics rendering, DCS
audio, switch matrix handling, lamp / coil drive — has been validated
**only** against our internal emulation harness using the seven
dearchived update bundles shipped under `updates/`. The emulator
boots all of them, exercises the boot scheduler, runs the XINA OS,
loads symbols, primes the DCS sound pipeline, and produces the
expected probe-handshake traffic on the parallel port.

That is a meaningful milestone, but it is **not** the same as
"works on a real machine". On real hardware:

* timings are unforgiving and can expose races our harness silently
  tolerates;
* the parallel port talks to actual switch and lamp boards whose
  electrical behaviour we can only approximate;
* the DCS-2 sound subsystem talks to a real audio DAC, not SDL2_mixer;
* a real cabinet has wear, leaky capacitors, stuck switches, dimming
  bulbs and other realities that no emulator can predict.

So treat every "works" / "supported" / "stable" you read here as
**"behaves as expected in our emulator and is therefore expected to
work on real hardware — but verification is pending"**.

If you have access to a Pinball 2000 cabinet (or even just the
mainboard on a bench) and would like to help us close that gap, please
read **[docs/42-cabinet-testing-call.md](docs/42-cabinet-testing-call.md)**
— there is a short report template and a list of what we'd love you to
exercise.

---

## Quick start

Prerequisites (Debian 12 / Ubuntu 24.04 reference):

```sh
sudo apt install -y build-essential pkg-config \
                    libsdl2-dev libsdl2-mixer-dev libunicorn-dev
```

Build and run:

```sh
make                                                     # → ./build/encore
./build/encore --game swe1                                # SWE1 with default settings
./build/encore --update 210                               # SWE1 v2.1
./build/encore --update 2.6 --dcs-mode io-handled         # RFM v2.6, I/O-handled DCS
./build/encore --update latest --game rfm                 # newest bundled RFM
```

Other useful entry points are documented in
[docs/02-quickstart.md](docs/02-quickstart.md) and
[docs/03-cli-reference.md](docs/03-cli-reference.md).

---

## Folder layout

```
Encore-Pinball2000/
├── Makefile
├── README.md                  ← you are here
├── build/                     ← build artefacts (created by `make`)
├── include/                   ← the single shared header
├── src/                       ← C sources (no generated files)
├── tools/                     ← helper scripts (sym_dump, build_update_bin, …)
├── roms/                      ← bundled chip ROMs, BIOS, sound containers
├── updates/                   ← 7 dearchived update packages
└── docs/                      ← full documentation tree
```

`roms/` and `updates/` contain everything Encore needs to boot the
bundled games out of the box; no external file lookups, no network
calls, no implicit `$HOME`/`$XDG_*` behaviour.

---

## Highlighted CLI flags

| Flag | Purpose |
|------|---------|
| `--game swe1\|rfm\|auto` | Pick title; `auto` infers from the active ROM set |
| `--update <path\|version>` | File, directory, `.exe`, or version token (`210`, `2.6`, `latest`) |
| `--dcs-mode bar4-patch\|io-handled` | DCS sound pipeline selector (default: `bar4-patch`) |
| `--headless` | Run without opening a window (CI / smoke testing) |
| `--fullscreen` / `--flipscreen` / `--bpp N` | Display tweaks |
| `--no-savedata` | Skip NVRAM / SEEPROM load (clean boot) |
| `--config FILE` | Load options from a YAML config |
| `--lpt-device /dev/parport0` | Forward the guest's LPT to a real parallel port (cabinet only) |

See [docs/03-cli-reference.md](docs/03-cli-reference.md) for the
complete list and
[docs/41-build-env-and-runtime.md](docs/41-build-env-and-runtime.md)
for environment variables and runtime knobs (keyboard shortcuts,
SDL_*, DISPLAY, debug toggles).

---

## Documentation

Start with **[docs/README.md](docs/README.md)** for the indexed
reading order. There are 40+ documents grouped by subsystem
(architecture, ROM pipeline, CPU, DCS sound, LPT, display, build,
testing, hardware primer, XINA OS, …). The first three docs read
linearly; everything else is reference material you can dip into.

---

## How you can help

We are actively looking for:

* **Cabinet testers** — see
  [docs/42-cabinet-testing-call.md](docs/42-cabinet-testing-call.md).
* **Mainboard / bench testers** — even partial setups (just the LPT
  loop, just the DCS audio path) are extremely useful.
* **Bug reports** with logs from `--update <ver> --headless` runs that
  fail to boot or produce incorrect behaviour.
* **Compatibility reports** for additional update bundles we don't
  yet ship.

Please open an issue on the project's repository (or contact the
maintainer through the channel listed in your distribution channel)
and include:

```
Game/version :
Bundle path  :
CLI invoked  :
Host OS      :
What worked  :
What didn't  :
Last 50 lines of stdout :
```

---

## Licensing & credits

The Encore source code in `src/`, `include/`, `tools/` and `docs/` is
original work intended for free release. The bundled ROMs and update
packages under `roms/` and `updates/` are the property of their
respective rights-holders and are included for testing convenience;
remove them if you intend to redistribute Encore in a context that
does not have permission to include them.

The XINA operating system inside the bundles is itself a derivative
of Comer's XINU; see [docs/44-xina-os-deep-dive.md](docs/44-xina-os-deep-dive.md)
for context.
