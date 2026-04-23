# 28 — Build System

Encore uses a minimal hand-written Makefile with no autoconf, no meson,
no generated files, and no per-distro configuration step.

## Targets

| Target  | Description |
|---------|-------------|
| `all`   | Default. Compiles all C sources → `build/encore`. |
| `clean` | Removes the `build/` directory entirely. |

```sh
make           # equivalent to make all
make clean     # wipe build artefacts
```

There is no `install` target; copy `build/encore` wherever you like.

## Source files

All fourteen C compilation units are listed explicitly (no glob):

```
src/main.c       # CLI, config YAML, wiring
src/cpu.c        # Unicorn engine, interrupt injection, execution loop
src/memory.c     # 16 MB guest RAM, ROM bank mapping
src/rom.c        # chip-ROM de-interleave, update flash loading
src/pci.c        # PCI config space 0xCF8/0xCFC
src/io.c         # PIC, PIT, CMOS, LPT, UART, DCS UART, sgc patches
src/bar.c        # PCI BAR0..BAR5 MMIO, SRAM, flash
src/display.c    # SDL2 window, framebuffer rendering, keyboard input
src/splash.c     # startup splash screen (decode + present + dismiss)
src/sound.c      # DCS-2 sample container, SDL2_mixer playback
src/netcon.c     # TCP bridges for COM1 and PS/2 injection
src/lpt_pass.c   # Linux ppdev passthrough, game auto-detect
src/symbols.c    # XINU SYMBOL TABLE reader (runtime)
src/stb_impl.c   # stb_image + stb_image_write implementation unit
```

Plus one **embedded asset** linked verbatim into the binary by
`ld -r -b binary` (see the `$(SPLASH_OBJ)` rule):

```
assets/splash-screen.jpg   # default startup splash — overwrite and
                           # `make` to ship your own; full doc in
                           # docs/49-splash-screen.md.
```

The single shared header is `include/encore.h`. Every `.o` file depends
on it:

```makefile
$(BLDDIR)/%.o: $(SRCDIR)/%.c $(INCDIR)/encore.h | $(BLDDIR)
	$(CC) $(CFLAGS) -c $< -o $@
```

This means any change to `encore.h` triggers a full rebuild. Keep the
header lean; avoid including system headers that are only needed by one
module.

## Compiler flags

```makefile
CC      = gcc
CFLAGS  = -O2 -g -Wall -Wextra -Wno-unused-parameter -Iinclude \
          $(shell pkg-config --cflags sdl2 SDL2_mixer)
LDFLAGS = $(shell pkg-config --libs sdl2 SDL2_mixer) -lunicorn -lm -lpthread
```

Key points:

* `-O2 -g` — optimised but with debug symbols; `gdb` works without
  sacrificing performance.
* `-Wall -Wextra` — full warning set; the codebase compiles clean at
  this level.
* `-Wno-unused-parameter` — Unicorn hook callbacks receive parameters
  they legitimately ignore; suppressing this keeps the log clean.
* `pkg-config sdl2 SDL2_mixer` — resolves include paths and link flags
  from the distro's SDL2 installation. Requires `libsdl2-dev` and
  `libsdl2-mixer-dev`.
* `-lunicorn` — explicit (not via pkg-config); requires `libunicorn-dev`
  (Unicorn Engine 2.x).
* `-lm -lpthread` — math library and pthreads (the latter for
  `clock_gettime` on some older glibc versions).

## Dependencies

Full package list with minimum versions and multi-distro install
commands: [41-build-env-and-runtime.md](41-build-env-and-runtime.md).

Summary for Debian 12 / Ubuntu 24.04: `build-essential pkg-config
libsdl2-dev libsdl2-mixer-dev libunicorn-dev`.

No other runtime dependencies. The binary is statically linked to
`stb_image_write.h` (header-only, compiled through `stb_impl.c`).

## Output

```
build/
├── encore          ← main binary (~800 KB stripped, ~2 MB with -g)
├── main.o
├── cpu.o
├── ...
└── stb_impl.o
```

The binary is a 64-bit ELF (`x86_64-linux-gnu`). It emulates an i386
guest using Unicorn's `UC_MODE_32` inside a native 64-bit process.

## Parallel builds

`make -j$(nproc)` works correctly; all `.o` targets are independent.
The link step serialises naturally. A four-core machine typically
finishes in under two seconds.

## Cross-references

* Full build prerequisites & env vars: [41-build-env-and-runtime.md](41-build-env-and-runtime.md)
* Source layout: [05-architecture.md](05-architecture.md)

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
