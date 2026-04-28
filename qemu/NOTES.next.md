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
- `--bpp 16|32`: QEMU display backends should own presentation details.
- `--splash-screen`: not needed for first QEMU usability.
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
