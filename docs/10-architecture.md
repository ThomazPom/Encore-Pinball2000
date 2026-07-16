# 10 — Architecture

Let's take a walk through the Pinball 2000 machine. We'll follow the boot path from ROM image through CPU start, protected mode entry, XINA loading, game code, display frames, and DCS audio—watching how QEMU and our custom `pinball2000` machine work together to keep the 1999 firmware happy.

> [!NOTE]

## The big idea

When you run `qemu-system-i386 -M pinball2000,game=swe1,roms-dir=roms`, QEMU creates an x86 machine with 16 MiB of RAM, one TCG i386 CPU, a QEMU ISA bus with i8259+i8254, and a set of Pinball 2000 devices mapped at the addresses PRISM and XINA expect.

> [!IMPORTANT]
> `qemu-system-i386 -M pinball2000,game=swe1,roms-dir=roms` creates an x86 machine with 16 MiB of RAM, one QEMU TCG i386 CPU, a QEMU ISA bus with i8259+i8254, and a set of hand-written Pinball 2000 devices mapped at the addresses PRISM/XINA expects.

The machine is defined in `qemu/pinball2000.c`. Unlike the historical prototype, we don't own the CPU loop—QEMU does.

## What QEMU gives us

| Subsystem | What it owns | How we use it |
|---|---|---|
| TCG x86 CPU | Instruction execution, protected mode, IDT dispatch, exceptions, `IRET`, `HLT` | `x86_cpus_init()` creates CPU0 from the machine's default CPU type. |
| `MemoryRegion` / address spaces | RAM aliases, ROM regions, MMIO callbacks, I/O ports | We alias RAM at physical 0, then register ROM/BAR/MMIO/port regions. |
| ISA bus | Device IRQ wiring and legacy I/O namespace | `isa_bus_new()` creates the bus. |
| i8259 PIC | IRQ prioritization, masking, INTR delivery to CPU | `i8259_init()` and `isa_bus_register_input_irqs()`. |
| i8254 PIT | Guest-programmed PIT channels at 1,193,182 Hz | `i8254_pit_init(isa_bus, 0x40, 0, NULL)`. |
| `qemu_console` | Display surface and update callbacks | `graphic_console_init()` in `p2k-display.c`. |
| chardev | Serial TX/RX for XINA/diagnostics | `serial_hd(0)` and `qemu_chr_fe_*` in the COM1 stub. |
| QEMU audio | Output voice and host backend | `AUD_register_card()` / `AUD_open_out()` in DCS audio. |
| reset framework | Post-reset hook ordering | `qemu_register_reset(p2k_post_reset, s)`. |

> [!NOTE]
> This is the main architectural break from the historical prototype: no `uc_emu_start()` batch loop, no manual interrupt-frame pushing, no hand-maintained PIC IRR/ISR shadows, no custom PIT loop, and no host-side `IRET`/IDT emulation. QEMU already does those.

That's the big win: no batch loop, no manual interrupt-frame pushing, no hand-built PIC/PIT. QEMU owns the execution engine; we just model the board.

## What Encore adds

| Module | What it does | Installed by |
|---|---|---|
| `pinball2000.c` | MachineClass registration and top-level wiring | `mc->init = pinball2000_init`. |
| `p2k-rom.c` | Load and de-interleave chip ROM banks and DCS ROM | `p2k_load_bank0/extra_banks/dcs_rom`. |
| `p2k-boot.c` | Post-reset protected-mode entry recipe | Reset handler. |
| `p2k-plx9054.c` | ROM windows, option ROM shadow, BIOS shadows, BAR5 mirrors | `p2k_map_rom_windows`. |
| `p2k-bars.c` | BAR2 SRAM plus 16 MiB all-ones sentinel window | `p2k_install_plx_bars`. |
| `p2k-plx-regs.c` | PLX BAR0 register file, 93C46 SEEPROM, DCS serial detect | `p2k_install_plx_regs`. |
| `p2k-bar3-flash.c` | Intel-style update flash, savedata seed, update bundle assembly | `p2k_install_bar3_flash`. |
| `p2k-dcs*.c` | BAR4 DCS, UART overlay, shared protocol core, optional audio mixer | Multiple `p2k-dcs` files. |
| `p2k-gx.c`, `p2k-gp-blt.c`, `p2k-display.c`, `p2k-vsync.c` | MediaGX register/FB shape, BLT engine, console output, VBLANK ticker | Multiple display/graphics files. |
| `p2k-isa-stubs.c`, `p2k-superio.c` | Legacy keyboard/CMOS/COM1/POST and SuperIO/CC5530 stubs | ISA stub installers. |
| `p2k-pci.c` | Temporary CF8/CFC PCI config table | `qemu/p2k-pci.c`. |
| `p2k-lpt-board.c` | Driver-board parallel-port protocol and desktop input bridge | LPT installer. |
| Diagnostics/bridges | Timing audit, adaptive HOTLOOP IRQ0 re-raise, diag sampler, mem-detect patch, probe-cell shim, NIC shadow | Various diagnostic modules. |

## Machine init: the guided tour

When QEMU starts the `pinball2000` machine, we follow a deliberate sequence. Think of it as setting up a stage before the actors arrive.

> [!TIP]
> See [11-machine-init.md](11-machine-init.md) for a line-by-line init ledger if debugging device initialization.

Here's the path:

1. **Require `game=` and default `roms-dir` to `roms`** — We need to know which game you want, and where the ROMs live.
2. **Alias QEMU machine RAM at physical 0** — The CPU needs 16 MiB of RAM starting at address 0.
3. **Create CPU0 through QEMU x86 CPU init** — QEMU spins up an i386 CPU.
4. **Create ISA bus, i8259, and i8254 PIT** — Standard PC interrupt and timer infrastructure.
5. **Load ROM bank0, optional banks 1-3, and optional DCS ROM** — We read the chip files from disk.
6. **Map ROM, option-ROM, BIOS, BAR5, and alias windows** — Now the CPU can see the ROMs at the addresses the game expects.
7. **Enable MediaGX TCG extension gate** — QEMU needs to know this machine uses Cyrix instructions.
8. **Install all ISA, PCI, PLX, flash, DCS, LPT, MediaGX/display, timing, and diagnostic devices** — Every board surface the game touches.
9. **Register the post-reset protected-mode boot recipe** — After QEMU resets the CPU, we drop it into protected mode at the PRISM entry point.
10. **Report the machine ready** — QEMU starts running.

See [11-machine-init.md](11-machine-init.md) for the line-by-line walk.

## The data flow

```text
 host files
 ┌──────────────┐
 │ roms/*.rom   │
 │ updates/*    │
 │ savedata/*   │
 └──────┬───────┘
        │ p2k-rom.c / p2k-bar3-flash.c
        ▼
 ┌──────────────────────────────────────────────┐
 │ QEMU MemoryRegion address space              │
 │  RAM 0..16M  ROM windows  BAR2/3/4/5  GX MMIO│
 └──────┬───────────────┬───────────────┬───────┘
        │               │               │
        │ CPU fetch/load │ MMIO/PIO      │ framebuffer alias
        ▼               ▼               ▼
 ┌────────────┐   ┌──────────────┐   ┌──────────────┐
 │ TCG i386   │   │ p2k devices  │   │ qemu_console │
 │ IDT/IRET   │   │ PLX/DCS/LPT  │   │ 640x480 view │
 │ exceptions │   │ PCI/ISA/GX   │   └──────────────┘
 └─────┬──────┘   └──────┬───────┘
       │ IRQ0/IRQ4/etc   │ DCS commands
       ▼                 ▼
 ┌────────────┐    ┌──────────────┐
 │ i8254 PIT  │───▶│ i8259 PIC    │──▶ CPU INTR
 └────────────┘    └──────────────┘
       │
       └─ QEMU virtual time; timing-audit observes, it does not drive
```

The guest programs the PIT and PIC itself through QEMU's i8254/i8259. In
`--strict`, IRQ0 follows that natural path exclusively. The default adaptive
HOTLOOP mode additionally re-raises the real i8259 IRQ0 input from a TCG
block-boundary hook; HOTLOOP-only suppresses natural PIT IRQ0 edges so there is
one source. It does not patch guest timer variables or push interrupt frames,
but it is still a host-assisted delivery path. COM1 can pulse IRQ4 when UART
interrupts are enabled. VSYNC is a board-level display-state timer.

## Runtime ownership

The guest owns timer programming and the i8259/IDT still perform interrupt
arbitration and entry. `--strict` uses PIT edges; default HOTLOOP uses a
rate-controlled host re-raise to avoid TCG/PIC edge coalescing. Our device job
remains the same: BAR4 carries DCS commands, MediaGX registers expose display
state, and LPT strobes operate independent lamp-output and switch-input state.

## Temporary patches

A few modules are explicitly temporary. We keep narrow, kill-switchable compatibility bridges while we develop fuller device models:

> [!WARNING]
> Compatibility mechanisms are explicit: the ROM-specific mem-detect rewrite,
> fixed CF8/CFC responder, and accepted `--update none` probe-cell shim. Their
> scope and maintenance rules are in [30-symptom-patches.md](30-symptom-patches.md).

* **`qemu/p2k-mem-detect.c`** — scans RAM and rewrites one byte so the game sees 14 MiB instead of 4 MiB.
* **`qemu/p2k-pci.c`** — hand-services legacy `CF8/CFC` PCI config ports from static tables.
* **`qemu/p2k-probe-cell-shim.c`** — (gated to `--update none`) writes staged DCS probe values.

These are documented debt. See [30-symptom-patches.md](30-symptom-patches.md) for the full inventory and sunset criteria.

> ⚠️ Do not reintroduce guest-frame injection or timer-variable writes. The
> existing HOTLOOP path only re-raises QEMU's IRQ0 line and lets i8259/IDT/IRET
> work normally; any replacement must preserve that boundary or improve it.

## Boundaries between QEMU and our code

| Boundary | Rule | Example |
|---|---|---|
| CPU state | Only the reset recipe writes architectural CPU state directly. | `p2k_post_reset()` sets CR0, segment caches, EIP, ESP, EFLAGS. |
| Interrupts | Device IRQs go through QEMU IRQ wires and i8259. | COM1 pulses IRQ4 through the stored `qemu_irq`. |
| Board MMIO | P2K devices own guest-visible board registers. | BAR4 translates DCS word accesses to the shared core. |
| Display | Guest framebuffer memory is ordinary RAM; QEMU console is the host view. | GX FB aliases RAM at `0x800000`, display reads that RAM. |

## Display: the tricky part

The framebuffer is just a slice of RAM at GX base + 0x800000 (640×240 RGB555, 2048-byte row pitch). The game blits into it. We alias that RAM region so QEMU's display code can read it directly and push frames to SDL or GTK. The MediaGX `DC_TIMING2` register needs to increment on each read—that's how the game knows the display controller is alive.

When the BLT engine writes to registers, we don't actually perform the blit in the emulator—we let the guest do the work by copying bytes. We just return "idle" status from the right register so the game knows it can queue the next operation.

## Development posture

The architecture favors narrow board modules. `p2k-internal.h` lists one install function per concern—ROM loading, timing audit, PLX, DCS, LPT, display. New work should follow that split: add a focused MemoryRegion, I/O region, timer, or observer, then install it from `pinball2000_init()` at the point where its dependencies already exist.

## Practical launch

The wrapper turns your friendly flags into a QEMU machine option: `pinball2000,game=$GAME,roms-dir=$ROMS_DIR`, with `update=` added for explicit update directories. Extra display, audio, serial, and monitor options remain ordinary QEMU CLI options handled outside the machine.

## See also

* [11-machine-init.md](11-machine-init.md) — exact machine init ledger.
* [12-cpu-and-timers.md](12-cpu-and-timers.md) — CPU, PIT/PIC, timing audit, Cyrix shims.
* [13-memory-map.md](13-memory-map.md) — address ranges and I/O ports.
* [14-boot-recipe.md](14-boot-recipe.md) — post-reset protected-mode entry.
* [15-rom-loading.md](15-rom-loading.md) — ROM/update/savedata loading.
* [20-plx-pci.md](20-plx-pci.md), [23-mediagx-and-display.md](23-mediagx-and-display.md), [25-dcs-sound.md](25-dcs-sound.md) — device deep dives.
* [30-symptom-patches.md](30-symptom-patches.md) — debt ledger.
