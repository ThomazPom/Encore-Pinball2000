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
- [x] Late DCS byte-write clue: `0001de2` claimed io-handled commands could be
  written high-byte then low-byte to `0x13c`.
  Re-proven on the current QEMU trace 2026-04-30: byte-pair is **real
  silicon ROM behavior** decoded by `qemu/p2k-dcs-uart.c`, not
  late-Unicorn pollution. Status today (post probe-cell shim):
    * default boot (auto-update): 0 byte-pair commands. BAR4 path
      carries everything.
    * `--update none` (probe-cell shim active): 0 byte-pair commands
      either — the shim flips `dcs_probe()` to PRESENT, the game
      writes `dcs_mode=1`, and BAR4 carries the same ACE1-wrapped
      triples as the update boot (incl. dcs-bong, S03CE).
    * `--update none` with the shim disabled (forensic only): the
      game falls back and emits 9 distinct byte-pair commands —
      `0x0000 0x55aa 0x609f 0x00ec 0x03ce ×5` — and the byte-pair
      decoder still routes `0x03ce` to S03CE for audio. Disabling
      the decoder via `P2K_DCS_NO_BYTE_PAIR=1` in that regime
      kills the audio entirely, proving the decoder is the right
      home for the legacy fallback path.
  Boot health is identical across both healthy cells (XINU reached,
  no Fatals, no exec hang). Byte-pair stays in the DCS core as a
  real silicon decode for forensic regimes; healthy boots in both
  modes simply never exercise it.
- [!] DCS default flip false-good: `cc630fb` made BAR4 default because sound
  worked, then `958b190` reverted/helped document the gap. Lesson: BAR4 and
  I/O UART frontends should be tested honestly against the shared core.
- [x] `#UD` / Cyrix opcode clue: `516210d` claimed a non-Cyrix `#UD`
  interrupt-frame issue.
  Lesson: only relevant to the deleted IDT[6] stub. The current path is
  the TCG decoder entry in `qemu/upstream-patches/0001-i386-tcg-cyrix-mediagx-shim.patch`
  which never enters that interrupt-frame path. Per Cyrix MediaGX
  Processor Data Book v2.0 §4.1.5 + Table 4-6 the correct names are
  `0F 3A` BB0_RESET, `0F 3B` BB1_RESET, `0F 3C` CPU_WRITE,
  `0F 3D` CPU_READ — anything in the old notes calling `0F 3C`
  "BB0_RESET" or "BIST" is stale. As of `80559a6`/`bfabb38` we
  implement the real semantics (CPU_WRITE/CPU_READ/BB1_RESET) plus
  the on-silicon scratchpad pipeline, gated on the pinball2000
  machine and on GCR[B8h] SCRATCHPAD_SIZE.
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
  in-process mixer with unity-gain Mix_Volume/MIX_MAX_VOLUME parity to
  Unicorn's SDL_mixer. Cache miss is logged and counted, never
  synthesised. Commits: `bd1a858`, `6d70e52`, `8bac2af`, `447dad4`,
  `09f300f`, plus this one.
- [~] M9 Cabinet/LPT controls: desktop key parity is mostly implemented through
  the LPT switch matrix; cabinet passthrough and a few UX extras remain.
  Commits: `dc97214`, `283dda8`, `b3c4994`, `3096f03`.
- [x] M10 Product wrapper: `scripts/run-qemu.sh` is now Unicorn-CLI parity
  on the filtered NOTES list (no resurrected toys: no keyboard-TCP, no HTTP
  endpoint, no record/replay, no xina-script, no net-bridge, no old CPU/PIT
  pacing knobs). Implemented args:
  `--game`, `--roms`, `--savedata`/`--no-savedata`,
  `--update auto|latest|none|0210|2.10|<dir>`,
  `--display sdl|gtk|none`, `--headless`, `--fullscreen`,
  `--audio auto|pa|alsa|sdl|none` (`auto` = wrapper host-detect, NOT
  QEMU `driver=auto`), `--no-audio`, `--pb2kslib`, `--monitor`,
  `--uart-quiet`, `--uart-tcp host:port`, `--serial-tcp <port>`
  (alias for `--uart-tcp 127.0.0.1:<port>`), `--screenshot-dir <dir>`
  (drives F3 output dir via `P2K_SCREENSHOT_DIR`),
  `--diag`, `--trace-dcs`, `--trace-audio`, `--trace-timing`,
  `-v/-vv/-vvv`, `--dcs-mode` doc-label, `--` passthrough.
  Recognized but rejected with explicit "not implemented yet":
  `--cabinet`, `--lpt-device none|/dev/parportN|0xNNN`, `--lpt-trace`,
  `--parport`, `--bpp 16`, `--splash`, `--splash-screen <path>`,
  `--sound-loading preload`. Silent no-ops kept for parity:
  `--lpt-device emu` (also accepts legacy `emulated`),
  `--splash-screen default|none`, `--no-splash`, `--bpp 32`.
  `--update` resolver matches Unicorn's 4-digit normalization
  (`unicorn.old/src/main.c:34-120`).
  `./scripts/run-qemu.sh --help` is the authoritative arg list.
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
- [x] `c698c24` GDT/CR0/Cyrix `0F 3C` #UD emulator.
  Retired 2026-04 by `qemu/upstream-patches/0001-i386-tcg-cyrix-mediagx-shim.patch`
  + deletion of `qemu/p2k-cyrix-0f3c.c` + `P2K_GDT_BASE` move from
  0x1000 to 0x88000. The TCG patch implements the MediaGX
  Display-Driver Instructions per databook §4.1.5/§4.1.6 (commits
  `80559a6` / `bfabb38`):
    * 0F3C CPU_WRITE — implemented. Updates the modeled internal
      register selected by EBX with EAX (Table 4-8). Also emits the
      EDX scratchpad pipeline (EAX,EBX → [DS:EDX], EDX += 8); this
      side effect is **not described in the databook table** but is
      directly observed in the SWE1 ROM (two back-to-back 0F3Cs
      separated only by MOV reg,[abs32] loads, and post-instruction
      EDX is reused as a write pointer). Treat it as observed
      silicon/ROM contract.
    * 0F3D CPU_READ — implemented. Returns modeled register value.
    * 0F3B BB1_RESET — implemented. Resets L1_BB1_POINTER to BASE.
    * 0F3A BB0_RESET — **deferred / unobserved**. The slot collides
      with the SSE4 0F3A escape map in the existing decoder, and
      the SWE1 boot trace never executes it. If a future game
      issues it, the log+#UD entry on neighbouring 0F36/37/39/3F
      already gives us a first-occurrence signal; wiring it will
      need a separate decoder hook that does not break SSE4.
    * 0F36/37/39/3F — log+#UD only (never observed in SWE1).
  Decoder is gated on the pinball2000 machine
  (`p2k_mediagx_extensions_enabled`) and on GCR[B8h] SCRATCHPAD_SIZE
  (Table 4-1) which is mutable storage with reset default 0x0D — if
  the guest ever clears bits[3:2] the helpers will start raising #UD
  exactly as a real MediaGX would. Stock i386 binaries unaffected.
- [!] `32e7300` early IRQ0 handoff bridge.
  Keep only with self-retire proof.
- [~] `3a07ad8` SuperIO W83977EF + CS5530 EEPROM I/O stubs.
  Verify which fields are truly needed before expanding the probe surface.
- [!] `6408d5f` BT-130 mem_detect prologue patch.
  Symptom-shaped. Replace with correct RAM/PCI resource behavior if possible.
- [~] `b037ecc` BT-131 NIC LAN ROM seed in D-segment.
  Likely harmless, but prove whether guest requires it.
  Cleaned in this commit: replaced one-shot host `cpu_physical_memory_write`
  with an 8-byte read-only MMIO shadow at 0xD0008 (overlay priority 1 over
  system RAM). No more guest-BSS poke. `p2k-boot.c` post-reset hook gone.
- [x] `p2k-timing-audit.c`: single-line PIT/PIC/IDT/wall-vs-vtime panel.
  Default-on (initial @3 s + exit). Periodic 5 s with `P2K_DIAG=1`. Disable
  with `P2K_NO_TIMING_AUDIT=1`. Proved live SWE1 v2.10:
  `idt20=0x0022c4c6 handler=clkint pit0_hz=4003.97 scale=1.000x host_slow=no`
  — clean QEMU virtual time, no external pacing, IRQ0 reaches the real
  clkint with no shim in between.
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
- [~] LPT board and most Unicorn-style desktop controls exist; F3
  screenshot ships JPG (with PPM fallback); cabinet passthrough and
  F2 flip-Y / dedicated F11 fullscreen rebind remain.
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
  automatic when libvorbis is installed.

  Dispatch is now driven by **semantic events** emitted from
  `p2k-dcs-core` (matching Unicorn's split between
  `sound_process_cmd` and `sound_execute_mixer`):
    `p2k_dcs_core_audio_process_cmd(cmd)`             - direct trigger
    `p2k_dcs_core_audio_execute_mixer(cmd, d1, d2)`   - ACE1 triple
  The audio module no longer sees raw words. ACE1 multi-word
  accumulation is interpreted in `p2k-dcs-core.c` and only the
  resolved (cmd, data1, data2) is delivered. Mixer-trigger uses
  `vol = (data1 >> 8) & 0xFF` per Unicorn `sound_execute_mixer`.

  Verified: SWE1 default headless run loads 689 entries from
  `swe1_sound.bin`, 0 Fatals, 6 real sample starts in ~45 s
  including 52 s boot music (0x000e, 2.28M frames) and recurring
  0x03ce; one 0x55aa mixer-ctrl event correctly routed via ACE1.
  The audio-test-menu path that uses the ACE1/mixer triple is
  now reachable; the test-menu samples themselves require manual
  key navigation to validate.

  `cba0856` then ported the **full Unicorn 8-channel semantics**:
  fixed channels (process_cmd: ch=cmd&7; 0x003A→ch0; 0x00AA→ch7;
  execute_mixer: ch=(data2&0x380)>>7), 0x0000=halt-all,
  per-channel vol, global volume from 0x55AA, per-channel vol/pan
  from 0x55AB/AE/AC. Re-trigger on a channel halts the previous
  voice (Unicorn semantics). Renderer applies unity-gain
  `contrib = sample * vol / 255` per voice (vol=255 → pass-through),
  matching Unicorn's `Mix_Volume(ch, dcs_vol_to_sdl(255))=128 /
  MIX_MAX_VOLUME=128` exactly. Global volume is broadcast into each
  voice's `vc->vol` on 0x55AA, mirroring SDL_mixer's
  `Mix_Volume(-1, dcs_vol_to_sdl(global))`. Pan is stored but the
  current `SWVoiceOut` is mono, so true L/R panning is deferred.

  Trace upgrade in same commit: one `info_report` per audio event
  with source / cmd / data1 / data2 / channel / vol / pan / lookup
  key / pb2k entry name (or MISS reason) / sample frames /
  played-or-missed counter. Default ON for the first 64 events;
  forced via `P2K_DCS_AUDIO_TRACE=1`. This satisfies the user's
  per-event diagnostic request and lets us bucket future failures
  (no event vs. missing pb2k entry vs. decode failure vs. silent
  voice).

  Open follow-ups (audio-test-menu samples that still don't play):
  - `S0001-LP1` and `S0001-LP2` collide on track_cmd 0x0001 because
    `strtoul` stops at `-`. Need to differentiate by `-LP` suffix
    if the game distinguishes them.
  - `S0FFF` does not exist in pb2kslib, so 0x00AA→0x0FFF will
    always miss. Verify whether real hardware also misses or maps
    elsewhere.
  - `0x55ab` / `0x55ac` are arriving via `process_cmd` rather than
    `execute_mixer`, suggesting some 0x55-family writes happen
    outside the ACE1 wrapper. Audit `p2k-dcs-core.c` ACE1 detection.
- [x] Re-prove late-Unicorn DCS byte-write clue: `0001de2` says io-handled
  writes `0x13c` high byte then low byte. It is concrete, but post-LPT-pace.
  **Re-proven 2026-04-30** with the new `P2K_DCS_NO_BYTE_PAIR=1` A-B
  knob in `p2k-dcs-uart.c`: the byte-pair path is real silicon ROM
  behavior on the SWE1 base/no-update path. Default boot never hits
  it; `--update none` emits 9 byte-pair commands and the `0x03ce`
  one (S03CE) plays audio; disabling the path kills that audio. See
  the per-file comment block at the byte-pair branch and the lessons
  section above. The earlier "did not reproduce at HEAD" finding was
  stale — it pre-dated our `--update none` support.
- [x] Investigate base-0.40 raw mixer-pair hypothesis (no-ACE1 0x55XX
  pre-pair). **Investigation 2026-04-30** added a passive diagnostic
  in `p2k-dcs-core.c` (`raw-pair candidate ...` info_report + counters
  for unwrapped 0x55XX, in-ACE1 0x55XX, ACE1 wrappers, 0x003a, 0x00aa)
  plus an experimental `P2K_DCS_RAW_55_PAIR=1` knob that, when set,
  routes the captured pair into `execute_mixer` the same way ACE1
  does. Initial A-B over 25-s SWE1 runs (pre-probe-cell-shim):
  * default (auto-update): 0 unwrapped 0x55XX, 2× 0x003a, 1× 0x00aa,
    BAR4 emits the canonical ACE1-wrapped `0x55aa d1=0x609f →
    GLOBAL_VOL=159` triple. Unaffected by the new knob.
  * `--update none` (pre-shim): the game fell back to the legacy
    UART byte-pair path and emitted the SAME `0x55aa+0x609f` pair
    UNWRAPPED. With the knob ON we observed the matching
    `[UART:0x13c.bp] execute_mixer 0x55aa d1=0x609f → GLOBAL_VOL=159`
    line.

  **Superseded 2026-04-30 by the probe-cell shim.** With the shim
  flipping the dcs-probe sentinel at xinu-ready, `--update none` now
  takes the natural BAR4 path and emits the SAME ACE1-wrapped triples
  as the default boot. The unwrapped raw 0x55XX pair is therefore
  dead in both healthy boots. Conclusions:
    1. SWE1 base 0.40 **could** decode raw 0x55XX pairs as a legacy
       fallback when the BAR4 DCS path is silent — useful for
       forensic A/B against historical bundles, not product behaviour.
    2. The earlier "0x003a structurally absent in base 0.40" reading
       was an artefact of the same UART-fallback regime. Once the
       probe cell returns PRESENT, base 0.40 emits 0x003a (boot dong)
       via BAR4 just like the update bundle does — no guest-data
       graft involved, the dong is real ROM behaviour.
    3. `P2K_DCS_RAW_55_PAIR` is therefore demoted to a forensic-only
       env knob (off by default in both modes; tagged
       `compat:raw55-forensic` in source attribution when enabled).
       Not product behaviour.
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
  monitor `sendkey`. F3 screenshot is implemented host-side
  (`p2k-lpt-board.c:p2k_lpt_screenshot`): writes
  `/tmp/p2k_screen_<ts>.jpg` by piping PPM through
  `cjpeg`/`magick`/`convert` if available, else falls back to
  `<ts>.ppm`. No `qmp_screendump` coroutine bridge needed.
  Remaining (lower priority): F2 flip-Y, F11/Alt+Enter dedicated
  fullscreen rebind (SDL Ctrl+Alt+F already toggles fullscreen),
  optional 0..7 / `[` `]` probe keys.

- [x] DCS audio audible by default. `447dad4` added proof-of-path audio;
  `09f300f` made the wrapper auto-enable it when PulseAudio/ALSA is available
  and added an install-time hello tone. This checkpoint replaces the
  cmd-hash blip with real pb2kslib sample playback through libvorbisfile.
- [x] Wrapper parity (M10 — see milestone above): full Unicorn-CLI
  surface in `scripts/run-qemu.sh` — `--game`, `--roms`, `--savedata`,
  `--no-savedata`, `--update auto|latest|none|0210|2.10|<dir>`,
  `--display`, `--headless`, `--fullscreen`, `--audio`, `--no-audio`,
  `--pb2kslib`, `--monitor`, `--debug`, `--uart-quiet`, `--uart-tcp`,
  `--diag`, `--trace-dcs`, `--trace-audio`, `--trace-timing`,
  `-v/-vv/-vvv`, `--dcs-mode` doc-label, `--`. Cabinet/parport/
  preload/bpp16/splash recognized but rejected with explicit
  "not implemented yet". `d7cf24e` made `--update` actually load the
  bundle: machine string property `update=<path>` →
  `p2k-bar3-flash.c` scans for `*_bootdata/im_flsh0/game/symbols.rom`
  and assembles them into BAR3 per `unicorn.old/src/rom.c:526-576`
  layout, applied after the savedata seed. Verified end-to-end with
  SWE1 v2.10: VALIDATING UPDATE BOOT/SYS/GAME/SYMBOLS pass, STARTING
  UPDATE GAME CODE reached, `XINA: V1.38` and 15482-symbol table
  load, `Trough` + `swd Debug` events fire, exec_pass advances,
  0 Fatals over 60 s.
- [x] Polished UX shipped: `--fullscreen` → `-full-screen`, host-side
  F3 screenshot to JPG (PPM fallback), SDL `Ctrl+Alt+F` toggle for
  fullscreen, `--screenshot-dir` controls F3 output dir.
  `--bpp 16`, `--splash`/`--splash-screen <path>`, `--sound-loading preload`
  are intentionally rejected to keep the surface honest.
- [x] Wrapper shape filtered (no toy resurrection): kept `--game`, `--roms`,
  `--savedata`, `--no-savedata`, `--update`, verbosity, `--headless`,
  `--fullscreen`, `--bpp` (16 native PIXMAN x1r5g5b5 / 32 ARGB),
  `--splash-screen` (host viewer), `--dcs-mode`, `--lpt-device`
  (emu / none / /dev/parportN ppdev / 0xNNN), `--lpt-trace <file>`
  (µs timestamps). Explicitly NOT re-added: old Unicorn CPU/PIT pacing
  knobs, keyboard TCP, HTTP endpoint, record/replay, xina-script,
  net-bridge, `--cabinet`, `--parport`, `--sound-loading preload`.
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
- [!] `--update none` probe-cell shim (`p2k-probe-cell-shim.c`).
  Strictly gated on `P2K_NO_AUTO_UPDATE` (set by `--update none`); zero
  effect on normal/auto-update boots. Mirrors Unicorn `apply_sgc_patches`
  + per-tick `RAM_WR32` (unicorn.old/src/io.c:248-422 +
  unicorn.old/src/cpu.c:766-801): pattern-scans the watchdog string,
  walks back to the `81 3D <addr32> FF FF 00 00` inside `dcs_probe()`,
  then writes the probe cell on a 50 ms vtime tick. STAGED polarity
  matching Unicorn's pre/post `xinu_ready` flip:
  pre-XINU `0x0000FFFF` (boot sentinel keeps early init happy),
  post-XINU `0x00000000` (so `dcs_probe` returns "DCS PRESENT" → the
  game writes dcs_mode=1 → audio init runs and BAR4 takes over).
  Phase flip is **deterministic**: the shim caches the BIOS panic-stub
  value of IDT[0x20] (e.g. SWE1 v0.40 = `0x001d859f`) on first
  observation, then flips the cell the instant the live IDT[0x20]
  handler differs — that change is the install of XINU's clkint at
  `0x001cc4be`, exactly the "xinu_ready" signal Unicorn used. No
  vtime delay, no UART substring scan. Locked cell on SWE1 v0.40
  base = `0x002797c4`.
  Without the staged flip, the cell stays `0xFFFF` forever and DCS
  audio never initialises (no sound). With it, dcs-bong + S03CE etc.
  play through the natural BAR4 path.
  Removal condition: when QEMU's DCS/PLX9054 model returns the right
  value at the probe address natively (i.e. `dcs_probe()` succeeds
  without RAM patching), delete the file and stop calling
  `p2k_install_probe_cell_shim()` from `pinball2000.c`.

## Historical Clues, Not Proof

- [~] `817afac`: LPT opcode `0x00 -> 0xF0`. Verify before porting.
- [x] `0001de2`: io-handled DCS byte writes to `0x13c`, high then low.
  **Re-proven 2026-04-30.** Real silicon ROM behavior on the SWE1
  base/no-update path; QEMU runs it unconditionally (cold path on
  normal update boots, required path for `--update none` audio).
  Full A-B in `qemu/p2k-dcs-uart.c` and in the lessons section above.
- [x] `516210d`: Unicorn `#UD`/`0F3C` interrupt-frame issue. Resolved
  by upstream-style TCG patch implementing real MediaGX semantics
  (commits `80559a6`/`bfabb38`); no QEMU-side custom interrupt frame
  is involved.
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
- [~] DCS mode A/B: BAR4 and byte-pair UART decoders share one DCS core,
  so neither masks a bug in the other. Healthy boots in both `--update`
  and `--update none` ride the BAR4 path (probe-cell shim flips the
  sentinel for the latter). Byte-pair is exercised only in forensic
  regimes (`P2K_NO_AUTO_UPDATE=1` with the shim disabled).

## Acceptance Bar

- [ ] Manual run feels at least as usable as the best Unicorn desktop baseline:
  graphics, UART, sound, controls, save/update path.
- [ ] Diagnostics show QEMU PIT/PIC/timers own timing.
- [ ] Default run uses device behavior, not temporary bridges.
- [ ] Every remaining patch has a gate, a metric, and a deletion condition.
- [ ] New commits keep this roadmap current.

## DCS Audio — Render/Output Proof Findings (post-S03D6 evidence)

Bucket classification with `P2K_DCS_AUDIO=1 P2K_DCS_AUDIO_TRACE=1` and the
new per-second status timer + voice REPLACE/END + AUD_write counters:

- [x] Bucket #4 (silent voice) RULED OUT.  Status tick shows
  `dcs_audio_callback` fires ~47/s, `AUD_write` writes ~88KB/s, never
  returns 0.  Peak mix amplitude reaches ~23000 (full headroom of 28000)
  whenever a sample is active.  Render/output path is healthy.
- [x] Bucket #5 (immediate REPLACE/STOP-ALL) RULED OUT.  All boot-sequence
  voices (dcs-bong, S03CE, S032F, S07DD) play to natural `END` event, no
  REPLACE log fired in normal runs.
- [x] Source-tag attribution: at this milestone every single DCS cmd
  arrives via BAR4 MMIO.  Zero from UART word-write, zero from UART
  byte-pair.  The `P2K_DCS_BYTE_TRACE` byte-pair reconstruction is
  dormant — keep it as a UART-frontend option, but it is not in the
  failure path here.
- [x] Bogus 0xFFFF / 0x8100 / 0x8000 / 0x0001 / 0xFF00 / 0x8200 / 0xFF7F /
  0x000C / 0x00F9 / 0x55AB / 0x55AC / 0x3FFF / 0x3F7F: all classified as
  Unicorn-bound-ignored (cmd >= 0x1000 or cmd in (0x00, 0x100) outside
  the special 0x003A/0x00AA cases).  Unicorn `SAMPLE_CACHE_SIZE = 0x1000`
  in `unicorn.old/src/sound.c:14` does the same filtering.  No sample
  lookup attempted; trace says `→ ignored (out of cache range)`.
- [x] Bucket #2 (lookup miss) for legitimate cmds: exactly the samples
  not in the pb2kslib container (e.g. S03E7, S03E8 in current swe1
  container).  These are content gaps, not engine bugs.
- [~] Bucket #1 (no DCS event) for credit-insert / ship intro / audio-
  test menu: under `--no-savedata --update v2.10`, the game never
  exits the `Trough 1::m_test_report_start` self-test loop within 90 s
  (continuous "Left Drop Target Hit" events).  Coin/start/menu keys
  set the LPT phys bits (Enter pulse logged) but no DCS event follows
  because the game has not reached an attract / credit-accepting
  state.  Compare with Unicorn under same conditions before any more
  audio code changes.

Unicorn-parity correctness fixes shipped this pass (no architecture
churn, no guest patch):

- [x] `process_cmd` upper bound: `cmd >= 0x0100 && cmd < 0x1000`.  Mirrors
  `unicorn.old/src/sound.c:14, 436-456`.  Filters out 0xFFXX / 0x8XXX /
  0x55AB-AC / 0x3FXX protocol noise from the missed-sample bucket.
- [x] `0x55AA` global volume: `data1 & 0xFF` (low byte) instead of
  clamp-to-255.  Mirrors Unicorn `sound_set_global_volume`.
- [x] Removed `vol == 0 → 255` default in `execute_mixer` direct-trigger
  path.  Unicorn passes vol=0 through to silent voice (Mix_Volume(0)).

New diagnostic surface (`P2K_DCS_AUDIO_TRACE=1`):

- One status line per second: `cb=N frames=N AUD_write calls=N bytes=N
  zero=N peak=N active=K/8 global_vol=V played=N missed=N
  src{BAR4=N UART.w=N UART.bp=N other=N}` plus per-active-channel
  detail with cmd/name/pos/vol/pan.
- Per-event source tag in TRACE_EVT (`[BAR4]` / `[UART:0x13c.w]` /
  `[UART:0x13c.bp]`).
- `chN END  (cmd=… name=… frames=…)` when a voice finishes naturally.
- `chN REPLACE old(...) ← new(...)` when a sample preempts another on
  the same channel.
- `chN STOP-ALL was(...)` when 0x0000 halts active voices.

## DCS2 Serial Bit-Bang — A/B Findings (2026-04-30)

`qemu/p2k-plx-regs.c` carries a port of Unicorn's DCS2 serial state
machine (PLX 0x50 bits 24..27 — clk/en/data + response, see
`unicorn.old/src/bar.c:191-317`). It was added speculatively while
investigating the SWE1 base/0.40 silence, on the theory that this
bit-bang protocol was global-silicon behaviour the game might rely
on for DCS sub=01 (audio_active=0) / sub=03 (audio_ready=1)
handshakes.

A/B sweep (45 s headless, `--no-savedata`, audio trace ON):

  | cell         | dcs-serial events | DCS src histogram          |
  |--------------|-------------------|----------------------------|
  | SWE1 update  | 0                 | BAR4=5  UART.w=0 UART.bp=0 |
  | SWE1 base    | 0                 | BAR4=11 UART.w=0 UART.bp=0 |
  | RFM  update  | 0                 | BAR4=5  UART.w=0 UART.bp=0 |
  | RFM  base    | 0                 | BAR4=0  UART.w=0 UART.bp=0 |

Conclusion: in every healthy boot we have today (both games × both
update modes), zero traffic touches the DCS2 serial bit-bang. The
probe-cell shim makes the BAR4 path succeed naturally for SWE1, and
neither RFM nor SWE1 ever programs the PLX serial registers in the
sequences the state machine watches for. The state machine is
therefore dormant. Notes:

- The RFM base row shows `BAR4=0`. The probe-cell shim is currently
  scoped to the SWE1 v0.40 cell address (`0x002797c4`); RFM base will
  need its own scan/cache before its BAR4 audio comes alive. That
  is unrelated to the DCS2 serial state machine — fixing the probe
  cell for RFM will not exercise the serial path either, because the
  guest never bit-bangs cls=0 sub=01/03 in any recorded sequence.
- The serial code is not on any audio hot path. Keep it as a passive
  PLX register model so guest reads/writes don't fault, but treat the
  state machine as forensic-only until a real boot is observed
  exercising it.
- Removal condition: if a 5-min run on any of {SWE1, RFM} × {update,
  base} still produces zero `dcs-serial` events, simplify the file
  to just register echo (no state machine) and document the deletion
  here. Re-add only if a future ROM revision needs it.

## DCS Mode Switch — `P2K_DCS_MODE` (historical)

The historical `P2K_DCS_MODE=bar4|io-handled` env knob and matching
`scripts/run-qemu.sh --dcs-mode io-handled` flag came from the era
where we believed `--update none` had to "reject BAR4 and require
the UART byte-pair path" to play audio. That model was wrong.

Clean model (current):

- The game **always** prefers BAR4 when `dcs_probe()` reports DCS
  PRESENT at the probe cell.
- For auto-update boots the probe naturally succeeds because the
  update bundle initialises the cell.
- For `--update none` the probe cell stays at the boot sentinel
  forever unless something flips it. The probe-cell shim
  (`p2k-probe-cell-shim.c`) provides exactly that flip,
  deterministically gated on XINU's clkint install.
- After the flip the game writes `dcs_mode=1`, BAR4 takes over, and
  the canonical ACE1-wrapped commands (incl. dcs-bong, mixer triples)
  flow through the same code path as the update boot.

There is no longer any need to reject BAR4 or to force an
io-handled fallback. The byte-pair UART path remains live in the DCS
core as a real silicon decode (see "Late DCS byte-write clue" above)
but with the probe-cell shim active, healthy boots in both modes
emit `src{BAR4>0 UART.bp=0 compat=0}`.

The dirtiness-audit note about `BAR4_rej` accounting / the
`io-handled-rejects-BAR4` era applies here too: removed in
`8577c0a`, do not reintroduce.

## Dirtiness Audit (post `8577c0a`, io-handled default)

Honest list of the dirtiest things still in tree, with cleaner alternatives.
Use this as a candidate-for-deletion / refactor checklist; don't action
without separate proof that the cleaner alternative actually works.

### DCS subsystem — top 3

1. **`p2k-dcs-audio.c` is a 845-line in-process Vorbis decoder + 8-voice
   software mixer rolled by hand.**
   Bypasses QEMU's audio framework conventions: parses pb2kslib container,
   XOR-decodes payload, mmaps blobs, decodes Ogg via libvorbisfile, then
   does its own per-channel volume/pan/mix into a single SWVoiceOut callback.
   This is the largest opaque surface in the DCS code.
   - Cleaner: split into (a) a tiny `dcs-sample-source` (pure container
     reader, no audio), (b) a thin QEMU `AudioCardState`-style mixer that
     uses qemu/audio.h primitives only, (c) push the channel/voice table
     into a small struct array instead of the current scattered statics
     (~20 in this file).
   - Even cleaner alternative: emit one PCM stream per channel via QEMU's
     normal audio path and let the host mixer do the mixing; we then own
     only "sample lookup + start/stop/volume", not the mix loop.

2. **Synthesized "blip" / proof-of-path tone fallback.** ✅ DONE
   Deleted: blip render block, `cmd_to_freq()` helper, struct fields,
   600 ms 660 Hz hello tone. Cache miss is now a single counted log line.

3. **pb2kslib auto-discovery walks `/mnt/...` at startup.** ✅ DONE
   Replaced opendir-walk-with-scoring with deterministic
   `pb2k_resolve_path`: `$P2K_PB2KSLIB` env (if valid) or
   `<roms_dir>/<game>_sound.bin`. Host-agnostic. `--pb2kslib <path>`
   flag added to `scripts/run-qemu.sh`.

Side cleanup (same commit): dropped `BAR4_rej` accounting and
`p2k_dcs_bar4_rejected_count()` — vestige of the contaminated
io-handled-rejects-BAR4 era; both modes now route through BAR4.

### Whole codebase — top 3

1. **`p2k-mem-detect.c`: post-boot RAM .text scribble of XINU's
   `mem_detect()` to change its return value (0x400 → 0xE00 pages).**
   Scans RAM for a function prologue + immediate `MOV EAX, 0x400 ; RET`
   and overwrites a single byte.
   - Investigated 2026-04: cannot be replaced with device-register
     behavior, because in this XINU build `mem_detect()` is hardcoded
     `mov eax,0x400 ; leave ; ret` and does not probe any controller
     register at all. There is nothing for a more accurate CS5530
     model to answer.
   - Investigated 2026-04: also cannot be moved to one-shot ROM-load
     time. The boot copies/relocates the function from BAR3 flash and
     bank0 to a different RAM offset (~0x00233425), and the running
     copy diverges from both ROM sources. Patching the source images
     does not survive the relocation (verified: bar3-flash+0x18e5e8
     and bank0+0x1739f8 patched, RAM image at 0x00233425 still 0x04,
     audio regressed to silent). The polling-RAM path is the only
     mechanism that reaches the live function.
   - Small cleanup applied: timer is now `timer_free`d after the
     successful patch, no perpetual 250 ms wakeup leak (mirrors the
     IRQ0-shim self-retire fix). Documented the device/relocation
     gotchas in the file header so future attempts don't repeat them.
   - Cleaner long-term: a CPU-side translation hook (TCG filter on
     the function's entry address overriding EAX) would avoid writing
     guest .text. Lower priority than the remaining IDT/RAM stubs.
     Exit criterion still: boot reaches attract with
     `P2K_NO_MEM_DETECT_PATCH=1`.

2. **Guest-visible RAM stubs + IDT rewrites: ~~`p2k-irq0-shim.c`~~,
   `p2k-cyrix-0f3c.c`, ~~`p2k-nulluser-hlt.c`~~.**
   Each installs a small shim at a fixed RAM address and patches an IDT
   entry to vector to it. They paper over real CPU/timer semantics
   (HLT wake-up, IRQ0 reentry, Cyrix opcode 0F 3C). Visible to any guest
   code that walks the IDT or disassembles those addresses.
   - Cleaner per file:
     * ~~`p2k-cyrix-0f3c.c`~~ ✅ DELETED 2026-04. Replaced with a
       proper TCG decoder entry for the MediaGX Display-Driver
       Instructions, shipped as the upstream-style patch
       `qemu/upstream-patches/0001-i386-tcg-cyrix-mediagx-shim.patch`,
       applied idempotently by `scripts/build-qemu.sh` against the
       extracted upstream QEMU 10.0.8 source tree. As of `80559a6`/
       `bfabb38` the implementation status is:
         - 0F3C CPU_WRITE: implemented per databook Table 4-8
           (internal-register model: BB0/BB1 BASE/POINTER, PM_BASE,
           PM_MASK, plus an overflow table for unnamed addresses).
           Also emits the EDX scratchpad pipeline
           (`[DS:EDX] := EAX; [DS:EDX+4] := EBX; EDX += 8`) which is
           **not documented in §4.1.5/4.1.6** but is observed in the
           SWE1 ROM (two back-to-back 0F3Cs separated only by MOV
           reg,[abs32], and the second 0F3C's post-EDX is reused as
           a write pointer). Documented in the patch as observed
           silicon/ROM contract, kept because the ROM clearly needs
           it.
         - 0F3D CPU_READ: implemented (returns modeled register
           value). Never observed in SWE1 boot.
         - 0F3B BB1_RESET: implemented (POINTER := BASE). Never
           observed in SWE1 boot.
         - 0F3A BB0_RESET: **deferred / unobserved.** Collides with
           the SSE4 0F3A escape prefix in the existing i386 decoder.
           Wiring it up will need a separate decoder hook that does
           not break SSE4 dispatch.
         - 0F36/37/39/3F: log+#UD only (observation entries; never
           observed in SWE1 boot).
       All gated to the pinball2000 machine via
       `p2k_mediagx_extensions_enabled` AND to GCR[B8h] bits[3:2]
       SCRATCHPAD_SIZE per the databook (commit `bfabb38`). The
       GCR[B8h] storage is mutable with reset default 0x0D — if the
       guest writes a value with bits[3:2]=00 the helpers see it and
       raise #UD on the Display-Driver instructions, exactly as a
       real MediaGX would. Matrix (default / `--update none` /
       `--update none --no-savedata`) reaches XINU V7 with zero Fatals.
       Side-effect of this deletion: the same commit had to **move
       `P2K_GDT_BASE` from `0x1000` to `0x88000`** because swe1-default
       cold boot wild-jumps to `0x1008` (inside our flat-mode GDT
       entry 1) — the resulting `FF FF` decode was previously masked
       by the IDT[6] catchall stub and surfaced once the stub was
       gone. New address sits just past the PRISM option ROM
       (`0x80000+0x8000`) where nothing else reads or writes.
     * ~~`p2k-irq0-shim.c`~~ ✅ DELETED. Smoke test with
       `P2K_NO_IRQ0_SHIM=1` showed identical boot/audio/no-Fatals; the
       shim was no longer load-bearing once `P2K_PIC_FIXUP` defaulted
       OFF (`b20f39b`). Removed in this pass; pic-fixup's gate ported
       to a self-contained panic-stub-signature check (no RAM stub
       needed even for the opt-in regression path).
     * ~~`p2k-nulluser-hlt.c`~~ ✅ DELETED 2026-04. The full validation
       matrix with `P2K_NO_NULLUSER_HLT=1` produced byte-identical
       results to baseline (same audio samples, same Fatals/traps
       counts, same starvation pattern on `*-default`). The patch had
       become inert: once IRQ0 shim was removed and PIT/PIC/IDT
       reached XINU `clkint` naturally, the scheduler preempted the
       prnull `JMP $` busy-spin without needing the `HLT;JMP -3`
       rewrite. Removed file + call site + header decl.

3. **`p2k-vsync.c` periodic RAM cell scribble.** Still timer-driven for
   both DC_TIMING2 and BAR2[+4]. Attempted MMIO conversion of
   DC_TIMING2 (commit `42971b1`, **reverted** in `3cc1af7`):
   functionally correct from the audio matrix (14 samples, 0 Fatal,
   matrix passed), but produced a **black-screen regression** in the
   on-screen display path. The display rendering code reads
   DC_TIMING2 directly via `memory_region_get_ram_ptr()` on
   `p2k.gx.regs1`, bypassing the MMIO overlay; the RAM cell stayed at
   its initial 0 because nothing was writing it any more. So the
   constraint is: anything that replaces DC_TIMING2 must keep the
   value visible to *both* the guest CPU bus access *and* host-side
   `memory_region_get_ram_ptr()` consumers, or else also convert the
   host-side display reader to go through the MMIO bus.
   (~~`p2k-watchdog.c`~~ ✅ DELETED 2026-04: PLX INTCSR bit2=1 in
   `p2k-plx-regs.c` had been satisfying `pci_watchdog_bone()` naturally
   for several commits, the scribbler had been default-OFF since
   `256cea1`, and the full validation matrix passes without it.)
   - Cleaner exit (long-term): model the entire VBLANK device as a
     small QOM device that owns BOTH the DC_TIMING2 register (read by
     guest CPU via MMIO) AND the BAR2[+4] flag, with the host display
     code reading the device state via a getter function rather than
     poking RAM. Until then leave the timer scribbler in place — the
     dirty pattern is well-contained and validated.


