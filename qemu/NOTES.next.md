# QEMU Parking Lot

Small notes we want to remember without interrupting current bring-up work.

## Future CLI Shape

Do not clone the full Unicorn CLI by default. The QEMU wrapper should preserve
user workflows, not old implementation baggage.

Likely keep:

- `--game swe1|rfm|auto` once RFM is proven.
- `--roms PATH`.
- `--savedata PATH` and `--no-savedata`.
- `--update none|latest|VERSION|PATH`.
- `-v`, `-vv`, `-vvv` / `--verbose`.
- `--headless`.
- `--fullscreen` if QEMU display backend support stays simple.
- `--bpp 16|32` if it remains useful for presentation/debug compatibility.
- `--splash-screen` if we keep the old polished startup / fallback UX.
- `--dcs-mode io-handled|bar4-fallback` while both paths exist.
- `--lpt-device none|emu|/dev/parportN|0xBASE` when cabinet passthrough lands.
- `--lpt-trace FILE`.
- `--config FILE` later, if we still want config-file ergonomics.
- Unicorn-style gameplay key bindings, but routed through the emulated LPT
  driver-board / switch matrix path, not through PS/2 keyboard injection.
  Preserve this explicit Unicorn desktop-control inventory:

  Host/window controls:
  - `F1`: quit emulator.
  - `F2`: flip display vertically.
  - `F3`: screenshot.
  - `F11` / `Alt+Enter`: toggle fullscreen.
  - `F12`: dump guest switch state / switch matrix diagnostics.

  Cabinet/gameplay controls:
  - `Space` / `S`: Start button (`sw=2` / physical column 0 bit 2 in the
    Unicorn mapping).
  - `F10` / `C`: insert credit / coin.
  - `F6`: left action button.
  - `F7`: left flipper.
  - `F8`: right flipper.
  - `F9`: right action button.
  - `F4`: toggle coin door closed/open interlock.

  Coin-door service/test controls:
  - `Esc` / `Left`: button 1, service credits in attract or Escape in tests.
  - `Down` / keypad minus: button 2, volume down or menu down.
  - `Up` / equals / keypad plus: button 3, volume up or menu up.
  - `Right` / `Enter` / keypad Enter: button 4, begin test or Enter.

  Optional debug controls from Unicorn, if still useful:
  - `0`..`7`: force/probe physical switch bits.
  - `[` / `]`: decrement/increment probe column.

  These bindings are user-facing compatibility. Internally they should update
  the emulated P2K driver-board/switch-matrix state. They should not be
  implemented as raw PS/2 keyboard injection unless a specific guest path is
  proven to require PS/2.

Likely adapt:

- `--serial-tcp PORT`: keep as wrapper sugar for QEMU serial TCP, because it is
  the clean path to the XINA/monitor console.
- `--cabinet-purist`: replace with clearer QEMU-era options such as `--cabinet`
  or `--no-symptom-patches` if needed.
- `--flipscreen`: only keep if cabinet display orientation proves useful.

Likely drop:

- `--keyboard-tcp PORT`: probably Unicorn baggage. XINA/monitor access should
  go through serial TCP, and gameplay input should go through the cabinet/LPT
  switch-matrix path, not PS/2 scancode injection. Reconsider only if a proven
  guest path genuinely requires raw PS/2 keyboard input.
- `--record` / `--replay`, `--http`, `--xina-script`, `--net-bridge`: useful
  someday maybe, but not part of the baseline.
- Any old Unicorn CPU/PIT/IRQ pacing option. QEMU owns CPU execution, i8254 PIT,
  i8259 PIC, timers, and interrupt delivery.

## PIT Configurability

Default should remain guest-programmed PIT behavior. SWE1 currently programs
PIT channel 0 divisor around 298, i.e. about 4003.97 Hz. Later, a diagnostic
or compatibility override may expose fixed rates such as guest/default, 4004,
or 4096 Hz, but this must not replace the default hardware model.

## Display Ownership

Distinguish host framebuffer presentation from guest-visible graphics device
initialization. A QEMU `DisplaySurface` can be a simple RAM view, but that does
not prove `set_gfx_mode()` sees a valid MediaGX/Allegro device. Long-term goal
is QEMU-side GX/MediaGX semantics, not only framebuffer readout.

## DCS Sound Priority

Next functional milestone after display/UART stability is DCS sound.

Status (session 2026-04-28): the architectural half is DONE.  The DCS
state machine is now centralized in `qemu/p2k-dcs-core.c`.  Both the
BAR4 MMIO view (`p2k-dcs.c`) and the I/O 0x138-0x13F overlay
(`p2k-dcs-uart.c`) are thin frontends that route into the shared core.
The 16550-style register surface stays local to the I/O view because it
only exists there, not on BAR4.  Old "PARTIALLY TEMPORARY" headers were
removed from both views.

Remaining DCS work is real audio output: pull the sample dispatch /
ADSP-2105 mixer behavior from `unicorn.old/src/sound.c` and wire it to a
QEMU audio backend.  Until then the protocol-only core is enough to get
the game past DCS init (already proven).

## Watchdog / PLX INTCSR Candidate Fix

Track this as a strong QEMU improvement over the old Unicorn/mainline watchdog
workaround. The core polarity fix is ROM/disasm-backed and likely correct; the
remaining caution is bundle coverage and interaction with future DCS work.

Old Unicorn/mainline family:

- Scanned the guest for the `pci_watchdog_bone()` / `pci_read_watchdog()` path.
- Periodically scribbled guest RAM watchdog/probe cells.
- Also primed a guest PLX BAR pointer so `[ptr + 0x4c]` reached the emulated
  PLX register window.
- Comments and polarity around `INTCSR` bit 2 were historically confusing.

Current QEMU candidate:

- ROM inspection for SWE1 v2.10 shows the caller Fatals when
  `pci_watchdog_bone()` returns 1.
- `pci_watchdog_bone()` returns 1 when the probe succeeds and PLX `INTCSR`
  bit 2 is clear.
- Therefore forcing PLX `INTCSR` bit 2 set makes this path return 0 and skip
  the Fatal without RAM scribbling.
- This is likely cleaner than the Unicorn/mainline RAM scribbler, because it
  moves the behavior into the PLX device model.

Validation still needed before deleting the fallback:

- Long-run SWE1 with watchdog scribbler off by default.  DONE (180s, 0 Fatal).
- At least one update bundle and RFM/RFM update coverage.  RFM base coverage
  DONE (60s, 0 Fatal, XINU V7); update bundles deferred until the wrapper
  grows a `--update` option (see "Future CLI Shape").
- Confirm DCS sound still works once the DCS path is implemented.
- Keep `P2K_WATCHDOG_SCRIBBLER=1` or equivalent only as a temporary opt-in
  fallback until the matrix is proven.
- Update stale comments that still say `INTCSR` bit 2 clear is the healthy
  value.  DONE (header in `qemu/p2k-plx-regs.c`).

## Retired Temporary Patches (post-bring-up audit)

Tracked here so we know which boot bridges have been replaced by real
device behavior or by empirical evidence that they were unneeded.

- COM1 IRQ4 TX-empty signaling — real device behavior in
  `qemu/p2k-isa-stubs.c`; XINU console no longer hangs (replaced the
  earlier "exec is hung" symptom path).
- PLX INTCSR bit2 polarity — fixed in `qemu/p2k-plx-regs.c`; the
  watchdog scribbler is now opt-in via `P2K_WATCHDOG_SCRIBBLER=1`
  instead of running by default.
- DCS state duplication — collapsed into `qemu/p2k-dcs-core.c`;
  BAR4 and I/O 0x138-0x13F views are now thin frontends.
- PIC fix-up 250us timer — DEFAULT-OFF.  Empirically the legacy port
  of `unicorn.old/src/io.c:121-127` was actively hurting throughput
  (exec_pass plateau ~0x129 with timer armed vs ~0x6ef with timer off
  on a 90s SWE1 run).  Opt-in via `P2K_PIC_FIXUP=1`; full removal
  pending one more bundle/update sweep.

## Post-LPT-Pace Master Fixes

Treat later Unicorn/mainline fixes from the post-LPT-pace era as possible
clues, not automatically correct patches.

- Prefer porting device behavior that is independently justified by ROM,
  symbols, logs, or hardware docs.
- Avoid importing timing/guard/scribble layers just because they made one
  Unicorn run more stable.
- The PLX `INTCSR` bit-2 polarity fix above is stronger than that category:
  it is backed by the actual `pci_watchdog_bone()` caller/callee bytes.

Short-list of claims/lessons worth checking surgically:

These came from a difficult late-Unicorn phase where master often said it had
"fixed" things while the architecture was still unstable. Treat each item as a
lead to verify, not as proven truth.

- `817afac`: claimed/found that emulated LPT opcode `0x00` should return
  `0xF0`, not `0x00`. Verify against protocol evidence or current guest
  behavior before porting.
- `0001de2`: claimed/found that `io-handled` DCS needs byte writes to port
  `0x13c` for sound command delivery. Likely relevant, but prove it in the QEMU
  DCS path. Historical nuance: around the LPT-pace era, `io-handled` sound
  appeared genuinely strong/useful, while late master had many timing/IRQ guard
  experiments and sound became harder to interpret. Use the earlier good
  behavior as a clue, but re-prove QEMU sound cleanly instead of trusting the
  final master state. The `unicorn.old/` snapshot is currently the most
  functional practical reference for this path; use it to understand expected
  behavior, not as code to copy blindly.
- `516210d`: claimed/found a `#UD` / `0F3C` non-Cyrix interrupt-frame issue.
  Only relevant if QEMU keeps a custom 0F3C handler; verify against actual
  handler bytes and IRET frame behavior.
- `2c2c180`: env-gated PDB/LPT CSV tracing looks useful for comparing traffic
  to hardware or NuCore. This is diagnostic tooling, not a behavioral fix.
- `be99437`: historically useful because it exposed that IRQ0/PLX/watchdog
  behavior mattered, but it mixed too many changes and belongs to the unstable
  pre/post-LPT-pace transition. Do not port wholesale.
