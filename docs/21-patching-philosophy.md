# 21 — Patching Philosophy

Encore deliberately applies **as few guest patches as possible**. This
document lays out the rules we follow.

## Core principle

> **A patch is never the first answer. A patch is the last answer.**

For every behaviour where the emulator differs from real hardware, we
ask in order:

1. Can we make the emulator's MMIO / IRQ handling more faithful so
   the game's own code works unchanged?
2. Can we use a different CLI/config mode so the guest exercises a
   path we have already emulated correctly?
3. Does the game's own `SYMBOL TABLE` give us a ROM-agnostic hook
   point (`sym_lookup`) we can install a host-side callback at?
4. Only then: is there a ROM-agnostic *byte pattern* we can scan for
   and replace?
5. And only as a last resort: hard-code per-bundle addresses.

The answer almost always lives at step 1 or 2. When it does land at
step 4 or 5, we document the why extensively so the next developer
can re-attempt step 1.

## The patch budget

Current patches, counted:

| Patch | Kind | Trigger | Why |
|---|---|---|---|
| IVT stubs at `0x20000` | emulator stub, not guest patch | boot | Unicorn has no default handlers |
| PRISM ROM FB write NOP | pattern scan | boot | ROM wants to write framebuffer at wrong address |
| `mem_detect` → 14 MB | pattern scan (BT-130) | sgc | MediaGX memory controller reply differs |
| `nulluser` JMP → HLT | pattern scan (BT-74) | sgc | spin-loop starves IRQ delivery |
| Watchdog cell scribble | scan + runtime write (BT-107) | per-batch | no DSP to feed the real watchdog |
| DCS-mode CMP/JNE → MOV | pattern scan | xinu_ready | no BAR4 DCS to answer the probe |
| Safety HLT at `0x801F7` | hardcoded | boot | catches Init2 fallthrough |
| Idle stub at `0xFF0000` | hardcoded | boot | Fatal-redirect target |

Eight patches total. Of those, six are pure pattern scans (ROM-agnostic)
and two are hardcoded at addresses that were never used by any bundle's
real code (safe to own).

Explicitly *not* patched:

* Fatal / NonFatal call sites — redirected at HLT time, no byte changes.
* CMOS, LocMgr, PIC base-mask — handled in the I/O emulation.
* BSS / Q-table pre-init — left for the game to do natively.
* Any update-version-specific initialisation sequence.

This is a dramatically smaller patch set than Encore's ancestor
projects. Earlier prototypes shipped dozens of per-bundle hardcoded
patches; the current version runs all seven dearchived bundles from a
single set of eight generic interventions.

## Why pattern scans?

A pattern scan is ROM-agnostic. If a new bundle appears tomorrow with
different addresses but the same compiler and the same source, the
scan still finds the signature and the patch still applies.

A good pattern has three properties:

1. **Uniqueness.** The byte sequence must not match anywhere else in
   the binary. We confirm this with an offline grep across all seven
   bundles before committing the pattern.
2. **Stability.** The exact bytes must be preserved across compiler
   version changes and source refactors. Opcode prologues (`55 89 E5`
   = `push ebp; mov ebp, esp`) are stable; operand bytes often are not.
3. **Small payload.** The smaller the patch, the less collateral. Most
   Encore patches are 5 bytes or fewer.

## Why `sym_lookup` before hardcoded?

When we have no unique pattern but know the function name, a lookup in
the bundle's own symbol table is the next-best option. The advantage is
portability: one patch site works across every bundle that ships that
symbol. The disadvantage is that stripped bundles — about half of
what's out there — don't ship the symbol, and we fall through to the
hardcoded address.

## Why not just run slower and let the watchdog breathe?

A common suggestion for the watchdog issue is "make the emulator fast
enough that the game's real watchdog-feeding code runs in time". This
does not work because:

* Real cabinets run a separate ADSP-2105 DSP that autonomously
  returns "alive" readings to the watchdog probe. The CPU does not
  feed the watchdog; the DSP does.
* Emulating the DSP adds more work to an already-busy CPU loop. We
  would have to emulate the DSP to the same fidelity as the i386,
  which is a massive undertaking.
* The scribble (step 4) costs one `mov` per batch. The alternative
  (emulate the DSP) costs tens of thousands.

## Why not patch the game's BIOS boot path instead?

We skip the real BIOS entirely (`cpu_setup_protected_mode` enters
protected mode directly). Patching something we don't execute would
be a no-op. Some earlier projects did patch the BIOS; Encore does
not need that because the PRISM option ROM's own code takes care of
everything post-BIOS.

## Removal versus addition

Over time, the patch set has shrunk. Every time a patch becomes
unnecessary (because we improved the emulation elsewhere), we delete
it rather than leaving dead code. The `git log --oneline` history of
the project is full of commits titled `cleanup: remove X` and `Drop
dead …`. The trajectory has been consistently downward:

```
Earlier prototypes       ≈ 25 patches
Encore v0.1              ≈ 14 patches
Encore today (this doc)  =  8 patches, 6 of them pattern-scanned
```

Keep the trajectory going. When you add a patch, write down in the
commit message what you'd need in order to delete it later.

## Principle in commit messages

Every patch-adding commit should answer these questions in its body:

1. What symptom does this patch eliminate?
2. Why is the emulator-side fix not feasible right now?
3. What would a future emulator-side fix look like?
4. What pattern/scan is used, and why is it unique?
5. How many of the 7 dearchived bundles were tested, and what did they
   report?

The last question is the single best protection against "this patch
fixes A and silently breaks B".
