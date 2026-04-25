# 07 — Memory Map

The Pinball 2000 head is an embedded MediaGX PC with a single PLX
9050 PCI-to-local-bus bridge, a custom PRISM ROM board exposing
several BAR windows, and a DCS-2 audio board on another PCI function.
Encore's memory map mirrors this layout and is defined at the top of
`include/encore.h`.

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## Summary

| Guest range | Size | Backing | Use |
|---|---:|---|---|
| `0x00000000` | 16 MB | direct RAM | Guest RAM (code + data + stack) |
| `0x000A0000` |  128 KB | direct RAM | VGA framebuffer area (historical) |
| `0x000C0000` |   32 KB | direct RAM | PRISM option ROM shadow |
| `0x000F0000` |   64 KB | direct RAM | BIOS shadow |
| `0x00800000` |    — | inside RAM | GX_FB — MediaGX framebuffer |
| `0x08000000` |   16 MB | MMIO | PLX bank 0 (`LAS3BA`) |
| `0x08800000` |   16 MB | MMIO | PLX bank 1 (`CS0BASE`) |
| `0x09800000` |   16 MB | MMIO | PLX bank 2 (`CS1BASE`) |
| `0x0A800000` |   16 MB | MMIO | PLX bank 3 (`CS2BASE`) |
| `0x0B800000` |    8 MB | MMIO | PLX CS3 — DCS-2 sound ROM |
| `0x10000000` |   96 B  | MMIO | PLX 9050 register file (BAR0) |
| `0x11000000` |  128 KB | MMIO | BAR2 — SRAM + character display |
| `0x12000000` |    4 MB | MMIO | BAR3 — update flash |
| `0x13000000` |   16 MB | MMIO | BAR4 — DCS audio interface |
| `0x14000000`…`0x17000000` | 4 × 16 MB | MMIO | BAR5 — ROM bank flash windows |
| `0x18000000` |    8 MB | MMIO | DCS-2 sound ROM BAR5 |
| `0x40000000` |    8 MB | MMIO | MediaGX config registers |
| `0x40800000` |    4 MB | MMIO | MediaGX framebuffer region |
| `0xFFFF0000` |   64 KB | direct RAM | BIOS reset vector mapping |

The MMIO dispatcher is `src/bar.c`. It splits on
the high 8 bits of the faulting address to pick a bank.

## RAM

`RAM_SIZE` is 16 MiB. The real motherboards support up to 128 MB of
EDO DRAM but Williams shipped 16 MB (typical) or 32 MB. Our guest
never needs more than 14 MB, and `mem_detect()` is patched to return
14 MB regardless of what the memory controller replies (see
[15-watchdog-scanner.md](15-watchdog-scanner.md)).

RAM is `mmap`'d once and mapped into Unicorn with
`uc_mem_map_ptr`. That means `g_emu.ram[addr] = v` on the host is
byte-identical to a guest write. The four accessors `RAM_RD8`,
`RAM_RD16`, `RAM_RD32`, `RAM_WR16`, `RAM_WR32` in `encore.h` are
trivial pointer-casts.

## BAR0 — PLX 9050 registers

96 bytes of config registers. Encore answers everything as "safe
defaults"; the only register that matters is `LAS3BA`/`BIGEND0` which
the guest probes during init.

## BAR2 — SRAM / character display

128 KiB of battery-backed SRAM. The first ~1 KiB is used as a
character display RAM region; the rest is NVRAM. On each write we mirror
into `g_emu.bar2_sram[]` so we can persist it to disk as `.nvram2`
on clean exit. See [10-savedata.md](10-savedata.md).

The game also writes VSYNC handshake bytes at offsets 4..7 of BAR2;
our host-side VBLANK driver writes `01 00 00 00` there every VBLANK
(`cpu.c:529-534`), matching what the real board would strobe.

## BAR3 — update flash

4 MiB erased-to-`0xFF`. Contents come from one of:

* an explicit `update.bin` file (`--update FILE`);
* the four component ROMs concatenated at fixed offsets;
* an installer-ZIP that is unzipped and then treated as a directory.

Layout inside the flash (cross-referenced against `rfm_15_update.bin`):

```
0x000000   bootdata.rom        (32 KB)
0x008000   im_flsh0.rom        (~615 KB)
0x09e1f4   game.rom             (~2.5 MB)
0x308ff4   symbols.rom          (~750 KB)
```

The `symbols.rom` blob is what `src/symbols.c` scans for the
`"SYMBOL TABLE"` magic to build the in-memory symbol index.

## BAR4 — DCS audio

16 MiB MMIO window. The real board maps:

* a reset / handshake register file around `+0x0`
* a command FIFO at `+0x???`
* a status word at `+0x???`

Encore does not emulate these as real MMIO; instead we pre-fill BAR4
with `0xFF` at boot and, depending on `--dcs-mode`, either:

* patch the DCS probe to unconditionally declare BAR4 present and
  route commands through `sound.c`, or
* let the game take the I/O-UART path (ports `0x138..0x13F`) where
  our `io.c` handlers answer.

Full story: [13-dcs-mode-duality.md](13-dcs-mode-duality.md).

## BAR5 — ROM banks

Four 16 MB windows, one per PLX chip-select bank. The game reads the
DCS presence table at bank 0 offset `0x10000` (4 KiB) to decide
whether DCS is fitted. On dearchived bundles this table is stripped,
so we leave the chip-ROM data in place (it may or may not be valid)
and rely on the `--dcs-mode bar4-patch` byte-patch to override the
decision.

## MediaGX registers and framebuffer

`GX_BASE = 0x40000000` holds the graphics + display-controller
register file. Key offsets used by the game:

| Offset | Symbol | Role |
|---:|---|---|
| 0x8300 | `DC_UNLOCK` | writeable unlock cookie |
| 0x8304 | `DC_GENERAL_CFG` | enable bits |
| 0x8308 | `DC_TIMING_CFG` | video-mode timing |
| 0x8310 | `DC_FB_ST_OFFSET` | start offset of visible FB |
| 0x8314 | `DC_LINE_SIZE` | pixels per row |
| 0x8354 | `DC_TIMING2` | current scanline / VBLANK |
| 0x8200 | `GP_RASTER_MODE` | 2D raster op mode |
| 0x820C | `GP_BLT_STATUS` | blit engine busy bit |

The display controller's start offset at `DC_FB_ST_OFFSET` is read by
`display.c:display_update()` and used as the base of the 640×240 scan
buffer.

## BIOS reset and the first instruction

Real hardware starts at `0xFFFFFFF0` in real mode. Encore skips that
entirely. `cpu_setup_protected_mode()`:

1. Builds the GDT at `0x18000`.
2. Builds the IDT at `0x19000`, filled with IRET+EOI stubs.
3. Copies the PRISM option ROM (bank 0) into RAM at `0x80000` (the
   game's expected post-BIOS load address).
4. Sets `CR0.PE = 1`, configures segment registers.
5. Jumps directly to the PRISM option ROM entry inside RAM.

This corresponds to what a real BIOS would do after running `Init2`
from the option ROM. The BIOS image (`BIOS_SIZE = 64 KB`) is loaded
only so that any code path that still references `0xFFFF0000` lands on
plausible bytes; we never run it.

## PRISM card detailed notes

These details were captured from live boot traces of the original
P2K runtime and from analysis of the PRISM option ROM. They are not
all directly used by Encore's code — Encore implements only what
the game actually exercises — but they are useful when investigating
new behaviour or extending BAR coverage.

### PRISM PCI BAR layout

| BAR     | Config reg | Runtime address | Purpose                                   |
|---------|-----------|-----------------|-------------------------------------------|
| BAR0    | `0x10`    | `0x10000000`    | PLX 9050 register file                    |
| BAR2    | `0x18`    | `0x11000000`    | DCS-2 interface / character display out   |
| BAR3    | `0x1C`    | `0x12000000`    | Update-flash region (returns `0xFF` if no update) |
| BAR4    | `0x20`    | `0x13000000`    | DCS audio command port                    |
| BAR5    | `0x24`    | `0x14000000`    | ROM flash bank 0                          |
| ROM BAR | `0x30`    | `0x18000001`    | Expansion / DCS sound ROM bank 4          |

BAR2 doubles as a character/text display output: the original
runtime writes ASCII bytes to it for debug messages.

### PLX 9050 registers (BAR0)

| Register | Offset | Typical value | Meaning                          |
|----------|--------|---------------|----------------------------------|
| LAS0BA   | `0x14` | `0x00100001`  | DCS/system image window          |
| LAS3BA   | `0x20` | `0x08000001`  | ROM bank 0 base                  |
| CS0BASE  | `0x3C` | `0x08800001`  | ROM bank 1                       |
| CS1BASE  | `0x40` | `0x09800001`  | ROM bank 2                       |
| CS2BASE  | `0x44` | `0x0A800001`  | ROM bank 3                       |
| CS3BASE  | `0x48` | `0x0B800001`  | ROM bank 4 (DCS-2 sound)         |
| CNTRL    | `0x50` | dynamic       | SEEPROM bit-bang (CLK/CS/DI/DO)  |

Note: `LAS0BA`–`LAS3BA` are host DMA windows, not ROM bank
selects. `CS0BASE`–`CS3BASE` are the actual bank addresses XINU
uses. After the game starts, XINU zeros the PLX registers and
re-programs them from SEEPROM data.

### Option ROM

The first 32 KB of the correctly de-interleaved ROM bank 0 is a
valid ISA option ROM with header `55 AA` and size byte `0x40`
(64 × 512 = 32 768 bytes). Its checksum is valid. The option ROM
hooks `INT 0x19`, switches the CPU to protected mode, and jumps to
the game image at `0x08170284`.

### DCS-2 BAR2 channel-init protocol

The DCS-2 task initialises the audio board in three phases by
writing DWORD sequences to BAR2 MMIO. Phase 3 ends by polling a
completion flag at `RAM[0x2797C4]` for `0xFFFF`. Once the flag is
set, the task calls `regres()` with an 8-space resource ID,
unblocking the XINA shell. Encore's BAR2 SRAM faithfully echoes
back every written byte so the channel-init `REPNZ SCAS` loop
terminates correctly.

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
