# 44 — XINA OS Deep Dive

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

XINA is the operating environment that every Pinball 2000 (P2K) game
binary runs on top of. This document gathers what we know about its
heritage, process model, boot sequence, on-disk layout and the
quirks Encore has to accommodate.

## 1. Heritage: XINU → XINA

XINA stands for "It's Not APPLE". APPLE was the operating system used
on Williams' earlier WPC platform; the acronym expanded to "Applied
Pinball Programming Language Environment".

XINA itself is built on PC-XINU, an embedded multitasking OS
originally designed and documented by Douglas Comer at Purdue
University. XINU stands for "XINU Is Not UNIX". Williams chose XINU
over Linux for two reasons: a simpler real-time threading model and a
non-GPL licence — Williams' legal team did not want to be obligated
to release kernel modifications under the GPL, as a modified Linux
kernel would have required.

The version banner printed at boot reflects two layers
independently: `XINA: V1.12` is the application layer, and `XINU: V7`
is the underlying kernel.

## 2. Process model (lightweight tasks)

XINU uses cooperative multitasking with a priority ready-queue.
Processes are created with `create()`, placed in the ready queue with
`ready()`, and the scheduler runs `resched()` to transfer control to
the highest-priority ready process.

The XINA shell runs as a XINU process at a fixed priority. Each game
subsystem (display manager, sound manager, switch scanner, …) is a
separate XINU process.

Key scheduler globals you will see referenced from Encore:

* `clkruns` — clock-interrupt enable flag.
* `prnull` — null/idle process descriptor.
* `nulluser` — the idle loop. Encore patches this loop's `JMP $`
  into `HLT + JMP` to avoid burning 100 % host CPU.

Synchronisation primitives:

* Semaphores — `screate`, `swait`, `ssignal`.
* Resources — `getres`, `regres`. The DCS-2 initialiser uses an
  8-space resource ID to gate XINA shell startup behind successful
  audio-board init.

## 3. Boot sequence on Pinball 2000 hardware

For the detailed milestones see
[22-xinu-boot-sequence.md](22-xinu-boot-sequence.md). In summary:

```
BIOS POST
  → PRISM option ROM scanned at 0xC0000
  → option ROM hooks INT 0x19
  → INT 0x19 fires
  → PRISM handler switches to protected mode
  → game code at 0x08170284 takes over
  → XINU scheduler initialises
  → XINA shell starts
  → DCS-2 initialiser runs as a background task
  → XINA prompt appears on COM1
```

The DCS-2 initialiser runs as a XINU task. It writes a sequence of
DWORD values to BAR2 MMIO, then polls a completion flag at
RAM[0x2797C4]. When the flag becomes `0xFFFF`, the DCS-2 task calls
`regres()` with an 8-space resource ID, unblocking XINA's
`getres()` and allowing the shell to fully start.

## 4. Symbol table format

Cross-reference: [20-symbols-rom.md](20-symbols-rom.md).

Each update bundle ships a `symbols.rom` file. Encore maps it into
the guest address space at runtime and parses it via `symbols.c` to
build an index.

The format is a text section labelled `SYMBOL TABLE` followed by
lines of the form:

```
<hex-address> <symbol-name>
```

Addresses are 32-bit unsigned hexadecimal. Not all symbols are
present in all bundles. SWE1 v2.1 retains the internal XINU
scheduler symbols (`clkruns`, `Fatal`, `resched`); earlier builds
and all RFM builds ship stripped tables containing only exported
API names.

`sym_lookup("clkruns", &addr)` returns 0 on success. The fallback
path uses pattern-scanned addresses when the symbol is absent.

## 5. Filesystem layout in the bundle

After extracting an update from its `.exe` self-extractor, the
result is a directory tree of `.rom` files:

| File           | Purpose |
|----------------|---------|
| `game.rom`     | Main XINU + XINA + game engine binary; the i386 protected-mode code that runs under emulation. |
| `symbols.rom`  | Symbol table (see above). |
| `im_flsh0.rom` | Image flash bank 0 — game assets, scenes, bitmaps. |
| `bootdata.rom` | Public bootloader data / configuration block. |
| `pubboot.rom`  | Public bootloader code. Handles the early flash-load path before XINU takes over. |
| `sf.rom`       | Sound flash — audio data for the DCS-2 subsystem. |

Encore processes `bootdata.rom + im_flsh0.rom + game.rom +
symbols.rom` concatenated in that order (the update binary format).
`pubboot.rom` and `sf.rom` are handled by a separate code path in
the original P2K runtime; Encore defers this to the sound-container
extraction tool.

## 6. How update bundles map to the XINA filesystem

On a real cabinet, the update process (via the serial Update Manager
or `fupdate.exe`) sends all six ROM components over the RS-232
serial port to the cabinet. The `n_update` process in the game
receives them and writes each to the appropriate flash region.

The bundle directory name encodes the game ID and version:
`pin2000_<game-id>_<version>_<date>_B_10000000`. Encore uses the
version digits for bundle matching, e.g. `--update 260` selects
version 2.60.

See [45-official-update-manager.md](45-official-update-manager.md)
for details of the original Update Manager.

## 7. XINA shell commands

The XINA shell provides approximately 99 interactive commands
accessible via COM1 serial terminal at 9600 baud (or via
`--serial-tcp` in Encore). Useful diagnostics include:

| Command       | Purpose |
|---------------|---------|
| `ps`          | List processes. |
| `mem`         | Memory usage. |
| `fatal`       | Fatal error log. |
| `nonfatal`    | Non-fatal error log. |
| `dcs`         | DCS audio control. |
| `pdb`         | Power driver-board status. |
| `gx`          | MediaGX register dump. |
| `switch test` | Switch-matrix test. |
| `lamp test`   | Lamp-matrix test. |
| `fb`          | Monitor flip / sync (used when removing playfield glass). |
| `scenemgr`    | Manages the 12+ RFM game scenes/modes. |

Function key mappings used by XINA (relevant for the emulator
keyboard guide, see
[36-cli-keyboard-guide.md](36-cli-keyboard-guide.md)):

| Key | Function |
|-----|----------|
| F1  | Help     |
| F2  | Flip     |
| F3  | Shell toggle |
| F4  | Start    |
| F5  | Escape   |
| F6  | Down     |
| F7  | Up       |
| F8  | Enter    |

## 8. Known XINA quirks the emulator handles

* **Idle loop patching (BT-74).** XINU's `nulluser` idle process
  spins in a tight `JMP $` loop, burning 100 % CPU on the host.
  Encore pattern-scans for this loop and replaces it with
  `HLT + JMP` so the host CPU idles between guest instructions.
* **Scheduler priming (BT-71).** On some bundles XINU's ready queue
  is not properly seeded before the first `resched()` call, causing
  a hang. Encore injects a dummy scheduler tick at the right
  moment. See [23-boot-scheduler-fix.md](23-boot-scheduler-fix.md).
* **DCS-2 channel init loop.** If the BAR2 SRAM does not return
  `0x00` for byte reads at the channel slots written by the DCS-2
  task, the `REPNZ SCAS` instruction scans indefinitely. Encore's
  BAR2 SRAM faithfully echoes back every written byte.
* **Watchdog and DCS probe cell.** The same RAM location scribbled
  by the watchdog suppression code is also the cell probed by the
  game's DCS-mode detect function — and the probe has inverted
  polarity. Encore either patches the probe outright
  (`--dcs-mode bar4-patch`) or scribbles `0x0000` instead of
  `0xFFFF` to satisfy the inverted probe (`--dcs-mode io-handled`).
  See [13-dcs-mode-duality.md](13-dcs-mode-duality.md) and
  [14-dcs-probe-polarity.md](14-dcs-probe-polarity.md).
* **Port `0x6F96`.** Must return `0x00` ("DCS-2 ready"). Returning
  any other value causes a 120-second hang after DCS-2 phase 3 init.
* **CMOS index `0x3E`.** The PRISM option ROM sets bit 0 of this
  CMOS byte to mark the PRISM as present. Encore's CMOS emulation
  handles this naturally.
