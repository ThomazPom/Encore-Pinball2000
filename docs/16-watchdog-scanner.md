# 16 — The base-ROM DCS probe-cell scanner

> [!IMPORTANT]
> This page originated with the Unicorn watchdog suppressor. Current QEMU has
> no general watchdog scanner, Fatal redirection, `nulluser` rewrite, or
> combined post-start patch function. The only surviving scanner described
> here is `qemu/p2k-probe-cell-shim.c`, and it is inert unless `--update none`
> sets `P2K_NO_AUTO_UPDATE=1`. Memory detection is a separate module documented
> in [33-mem-detect.md](33-mem-detect.md).

The Pinball 2000 platform has a software watchdog: a periodic timer
that must see a liveness signal, otherwise it assumes the game has
hung and reboots the board. On the base/no-update software path, Encore uses a
staged guest-cell value so the game recognizes the emulated DCS device. Native
ADSP execution exists; the remaining shim concerns the game-side probe, not
DSP decoding.

Implementation: `qemu/p2k-probe-cell-shim.c`. ROM-agnostic — the
scanner finds the cell at runtime with no hardcoded per-bundle
addresses.

> [!WARNING]
> The behaviour described here is based on QEMU emulator testing only. Real-cabinet validation is pending — see [docs/29-cabinet-testing-call.md](29-cabinet-testing-call.md).

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

The scan is triggered once, after XINU is booted, by the probe-cell shim installer. The whole thing is inlined in one function. It is the most important piece of reverse-engineered logic
in the project because it makes Encore immune to per-bundle differences
in where `pci_read_watchdog` lives.

> [!IMPORTANT]
> The string-anchored scan is ROM-agnostic by design. Williams never rewrote the error messages across 7 years of updates, even when the compiler reordered code sections. The diagnostic string is a unique fingerprint that survives version changes.

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

> [!TIP]
> The backward CALL scan exploits Williams' consistent Fatal() calling convention: push error-string address, then call Fatal(). This pattern is stable across all P2K game binaries.

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

> [!NOTE]
> The scanner filters addresses outside the plausible BSS range (0x100000–0x1000000) to avoid false matches on unrelated constants that happen to share the byte pattern.

## What we do with the address

The probe-cell shim primes the cell once immediately (so the first probe already sees
the right value) and then writes unconditionally on every
batch. See [34-probe-cell-shim.md](34-probe-cell-shim.md)
for the value-polarity rationale.

## Failure mode

If any step fails, the probe-cell shim logs the failed stage and returns
without writing guest memory. QEMU does not redirect the resulting Fatal or
install an idle stub; the guest's own failure behavior is preserved.
The emulator stays alive; you can still exit cleanly with F1.

> [!CAUTION]
> If the watchdog scan fails, the game will run until the watchdog timer expires, then hang at a Fatal() redirect. This is not a hard crash — the emulator remains responsive and can be exited cleanly — but the game will not progress beyond the timeout point.

## Related patches

There are none in this function. `p2k-mem-detect.c` independently handles a
ROM-specific hard-coded RAM-size value. The historical `nulluser` patch is
deleted.

## When the scanner runs

The trigger is the first time `xinu_booted && xinu_ready`
becomes true. At that point the code section has been fully relocated
and the error-string data section is populated; the scan can succeed.
Running any earlier gives inconsistent results because the string has
not been copied yet.

## Observability

Every stage emits a log line so you can see the scan progress:

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

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
