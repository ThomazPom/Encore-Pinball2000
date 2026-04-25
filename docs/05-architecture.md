# 05 — Architecture

This document gives you a mental model of how Encore is structured
before you start poking at individual subsystems. If you have time for
only one architecture doc, read this one and then come back for the
deep dives.

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## The compilation units

```
src/main.c       CLI, config, wiring, boot-assist stubs
src/cpu.c        Unicorn loop, IRQ inject, patch scans, HLT redirection
src/memory.c     Unicorn memory-region mapping
src/rom.c        Chip-ROM de-interleave, bundle load, savedata
src/pci.c        Raw PCI config-space read/write
src/io.c         PIC, PIT, CMOS, UART, LPT, DCS-UART, SGC patches
src/bar.c        PCI BAR0..BAR5 MMIO dispatcher
src/display.c    SDL2 window, framebuffer blit, SDL event pump
src/sound.c      SDL2_mixer init, DCS cmd dispatch, sample container
src/netcon.c     TCP ↔ UART and TCP ↔ PS/2 bridges
src/lpt_pass.c   Linux ppdev / raw-port real-cabinet passthrough
src/symbols.c    XINU SYMBOL TABLE index + lookup
src/splash.c     Optional splash-screen renderer
src/stb_impl.c   stb_image_write implementation unit (header-only)
```

Plus one header: `include/encore.h` — the entire cross-file contract.
There is one global, `EncoreState g_emu`, defined in `main.c` and
used everywhere.

## Process model

Encore is single-threaded-ish. There is one Unicorn thread (the main
thread); `SDL_mixer` and the kernel ppdev driver spawn their own
helpers internally, but we do not manage them. No `pthread_create`
calls exist in the repository.

The main thread alternates between three kinds of work:

```
  ┌──────────────────────────────────┐
  │ uc_emu_start(batch=200 000)      │  ← 99.99% of wall time here
  └──────────────┬───────────────────┘
                 │
      +----------+-----------+
      ▼                      ▼
  handle errors      every 128 iters:
  (HLT, INV, IRQ)   display_update,
                     display_handle_events,
                     netcon_poll
                     heartbeat log
```

Interrupts are delivered by setting a bit in `g_emu.pic[0].irr` from
the host side, then — when the next batch ends — calling
`cpu_inject_interrupt()` which pushes an interrupt frame onto the
guest stack and jumps to the IDT entry. Unicorn itself does not know
about PIT/PIC/VBLANK; all of that is synthesised from host wall-clock
(`clock_gettime(CLOCK_MONOTONIC)`) in `cpu.c:496-552`.

## Data flow — boot

1. `main()` parses CLI, auto-loads `encore.yaml` if appropriate.
2. `rom_load_all()` streams eight chip ROMs into a 16 MB bank,
   de-interleaves, assembles the update flash, loads savedata.
3. `cpu_init()` creates the Unicorn engine, pins the BIOS reset vector
   and installs hooks for MMIO and code.
4. `memory_init()` maps 16 MB RAM + all MMIO regions as Unicorn memory
   regions (some direct-backed with `uc_mem_map_ptr`, some MMIO with
   `uc_mmio_map`).
5. `io_init()` resets PIC/PIT/CMOS stub state.
6. `bar_init()` seeds PLX 9050 registers and sets SEEPROM defaults.
7. `display_init()` / `sound_init()` create the SDL surfaces (unless
   `--headless`).
8. `netcon_init()` opens the TCP listeners if ports were asked for.
9. `lpt_passthrough_open()` probes `/dev/parport0`.
10. `cpu_run()` enters the emulation loop until F1 / SIGINT / error.

## Data flow — one instruction batch

```
host: clock_gettime → decide if PIT tick due → set IRR bit 0
host: decide if VBLANK due → bump vsync_count, write DC_TIMING2
host: if xinu_ready && pending → cpu_inject_interrupt()
host: uc_emu_start(eip, 0, 0, 200000)    — the actual CPU work
guest: runs some code, hits MMIO/port → io.c/bar.c hook called
guest: hits HLT → UC stops with UC_ERR_OK
host: exec_count++; handle error or HLT; read EIP
host: every 0x7F iterations → display_update, events, netcon_poll
```

Batch size (200 000) is the single-largest tuning knob in Encore. At a
typical guest cycle-per-host-ns ratio, that gives the host about 5 ms
between wake-ups — low enough for 60 Hz SDL redraw, high enough that
the JIT amortises its prologue/epilogue.

## Subsystem ownership map

| Subsystem | Owner module | Exposed through |
|---|---|---|
| Guest RAM, ROM banks, flash  | `memory.c`, `rom.c`       | `g_emu.ram`, `g_emu.flash` |
| Unicorn engine               | `cpu.c`                   | `g_emu.uc` |
| PCI config space 0xCF8/0xCFC | `pci.c`                   | `pci_read/write` |
| BAR0..BAR5 MMIO              | `bar.c`                   | `bar_mmio_read/write` |
| I/O ports 0..0xFFFF          | `io.c`                    | `io_port_read/write` |
| PIT / PIC / CMOS / UART     | `io.c`                    | state inside `g_emu` |
| DCS-UART side of audio       | `io.c` (`0x138..0x13F`)   | command queue |
| SDL window & input           | `display.c`               | SDL callbacks |
| DCS command dispatch         | `sound.c`                 | `sound_process_cmd` |
| LPT emulation                | `io.c`, `lpt_pass.c`      | matrix + keydown callbacks |
| XINU symbol lookup           | `symbols.c`               | `sym_lookup()` |

Every module is self-contained: none of them calls `SDL_*`,
`Mix_*` or Unicorn APIs outside of their own owner. The one exception
is `cpu.c`, which touches everything — that is intentional because
`cpu.c` is also the only place that runs on the guest-execution hot
path.

## Memory budget

* Guest RAM — 16 MB, `mmap`'d, Unicorn-mapped as a direct-backing
  region so host-side reads and writes of `g_emu.ram[addr]` are
  indistinguishable from guest reads and writes. Saves ~90 % of the
  cost that `uc_mem_read/write` would charge.
* Update flash — 4 MB, also direct-backed.
* BAR2 SRAM — 128 KB, host-side `uint8_t[]`, copied to Unicorn at
  boot and written-through on SRAM writes.
* Screen framebuffers — 640×480 × {2, 4} bytes each for bpp 16 / 32.
* DCS ROM — 8 MB for the sound bank, direct-backed.

Total peak RSS is around 40 MB including SDL and the loaded bundle.

## The one global

`EncoreState g_emu` is defined at the top of `main.c` and zero-
initialised. Every module reads and writes into it directly — there is
no opaque handle passing, no init context, no "emu_t *".

Rationale: this is a single-binary emulator with no multi-instance
use-case. A pinball cabinet runs exactly one title at a time. The
ceremony of a handle struct would save no lines, catch no bugs and
obscure every code path. The one risk (accidental thread races) is
irrelevant because we are single-threaded-ish by design.

## Where to go next

* Unicorn hooks / IRQ path: [06-cpu-emulation.md](06-cpu-emulation.md)
* Address space: [07-memory-map.md](07-memory-map.md)
* Specific patches: [21-patching-philosophy.md](21-patching-philosophy.md)
* Frame timing: [24-fps-and-pacing.md](24-fps-and-pacing.md)

## Historical research notes

The following PCI device identities were captured from live boot
traces of the original P2K runtime and are what the PRISM option ROM
expects to find on the bus. Returning a different vendor/device pair
causes the option ROM to refuse to boot. Encore's PCI emulation
mirrors these exactly.

| Slot   | Device                                | Vendor | Device   | Class      | Rev |
|--------|---------------------------------------|--------|----------|------------|-----|
| Dev 0  | Cyrix MediaGX (CPU / north bridge)    | `0x1078` | `0x0001` | `0x060000` | `0x0` |
| Dev 8  | WMS PRISM                             | `0x146E` | `0x0001` | `0x030000` | `0x2` |
| Dev 18 | Cyrix CX5520 (south bridge)           | `0x1078` | `0x0002` | `0x060100` | `0x0` |

The standard QEMU PC chipset devices i440FX (`0x8086:0x1237`) and
PIIX3 (`0x8086:0x7000`) have nothing to do with real Pinball 2000
hardware. Earlier emulator attempts that exposed these IDs were
rejected by the PRISM option ROM at boot.

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
