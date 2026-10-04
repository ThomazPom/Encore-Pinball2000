# 30 — Compatibility and support

Encore emulates the two released Pinball 2000 games on a custom QEMU machine.
Compatibility is a layered claim: an asset may be recognized structurally,
one revision may pass a short smoke, and the default game path may be suitable
for ordinary use without every combination having received the same test.

> [!IMPORTANT]
> The supported baseline is **Linux x86_64, the published QEMU 10.0.8 build,
> the standard SDL renderer and audio backend, the emulated LPT board, natural
> guest timing and the default `adsp-hybrid-thread` sound engine**. Start there
> before enabling a physical board, network card or experimental backend.

This page records what the current tree can select and what has actually been
checked. It does not replace the procedures in
[Testing and validation](26-testing-validation-matrix.md).

## Read the status words literally

| Status | Meaning |
|---|---|
| supported baseline | normal maintained path, included in current build/release gates |
| smoke-validated | booted and showed the named progress on a dated current-tree run |
| structurally admitted | files/signatures satisfy a focused checker; whole-game behavior was not proved |
| experimental | implemented for research or comparison, outside the recommended baseline |
| unverified | no current evidence broad enough for a support claim |

A successful build is not a game boot. A headless 15-second matrix cell is not
a desktop play session. Emulator-only evidence is not powered-cabinet
validation.

## Supported games and ordinary selections

The public launcher accepts exactly two game identities:

| Game | Launcher value | Game number | Base path | Latest installed bundle |
|---|---|---:|---|---|
| Star Wars Episode I | `swe1` | 50069 | `--update none --no-savedata` | 2.10 (`0210`) |
| Revenge From Mars | `rfm` | 50070 | `--update none --no-savedata` | 2.60 (`0260`) |

Both mandatory base-ROM pairs are present in the maintained asset tree. RFM
with the ordinary unsuffixed pair and an erased update flash identifies itself
as version 0.1 and `PRODUCTION_BUILD, FREE_PLAY_ONLY`. It also has a separate
revision-2 prototype pair, selected with `--update r2`; that path identifies
itself as RFM 0.80 revision 2 and is not an update-flash bundle.

`--game auto` chooses from those same two games. A recognized physical driver
board may identify the cabinet; the emulated-board fallback selects SWE1. For
reproducible diagnosis or validation, name the game explicitly.

> [!CAUTION]
> `--update none` only disables update discovery/staging. Existing persistent
> BAR3 flash may still contain an update. Pair it with `--no-savedata` when the
> claim is specifically about base ROMs. See
> [ROM and update selection](15-rom-loading.md) and
> [Persistent cabinet state](09-savedata.md).

## Preserved update inventory

The current asset set contains 25 extracted update bundles. Their complete
directory identities matter because a version number alone is not unique.

| Game | Preserved bundles |
|---|---|
| SWE1 | `0130_09211999`, `0140_07252000`, `0150_09222003`, `0166_04032022`, `0200_04112025`, `0201_05012025`, `0210_10312025` |
| RFM | `0120_06091999`, `0130_11241999`, `0140_01312000`, `0150_07252000`, `0160_09222003`, `0180_04232006`, `0190_03292018`, `0191_05302018`, `0195_03292018`, `0200_12032018`, `0210_04112019`, `0220_10222019`, `0221_04052020`, `0222_06302020`, `0223_04082021`, `0224_01292022`, `0250_12162022`, `0260_08082024` |

With the two base paths, `--all-updates` therefore discovers 27 game paths.
Across the six DCS engines, the exhaustive local cross-product is 162 cells.
That complete 162-cell matrix has **not** been rerun for this documentation
refresh; inventory is not certification.

### Guest-extension compatibility is narrower

`guest-extensions/check-romset.py` checks the volatile extension ABI used by
`--guest-extensions`/`--setip`/`--dns`/`--tournament`, including the unique
native `DNSIPA`, `TS_IPA`, `GmTour` and `CrdFPl` resources and their expected
type relationships, plus the structural `udpsend()` anchor used by the
automatic Slirp UDP correction. On 2026-09-28 it reported:

```text
24 supported, 2 pre-network, 0 failed
```

The expected pre-network skips are SWE1 1.30 and RFM 1.20. They do not expose
the later network path. Normal non-network boot does not require an extension,
and an `OK` says nothing about graphics, sound, timing or an entire play
session. The checker fails if no preserved game ROM is found, so an empty
asset tree cannot produce a vacuous success.

## Current runtime evidence

### Four-path smoke — 2026-09-26

The current dirty documentation branch, based on commit `655f664`, ran the
matrix collector for 15 seconds per path with the default hybrid engine,
cabinet input at 11 seconds, no savedata, no display and WAV output:

| Path | Resolved update | GP blits | Live DSP cycles | Health | Result |
|---|---:|---:|---:|---|---|
| SWE1 base 0.40 | none | 20 | 205,206,940 | PASS | PASS |
| SWE1 latest | 2.10 | 20 | 191,076,220 | PASS | PASS |
| RFM base | none | 20 | 148,560,000 | PASS | PASS |
| RFM latest | 2.60 | 20 | 176,945,500 | PASS | PASS |

All four cells also matched the requested game, update and engine and contained
no recognized fatal/crash marker. This proves the four current selection and
collector paths reach short observable graphics/audio progress. It does not
prove long-run stability, rendered pixels, subjective sound quality, every
update, every engine or a physical cabinet.

For a release claim, run the default 24-cell matrix at its normal 60 seconds
per cell and perform a real desktop acceptance session. Use `--all-updates`
only when the broader 162-cell cost and artifact review are intended.

## QEMU compatibility

Encore is not a command-line recipe for stock QEMU. The machine sources and
four ordered upstream patch families are grafted into a versioned QEMU source
tree; a stock `qemu-system-i386` has no `pinball2000` machine and cannot boot
these games.

| QEMU release | Current status | Evidence |
|---|---|---|
| 10.0.8 | supported default and published release | complete build, registered machine, routine ROM-backed smoke and release gate |
| 10.2.4 | known-good source-build alternative | complete build plus reduced and normal SWE1 2.10 hybrid runs on 2026-09-26 |
| 10.0.0 through 10.2.4 otherwise | patch-source compatible only | every selected patch family applies with zero fuzz; compilation and boot are not thereby proved |
| another QEMU version | best effort / unverified | needs a matching patch-family variant, complete build and boot validation |

The 10.2.4 check used the freshly built binary and confirmed machine
registration. A reduced 20-second SWE1-latest run passed live-DSP health and
collected five complete timing windows. A second 22-second launch used the
ordinary direct SDL framebuffer, auto-selected SDL audio, emulated LPT and
natural timing; it accepted door/coin/volume input, executed GP blits, produced
non-zero live-DSP PCM, reported health PASS and shut down through F1. The same
source change rebuilt successfully against 10.0.8. This is sufficient to
retain 10.2.4 in the builder's known-good list, but 10.0.8 remains the release
pin.

Build the default or an admitted alternative with:

```bash
scripts/build-qemu.sh
scripts/build-qemu.sh 10.2.4
```

`--latest` means the newest entry in `KNOWN_GOOD_VERS`, not the newest release
on the Internet. `--latest --unstable` deliberately leaves the validated set.
See the machine-source [QEMU README](../qemu/README.md) for patch-family rules.

## Host platform boundary

| Host or distribution | Support level |
|---|---|
| published binary on Linux x86_64 | supported distribution format |
| Debian 13 x86_64 | release build environment; cabinet laboratory also exercises Debian |
| Ubuntu 24.04 x86_64 | current QEMU CI build environment |
| another current Linux x86_64 distribution | source-build best effort when the required compiler, Python and libraries are available |
| another CPU architecture | no published binary; source/runtime unverified |
| non-Linux host | unsupported by the release and cabinet workflows |

The release downloader rejects a non-`x86_64`/`amd64` host. The source builder
does not maintain a distro allowlist, but its documented package recipe and
automated coverage are Debian-family Linux. Cabinet integration additionally
depends on systemd/logind and Linux display/device interfaces. See
[Cabinet installation](01-cabinet-installation.md) for the exact host contract.

The game CPU is always the custom i386 TCG path modelling the MediaGX guest;
it is not host KVM acceleration. A fast x86_64 host does not remove the need
to validate timer, worker and presentation behavior.

## Display, audio and input boundaries

The standard minimal build advertises these QEMU backends:

| Concern | Standard build | Notes |
|---|---|---|
| display | `sdl`, `none` | Encore's direct framebuffer uses SDL; `none` is diagnostic/headless |
| audio driver | `sdl`, `wav`, `none` | SDL is the normal live path; WAV is for capture/matrix work |
| cabinet output/input | emulated LPT | recommended baseline without physical hardware |
| desktop keyboard | built-in or strict custom switch keymap | ROM-backed smoke covers routing semantics |
| sound engine | `adsp-hybrid-thread` | default maintained engine; five alternatives remain comparison paths |

GTK is an opt-in source build (`P2K_ENABLE_GTK=1`), not present in published
minimal binaries. DBus display, VNC, PulseAudio, ALSA and OSS QEMU backends are
disabled in that build. Host audio may still reach an ALSA/Pulse/PipeWire
system through the SDL library; that does not make those QEMU drivers present.

The cabinet installer supports existing GDM/SDDM Wayland sessions, Cage,
Weston and direct SDL2/KMSDRM console profiles. It does not install an Xorg or
XWayland cabinet session. Renderer details and experimental framebuffer worker
modes belong in [MediaGX and display](23-mediagx-and-display.md).

The six selectable DCS engines are implemented and exercised by the matrix
tool, but only the hybrid live engine is the default compatibility baseline.
Engine semantics, cache requirements and comparison evidence are in
[DCS sound](25-dcs-sound.md).

## Guest-visible compatibility mechanisms

The machine supplies the fixed PCI discovery surface used by PRISM rather
than instantiating a general-purpose QEMU PCI host. Reads through configuration
ports `0xcf8/0xcfc` expose the MediaGX bridge, WMS-skinned PRISM/PLX device,
raw PLX 9050 discovery face and Cx5520 ISA bridge with the fixed BAR addresses
mapped by the device models. Configuration writes do not relocate those BARs.
This is sufficient for the software's discovery path but is not a claim of
general PCI, MSI, hotplug or passthrough support.

Base-ROM and update boots use the same installed BAR4/UART views over the same
DCS protocol core. Base-ROM detection is handled by those devices; it does not
need a periodic guest-memory patch or a separate compatibility timer.

The only retained compatibility memory mutation is the separately opt-in
`P2K_MEM_DETECT_PATCH=1` experiment described below: it scans a bounded
relocated-game range for one exact `sizmem()` body and changes the XINU result
from 4 MiB to 14 MiB. Without that environment variable, the guest's native
result is left untouched. That is 4 MiB for the current base/latest smoke and
most preserved updates, but 8 MiB for RFM 1.80. The exact 1.80 body therefore
does not match this 4→14 MiB experiment.

## Physical and optional devices

The emulated LPT board is the safe baseline. `auto`, `required`, explicit
`/dev/parportN` and hybrid input depend on Linux ppdev permissions and real
wiring. This workstation had no parallel device during the refresh, so current
physical behavior is code-reviewed and fixture-tested, **not cabinet-certified**.
Follow [Real LPT passthrough](46-real-lpt-passthrough.md) before connecting a
powered board.

The optional SMC8416-compatible network card and Slirp/bridge modes are not
required to boot or play locally. Guest networking, forwarding and old update
behavior have their own compatibility boundary in
[Optional network card](48-network.md).

The read-only PUB card model, QEMU framebuffer renderer/async worker, detailed
diagnostic sampler and the `P2K_MEM_DETECT_PATCH=1` XINU 4→14 MiB override are
experimental. The memory override is signature-bounded and off by default;
current ordinary paths retain each guest's native ceiling: normally 4 MiB,
with RFM 1.80's verified 8 MiB value as an exception. See
[Game changelogs](50-game-changelogs.md#why-rfm-180-needs-8-mib).

## Validate a new claim

Use progressively stronger evidence instead of promoting the first green
check:

```bash
# Deterministic repository gates and extension ABI
python3 -m unittest discover -s scripts/tests -p 'test_*.py'
python3 guest-extensions/check-romset.py

# Complete custom-machine build and registration
scripts/build-qemu.sh 10.2.4
"$HOME/.cache/p2k-qemu-build/qemu-10.2.4/build/qemu-system-i386" -M help \
  | grep pinball2000

# Default game/update/engine matrix
python3 docs/measurements/validation-matrix/run-matrix.py \
  --output /tmp/p2k-validation-release

# Natural user-visible acceptance
scripts/run-qemu.sh --game swe1 --update latest --no-savedata
```

For a new QEMU release, also validate patch selection, rebuild from a pristine
source tree and repeat at least the ROM-backed smoke. For a game/update claim,
retain the complete bundle identity. For a host/backend claim, record the
distribution, architecture, display session, audio path and whether the run
was normal or reduced/headless.

> [!NOTE]
> Current CI builds QEMU 10.0.8, checks repository tooling and extension
> signatures, verifies machine registration, then runs the ROM-backed switch
> smoke. It does not run the timing benchmark, 24/162-cell matrices, an
> interactive desktop session or physical cabinet hardware.

## Deliberate non-claims

Encore does not currently claim:

- complete current validation of all 25 update bundles across all six engines;
- binary support outside Linux x86_64;
- equivalence of an experimental backend to the standard SDL path;
- accurate behaviour for every feature of the physical PCI, SuperIO, PLX,
  MediaGX, PUB or network chips beyond what the software uses;
- physical driver-board or whole-cabinet validation from emulator-only runs;
- that a short clean run proves absence of rare long-run timing failures.

Known open defects and workarounds belong in
[Known limitations](35-known-limitations.md), not in an inflated compatibility
table.

---

← [Testing and validation](26-testing-validation-matrix.md) ·
[Documentation index](README.md) · [Known limitations](35-known-limitations.md) →
