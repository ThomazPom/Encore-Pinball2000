# 33 — BT-130 `mem_detect` ROM compatibility layer

What this doc covers: the XINU memory-detection failure on hobbyist-
maintained ROM updates, the current RAM-scan byte rewrite in
`qemu/p2k-mem-detect.c`, why it is a *ROM-compat layer* (not an Encore
emulation defect), and the specific paths that can delete it.

> [!IMPORTANT]
> **This is not an Encore emulation bug.** Encore correctly reports
> 16 MiB of installed RAM to the guest. Williams' shipping v2.00
> `mov eax, 0x1000` in `mem_detect()`, so no host intervention is
> needed. The problem is that a *hobbyist re-build of the update
> ROM* (swe1 v2.10, dated 2025-10-31, and the older Williams v1.x
> updates and rfm v2.60) shipped a regressed `mov eax, 0x400`. This
> patch is Encore's willingness to tolerate the community-side
> regression for the sake of playability.

## Per-ROM applicability

| ROM version | `mov eax` literal | Interpretation | Scanner action |
|---|---|---|---|
| swe1 v2.10 (hobbyist update dated 2025-10-31) | `0x400` = 4 MiB | regressed | patched to `0xE00` = 14 MiB |
| swe1 v1.30 / v1.40 / v1.50 (Williams pre-2.0 updates) | `0x400` | pre-fix versions | patched |
| rfm v2.60 (community update) | `0x400` | regressed | patched |

> [!WARNING]
> **This is the most invasive active host-side intervention.** It scans guest RAM, pattern-matches compiler output, and rewrites a byte in the executing `.text` segment. The patch is narrowly scoped, signature-gated, and self-retires when the target ROM does not exhibit the bug, but by construction it lives on the borderline of what is acceptable in an emulator. Track it, disable it when unnecessary, and prefer TCG-hook alternatives when they become feasible.

## Status block, verbatim

> `STATUS: ROM COMPATIBILITY LAYER (not an Encore emulation defect).`
> (`qemu/p2k-mem-detect.c:1-4`)

Full block: see the file header for the per-ROM table and maintenance contract.

> **Status:** accepted permanent firmware compatibility. Genuine v2.00 no-ops;
> affected ROMs receive the exact one-byte correction. Keep
> `P2K_MEM_DETECT_PATCH=0` only as a diagnostic opt-out.

## The symptom (on v2.10 specifically)

> [!NOTE]
> XINU's `mem_detect()` in the hobbyist v2.10 update is **compiled as a constant return**: `mov eax,0x400 ; leave ; ret`. No CS5530 register probe exists in the binary. Modeling a memory controller alone cannot fix a function that never asks for the value.

XINU's `mem_detect()` in the hobbyist v2.10 update returns 4 MiB, while
the reference cabinet reports 14 MiB usable (out of 16 MiB installed).
The regressed 4 MiB report causes XINU to Fatal at ~15 s with:

    *** Fatal: 10 Jul 2026 xx:xx:xx
    *** Fatal: Last[XPid 79 APid -1 (alp shld)]
    *** Fatal:      Current[XPid 122 APid -1 (price_init)]
    *** Fatal: malloc(131072): getmem(131104) failed

The byte rewrite is:

| Pattern | Meaning |
|---|---|
| `55 89 E5` | `push ebp; mov ebp, esp` |
| `B8 00 04 00 00` | `mov eax,0x400` |
| `C9 C3` | `leave; ret` |
| patch byte `+5 := 0x0E` | return `0xE00` pages (14 MiB) instead |

## Why v2.00 doesn't need it

Williams shipped v2.00 with `mov eax, 0x1000` (16 MiB). That value
matches the real cabinet's RAM install (~16 MiB physical, ~14 MiB
exclusively, so its `mem_detect()` returns the correct value out of
the box and no host-side rewrite is needed. Encore boots v2.00 via
`--update 0200` (or `--update 2.0`) and the scanner is a no-op on
that path: it finds no `mov eax, 0x400` byte pattern and retires after
the bounded one-second scan window.

## How it works

> [!IMPORTANT]
> **The patch only fires on exact byte-pattern match.** It searches for `55 89 E5 B8 00 04 00 00 C9 C3` (the specific prologue) and rewrites byte `+5` from `0x04` to `0x0E`. If the signature doesn't match, the patch remains silent — no fallback, no approximation.

`p2k_install_mem_detect()` arms a virtual timer unless the disable env is set.
The timer scans `0x00100000..0x00600000` immediately and then every 10 ms for
at most one second. Starting immediately is required: the former 300 ms delay
allowed RFM v2.60 to call `mem_detect()` and initialize its heap from the stale
4 MiB result before the live function was rewritten. The late rewrite logged
success but could not resize an already initialized heap.

When the 10-byte pattern is found, the patch writes one byte to the live RAM
copy and logs the address. On success, or after the bounded scan window when
the signature is absent, the timer is freed.

## Why a register model does not replace it today

The source is explicit: this guest does not probe a MediaGX/CS5530 memory-size
register, so there is nothing for a better device model to answer
(`qemu/p2k-mem-detect.c:11-17`). No controller probe exists in this build.

## Why ROM-load patching failed

> [!WARNING]
> **Flash/bank patching does not reach the executing copy.** The boot loader relocates and copies `mem_detect()` into RAM. Bytes written to BAR3 flash or bank0 never propagate to the live RAM function — only the RAM scan can touch the code the CPU actually executes.

The boot loader relocates/copies the function into RAM, and the running copy
diverges from BAR3 flash and bank0. The source records a 2026-04 attempt where
flash/bank bytes changed but the live RAM copy still returned 4 MiB
(`qemu/p2k-mem-detect.c:18-29`). The polling-RAM path is the mechanism that
reaches the executing function.

## Historical alternatives (not current homework)

> [!TIP]
> Two alternatives were considered:
> 1. High-level: A future XINU build that actually probes CS5530 memory-size registers (then device modeling works).  
> 2. Practical: A TCG function-entry hook that intercepts `mem_detect()` and overrides EAX without writing `.text` — cleaner than RAM scanning.
> 
> Disable switch: `P2K_MEM_DETECT_PATCH=0`

| SQL todo | Context |
|---|---|
| `arch-debt-mem-detect` | Close/deprioritize: the guest uses a hard-coded constant, not a device register. |
| `mem-detect-tcg-hook` | Close/deprioritize: no cabinet-facing gain over the bounded rewrite. |

## Risk profile

This is the most direct active symptom patch: it scans RAM, matches compiler
output, writes guest `.text`, and depends on a relocated function keeping the
same prologue. Its containment is narrow: one pattern, one byte, one log line,
one self-retiring timer, and an env disable (`qemu/p2k-mem-detect.c:50-57`,
`qemu/p2k-mem-detect.c:114-119`).

## See also

- [docs/30-symptom-patches.md](30-symptom-patches.md)
- [docs/13-memory-map.md](13-memory-map.md)
- [docs/14-boot-recipe.md](14-boot-recipe.md)
- [docs/36-roadmap.md](36-roadmap.md)
