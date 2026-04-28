# QEMU Next Roadmap

Purpose: one auditable roadmap from project day 0. This file should make it
easy to answer four questions: what is done, what remains, which commits are
real foundations, and which commits were temporary bridges or false-good
patches. Keep only decisions, lessons, and removal criteria.

Legend:

- `[x]`: accepted for current baseline.
- `[ ]`: not done.
- `[~]`: partial or needs verification.
- `[!]`: temporary, symptom-shaped, rollback/delete candidate.
- Rule: every future commit should check an item, add an item, or retire one.

## Direction Guardrails

Keep this section boring on purpose. The QEMU branch should model devices
cleanly, keep temporary bridges gated, and delete bridges once the matching
device behavior is proven.

- [x] QEMU owns CPU, PIT, PIC, timers, interrupt delivery, display, and host
  audio plumbing.
- [x] P2K code owns the missing board devices: PLX, flash/NVRAM, DCS, LPT,
  cabinet controls, SuperIO/probe surfaces, and product wrapper behavior.
- [x] `unicorn.old/` and external POCs are references only. Copy facts after
  re-validation, not implementation patterns.
- [x] Any remaining temporary bridge needs a gate, a metric, and a deletion
  condition.

## Foundation Ledger (Keep Lessons, Not Recipes)

These are the discoveries before the later "Encore/Unicorn mature" era. They
are significant because they proved the project was viable at all. Treat them
as lessons and validation targets.

- [x] `93bd72f` Encore v0.1: ROM auto-detect, savedata, modular architecture.
  Lesson: the project had a real emulator skeleton, not just scripts.
- [x] `0be2167` Encore v0.2: MMIO callback fix, EEPROM verify, XINA boot.
  Lesson: PRISM/XINA could boot under Unicorn with the right low-level hooks.
- [x] `b19a2d0` reached `Game(Williams - Episode I)` banner.
  Lesson: the guest code path was not fundamentally blocked by ROM loading.
- [x] `34a05ed` EIP write-back, display init, GP BLT, framebuffer aliasing.
  Lesson: CPU state sync and MediaGX-ish memory views were core bring-up facts.
- [x] `2a881aa` DC_TIMING2 read-increment made graphics rendering active.
  Lesson: small display register semantics could gate the whole visible game.
- [x] `76f542a` SWE1 booted with full graphics display.
  Lesson: the graphics path was achievable before later timing experiments.
- [x] `e59277c` FPS jumped from about 5 to 50-56 via direct memory/display
  throttling.
  Lesson: host-side presentation overhead was a real bottleneck and could be
  removed without changing game logic.
- [~] `5933e01`, `dd26b2e`, `286252c` optimized `uc_emu_stop`, direct RAM,
  EIP writeback, and `clock_gettime` frequency.
  Lesson: useful performance for Unicorn, mostly irrelevant once QEMU owns CPU.
- [~] `775d4f`, `ee0a3af`, `eb3cd8d`, `ac2213f` explored iteration timers,
  tcyc bypass, wall-clock PIT at guest-programmed 4003 Hz, and CMOS/RTC.
  Lesson: these explain why old runs felt alive; QEMU should keep RTC/PIT
  semantics but not rebuild the Unicorn timer loop.
- [x] `6601cbc`, `eb9492b`, `4904387` wired early coin door/service/slam tilt
  and fixed default coin-door/sound-path assumptions.
  Lesson: cabinet state defaults matter during boot and tests.
- [x] `fbd2f2d` activated LPT before PinIO probe (`BT-94`).
  Lesson: device activation order affects guest hardware discovery.
- [x] `512b324` matched i386 POC sound management.
  Lesson: there was an earlier i386 sound reference worth preserving through
  `unicorn.old/src/sound.c` and QEMU DCS work.
- [x] `1fa5337` DCS2 sound board init via I/O command interface + serial
  protocol.
  Lesson: DCS was not just BAR4; command/serial protocol mattered from early
  POCs.
- [x] `2f5e52d` DCS2 BAR4 mode produced working audio commands.
  Lesson: early audio success was real, even if later defaults/modes got muddy.
- [x] `e27a5a8`, `8354805`, `3c9deda` fixed stuck volume/buttons by returning
  open switch values (`0xFF`) on columns/rendering opcodes.
  Lesson: default-open cabinet inputs are a boot/playability invariant.
- [x] `6fe4944`, `d1c25db`, `89401bc`, `aeb76d0` refined LPT shift-register
  and mixed-polarity switch columns.
  Lesson: the switch matrix is not "just buttons"; polarity and serial state
  are device semantics.
- [x] `a6e1066`, `84fc15d`, `b431951`, `a3b6d38`, `81b342c`, `143caa8`,
  `98fb68e`, `9825b2b`, `625784a`, `69f579f` converged the practical key and
  service/test behavior.
  Lesson: this is the origin of the Unicorn key parity target now being ported
  to QEMU.
- [!] `e82fe0a`, `b3b90f2`, `1c12c19`, `2e5e815` were reverted Start-button
  explorations.
  Lesson: reference only; product behavior should come from devices and inputs.
- [x] `f08ec28`, `a32c487`, `641b590` added serial/keyboard TCP and fixed UART
  IRQ4 RX behavior.
  Lesson: UART IRQ behavior is real. Keyboard TCP is quarantined unless a
  current use case proves it belongs.
- [x] `3f9032f`, `33c05f8`, `6f89f76`, `4e834e8` dropped dead per-version
  patches and added symbol/multi-bundle infrastructure.
  Lesson: prefer ROM/symbol-driven generic behavior over hardcoded version
  gates.
- [x] `b1db763`, `1435252`, `557e753`, `b093ce0`, `fbff370` built the update
  loader/distribution path.
  Lesson: QEMU wrapper must recover `--update` and savedata/update ergonomics.
- [x] `609c8ef`, `98006cf`, `ca33590` made the large Encore documentation tree
  and ground-truth audit.
  Lesson: docs are part of the product, but QEMU docs must be decontaminated
  and current.

## Quarantined External POC References

These live outside this repo (`../poc-nucore`, `../poc-nucore-i386`,
`../nucore-portable`). They were alpha forensics, not product architecture.
They are evidence folders, not implementation sources.

- [~] Keep as evidence only: historical boot logs, display observations,
  symbols/update notes, rough device-address hints, and NuCore comparison logs.
- [~] Re-check only when useful: DCS/resource observations, display pipeline
  clues, and i386 POC sound-management references such as the path later echoed
  by `512b324`.
- [!] Alpha POC implementation patterns stay archived.
- [!] If an old POC observation looks useful, it must graduate through current
  QEMU proof: ROM/symbol evidence, clean device model, gated experiment, and
  SWE1/RFM validation.

## Unicorn Breakthrough Ledger Before QEMU

This is the pre-QEMU archaeology. Use it to remember what the project learned,
not to blindly port late-Unicorn patches. `unicorn.old/` is a reference library,
not the new source of truth.

- [x] Early DCS BAR4 sound path: `2f5e52d` forced BAR4 mode, disabled UART,
  and produced working audio commands. Lesson: the sample table and playback
  path in `unicorn.old/src/sound.c` are valuable references for QEMU audio.
- [x] LPT switch-matrix discovery: `d1c25db`, `aeb76d0`, `a6e1066`,
  `81b342c`, `98fb68e`, `143caa8`, `84fc15d` mapped polarity, shift-register
  behavior, switch table routing, coin door, service buttons, and trace tools.
  Lesson: desktop controls must update the emulated LPT/switch matrix, not PS/2.
- [x] User-control ergonomics: `69f579f`, `e24ccc4`, `1feac34`, `6897d3c`,
  `46e51c0`, `585289a`, `85b8118` built the practical desktop UX: start,
  credits, flippers, fullscreen, screenshots, probe keys, and splash screen.
  Lesson: preserve the workflow even if QEMU implements it differently.
- [!] Debug RAM-injection era: `e82fe0a`, `b3b90f2`, `1c12c19`, `6b65e78`,
  `51412c1` was reverted exploration.
  Lesson: closed history; product behavior should come from devices and inputs.
- [x] UART/net console lesson: `641b590` raised UART IRQ4 after RX bytes.
  QEMU equivalent is `9fcd992` for TX-empty. Lesson: XINU console waits on
  serial IRQ behavior; COM1 behavior is part of the device model.
- [x] Cabinet/LPT passthrough research: `a0cb997`, `4c0e88a`, `804d2c2`,
  `8f7f06a`, `745c56e`, `caf5de0`, `d2429d0`, `67cebab`, `eb2a6f4` covered
  ppdev/raw I/O, permissions, USB-LPT incompatibility, and public protocol docs.
  Lesson: QEMU desktop should stay rootless; real cabinet mode can come later.
- [x] DCS mode duality breakthrough: `4db3f2d`, `c967fe7`, `aa13af4`,
  `c0b8f47`, `c8d82c1`, `113a3ed`, `3f2106e`, `6426dc6` established BAR4
  vs io-handled topology and the danger of mode-specific patching. Lesson:
  QEMU should share one DCS core behind BAR4 and I/O frontends.
- [!] DCS/watchdog scribble era: `753d515`, `6495c42`, `ad543e8`, `b6357f1`,
  `ae427e9`, `e5b7c43`, `90fe7bc` showed that RAM scribbles could make DCS or
  watchdog paths look fixed.
  Lesson: false-good risk; model PLX/DCS/watchdog behavior instead.
- [x] ROM/update/assets pipeline: `9edcd4d`, `1435252`, `c404352`,
  `fbff370`, `f55332e`, `27adf49` improved RFM chip choice, all-terrain update
  loading, latest-update selection, savedata/config/bpp/fullscreen/shape-based
  sound scanning. Lesson: QEMU wrapper should recover the useful user-facing
  pieces, not the old CPU backend.
- [x] Logging/diagnostics hygiene: `e235631`, `dd93497`, `4ab57f4`, `1fc1916`,
  `47a5e20` made logs usable and docs closer to code. Lesson: QEMU diagnostics
  should be tiered and quiet by default except UART/Fatal visibility.
- [~] LPT purist/raw control era: `38b07c7`, `a0e956d`, `6192acc`,
  `ec53b63`, `f81eb88`, `0bf1782`, `4b02a63` moved toward verbatim guest CTL
  and cabinet-purist semantics. Lesson: future QEMU cabinet mode should disable
  symptom patches and trust real device lines where possible.
- [!] LPT pacing era: `deb5e53`, `e87a5ea`, `b2ed869` tried bus pacing for
  driver-board settling. Lesson: it made things feel smoother but at the wrong
  layer; QEMU timing belongs to QEMU devices/timers.
- [~] LPT/PDB protocol observations: `817afac`, `dd56b99`, `d619cfb`,
  `2c2c180`, `88174c4` may contain real opcode/status facts and tracing value.
  Lesson: verify each fact against current QEMU traces before porting.
- [~] Pre-vticks IRQ0 breakthrough: `be99437` fixed cadence enough to reach
  attract and exposed PLX/watchdog dependencies. Lesson: historically important,
  but mixed cadence, watchdog, PLX pointer and stats; use as context only.
- [!] Virtual-time/IRQ0 experiment stack: `6b3919b`, `352c0e2`, `a5bc0ee`,
  `b35516b`, `bce0828`, `fcf7f79`, `516210d`, `39f7c20`, `7e57775`,
  `eafc7ea`, `075faf8`, `5b0d407`, `c377122`, `72868be`, `6bead4b`,
  `d9bf97b`, `ed354f5`, `d58b88f`, `412e1bf`, `233e1da`, `5b68c40`,
  `e472565`, `709f582` produced excellent forensics but too many Unicorn-side
  guards. Lesson: QEMU migration exists to delete this timing layer.
- [~] Late DCS byte-write clue: `0001de2` claimed io-handled commands could be
  written high-byte then low-byte to `0x13c`.
  Lesson: post-LPT-pace clue only. Current QEMU trace did not reproduce it, so
  keep instrumentation as a diagnostic clue only.
- [!] DCS default flip false-good: `cc630fb` made BAR4 default because sound
  worked, then `958b190` reverted/helped document the gap. Lesson: BAR4 and
  I/O UART frontends should be tested honestly against the shared core.
- [~] `#UD` / Cyrix opcode clue: `516210d` claimed a non-Cyrix `#UD`
  interrupt-frame issue.
  Lesson: relevant only if QEMU keeps custom `0F 3C`; verify against current
  handler bytes before trusting it.
- [x] Human lesson: the best Unicorn period proved the ROMs can run, graphics
  can be fluid, and DCS can play. The late period proved Unicorn timing and
  synthetic IRQ injection were the wrong long-term maintenance surface.

## Product Target

- [ ] Baseline user experience: launch SWE1 from one wrapper command, see fluid
  graphics, see UART/XINA output by default, hear DCS sound, use Unicorn-style
  desktop controls, and run long enough without Fatal storms.
- [ ] Architecture target: QEMU owns CPU, i8254 PIT, i8259 PIC, timers,
  interrupt delivery, display surface, and audio backend. P2K code models
  missing board devices; it should not rebuild Unicorn timing.
- [ ] Cleanliness target: symptom patches are gated, logged, and have removal
  conditions. Device behavior should replace patches whenever possible.
- [ ] Regression target: SWE1 base, SWE1 update, RFM base, and RFM update each
  get at least one long run with UART, display, controls, watchdog, and DCS
  state observed.

## Milestones

- [x] M0 Decontaminate: archive Unicorn into `unicorn.old/` and start a QEMU
  board branch. Commit: `d8c5d57`.
- [x] M1 Boot QEMU i386: pinned QEMU source/build, custom machine, PRISM PM
  entry starts. Commit: `8bbb89f`.
- [x] M2 Load ROMs correctly: bank0 deinterleave, PLX aliases, bank priority,
  bank1/2/3, DCS ROM. Commits: `86aba35`, `2e10434`, `424ec6f`.
- [x] M3 Memory/flash map: BAR2/BAR3/BAR4, flash protocol, NVRAM/flash seed,
  sentinel windows. Commits: `767bb48`, `b66481d`, `d39ffa2`, `56d8eac`.
- [~] M4 PCI/PLX model: enough CF8/CFC and PLX behavior to boot; still not a
  real QEMU `PCIDevice` architecture. Commits: `32e7f10`, `a00ec47`,
  `e3365ca`, `256cea1`.
- [x] M5 ISA/UART/RTC probes: i8042, COM1, RTC, SuperIO-ish probes enough for
  XINA/XINU. Commits: `3c75d1d`, `17f0199`, `1cec4f0`, `9fcd992`, `3a07ad8`.
- [~] M6 PIT/PIC/IRQ0: QEMU owns i8254/i8259; legacy boot bridges are gated
  and still need final removal proof. Commits: `224a5eb`, `e6712a9`,
  `32e7300`, `dacd397`, `b20f39b`.
- [x] M7 Display/GX: visible QEMU display, GX base, BLT engine, Cyrix CCR
  discovery. Commits: `989a546`, `9e400d3`, `80c9cbb`, `f44066e`.
- [x] M8 DCS protocol/audio: BAR4 and I/O UART share one DCS core; real
  pb2kslib sample dispatch via libvorbisfile is decoded into an 8-voice
  QEMU SWVoiceOut mixer; proof-of-path blip remains as fallback for
  protocol bytes not in the sample table. Commits: `bd1a858`, `6d70e52`,
  `8bac2af`, `447dad4`, `09f300f`, plus this one.
- [~] M9 Cabinet/LPT controls: desktop key parity is mostly implemented through
  the LPT switch matrix; cabinet passthrough and a few UX extras remain.
  Commits: `dc97214`, `283dda8`, `b3c4994`, `3096f03`.
- [ ] M10 Product wrapper: Unicorn-like CLI, savedata/update options, default
  UART visibility, fullscreen/headless, bpp/splash compatibility.
- [ ] M11 Validation matrix: SWE1/RFM base/update, long-run, no default
  symptom patches, DCS sound, controls, UART, watchdog.
- [ ] M12 Optional diagnostics/compat knobs: keep guest-programmed PIT as
  default, but allow later fixed-rate diagnostics such as 4004/4096 Hz without
  replacing the hardware model.

## Commit Ledger From QEMU Day 0

- [x] `d8c5d57` archive Unicorn into `unicorn.old/`, scaffold QEMU board.
  Keep. This is the decontamination boundary.
- [x] `8bbb89f` build and run SWE1 PRISM PM-entry on pinned QEMU i386.
  Keep. Foundation for the custom QEMU path.
- [x] `86aba35` map bank0 at PLX/option-ROM/BAR5/alias windows.
  Keep. Required ROM visibility.
- [x] `2e10434` fix bank0 deinterleave and ROM overlap priority.
  Keep. Required correctness fix.
- [x] `3c75d1d` early ISA stubs for i8042/port61/CMOS/POST/COM1.
  Keep as baseline, but later commits refine specific devices.
- [!] `32e7f10` minimal CF8/CFC PCI config stub.
  Keep temporarily. It got boot moving, but real QEMU devices should replace
  hardcoded config-space fiction.
- [x] `767bb48` BAR2/BAR3/BAR4 RAM stubs.
  Keep. Basic memory map.
- [x] `989a546` Cyrix MediaGX GX_BASE MMIO stub.
  Keep. Early graphics-device surface.
- [x] `9e400d3` framebuffer alias and `p2k-display.c`.
  Keep. First visible graphics path.
- [~] `bd1a858` first BAR4 DCS audio MMIO state machine.
  Superseded by shared DCS core; keep the useful behavior only.
- [x] `b66481d` seed BAR3 from savedata flash.
  Keep. Persistence/flash continuity.
- [!] `224a5eb` PIC boot bridge.
  Rollback candidate. Default-off since `b20f39b`; delete after bundle sweep.
- [x] `e3365ca` PLX BAR0 + 93C46 SEEPROM model.
  Keep. Device behavior.
- [x] `d39ffa2` seed BAR2 from NVRAM and add Intel 28F320 protocol.
  Keep. Flash/NVRAM behavior.
- [!] `e6712a9` early IRQ0 boot bridge.
  Verify QEMU PIT/PIC can survive without it before deletion.
- [~] `941d3ab` VSYNC ticker around BAR2/DC_TIMING2.
  Verify. Useful display progress, but confirm it matches guest expectations.
- [!] `911ca7e` watchdog RAM workaround.
  False-good symptom patch. Replaced by PLX INTCSR bit2 behavior.
- [x] `a00ec47` expose PLX 9050 raw ID at PCI dev9.
  Keep for now. Helped PCI probe; eventually fold into real PLX device.
- [x] `318d93d` disable PCI sentinel RAM workaround.
  Keep. Good removal of a symptom patch.
- [~] `6d70e52` DCS-2 UART overlay at I/O `0x138-0x13F`.
  Keep as frontend only. Shared core should own DCS state.
- [x] `424ec6f` load PLX banks 1/2/3 and DCS sound ROM.
  Keep. Required assets.
- [x] `56d8eac` BAR2 SRAM plus sentinel window.
  Keep. Memory behavior.
- [x] `17f0199` CMOS RTC live time and status.
  Keep. Real device-ish behavior.
- [x] `dc97214` minimal LPT driver-board on `0x378-0x37A`.
  Keep as stub. Needs key mapping, possible cabinet passthrough, protocol proof.
- [!] `518a78e` re-arm watchdog RAM workaround excluding PCI sentinels.
  Temporary regression bridge. Now opt-in only; delete after validation matrix.
- [~] `c698c24` GDT/CR0/Cyrix `0F 3C` #UD emulator.
  Verify. May be needed, but exact QEMU-era requirement should be proved.
- [!] `32e7300` early IRQ0 handoff bridge.
  Keep only with self-retire proof.
- [~] `3a07ad8` SuperIO W83977EF + CS5530 EEPROM I/O stubs.
  Verify which fields are truly needed before expanding the probe surface.
- [!] `6408d5f` BT-130 mem_detect prologue patch.
  Symptom-shaped. Replace with correct RAM/PCI resource behavior if possible.
- [~] `b037ecc` BT-131 NIC LAN ROM seed in D-segment.
  Likely harmless, but prove whether guest requires it.
- [x] `9388676` mark temporary symptom patches with removal conditions.
  Keep. Good hygiene.
- [x] `663a617` read-only PIT/PIC/IDT diagnostic sampler.
  Keep as diagnostic only.
- [x] `dacd397` early IRQ0 bridge self-retires when guest installs real clkint.
  Keep for now. It improves a boot bridge by adding a removal path.
- [x] `80c9cbb` GP BLT engine MMIO.
  Keep. Graphics behavior.
- [~] `d7b99d8` nulluser idle `JMP$ -> HLT;JMP-3`.
  Verify. Performance/idle improvement, but prove it does not hide scheduler
  bugs.
- [x] `0a6fa5c` temporary `allegro_init` runtime patch.
  Deleted post-`9ccf4b4`. Module removed from build; SWE1+RFM default still 0 Fatals.
- [x] `5a3388b` demote allegro patch to opt-in and add gfxlist diagnostic.
  Keep. Good containment.
- [x] `f44066e` Cyrix CCR I/O `0x22/0x23`.
  Keep. Realer device behavior; fixes `free_resource` class failure.
- [x] `396d312` document allegro patch as superseded.
  Historical; module is now physically deleted.
- [x] `1cec4f0` model i8042 self-test/OBF.
  Keep. Probe behavior.
- [x] `9fcd992` COM1 IRQ4 TX-empty signaling.
  Keep. Real device behavior; fixes `exec is hung`.
- [x] `256cea1` PLX INTCSR bit2=1 retires `pci_watchdog_bone` Fatal.
  Keep. Strong ROM/disasm-backed fix, better than RAM scribbling.
- [x] `bbdf1a3` stale-comment sweep + watchdog validation matrix.
  Keep. Hygiene and proof tracking.
- [x] `8bac2af` collapse DCS state into shared core.
  Keep. Correct architecture; audio output still pending.
- [x] `b20f39b` retire PIC boot bridge by default.
  Keep. Good rollback of symptom layer.
- [x] `9ccf4b4` record DCS shared-core and PIC retirement in notes.
  Keep. Documentation milestone.

## Current Baseline

- [x] Branch is decontaminated; old implementation exists only under
  `unicorn.old/` as reference, not source of truth.
- [x] QEMU build is pinned and narrow: i386-softmmu custom machine, not a full
  multi-system product.
- [x] SWE1 reaches running graphics with QEMU CPU/PIT/PIC/timers.
- [x] QEMU PLX INTCSR fix avoids the watchdog RAM workaround by default.
- [x] QEMU COM1 IRQ4 TX-empty unblocks XINU console waits.
- [x] QEMU display is functional enough to play/test visually.
- [~] DCS protocol initializes and proof-of-path audio is audible by default;
  authentic DCS-2 sample playback remains missing.
- [~] LPT board and most Unicorn-style desktop controls exist; cabinet
  passthrough and F2/F3/F11-style polish remain.
- [~] Some bridge patches remain: early IRQ0 bridge, mem_detect patch, PCI stub,
  optional watchdog/PIC workarounds.

## Must Finish Next

- [x] DCS actual sound playback through QEMU audio backend. Use
  `unicorn.old/src/sound.c` as behavior reference, not blind copy.
  Done: `qemu/p2k-dcs-audio.c` rewrites the proof-of-path module into
  a real pb2kslib-driven sample player. mmap's the container, decodes
  0x48-byte XOR'd entries (XOR 0x3A), extracts name + offset + size +
  track_cmd ("S####" hex or `dcs-bong`=0x003A). On DCS command, looks
  up the matching entry, de-XORs the OGG payload, decodes via
  `libvorbisfile` (`ov_open_callbacks` over in-memory blob) to mono
  S16 at 44100 Hz with linear-rate resample, then mixes up to 8
  concurrent voices into the QEMU `SWVoiceOut` callback. Cached per
  `track_cmd`. Build script (`scripts/build-qemu.sh`) injects a meson
  `dependency('vorbisfile', required: false)` so the link is
  automatic when libvorbis is installed. Verified: SWE1 default
  headless run loads 689 entries from `swe1_sound.bin`, decodes and
  plays 12 distinct samples in ~45 s including the long boot-music
  track (cmd 0x000e, 2 286 846 frames ≈ 52 s at 44.1 kHz). 0 Fatals.
- [~] DCS protocol-byte filtering. The fallback blip path still fires
  for cmds that have no matching pb2k entry (volume/echo/handshake
  bytes such as 0x7F, 0x00, 0x90, 0x48, 0x4F, 0x80, 0x28). Drop the
  blip default once the dispatch knows which cmds are sample triggers
  vs control bytes. Reference: `unicorn.old/src/sound.c` track_vol /
  track_pan command parsing. Removal condition: blip path only logs,
  does not emit audio.
- [x] Re-prove late-Unicorn DCS byte-write clue: `0001de2` says io-handled
  writes `0x13c` high byte then low byte. It is concrete, but post-LPT-pace.
  Status: instrumented in `p2k-dcs-uart.c` behind `P2K_DCS_BYTE_TRACE=1`.
  60s SWE1 trace at HEAD shows ZERO byte writes to 0x13C; only word
  writes are used. The clue does not reproduce at the current milestone;
  re-evaluate if/when the post-LPT-pace path is reached.
- [x] Make UART/XINA output visible by default, Unicorn-style, or ensure the
  wrapper always enables it for bring-up.
  Done: `p2k-isa-stubs.c` defaults `s_uart_to_stderr = true`; opt-out via
  `P2K_NO_UART_STDERR=1`. Verified Fatal/NonFatal lines appear without env.
- [x] Implement desktop controls through LPT/switch matrix: F1/F2/F3/F4,
  F6-F12, Space/S, C/F10, arrows, Esc, Enter.
  Done in `p2k-lpt-board.c` via `qemu_input_handler_register`:
  F1 quit, F4 coin door, F5/Enter/KP_Enter ~60-frame Enter pulse,
  F6 left action (Phys[10] b7), F7 left flipper (b5), F8 right flipper
  (b4), F9 right action (Phys[10] b6), Space/S start (col 0 b2 of
  opcode 0x04), F10/C coin slot 1 (Phys[8] b0), F12 state dump,
  Esc/Left service (Phys[9] b0), Down/KP- volume- (b1), Up/=/KP+
  volume+ (b2), Right begin-test (Phys[9] b3). Verified via QEMU
  monitor `sendkey`. Remaining (lower priority): F2 flip-Y,
  F3 screenshot (needs `coroutine_fn` bridge for `qmp_screendump`),
  F11/Alt+Enter fullscreen, optional 0..7 / `[` `]` probe keys.

- [x] DCS audio audible by default. `447dad4` added proof-of-path audio;
  `09f300f` made the wrapper auto-enable it when PulseAudio/ALSA is available
  and added an install-time hello tone. This checkpoint replaces the
  cmd-hash blip with real pb2kslib sample playback through libvorbisfile.
- [~] Wrapper parity: `--game`, `--roms`, `--savedata`, `--no-savedata`,
  `--update`, `-v/-vv/-vvv`, fullscreen/headless.
  scripts/run-qemu.sh now handles `--game`, `--roms`, `--savedata`,
  `--no-savedata`, `--display`, `--headless`, `--monitor`, `--debug`,
  `--uart-quiet`, `--audio <driver>`, `--no-audio`, `-v/-vv/-vvv`, and
  `--`. `--update <dir>` is parsed but the machine does not yet consume
  it (warning emitted).
- [ ] Preserve polished UX if cheap: `--bpp`, `--splash-screen`, screenshot,
  display flip.
- [ ] Preserve useful wrapper shape without cloning baggage: likely keep
  `--game`, `--roms`, `--savedata`, `--no-savedata`, `--update`, verbosity,
  `--headless`, fullscreen, `--bpp`, `--splash-screen`, `--dcs-mode`,
  `--lpt-device`, `--lpt-trace`; likely drop old Unicorn CPU/PIT pacing,
  keyboard TCP, HTTP, record/replay, xina-script, and net-bridge for baseline.
- [ ] Keep PIT semantics honest: default is guest-programmed QEMU i8254.
  SWE1 has been observed around divisor 298, about 4003.97 Hz. Any 4004/4096
  override is a diagnostic/compat option, not the default truth.
- [ ] Validate SWE1 base/update and RFM base/update long runs.
- [ ] Define QEMU cabinet/purist semantics: no default symptom patches, later
  real parport, desktop emu rootless.
- [x] Fix README/env drift: `P2K_PIC_FIXUP=1` is opt-in; `P2K_NO_PIC_FIXUP`
  is only an override switch.
  Done in `qemu/README.md` env-var table: defaults column added, semantics
  flipped for PIC bridge and watchdog workaround, UART switch documented.

## Temporary Bridge Registry

- [!] PIC boot bridge: introduced `224a5eb`, retired default `b20f39b`.
  Owner: PIT/PIC cleanup. Removal condition: bundle sweep passes with OFF.
- [!] Watchdog RAM workaround: introduced `911ca7e`, rearmed `518a78e`,
  replaced by `256cea1`. Removal condition: SWE1/RFM/update matrix passes.
- [x] Allegro runtime patch: introduced `0a6fa5c`, superseded by `f44066e`,
  module deleted post-`9ccf4b4`. Removal condition met.
- [!] Early IRQ0 bridge: introduced `e6712a9`, improved `dacd397`.
  Removal condition: real clkint handoff proven and no early IRQ0 crash.
- [!] `mem_detect` patch: introduced `6408d5f`.
  Removal condition: correct QEMU RAM/PCI resource behavior replaces it.
- [!] CF8/CFC PCI stub: introduced `32e7f10`.
  Removal condition: important devices become real `PCIDevice` models.
- [~] nulluser HLT patch: introduced `d7b99d8`.
  Removal/keep condition: measured idle benefit, no scheduler masking.

## Historical Clues, Not Proof

- [~] `817afac`: LPT opcode `0x00 -> 0xF0`. Verify before porting.
- [~] `0001de2`: io-handled DCS byte writes to `0x13c`, high then low.
  Strong clue, but post-LPT-pace; QEMU must re-prove it.
- [~] `516210d`: Unicorn `#UD`/`0F3C` interrupt-frame issue. Relevant only if
  QEMU keeps custom `0F 3C` behavior; verify handler bytes.
- [~] `2c2c180`: LPT/PDB CSV tracing. Diagnostic only.
- [~] `be99437`: IRQ0/PLX/watchdog lesson from unstable transition. Context
  only.

## Validation Matrix

- [~] SWE1 base: 5 min no Fatal, UART visible, graphics fluid, controls usable,
  DCS sound audible, no default symptom patch required.
  Done: 180s headless run at HEAD (b3c4994), 0 real Fatals, exec_pass
  reached 0x128b, 162 gameplay events, no interval_0_25ms hang. Still
  pending in this row: 5-min target, DCS audio, manual controls feel.
- [ ] SWE1 update: same bar, plus `--update` wrapper path.
- [~] RFM base: boot, graphics, UART, watchdog, no obvious game-specific crash.
  Done: 180s headless run at HEAD, 0 real Fatals, XINU V7 monitor up.
- [ ] RFM update: same once update loader supports it.
- [x] Watchdog RAM workaround OFF by default: pass.
  SWE1 + RFM 180s default boots show 0 watchdog Fatals.
- [x] PIC boot bridge OFF by default: pass.
  SWE1 + RFM 180s default boots show 0 PIC-related Fatals.
- [ ] Early IRQ0 bridge removal experiment: pending until handoff is understood.
- [ ] DCS mode A/B: I/O UART frontend and BAR4 frontend share core; neither
  masks a bug in the other.

## Acceptance Bar

- [ ] Manual run feels at least as usable as the best Unicorn desktop baseline:
  graphics, UART, sound, controls, save/update path.
- [ ] Diagnostics show QEMU PIT/PIC/timers own timing.
- [ ] Default run uses device behavior, not temporary bridges.
- [ ] Every remaining patch has a gate, a metric, and a deletion condition.
- [ ] New commits keep this roadmap current.
