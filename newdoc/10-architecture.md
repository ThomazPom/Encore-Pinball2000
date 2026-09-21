# 10 — Architecture

Encore is a custom QEMU machine, not an application wrapped around a stock PC
guest. The launcher prepares the host and translates user options; the
`pinball2000` machine defines the guest-visible board; original Williams ROMs
and XINA then execute under QEMU's i386 TCG engine.

```text
scripts/run-qemu.sh
        │  host preparation, assets, policy, QEMU arguments
        ▼
custom qemu-system-i386
        ├── upstream QEMU x86/ISA devices and execution infrastructure
        ├── Encore machine/device modules under qemu/p2k-*.c
        └── four narrowly scoped upstream patch families
                │
                ▼
       PRISM option ROM → XINA → game/update code
```

This separation matters during diagnosis: a launcher choice, a host backend, a
device model and guest software are different owners even when the user sees a
single window.

## Layers and responsibilities

| Layer | Owns | Does not own |
|---|---|---|
| launcher and runtime scripts | host packages, asset acquisition, display/audio/network policy, savedata path, command construction | guest device behavior or XINA timing |
| custom QEMU build | upstream source selection, patch application, minimal i386-softmmu build | ROM/update payloads or persistent cabinet state |
| `pinball2000` machine | CPU/bus wiring, ROM windows, board devices, reset entry, diagnostics | game rules and XINA services |
| Williams guest software | scheduler, service menus, game logic, device drivers | host display/audio implementation |
| optional host integrations | SDL/KMSDRM/Wayland, QEMU audio, `ppdev`, Slirp/passt/TAP, FFmpeg | authority to rewrite guest behavior |

The machine is behavior-oriented. Some surfaces use complete upstream QEMU
devices; others model only the registers and protocol exercised by the known
software. A more elaborate chip model is not automatically more faithful if it
changes the observed board contract.

## What remains upstream QEMU

The machine subclasses QEMU's `X86MachineState` and uses:

- one i486-class TCG CPU and 16 MiB RAM;
- an ISA bus with upstream dual i8259 interrupt controllers;
- upstream i8254 PIT channel 0 feeding IRQ0 through the i8259 and CPU `INTR`;
- upstream i8042/PS/2 keyboard hardware, dynamically connected by Encore's
  input router;
- upstream MC146818 RTC/CMOS with base year 1999;
- QEMU memory-region, timer, chardev, display, audio and networking
  infrastructure.

There is no IOAPIC, floppy, CD-ROM or ordinary QEMU parallel device. Encore's
LPT model owns the guest port instead.

> [!IMPORTANT]
> There is one normal IRQ0 delivery path: i8254 → i8259 → x86 interrupt entry →
> XINU `clkint`. Encore does not run a parallel IRQ source or inject replacement
> edges. The default timing module does arm a post-IRET virtual-clock rendezvous
> that asks the vCPU to return to QEMU near the next projected PIT deadline;
> the i8254 still generates the edge. `--speed-target` separately scales the
> programmed channel-0 divisor.

See [CPU and timers](12-cpu-and-timers.md) for the timing path and measurement
hooks.

## Encore-owned guest hardware

| Area | Principal owners | Contract |
|---|---|---|
| ROMs and reset | `p2k-rom.c`, `p2k-plx9054.c`, `p2k-boot.c` | deinterleave chips, map immutable windows, enter the PRISM protected-mode entry point |
| fixed platform surfaces | `p2k-pci.c`, `p2k-plx-regs.c`, `p2k-superio.c`, `p2k-cyrix-ccr.c`, `p2k-isa-stubs.c` | topology and I/O behavior consumed during XINA discovery |
| persistent PRISM storage | `p2k-bars.c`, `p2k-bar3-flash.c`, `p2k-plx-regs.c` | BAR2 SRAM, BAR3 update flash and 93C46 SEEPROM |
| DCS sound | `p2k-dcs-core.c`, `p2k-dcs.c`, `p2k-dcs-uart.c`, `p2k-dcs-audio.c`, `p2k-dcs-adsp.c`, `p2k-adsp2105-core.c` | one protocol core, two guest frontends and selectable content engines feeding QEMU audio |
| driver board and input | `p2k-lpt-board.c`, `p2k-switch-keymap.c` | emulated, physical or disconnected LPT plus cabinet/XINA keyboard routing |
| MediaGX graphics | `p2k-mediagx-gate.c`, `p2k-gx.c`, `p2k-gp-blt.c`, `p2k-vsync.c`, `p2k-display.c`, `p2k-video-capture.c` | machine-gated instructions, graphics registers, blits, scan timing, host presentation and recording |
| network | `p2k-smc8416.c`, `p2k-nic-dseg.c` | optional ISA NIC plus boot-visible LAN-ROM shadow |
| update board | `p2k-pub-card.c` | optional experimental PUB flash window |
| diagnostics and compatibility | `p2k-diag.c`, `p2k-timing-audit.c`, `p2k-stall-profile.c`, `p2k-mem-detect.c`, `p2k-gfxlist-watch.c`, `p2k-guest-extensions.c` | observation by default; explicitly selected, signature-matched mutation where documented |

The complete address inventory belongs in [Memory map](13-memory-map.md).
Subsystem behavior belongs in the linked device pages rather than being
duplicated here.

## Machine construction

`pinball2000_init()` builds the machine in dependency order:

1. require or resolve the game through the LPT policy;
2. alias 16 MiB RAM at address zero and create the single i486 CPU;
3. create ISA, dual i8259, i8042 and i8254; insert the read-only IRQ0
   observation tap between PIT and PIC;
4. load ROM chips, map option-ROM/BIOS/game/sound windows and enable the
   machine-gated MediaGX TCG extensions;
5. install fixed ISA, SuperIO, Cyrix and PCI configuration surfaces;
6. prepare persistence, then install PLX SRAM/register/flash, DCS, LPT, input,
   MediaGX, display and VSYNC devices;
7. install opt-in extensions plus always-available diagnostic hooks, network
   shadow, optional PUB card and graphics-state observer;
8. register the post-reset protected-mode entry recipe.

Command-line `-device p2k-smc8416,...` realizes the optional NIC through
QEMU's device model; it is not forced into every machine instance.

The machine adds QOM properties for `game`, `rom-revision`, `roms-dir`,
`savedata-dir`, `update` and `pub-card`. The product launcher is the supported
human interface for them and also wires the required host backends.

## Boot without a PC BIOS POST

The production path does not execute a conventional real-mode PC BIOS boot.
After every QEMU system reset, `p2k_post_reset()`:

1. resets volatile guest-extension state;
2. copies the first 32 KiB of ROM bank 0 to physical `0x00080000`;
3. writes a small flat protected-mode GDT at `0x00088000`;
4. programs CPU0 for protected mode with `CS=0x08`, data selectors `0x10`,
   `EIP=0x000801D9`, `ESP=0x0008B000` and interrupts disabled.

From that point the original option ROM, XINA and selected game/update own the
boot. BIOS shadows remain mapped for guest code that references them, but the
high reset-vector mirror is not the entry path.

See [Boot recipe](14-boot-recipe.md) and
[ROM and update loading](15-rom-loading.md).

## Shared state has one owner

Several architectural rules prevent subtly divergent emulations:

- DCS BAR4 MMIO and I/O ports `0x138–0x13f` are thin frontends to one response
  queue and handshake core;
- all DCS content engines consume commands from that core and differ only in
  how they produce PCM;
- display paths read the same MediaGX registers and guest framebuffer, whether
  presentation uses direct SDL or a QEMU display surface;
- cabinet keys, monitor-injected keys and direct-renderer keys converge on the
  same input router and LPT state;
- LPT switch inputs and lamp/coil outputs remain separate state even though
  they share the driver-board protocol;
- savedata paths are owned by the three emulated persistent devices, with
  atomic full-image writes at exit;
- timing accounting consumes upstream PIT/PIC/TCG events; its default deadline
  rendezvous only changes when the vCPU re-polls, not the IRQ source or guest
  tick count.

When adding an interface, extend the existing owner instead of introducing a
parallel FIFO, switch matrix, framebuffer or timing state machine.

## Host threads and virtual time

The guest CPU and QEMU device callbacks remain governed by QEMU. Host workers
are used only where blocking or presentation work must leave that path:

| Worker or callback | When active | Role |
|---|---|---|
| `dcs-mailbox` | threaded ADSP engines; the default hybrid engine uses it | consume DCS mailbox commands and maintain the PCM ring |
| QEMU audio callback | an audio backend is active | pull PCM from the selected DCS engine into QEMU audio |
| `p2k-sdl-render` | default direct framebuffer | read RAM-backed graphics state, render SDL and collect input events |
| `p2k-qemu-display` | experimental asynchronous QEMU framebuffer | submit prepared frames away from the vCPU path |
| `p2k-video-capture` plus FFmpeg child | `--record-video` | copy complete frames into a nonblocking encoder pipe |

Exit and shutdown notifiers stop and join these workers before their state is
released. VSYNC itself is a QEMU virtual-clock timer, not a sleeping host
thread: it updates the guest-visible scan counter at sub-frame intervals and
pulses the frame flag at roughly 57 Hz.

> [!NOTE]
> A host worker can improve isolation from presentation or audio stalls, but it
> does not make host scheduling irrelevant. Timing conclusions still require
> guest delivery, depth, IStack and window-distribution evidence from the
> benchmark.

## Persistent and volatile mutation

Normal guest writes may change BAR2 NVRAM, BAR3 update flash and the PLX
SEEPROM; those three device images persist on clean exit. ROM chip inputs and
mapped ROM windows are not modified. See
[Persistent cabinet state](09-savedata.md).

Optional compatibility features are narrower:

- guest extensions inject a signature-resolved payload into guest RAM and are
  reset with the machine;
- the memory-detection override is opt-in and signature matched;
- diagnostic samplers are observers; the timing module also owns the documented
  default post-IRET deadline rendezvous, while experimental profilers remain
  opt-in.

The distinction must remain visible in both code and documentation: a helper
that mutates guest RAM is not a device fix merely because it is convenient.

## Build and upstream boundary

Encore does not vendor a full QEMU tree. `scripts/build-qemu.sh` downloads a
pinned upstream release into a cache, applies exactly one zero-fuzz variant
from each patch family, copies `pinball2000.c` plus every `p2k-*` source into
`hw/i386/`, generates the owned Meson/Kconfig source list and builds a minimal
`i386-softmmu` binary without default devices.

The four upstream patch families provide:

- machine-gated MediaGX instructions missing from stock i386 TCG;
- IRQ entry/intack/IRET/PIC observation hooks;
- the TCG execution-loop callback used by timing observation;
- the weak PIT divisor hook used only for deliberate speed targets.

All default hooks preserve upstream behavior for machines other than
`pinball2000`. Source-range patch validation is not enough to promote a QEMU
version: compilation, boot, normal play and benchmark validation remain
separate gates.

Maintainers can continue with the [`qemu/` source map](../qemu/README.md) for
per-file ownership and patch-range validation commands.

## Where to investigate next

| Question | Continue with |
|---|---|
| why and where execution begins | [Boot recipe](14-boot-recipe.md) |
| exact RAM/MMIO/I/O layout | [Memory map](13-memory-map.md) |
| IRQ0 cadence, delivery and stack safety | [CPU and timers](12-cpu-and-timers.md) |
| frame generation and presentation | [MediaGX and display](23-mediagx-and-display.md) |
| audio protocol and engines | [DCS sound](25-dcs-sound.md) |
| cabinet protocol and physical bridge | [LPT driver-board interface](26-lpt-board.md) |
| XINA services and scheduler | [XINA OS deep dive](06-xina-os-deep-dive.md) |
| deliberate shims and incomplete chip models | [Compatibility support](30-compatibility-support.md) |

---

← [Troubleshooting](04-troubleshooting.md) ·
[Documentation index](README.md) · [CPU and timers](12-cpu-and-timers.md)
