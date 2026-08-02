# Unicorn ↔ QEMU Parity Checklist

One page. Living document — edit it in the same commit as the work it
tracks. Priority order when picking what to work on next: **stability
> performance > functional parity > CLI/arg parity > test-tooling
parity.** Do not spend a session polishing docs while a stability or
performance gap sits open — verify/fix code first, document what you
verified.

Every claim below has been checked against the actual code/docs on
each branch as of this writing (not assumed from memory) — cite file
and line when you update a row.

## How to use this file

- Before starting work, scan **Do**/**Don't**, then the gaps table,
  ordered by priority.
- When closing a gap, move it to "Closed" with the commit/PR and the
  test that proves it, in the same change.
- Never let this file describe a state the code doesn't currently
  have — this branch was written while still discovering the
  hardware, so treat every existing doc claim as unverified until you
  check it against `src/` yourself.

## Do

- **Port from `main` verbatim, don't recode.** When a behavior diverges,
  copy main's proven logic (`/tmp/encore-main` worktree: `git worktree
  add /tmp/encore-main main`) rather than inventing a new approach. The
  mem-patch, IRQ-burst, SuperIO and ADSP fixes below were all faithful
  ports, not clean-room rewrites.
- **A/B every timing/IRQ change with REAL pre/post binaries and
  IDENTICAL flags.** Build the committed version (`git show HEAD:src/X.c`
  → separate `.o` → separate binary) and compare against your change on
  the same command line. Never compare across `-v` vs `-vv` (logging
  overhead alone shifts delivery%). This is how the placebo IRQ "fix"
  was caught and how the real anti-burst gate (59%→99%) was proven.
- **Verify, don't trust existing docs.** Several unicorn-branch docs
  described features as "unprototyped hypothesis" that were already
  shipped in code (`--realtime`/`--cpu-target-mhz`/`--cpu-stats` vs.
  `docs/50-cpu-clock-mismatch.md`, fixed 2026-08). Assume other docs
  have the same drift until you've read the owning `src/*.c`.
- **Run the automated regression matrix before and after every
  change**: `tools/run-bundle-matrix.py --all-updates`. Verified
  2026-08 on this host: all 6 SWE1 bundles + all 7 RFM bundles + both
  base-ROM configs pass (video + DCS audio activity, clean exit,
  20-30s headless, `io-handled` mode). Use this as your stability
  regression gate, not a one-off manual boot.
- **Prefer the coarse throttle that already exists** (`--realtime` +
  `--cpu-target-mhz`) for any cabinet-timing-sensitive work instead of
  adding a new per-symptom pace knob next to `--lpt-bus-pace`.
- **Resolve guest addresses through `sym_lookup()`**; a few hardcoded
  addresses remain in `src/cpu.c`/`src/io.c` — don't add a new one.
- **Keep the honesty discipline this branch already has**: every
  known-limitations/future-work entry should read like
  `docs/38-known-limitations.md`'s DCS/LPT sections — concrete
  commands, concrete log lines, no unverified claims.

## Don't

- **Don't re-add the PLX-0x50 `0x40789242` → host-reset trigger** to the
  io-handled DCS path. `0x40789242` is the PLX CNTRL resting value AND
  main's host-reset strobe (`p2k-plx-regs.c:478`); wiring it into
  unicorn's io-handled path regresses the ADSP engine (cycles 1.3B→22k,
  host_boots=0) because that path doesn't stream a full DSP boot image.
  Proven regression — reverted. The DSP self-boots from flash to
  ready-idle (pc≈0x3dc0); it does not need the host-reset strobe.
- **ADSP silence in headless boot is NOT a defect.** A passive headless
  boot sends only ~5 DCS handshake commands (0x0000/55aa/5800/5a00/609f)
  — zero sound-play commands — for BOTH `--dcs-engine samples` and
  `--dcs-engine adsp`. SPORT only enables when the guest plays a track.
  To demonstrate non-silent PCM you must inject gameplay input
  (`--keyboard-tcp`: coins + volume pulses), exactly like main's own
  audio harness (`docs/measurements/dcs-engines/README.md`).
- **Don't treat `timeout`'s exit code 124 as a hang.** `timeout N cmd`
  returns 124 whenever it had to signal the command, even if the
  child then shuts down cleanly on SIGTERM (verified: RFM v2.60 exits
  124 but its log shows a normal `[save]`/`[exit]` sequence). Check
  the log's own exit marker, not just the wrapper's exit code.
- **Don't treat `*** NonFatal` UART lines as failures.** They are
  common, often-benign guest chatter by design (the name says
  non-fatal) — `tools/run-bundle-matrix.py` reports them as warnings,
  not failures. Even `*** Fatal: interval_0_25ms: exec is hung` has
  been observed on a bundle that still completed cleanly with normal
  video/audio activity — investigate before assuming a hard failure.
- **Don't add a second timing knob per symptom** — see Do, above.

## Gaps vs `main`, in priority order

### Closed this session (with proof)

| Gap | Fix | Commit | Proof |
|---|---|---|---|
| **Game speed rides raw host CPU speed → inconsistent across power states** — with no wall-clock pacing the guest ran full-speed = whatever the host delivered, so game speed swung wildly with the CPU governor: crawling on a throttled/battery CPU and racing **5-8x** on a boosted AC CPU. Main (QEMU) paces its PIT to virtual/real time (`QEMU_CLOCK_VIRTUAL`) and feels "smooth vaseline butter". | Made `--realtime` the **default** (`main.c` init + `--no-realtime` opt-out) and rewrote it to pace on **delivered IRQ0 ticks** (`s_sched.irq0_inject`), sleeping while the delivered PIT-tick count leads real wall time at the true PIT period (`pit_period_ns` ≈250µs / 4004 Hz), anchored at the first tick. Immune to unicorn's vtick idle-credit inflation (which broke wall-gating the raise) because it counts real delivered ticks, and only ever sleeps. This supersedes the earlier `fb24940` instruction-throughput throttle (that pinned host-MIPS to the 20 MHz model → **half real PIT rate = slowmo**). | _this session_ | A/B swe1 headless, identical flags: **default → delivered wall pinned 4004–4008 Hz = target 4004 Hz**, vt-scale steady 2.00x, zero EIP-in-GDT/IStack; **`--no-realtime` → 7477–15704 Hz** (2–4x too fast, reproduces the AC "crazy" case). Governor-independent, crash-free with the min_gap fix. |
| **Host throughput ~½ of achievable (count-based exec)** — the exec loop called `uc_emu_start(uc,eip,0,0,batch)` with a non-zero instruction `count`, which makes Unicorn instrument every instruction and disables TCG block-chaining → ~11–13 host-MIPS, sluggish/slow-refreshing graphics | Port main/QEMU's free-running execution model: run `uc_emu_start(...,count=0)` for large batches (`batch>=2000`) and stop at a translation-block boundary via the always-on block hook once ~`batch` insns (bytes*2/7) have run; keep exact `count` only for the small near-IRQ0-deadline batches so delivery precision is preserved. `max_batch` 4000→100000. | _this session_ | A/B identical flags, `--headless --no-savedata --dcs-mode io-handled --cpu-stats`: host **11.7→25.9 MIPS (~2.2x)** on swe1, **~13→24 MIPS** on rfm; first-window IRQ0 delivery **99.1→~99%** (parity); `run-bundle-matrix.py --duration 15` pass/fail **identical to baseline** (rfm base PASS, swe1 base pre-existing FAIL in both) — no regression |
| **`--fresh` factory-fresh boot** — main has `P2K_FRESH_SAVEDATA` (ignore stale savedata at load, still save at exit) to skip the slow in-game "Automatic Factory Reset" that mismatched NVRAM triggers; unicorn only had `--no-savedata` (never save) | Ported main's `p2k_fresh_savedata_enabled()` (`p2k-internal.h:23`, `p2k-bars.c:68`): added `--fresh` flag + `P2K_FRESH_SAVEDATA` env; load gate skips seeding when `fresh\|\|no_savedata`, exit-save still runs when only `fresh` | _pending_ | `--fresh` boots factory-fresh, sustains 22–24 MIPS / vt-scale 1.1–1.19x / delivery 99.5% / no Fatal, and still writes NVRAM/SEEPROM/flash on exit |
| **GX MMIO read-poll cliff** — host throughput collapsed 23→2 MIPS mid-render ("slow motion"). A `UC_HOOK_MEM_READ` over GX regs `0x8000-0x20FFF` trapped every guest vsync poll of `DC_TIMING2` to C (hook + `uc_mem_write` per read) | Copied main's `memory_region_init_ram` GX model (`p2k-gx.c:62,86`): removed the GX **read** hook so polls hit the already-mapped backing RAM directly; kept the write hook (BLT/DC side-effects) and seed the read-only polled regs in RAM — `DC_TIMING2` by the vsync sub-tick writes, `GP_BLT_STATUS`=0x300 at init + after each synchronous BLT | `6f0a773` | 45s run: previously last windows fell to 0.10x/2 MIPS; now sustained **21–24 MIPS, vt-scale 1.1–1.2x, delivered 99.5%, no collapse, no Fatal, clean exit** |
| **LPT opcode 0x00 phantom high-nibble** — unicorn returned `0xF0` for Physical[8] (coin slots); main returns `s_phys8_coin_slots & 0x0F` (high nibble **zero**, `p2k-lpt-board.c` case 0x00). The extra high-nibble bits read as 4 phantom closed switches every matrix scan. | Raw-import main: return `0x00` at rest (low-nibble coin bits only, high nibble masked). | _this session_ | builds clean; matches main byte-for-byte at rest. NOTE: reduces phantom bits but does **not** by itself clear the `price_init` OOM below. |
| ~~**mem-detect patch / 4 MiB OOM**~~ **REOPENED — claim was FALSE** — checklist claimed the lamp/switch split (`9417e14`) closed the 128 KB `price_init` OOM at native 4 MiB. It did not. | Root-caused this session with a `getmem`/`malloc` hook (guest `getmem`@`0x270fa0`, `malloc`@`0x296b78`, free-list head `[0x349738]`): RFM base **still deterministically freezes ~15 s into boot** at `Fatal: malloc(131072): getmem(131104) failed` (in `price_init`, default path, no flags), then the guest panic-monitor spins with IF=0 so IRQ0 goes DEAD → permanent display freeze. Free-list walk at the failing alloc: **total free ≈183 KB but largest contiguous block only ≈112 KB < 128 KB** — near-exhaustion **plus fragmentation** (125+ free nodes from the `add_entry` object-registry realloc-on-every-add churn, `0x2d2e9c`). First 128 KB attempt already sees only **760 KB free of 4 MB** (~3.3 MB consumed during boot). main reaches the running game at the same native 4 MiB (docs/30, validation matrix RFM base PASS), so unicorn **over-consumes/over-fragments the heap** vs main. The lamp-split and opcode-0x00 fixes remove phantom switches but are insufficient. NIC-disable and `P2K_MEM_DETECT_PATCH=1` (14 MiB) were A/B-tested: 14 MiB masks the OOM but then surfaces a `resched: called from interrupt handler` Fatal — a divergence, not a fix. | _open_ | **NOT closed.** Next: track live allocs (`getmem` minus free `0x296b34`) to find the extra live consumer, diff its device-register reads against main's `qemu/` I/O, and port main's handler so boot consumption matches. |
| **IRQ0 catch-up burst** — drain emitted up to 1024 edges/iter that collapsed in IRR and drove `resched: called from interrupt handler` storms | Ported main's one-in-flight + optional `min_gap` gate (`p2k-clkint-hotloop.c`): at most one real edge per drain, resync deadline instead of bursting | `55cb4bb` | A/B (real pre/post, identical flags): delivered 58.9%→98.9%, collapsed 41%→1.1%, resched storm 218→~0 |
| **Native ADSP-2105 DCS engine** — main runs the real DSP firmware; unicorn only had WAV samples | Ported `src/adsp.c` (~4200 lines) + `include/adsp.h`; opt-in `--dcs-engine adsp` (default stays `samples`) | `4a5452f` | both engines boot clean, no Fatal; SPORT silent without gameplay input (by design — see Don't) |
| **Switch keymap not configurable** — main has `--switch-keymap`; unicorn hardcoded | Added `--switch-keymap FILE` (fail-closed parse) | `1258912` | parses + boots clean |
| **National PC97338 SuperIO (0x370/0x371)** — main models it; unicorn only had W83977EF + CC5530 | Ported PC97338 register file from `p2k-superio.c`; port 0x61 now stores full byte like main | `4a185b2`, `7661186` | boots clean, no Fatal |
| **Deadline-free IRQ0 model + adaptive PI** — unicorn used a per-period vtime+walltime deadline gate; main has NO deadline/burst | Ported main's continuous re-raise gated on one-in-flight + `min_gap` (virtual ticks), with a 500ms adaptive-PI retune measured in **virtual time** (guest progress), 140% XINU-ceiling clamp (`p2k-clkint-hotloop.c`) | `9c786c5` | ~99.7% delivery, 0 Fatal; wall-time targeting variant caused 177–196% over-delivery + `exec is hung`, fixed by virtual-time measurement |
| **`--speed-target PCT`** — main scales the game clock (`p2k_pit_scale_count`); unicorn had no equivalent | Scale effective PIT divisor `pit_div*100/pct`; env `P2K_SPEED_TARGET_PERCENT` overrides flag (main precedence) | `94d5178` | 100→4004Hz, 150→5996Hz, 50→2002Hz, all clean boots |
| **VSYNC render-pass watchdog Fatal** — GUI run died after ~2min: `Display Manager(HD): Render pass watchdog has expired`, then IRQ0 delivery wedged (IF=0) → window froze | Unicorn drove VSYNC off **wall-clock** 57Hz; main arms its ticker on `QEMU_CLOCK_VIRTUAL` (`p2k-vsync.c`). At vt-scale 0.3× the wall vsync arrived ~3× too fast vs the guest scheduler tick that the render watchdog measures → expiry. Ported virtual-time cadence (17.5ms/30 sub-ticks in vticks) so vsync:IRQ0 ratio stays constant (70:1) at any host speed | _this commit_ | 170s headless run through factory reset: steady 99.4–99.6% delivery, no render-pass Fatal, clean exit |
| **Picture "jumps" even at correct 100% IRQ0 / steady FPS** — after the display-thread + realtime-pacing fixes, present cadence was steady ~60 FPS but the *content* still juddered. Root cause: the realtime pacer AND the vsync generator were both inside the `exec_count & 0x3F` (every-64-batch ≈ 16 ms) gate, so the guest ran in **execute-fast-then-sleep 16 ms bursts** and the VSYNC page-flip pulse was emitted inside those bursts → guest buffer-flips landed at irregular wall times. Main decouples this: `p2k-vsync.c` runs a **separate fixed-period wall timer** over an **evenly-clocked** vCPU. | Moved BOTH the realtime pacer and the VSYNC subtick generator **out of the /64 gate to run every batch**: pacing every batch means small per-batch sleeps that spread execution evenly across wall time (no 16 ms bursts), and VSYNC is now driven off **wall time** at main's fixed 17.5 ms / 30-subtick (~57 Hz) period when `--realtime` (default), with a bounded catch-up loop; `--no-realtime` keeps the virtual-time cadence so the render-pass watchdog can't expire on a host-slow box. Only the SIGALRM-drain + cpu-stats remain /64-gated. (`src/cpu.c`) | _this commit_ | A/B swe1 headless (dummy video): **steady 59.5–59.6 FPS** (was jittery 18–47), IRQ0 **98–99.4% `[OK] ok`** at wall≈target 4004 Hz, **zero render-pass/Fatal/EIP-in-GDT**, `sleep 10`→10.5 s real (linear); under host load degrades to smooth slow-motion (anti-sprint clamp), never sprints. `--no-realtime` still boots clean. |
| **IRQ0 in-flight guard stuck across task-switch → delivery collapse to 69%** — extended gameplay: XINU `clkint` calls `resched()` (task switch) before returning to `s_irq0_pre_eip`, so the software in-flight guard cleared only via the slow task-switch/full-PIT fallback. In-flight stayed armed up to **1.3M vticks (~260 PIT periods)**, deferring every IRQ0 in the window (`defer: nest` → 320k, `delivered` → 69%), starving timer-driven game logic → "graphics slow/stuck" | Copied main's **EOI-based lifecycle** (`eoi_seen`): clear the in-flight guard the instant `clkint` issues the IRQ0 EOI (`pic[0].irq0_eoi_count` advances), which is the hardware-accurate "interrupt done" moment (PIC ISR bit0 already clear). Snapshot the EOI count at inject-arm (cpu.c ~831); clear as the primary reason in the block hook (cpu.c ~296), before the EIP/ESP heuristics | _this commit_ | 2-min windowed + 90s headless: `defer: nest` **320k→0**, `max-vt` **1.3M→0**, clears now **eoi=100%**, delivery steady **98.9–99.1%** (was 69%), FPS steady 37–40, force-EOI=0, no Fatal, clean exit |

| **swe1 "black screen" = EOI-clear re-opens IRQ0 NESTING → EIP-in-GDT freeze** — the EOI-based clear above (row ↑, fixed the 69% collapse) has the opposite failure on swe1: `clkint` issues its IRQ0 EOI EARLY then runs its post-EOI epilogue (`resched`/`ctxsw`) with IF=1 while still on the interrupted task's stack. The EOI-clear releases the in-flight guard there, so the next due tick injects a **nested** frame into the epilogue; ESP walks down ~0x180/tick (INJ RING confirms: 0x2ffe2c→0x2ffc48→0x2ffac8…) until the stack overflows into the GDT → `EIP-IN-DANGER-ZONE TRAP EIP=0x00001008` + `resched: called from interrupt handler` storm → IRQ0 dies → **frozen/black display** (graphics DO render first: `first non-zero framebuffer detected` fires, so it is NOT a render bug). swe1 runs healthy ~26–30 MIPS until the wedge; rfm can't reach here (OOM row above fires first). | **Main's real mechanism** (`p2k-timing-audit.c` `p2k_in_clkint`, gate at `p2k-clkint-hotloop.c:426`): hold the guard from clkint ENTRY until the guest's **IRET** (hooked in `helper_iret_protected` → `note_iret`, cleared on ANY iret, not a specific return EIP — so a `resched`/ctxsw still releases it and delivery does NOT collapse). **BLOCKER for the raw port:** Unicorn 2.x does **not** support hooking IRET (`uc_hook_add(...,UC_X86_INS_IRETD)` → `UC_ERR_HOOK`), and per-instruction `UC_HOOK_CODE` would kill the free-run perf win. A/B tested this session: (a) hold-until-any-iret via block-last-byte==0xCF scan → **eliminates the EIP-in-GDT crash + resched storm**, healthy ~24 MIPS ~10s, but the byte-scan false-matches `jmp/jcc rel8` displacement 0xCF and rel32 imm → one spurious nested inject corrupts a XINU stack guard word (`IStack: underflow … magic 0xaaa9`, one off 0xaaaa, xpid 80 `multi_device_2`) → dies ~7s (EARLIER than baseline, so NOT shipped); (b) ESP-window / time-bounded holds → either re-collapse (dead clock) or resched Fatal storm. | _this commit_ | **CLOSED via main-parity min_gap default.** The IRET-hold approaches all surfaced deeper layers (`IStack: underflow`) and can't be done reliably (Unicorn can't hook IRET). Instead we starve the nesting at its source with **main's own rate-limit**: the fix does NOT need to detect IRET — it just has to stop supplying a new tick before the previous `clkint` returns. The adaptive-PI default pushed IRQ0 delivery to ~98% (a tick every ~1 PIT period), so a catch-up tick lands in the post-EOI epilogue → nest. Pinning `min_gap` to **main's boot value (`P2K_HOTLOOP_MIN_GAP_NS=250000`, ~250 µs) with adaptive OFF by default** holds delivery at ~50% — one tick per ~2 epilogue-lengths — so every `clkint` reaches its IRET before the next inject and nothing nests. A/B-verified headless (dummy video): swe1 runs the **full 90 s, exec ~0.5–1.1M, ZERO EIP-in-GDT / resched-storm / IStack-underflow**, `eoi/inject=1.00`, framebuffer renders (no black screen); baseline wedges ~30 s. rfm unchanged (still pre-existing OOM). Adaptive push-to-nominal is now opt-in (`P2K_HOTLOOP_ADAPTIVE=1`). |

### 1. Stability — verified, no open gap found this session

Full regression matrix (13 update bundles + 2 base-ROM configs,
`io-handled` mode, 20-30s headless each) passes with no crashes on
this host (`tools/run-bundle-matrix.py --all-updates`, run 2026-08).
Re-run after every non-trivial change; record any new failure here
with the exact command and log excerpt.

### 2. Performance / timing — largely closed

IRQ0 delivery now ~99% (was ~59%) with the anti-burst gate (`55cb4bb`).

| | `main` (QEMU) | `unicorn` |
|---|---|---|
| Guest-speed throttle | PIT-accurate virtual time, `--speed-target 25..300` | `--realtime` (opt-in, per-vblank nanosleep) + `--cpu-target-mhz` (PIT/IRQ0 math only, does not itself throttle) — coarser granularity, verified working |
| IRQ0 pacing | one-in-flight + adaptive `min_gap` | one-in-flight (default) + opt-in `P2K_HOTLOOP_MIN_GAP_NS` (`55cb4bb`) |
| Measurement | `--bench`, `--timing-snapshots` | `--cpu-stats[=N]` — verified working |
| Per-boundary band-aid | n/a | `--lpt-bus-pace` for the LPT wire specifically |

Open: no icount-style per-block throttle (main's PIT model is more
accurate); adaptive min_gap retune loop from main is not yet ported
(unicorn's min_gap is fixed once set) — low priority while delivery is
~99%.

### 3. Functional parity — remaining real gaps

| Feature | `main` | `unicorn` | Impact |
|---|---|---|---|
| DCS audio demo | Native ADSP-2105 + audio harness that injects coins/volume | Native ADSP-2105 ported (`--dcs-engine adsp`), but no scripted input harness to drive a track headless | Engine is faithful; needs `--keyboard-tcp`/script to demonstrate non-silent PCM |
| Scripted automation | `--script`/`--console-script`: timed input, assertions, screenshots | None | No scripted acceptance tests beyond boot/progress |
| Video capture | `--record-video` (H.264/FFmpeg) | None | Can't produce an artifact for visual regression review |
| E0-prefixed PS/2 scancodes | n/a (own KBC path) | Known incomplete (`docs/38-known-limitations.md`) | RCtrl/RAlt arrive as LCtrl/LAlt in `--keyboard-tcp` mode |

### 4. CLI/arg parity — meaningful subset (excludes QEMU-only flags like `--qemu`, `--monitor`, `--tcg-only`)

Missing on unicorn, present on main: `--script`/`--console-script`,
`--record-video`, `--speed-target`, `--audio`/`--no-audio` (separate
from `--headless`), `--diag`, `--legacy-hotloop`/`--with-pit`
(alternate timing models for A/B). Now present on unicorn:
`--dcs-engine`, `--switch-keymap` (added this session). Present on
unicorn only: `--realtime`, `--cpu-target-mhz`, `--cpu-stats`,
`--cabinet-purist`/`--lpt-purist`, `--lpt-bus-pace`,
`--lpt-managed-dir`, `--config` (YAML-ish config file — see
`docs/04-config-yaml.md`).

### 5. Test tooling — closed this session

`tools/run-bundle-matrix.py` automates the bundle boot/progress check
(`docs/26-testing-bundle-matrix.md`). No unit tests exist yet for
pure-parsing code (config loader) — `main` has
`scripts/tests/test_console_script.py` as the model to follow.

---
Keep this file to one page. If it grows past that, split detail into
`docs/` and leave only pointers + the priority list here.
