# 14 — Boot Recipe

What this doc covers: the post-reset boot recipe implemented in `qemu/p2k-boot.c`. QEMU starts from its normal reset machinery, then the `pinball2000` reset handler stages the PRISM option ROM and rewrites CPU0 into the protected-mode state the Williams runtime expects.

## Why there is a recipe at all

A real PC-compatible BIOS would reset the CPU at the high reset vector, enumerate option ROMs, run the PRISM option ROM's real-mode path, and eventually transfer control to protected-mode game code. The QEMU machine does not ship or require a known-good real Pinball 2000 BIOS image. Instead, it reproduces the proven final state: the first 32 KiB of game ROM bank0 is the PRISM option-ROM blob, copied into RAM at `0x80000`, and the CPU starts at its protected-mode entry `0x801D9` (`qemu/pinball2000.h:104-127`).

> ℹ️ The code still maps BIOS shadows for compatibility, but the normal boot path does not execute BIOS POST. The high BIOS reset mirror is explicitly documented as “not executed” in the P2K boot path (`qemu/p2k-plx9054.c:100-102`).

## Registration and reset ordering

Machine init registers `p2k_post_reset()` as a QEMU reset handler after all devices have been installed (`qemu/pinball2000.c:130-131`). The boot file notes why: QEMU runs registered reset handlers after device reset, so writes to `env->eip`, `env->cr0`, and segment caches are not clobbered by x86 reset (`qemu/p2k-boot.c:9-12`).

```text
QEMU machine init
    │
    ├─ map RAM, ROM windows, BARs, I/O devices
    └─ qemu_register_reset(p2k_post_reset, s)

QEMU reset
    │
    ├─ reset devices and CPU
    └─ p2k_post_reset(s)
          ├─ copy bank0[0..32K) → 0x80000
          ├─ build temporary GDT
          └─ set CPU0 CR0/segments/EIP/ESP/EFLAGS
```

## Recipe constants

| Constant | Value | Meaning | Source |
|---|---:|---|---|
| `P2K_OPTROM_SIZE` | `0x00008000` | 32 KiB PRISM option ROM. | `qemu/pinball2000.h:131` |
| `P2K_OPTROM_LOAD_ADDR` | `0x00080000` | RAM staging address. | `qemu/pinball2000.h:132` |
| `P2K_PM_ENTRY_EIP` | `0x000801D9` | Protected-mode entry point. | `qemu/pinball2000.h:133` |
| `P2K_INITIAL_ESP` | `0x0008B000` | Initial stack pointer. | `qemu/pinball2000.h:134` |
| `P2K_INITIAL_CS_SEL` | `0x0008` | Flat code selector. | `qemu/pinball2000.h:135` |
| `P2K_INITIAL_DS_SEL` | `0x0010` | Flat data selector for DS/ES/SS/FS/GS. | `qemu/pinball2000.h:136` |
| `P2K_GDT_BASE` | `0x00088000` | Temporary GDT address in current QEMU code. | `qemu/p2k-boot.c:21-44` |
| `P2K_GDT_LIMIT` | `0x1F` | Four 8-byte descriptors minus one. | `qemu/p2k-boot.c:43-44` |

> [!WARNING]
> Older notes and the prompt's inherited boot outline mention a GDT at `0x1000`. HEAD does not do that. `qemu/p2k-boot.c` explains that `0x1000` reproduced a historical location but exposed a wild jump to `0x1008`; the current GDT is `0x88000` (`qemu/p2k-boot.c:21-42`).

## GDT contents

> [!NOTE]
> The GDT descriptors preserve historical compatibility types (`0x9F` for code, `0x93` for data) to match the proven boot path.

`p2k_build_gdt()` writes 32 bytes to `P2K_GDT_BASE` (`qemu/p2k-boot.c:46-61`):

| Entry | Selector | Descriptor bytes | Role | Source |
|---:|---:|---|---|---|
| 0 | — | all zero | Null descriptor. | `qemu/p2k-boot.c:54-55` |
| 1 | `0x08` | `FF FF 00 00 00 9F CF 00` | 32-bit flat code, compatibility type `0x9F`. | `qemu/p2k-boot.c:48-57` |
| 2 | `0x10` | `FF FF 00 00 00 93 CF 00` | 32-bit flat data, compatibility type `0x93`. | `qemu/p2k-boot.c:50-58` |
| 3 | `0x18` | `FF FF 00 00 00 9B 0F 00` | 16-bit code descriptor used by some PRISM transitions. | `qemu/p2k-boot.c:52-58` |

The CPU's GDTR is then set to base `0x88000`, limit `0x1F` (`qemu/p2k-boot.c:104-105`).

## Step-by-step reset handler

| # | Action | Source |
|---:|---|---|
| 1 | Return immediately if bank0 failed to load. | `qemu/p2k-boot.c:72-79` |
| 2 | Copy `s->bank0[0..0x7FFF]` to physical `0x80000`. | `qemu/p2k-boot.c:81-83` |
| 3 | Build and write the temporary GDT. | `qemu/p2k-boot.c:88-90`, `qemu/p2k-boot.c:46-61` |
| 4 | Iterate CPUs and select CPU0. | `qemu/p2k-boot.c:91-99` |
| 5 | Set `CR0.PE` and `CR0.ET`; set protected-mode/32-bit hflags. | `qemu/p2k-boot.c:100-102` |
| 6 | Load GDTR base/limit. | `qemu/p2k-boot.c:104-105` |
| 7 | Program CS selector `0x08`, flat base 0, 4 GiB limit, 32-bit code flags. | `qemu/p2k-boot.c:107-111` |
| 8 | Program ES/SS/DS/FS/GS selector `0x10`, flat base 0, 4 GiB limit, writable data flags. | `qemu/p2k-boot.c:112-118` |
| 9 | Set `EIP = 0x801D9`. | `qemu/p2k-boot.c:120` |
| 10 | Set `ESP = 0x8B000`. | `qemu/p2k-boot.c:121` |
| 11 | Set `EFLAGS = 0x00000002`, so IF is clear. | `qemu/p2k-boot.c:122` |
| 12 | Log the final PM entry tuple. | `qemu/p2k-boot.c:124-125` |

## Final CPU state

| Register / state | Value after recipe | Source |
|---|---:|---|
| `CR0.PE` | `1` | `qemu/p2k-boot.c:100` |
| `CR0.ET` | `1` | `qemu/p2k-boot.c:100` |
| `CS:EIP` | `0x0008:0x000801D9` | `qemu/p2k-boot.c:107-111`, `qemu/p2k-boot.c:120` |
| `DS/ES/SS/FS/GS` | selector `0x0010`, flat 4 GiB data | `qemu/p2k-boot.c:112-118` |
| `ESP` | `0x0008B000` | `qemu/p2k-boot.c:121` |
| `EFLAGS` | `0x00000002` (`IF=0`) | `qemu/p2k-boot.c:122` |
| `GDTR` | base `0x00088000`, limit `0x1F` | `qemu/p2k-boot.c:104-105` |
| Paging | off | No paging bits are enabled by this recipe. |

## Where bank0 comes from

`p2k_post_reset()` assumes `s->bank0` is already populated. Machine init loads it before registering reset: `p2k_load_bank0(s)` must succeed, then extra banks and DCS ROM are best-effort (`qemu/pinball2000.c:86-92`). The bank loader reads host chip files and de-interleaves 16-bit pairs; see [15-rom-loading.md](15-rom-loading.md).

## Comparison to a real BIOS path

| Real PC/Pinball 2000 expectation | QEMU machine shortcut |
|---|---|
| CPU resets at high real-mode vector. | QEMU reset happens, then reset hook writes protected-mode CPU state. |
| BIOS POST initializes chipset, shadows ROMs, and scans option ROMs. | Device MemoryRegions are already mapped by machine init. |
| PRISM option ROM's real-mode entry eventually reaches protected mode. | We stage the option ROM bytes and jump straight to the known PM entry. |
| BIOS image is required. | `roms/bios.bin` is optional compatibility data for shadow regions (`qemu/p2k-plx9054.c:71-109`). |

This bypass is intentional: Williams shipped the PRISM boot blob inside the game ROMs themselves, and the QEMU branch models the board state that the game needs rather than depending on an unavailable complete BIOS.


## Why `0x801D9` and not the real-mode entry

The header preserves the historical boot-recipe note: `0x801D9` skips the real-mode call pair at `0x801BF/0x801C4` that would normally be reached from a BIOS `INT 19` path (`qemu/pinball2000.h:113-119`). The QEMU branch is therefore reproducing the final protected-mode entry state, not simulating every BIOS instruction that would have led there.

## What the recipe deliberately does not do

| Not done | Reason |
|---|---|
| Load or execute a full BIOS image | `roms/bios.bin` is optional shadow data; the PM-entry hook is the proven path (`qemu/pinball2000.h:120-127`). |
| Build an IDT | QEMU CPU owns IDT semantics, and XINU/PRISM will load its own IDT later. |
| Enable paging | The recipe only sets `CR0.PE` and `CR0.ET` (`qemu/p2k-boot.c:100`). |
| Inject a fake IRQ0 handler | QEMU i8254/i8259 deliver PIT interrupts naturally after the guest programs them (`qemu/pinball2000.c:75-85`). |
| Install the historical #UD catchall | MediaGX `0F 3x` handling is in TCG helpers/gates, not a guest-visible IDT[6] trampoline (`qemu/p2k-mediagx-gate.c:1-31`). |

> [!TIP]
> The recipe does not build an IDT or install IRQ0 handlers. QEMU's i8254/i8259 deliver interrupts naturally once the guest programs the PIT.

## Boot-state checklist for debugging

> [!WARNING]
> If early boot regresses, verify this checklist before blaming devices. The reset hook must run after CPU reset, RAM must contain the option ROM, and GDTR/CR0/EFLAGS must match the expected state.

If early boot regresses, verify this checklist before blaming devices:

1. `p2k_load_bank0()` succeeded and `s->bank0` is non-null (`qemu/p2k-rom.c:64-69`).
2. The reset hook ran after CPU reset, visible through its info banner (`qemu/p2k-boot.c:124-125`).
3. Physical `0x80000` contains the first 32 KiB of bank0 (`qemu/p2k-boot.c:81-83`).
4. GDTR points at `0x88000`, not a stale low address (`qemu/p2k-boot.c:104-105`).
5. `EFLAGS` has only bit 1 set, so IF is clear until guest code enables interrupts (`qemu/p2k-boot.c:122`).
6. The option-ROM shadow at `0xC0000` is mapped separately from the execution copy (`qemu/p2k-plx9054.c:121-125`).
7. BAR3 flash is seeded or intentionally left erased before the guest resource walker reaches it (`qemu/p2k-bar3-flash.c:310-365`).

## See also

* [10-architecture.md](10-architecture.md) — top-level machine structure.
* [11-machine-init.md](11-machine-init.md) — when the reset hook is registered.
* [12-cpu-and-timers.md](12-cpu-and-timers.md) — CPU ownership after the jump.
* [13-memory-map.md](13-memory-map.md) — option-ROM/GDT/BIOS addresses.
* [15-rom-loading.md](15-rom-loading.md) — how `s->bank0` is built.
* [31-mediagx-gate.md](31-mediagx-gate.md) — MediaGX instruction support.
