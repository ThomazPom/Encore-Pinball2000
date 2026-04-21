# 15 — Watchdog Scanner

The Pinball 2000 platform has a software watchdog: a periodic timer
that must see a liveness signal, otherwise it assumes the game has
hung and reboots the board. On real hardware, the "liveness" is driven
by the DCS-2 sound board's response through the PLX bridge — a piece
of logic Encore cannot faithfully reproduce because we do not emulate
the DSP. Instead we *scribble* into the cell the watchdog probe reads,
keeping it happy indefinitely.

Implementation: `src/io.c:apply_sgc_patches()`. ROM-agnostic — the
scanner finds the cell at runtime with no hardcoded per-bundle
addresses.

## The four-step scan

```
1. Locate the string "pci_watchdog_bone(): the watchdog has expired"
   inside guest RAM.
2. From that string, find the `PUSH imm32` instruction (opcode 0x68)
   that loads the string address onto the stack.
3. From that PUSH, walk backward to the nearest `CALL rel32` (0xE8);
   resolve the call target — this is `pci_read_watchdog()`.
4. Inside `pci_read_watchdog()`, find the `CMP dword [addr32], 0xFFFF`
   idiom (opcodes `81 3D <addr> FF FF 00 00`). The `addr` operand is
   the cell we want.
```

The scan is triggered once, after XINU is booted, by
`apply_sgc_patches()`. The whole thing is inlined in one ~180-line
function. It is the most important piece of reverse-engineered logic
in the project because it makes Encore immune to per-bundle differences
in where `pci_read_watchdog` lives.

## Why string-anchored?

Hard-coding `pci_read_watchdog`'s address in the binary would bind
Encore to one bundle version. By anchoring on the error string, we
exploit the fact that every bundle has the same diagnostic text
(Williams never re-wrote the error messages across 7 years of updates)
even when the compiler re-ordered the code section. The string is
enough of a unique fingerprint — no other code fragment in the binary
references that specific text.

## Why the nearest preceding CALL?

The `Fatal()` function is reached by pushing format-string arguments
onto the stack and calling it. The pattern is:

```
... computation of the error condition ...
call pci_read_watchdog     ; E8 <rel32>
cmp  eax, 0                 ; 83 F8 00
je   normal_path
push "pci_watchdog_bone(): …"   ; 68 <str_addr>
call Fatal                      ; E8 <rel32>
```

Walking backward from the `PUSH str` to the nearest `CALL rel32`
therefore lands on `pci_read_watchdog()` every time, because no other
call sits between the health check and the error string push.

## Nested call follow

Once we have `pci_read_watchdog()`'s body, we search for the CMP
idiom. In some builds the CMP is directly inside the callee; in
others it is inside a nested `dcs_probe()` that the watchdog callee
invokes. The scanner handles both:

```c
uint32_t search_starts[2] = { callee_off, 0 };
int n_starts = 1;
/* Look for a nested CALL inside callee (up to 32 bytes in) */
for (uint32_t off = callee_off; off + 5 <= callee_off + 32; off++) {
    if (buf[off] == 0xE8) {
        int32_t rel2;
        memcpy(&rel2, buf + off + 1, 4);
        int64_t t2 = (int64_t)(scan_base + off + 5) + rel2;
        if (t2 >= scan_base && t2 < (int64_t)(scan_base + scan_size)) {
            search_starts[1] = (uint32_t)(t2 - scan_base);
            n_starts = 2;
            break;
        }
    }
}
for (int si = 0; si < n_starts && !health_addr; si++) {
    /* search for 81 3D <addr32> FF FF 00 00 */
}
```

Both the direct callee and its first nested call are scanned; the
first CMP match wins.

## The CMP pattern

Instruction form: `CMP dword [m32], imm32`. Bytes:

```
81 3D <addr32> FF FF 00 00
└─┘└┘└──────┘└─────────┘
 │  │    │      imm32 = 0xFFFF
 │  │  address (little-endian u32)
 │ modrm: disp32 + CMP
 opcode
```

The scanner additionally rejects `addr` values outside the plausible
BSS window `0x100000..0x1000000` to prevent spurious matches on
unrelated constants.

## What we do with the address

```c
g_emu.watchdog_flag_addr = health_addr;
uint32_t prime_val =
    (g_emu.dcs_mode_choice == ENCORE_DCS_IO_HANDLED) ? 0u : 0x0000FFFFu;
RAM_WR32(health_addr, prime_val);
```

The cell is primed once immediately (so the first probe already sees
the right value) and then written unconditionally by `cpu.c` on every
batch (`cpu.c:780`). See [14-dcs-probe-polarity.md](14-dcs-probe-polarity.md)
for the value-polarity rationale.

## Failure mode

If any step fails, `apply_sgc_patches()` logs
`watchdog scan: … — suppression inactive` and returns without
installing a scribble. The game will run normally until the watchdog
fires, at which point it prints the error string and jumps to
`Fatal()`. That Fatal is itself redirected to the idle stub by the
HLT-redirection policy (see [34-nonfatal-vs-fatal.md](34-nonfatal-vs-fatal.md)),
so the game does not actually reboot — it hangs politely instead.
The emulator stays alive; you can still exit cleanly with F1.

## Related patches in the same function

`apply_sgc_patches()` also hosts two other ROM-agnostic fixes:

1. **`mem_detect` patch (BT-130).** The function prologue
   `55 89 E5 B8 00 04 00 00 C9 C3` is scanned for; the `0x400` low
   byte is changed to `0x0E` so `mem_detect` returns 14 MB instead of
   4 MB, preventing prnull stack overflow.
2. **`nulluser` JMP patch (BT-74).** XINU's null task ends with a tight
   `JMP .` loop; we replace the loop body with `HLT; JMP $-3` so the
   guest halts instead of spin-locking the CPU, allowing IRQs to be
   delivered.

Both are small, pattern-anchored, bundle-independent.

## When the scanner runs

The trigger is the first time `g_emu.xinu_booted && g_emu.xinu_ready`
becomes true. At that point the code section has been fully relocated
and the error-string data section is populated; the scan can succeed.
Running any earlier gives inconsistent results because the string has
not been copied yet.

## Observability

Every stage emits an `[sgc]` log line so you can see the scan progress:

```
[sgc] applying minimal post-start fixes for watchdog/mem_detect/…
[sgc] watchdog scan: string at 0x0029a158
[sgc] watchdog scan: PUSH at 0x00227a90
[sgc] watchdog scan: CALL at 0x00227a8b → callee 0x001a41f0
[sgc] watchdog health reg: CMP [0x00344390],0xFFFF at 0x001a2ac0
[sgc] watchdog suppression active: [0x00344390] primed =0x0000FFFF
       (BT-107, dcs-mode=bar4-patch)
```

If any line is missing, the scan failed at that stage and the tag
itself tells you which stage.
