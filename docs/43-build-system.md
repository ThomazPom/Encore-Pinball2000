# 43 — Build System

Encore uses QEMU's native **meson + ninja** build system, not a hand-written Makefile.
The legacy Encore used a minimal Makefile; Encore today delegates to QEMU's infrastructure.

> [!NOTE]
> This doc describes how to build the **QEMU machine** (the Pinball2000 device tree that plugs
> into QEMU). It does not describe how to build QEMU itself — for that, see QEMU's upstream
> documentation at [qemu.org/docs](https://www.qemu.org/docs/master/devel/build-system.html).

## Targets

The build is controlled by `scripts/build-qemu.sh`, which downloads upstream QEMU, injects our
machine sources, and runs the meson/ninja dance:

```sh
scripts/build-qemu.sh              # download, configure, and compile
scripts/build-qemu.sh --latest     # build newest validated QEMU version
scripts/build-qemu.sh 10.0.8       # build specific QEMU version
```

There is no `install` target; the QEMU binary lives in `<build>/qemu-system-i386` and is invoked
via `scripts/run-qemu.sh`.

## Source files

Pinball2000-specific C files live in `qemu/`:

```
qemu/pinball2000.c         # Machine init, device wiring, PCI/ISA topology
qemu/p2k-pci.c             # PLX 9054 PCI device registration
qemu/p2k-bars.c            # BAR2 SRAM, Phase-3 sentinel
qemu/p2k-bar3-flash.c      # BAR3 Intel 28F320 flash emulation
qemu/p2k-dcs.c             # BAR4 DCS audio command window (stub)
qemu/p2k-dcs-audio.c       # DCS sample library parser, OGG playback
qemu/p2k-lpt-board.c       # LPT driver-board protocol, switch matrix
qemu/p2k-mediagx.c         # Cyrix MediaGX MMIO (display controller, GP blit)
qemu/p2k-isa-stubs.c       # ISA device stubs (UART, PIC, PIT config)
qemu/p2k-superio.c         # Winbond W83977EF + Cyrix CC5530 config I/O
qemu/p2k-internal.h        # Shared header for the above
```

Plus the machine declaration:

```
qemu/meson.build           # Adds the above files to QEMU's i386 build
```

All of these are compiled as part of the `qemu-system-i386` target when meson sees the
`pinball2000` machine type registered in `qemu/pinball2000.c`.

**Key difference from legacy Encore:** no standalone binary. The Pinball2000 machine is a
**QEMU machine type**, launched via `qemu-system-i386 -M pinball2000`. This is the standard
pattern for custom QEMU machines (see `raspi2b`, `virt`, `malta`, etc. in QEMU's `hw/`).

## Compiler flags

Controlled by meson's `buildtype` option:

```sh
meson setup build --buildtype=debug         # -O0 -g
meson setup build --buildtype=debugoptimized # -O2 -g (default)
meson setup build --buildtype=release       # -O3 -g -DNDEBUG
```

**Debug vs release:** `debugoptimized` is the sweet spot — fast enough for gameplay, debuggable with
`gdb`. Full `-O0` debug is painfully slow (TCG with no host optimisation). Full `-O3` release
strips some QEMU assertions that are useful for catching device bugs.

The QEMU codebase compiles with `-Wall -Wextra` and enforces clean builds. Pinball2000-specific
code inherits these flags.

## Dependencies

Build-time:

* **meson** ≥ 1.2 (build system orchestrator)
* **ninja** (fast incremental builds)
* **gcc** or **clang** (C compiler; C11 required)
* **pkg-config** (for finding SDL2, GLib, etc.)
* **libglib-2.0-dev** (QEMU's main utility library)
* **libpixman-1-dev** (QEMU's low-level pixel manipulation)
* **libsdl2-dev** (display backend for the Pinball2000 machine's output)
* **libvorbis-dev** + **libvorbisfile-dev** (for DCS OGG sample playback)

Runtime (when launching `qemu-system-i386`):

* Same as build-time, minus the `-dev` packages (only the `.so` libraries).
* ROM files under `roms/` (chip ROMs for SWE1 / RFM).
* Update bundles under `updates/` (flash overlays).
* Optionally, savedata under `savedata/` (if you have preserved NVRAM from a previous run).

Full package list with multi-distro install commands: see the repo's top-level `README.md`
under "Build Prerequisites".

> [!TIP]
> If meson complains about missing dependencies, run `sudo apt install <pkg>-dev` (Debian/Ubuntu)
> or `sudo dnf install <pkg>-devel` (Fedora/RHEL). The `-dev`/`-devel` suffix is mandatory for
> build-time headers.

## Output

```
<build>/
├── qemu-system-i386        ← the QEMU binary (includes Pinball2000 machine)
├── qemu-img                ← QEMU's disk image tool (unused by us)
├── qemu-nbd                ← network block device helper (unused by us)
└── ... (various QEMU utilities)
```

The `qemu-system-i386` binary is a native 64-bit ELF (`x86_64-linux-gnu`). It emulates an i486
guest using QEMU's TCG (Tiny Code Generator) JIT, which translates guest x86 instructions to
host x86_64 on the fly.

**Size:** ~20–30 MB (includes all QEMU's device models, not just Pinball2000). The Pinball2000-
specific code adds ~200 KB.

## Parallel builds

Ninja defaults to parallel builds (`-j` auto-detected from CPU count). A four-core machine
typically finishes an incremental rebuild (one changed `.c` file) in under 1 second. A clean
build (all of QEMU from scratch) takes 1–2 minutes on modern hardware.

If you only change Pinball2000 code (`qemu/p2k-*.c`), ninja recompiles just those files and
relinks `qemu-system-i386`. QEMU's core (`hw/`, `target/i386/`, `accel/tcg/`) is untouched.

## Adding a new source file

1. Create `qemu/p2k-mynewdevice.c`.
2. Add it to `qemu/meson.build` in the `pinball2000_ss.add(files(...))` block.
3. Re-run `scripts/build-qemu.sh` (it copies updated sources and rebuilds).
4. Or manually run `ninja -C <build>` if you're iterating quickly.

Meson will pick up the new file and compile it. If you skip step 3 and the file isn't
in the build tree yet, ninja will skip it and you'll get linker errors.

## Comparison with legacy Encore

| Aspect | Legacy Encore (Unicorn) | Encore (QEMU) |
|---|---|---|
| Build tool | hand-written Makefile | meson + ninja |
| CPU backend | Unicorn Engine (libunicorn) | QEMU TCG (built-in) |
| Device tree | global `g_emu` struct | QEMU's QOM (QEMU Object Model) |
| Parallel builds | `make -j` | `ninja` (parallel by default) |
| Incremental builds | no dependency tracking (all `.o` depend on `encore.h`) | meson tracks per-file deps |
| Binary output | standalone `build/encore` | `qemu-system-i386` (multi-machine QEMU) |
| Link time (clean build) | 2–3 sec | 60–120 sec (includes all of QEMU) |
| Link time (incremental) | 1 sec | 1 sec (Pinball2000 code only) |

Encore trades faster incremental builds (meson's precise dependency tracking) for a
longer initial build (QEMU is large). Once built, iteration speed is comparable.

## No `make install` or packaging yet

Encore does not have a `make install` target or packaging recipes (`.deb`, `.rpm`,
Flatpak, AppImage). To "install" it:

1. Copy `<build>/qemu-system-i386` to `/usr/local/bin/` (or `~/bin/`).
2. Copy `roms/`, `updates/`, and `scripts/run-qemu.sh` to a known location (e.g.,
   `/opt/encore-pinball2000/`).
3. Edit `run-qemu.sh` to point at the copied paths.

This is tracked in the roadmap. A proper Debian package would:

* Install `qemu-system-i386` to `/usr/bin/`.
* Install ROMs to `/usr/share/encore-pinball2000/roms/`.
* Install updates to `/usr/share/encore-pinball2000/updates/`.
* Install a `.desktop` file and icon for GUI launchers.
* Create a `~/.local/share/encore-pinball2000/savedata/` directory for user data.

None of this exists yet. Manual copying is required.

> [!WARNING]
> Tracked in [docs/36-roadmap.md](36-roadmap.md) under "Packaging and distribution."

## Cross-references

* How to run the built binary: [02-quickstart.md](02-quickstart.md)
* Architecture overview (source tree layout): [10-architecture.md](10-architecture.md)
* Roadmap (packaging plans): [36-roadmap.md](36-roadmap.md)

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
