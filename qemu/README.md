# `qemu/` — Pinball 2000 custom QEMU machine

Out-of-tree QEMU **machine type** `pinball2000`. Built into a vendored,
patched, pinned `qemu-system-i386` (currently 10.0.8) by
[`../scripts/build-qemu.sh`](../scripts/build-qemu.sh). Exposes the
Williams Pinball 2000 board (Cyrix MediaGX + CS5530 + PLX 9054 + DCS-2
sound + LPT driver board) so the unmodified game ROMs run.

> [!TIP]
> **Looking for documentation?** This file is a quick device map.
> Detailed docs live in [`../docs/`](../docs/README.md):
> [architecture](../docs/10-architecture.md),
> [memory map](../docs/13-memory-map.md),
> [boot recipe](../docs/14-boot-recipe.md),
> [symptom-patch policy](../docs/30-symptom-patches.md),
> [roadmap](../docs/36-roadmap.md).

## Architectural intent

> [!NOTE]
> QEMU owns CPU execution, PIT/PIC timing, virtual timers and interrupt
> delivery — we do **not** rebuild any of that layer. Each Pinball-2000
> peripheral lands here as a normal QEMU device (`PCIDevice` for
> PCI-attached parts, `MemoryRegionOps` for MMIO, `isa_register_ioport`
> for legacy I/O, `qemu_console` for video, `CharBackend` for serial).

> [!IMPORTANT]
> A handful of files in this directory are still **symptom patches**
> (host-side RAM pokes, IDT pinning, code rewrites, cf8/cfc shims).
> Each such file carries a `STATUS: TEMPORARY SYMPTOM PATCH` block at
> the top stating the removal condition. Treat any new patch the same
> way: ship it with a written sunset criterion or do not ship it.

Full policy + grep-verified inventory:
[`../docs/30-symptom-patches.md`](../docs/30-symptom-patches.md).

## File layout — one concern per file

| File                         | Concern                                                       | Doc |
|------------------------------|---------------------------------------------------------------|-----|
| `pinball2000.c`              | `MachineClass` registration; init-order ledger                | [11](../docs/11-machine-init.md) |
| `pinball2000.h`              | Address-map / register / boot-entry constants                 | [13](../docs/13-memory-map.md)   |
| `p2k-internal.h`             | Private declarations shared between p2k-*.c modules           | —   |
| `p2k-rom.c`                  | Bank0..3 + DCS ROM de-interleave loader                       | [15](../docs/15-rom-loading.md)  |
| `p2k-boot.c`                 | Post-reset PM-entry recipe (option-ROM copy + GDT + regs)     | [14](../docs/14-boot-recipe.md)  |
| `p2k-plx9054.c`              | PLX 9054 ROM/BIOS/bank0/BAR5 windows                          | [20](../docs/20-plx-pci.md)      |
| `p2k-plx-regs.c`             | PLX 9050 BAR0 registers + 93C46 SEEPROM model                 | [20](../docs/20-plx-pci.md)      |
| `p2k-pci.c`                  | cf8/cfc PCI config-space dispatcher (**symptom patch**)       | [20](../docs/20-plx-pci.md)      |
| `p2k-bars.c`                 | BAR2 SRAM + 16 MiB sentinel (seeds from `savedata/*.nvram2`)  | [22](../docs/22-sram-bar2.md)    |
| `p2k-bar3-flash.c`           | BAR3 Intel 28F320 command protocol (seeds from `*.flash`)     | [21](../docs/21-flash-bar3.md)   |
| `p2k-dcs.c`                  | DCS-2 sound on BAR4 MMIO (state shared with dcs-uart)         | [25](../docs/25-dcs-sound.md)    |
| `p2k-dcs-uart.c`             | DCS-2 sound legacy I/O view 0x138-0x13F (state split)         | [25](../docs/25-dcs-sound.md)    |
| `p2k-dcs-core.c`             | DCS shared command/state machine                              | [25](../docs/25-dcs-sound.md)    |
| `p2k-dcs-audio.c`            | Audio backend wiring                                          | [25](../docs/25-dcs-sound.md)    |
| `p2k-dcs-adsp.c`             | Experimental u109/u110 + update sound-flash path              | [25](../docs/25-dcs-sound.md)    |
| `p2k-lpt-board.c`            | BT-94/107 driver-board protocol on 0x378-0x37A                | [26](../docs/26-lpt-board.md)    |
| `p2k-gx.c`                   | Cyrix MediaGX 16 MiB MMIO + framebuffer alias to RAM 0x800000 | [23](../docs/23-mediagx-and-display.md) |
| `p2k-gp-blt.c`               | GP_BLT engine emulation                                       | [23](../docs/23-mediagx-and-display.md) |
| `p2k-gfxlist-watch.c`        | Display-list change watcher                                   | [23](../docs/23-mediagx-and-display.md) |
| `p2k-display.c`              | 640×480 ARGB8888 / x1r5g5b5 console reading FB at 0x800000    | [23](../docs/23-mediagx-and-display.md) |
| `p2k-vsync.c`                | ~57 Hz VSYNC ticker (BAR2[4]=1 + DC_TIMING2 walk)             | [24](../docs/24-vsync.md)        |
| `p2k-isa-stubs.c`            | i8042 / CMOS / POST / COM1 minimal stubs + UART TX filter     | [27](../docs/27-isa-stubs.md)    |
| `p2k-superio.c`              | W83977EF (0x2E/0x2F) + Cyrix CC5530 (0xEA/0xEB) chip-IDs      | [27](../docs/27-isa-stubs.md)    |
| `p2k-mediagx-gate.c`         | MediaGX TCG opcode-extension gate                             | [31](../docs/31-mediagx-gate.md) |
| `p2k-cyrix-ccr.c`            | Cyrix CCR and MediaGX instruction enable state                | [31](../docs/31-mediagx-gate.md) |
| `p2k-mem-detect.c`           | XINU BT-130 mem_detect rewrite (**symptom patch**)            | [33](../docs/33-mem-detect.md)   |
| `p2k-probe-cell-shim.c`      | DCS probe-cell shim (gates on XINU clkint install)            | [34](../docs/34-probe-cell-shim.md) |
| `p2k-nic-dseg.c`             | BT-131 SMC8216T LAN-ROM shadow into D-seg (**symptom patch**) | [30](../docs/30-symptom-patches.md) |
| `p2k-diag.c`                 | Read-only PIT/PIC/IDT/XINU sampler (`P2K_DIAG=1`)             | —   |
| `p2k-timing-audit.c`         | Boot-time PIT/PIC/timer audit                                 | [12](../docs/12-cpu-and-timers.md) |

## Build & run — pointers

> [!TIP]
> * `bash ../scripts/build-qemu.sh` — vendor + build (idempotent).
>   Full flag set: [`../docs/03-cli-reference.md`](../docs/03-cli-reference.md#build-qemush).
> * `bash ../scripts/run-qemu.sh --game swe1` — happy-path launch.
>   Full flag set: [`../docs/03-cli-reference.md`](../docs/03-cli-reference.md#run-qemush).

## Removal-condition discipline (short form)

> [!WARNING]
> When you change a `STATUS: TEMPORARY SYMPTOM PATCH` file:
>
> 1. Keep it kill-switchable (env var) so it can be disabled while the
>    real device is being wired up.
> 2. Update the `STATUS:` block if the removal condition changes.
> 3. When the real-device path lands, **remove** the symptom patch in
>    the same commit — do not leave both running.

Long form, with rationale:
[`../docs/30-symptom-patches.md`](../docs/30-symptom-patches.md).
