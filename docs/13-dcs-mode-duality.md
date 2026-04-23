# 13 — DCS Mode Duality (`--dcs-mode`)

One of the more interesting knobs in Encore is how the guest reaches
its sound subsystem. There are two distinct paths, corresponding to
two distinct decisions the game's own init code tries to make. The
flag `--dcs-mode` selects which of those two paths we support.

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## What actually changes between the two modes

The flag name suggests "CPU patch vs natural probe", and that's
part of it — but it is **not** the variable that determines whether
boot itself succeeds. In practice, `--dcs-mode` flips *two*
independent things at once:

| | BT-107 cell prime (in `io.c:apply_sgc_patches`) | Per-tick scribble (`cpu.c`) | 5-byte CPU patch at `xinu_ready` |
|---|---|---|---|
| `bar4-patch` | `0x0000FFFF` | `0x0000FFFF` | **applied** (forces `dcs_mode=1`) |
| `io-handled` | `0x00000000` | `0x00000000` | **skipped** |

The CPU patch (third column) is what the flag nominally controls,
and it only runs **after** the game reaches the XINU-ready
milestone. The prime value (first column) is written at SGC-scan
time, which is *before* XINU. On pre-XINU-era bundles — and on
`--update none` — the prime value **is what gets read by early
boot code that isn't the DCS probe**, and its polarity decides
whether the game even reaches XINU.

So the real mental model is:

* **Prime / scribble polarity** decides whether early boot survives,
  and — on mature bundles — also decides whether the unmodified DCS
  probe returns 0 or 1.
* **CPU patch** is an independent belt-and-braces that forces the
  DCS BAR4 path *after* XINU, regardless of what the probe did.

The two effects happen to be tied to the same flag, which is why
`io-handled` is not simply "bar4-patch minus a 5-byte rewrite" — it
*also* flips the prime/scribble, and that is the part that can
break boot.

## The game's internal decision (for reference)

During early init (post-XINU) the game probes for a DCS-2 board.
The probe function disassembles as:

```
test_dcs:
    call  dcs_probe           ; returns 1 if DCS PRESENT (see doc 14)
    cmp   eax, 1
    jne   .io_uart            ; 75 21 (+0x21 bytes)
    mov   [dcs_mode_slot], eax   ; A3 xx xx xx xx
    jmp   .out
.io_uart:
    mov   [dcs_mode_slot], 0
.out:
    ret
```

On real hardware with a DCS-2 board, the probe returns 1, `dcs_mode`
is stored as 1, and downstream code takes the "BAR4 MMIO" path.
Without a DCS-2 board, the probe returns 0 and the game falls back
to a legacy "I/O port UART" path using ports `0x138..0x13F`.

Encore can serve either path — but **not transparently the same
way**. Hence the flag.

## `--dcs-mode bar4-patch` (default)

At the `xinu_ready` transition, `cpu.c` runs a pattern scan for the
5-byte prologue:

```
83 F8 01        cmp eax, 1
75 21           jne +0x21
A3 ?? ?? ?? ??  mov [dcs_mode_slot], eax
```

The 5 bytes (CMP + JNE, not the MOV) are replaced with:

```
B8 01 00 00 00  mov eax, 1
```

The MOV now stores 1 unconditionally → game takes the BAR4 path →
`sound.c` handles the ensuing command stream.

Meanwhile, `io.c`'s SGC scan primed the BT-107 watchdog cell with
`0x0000FFFF`, and `cpu.c` continues to scribble that same value
every tick. 0xFFFF is the value the game's own pre-XINU sanity
checks expect to see — early boot survives, XINU fires, and the CPU
patch then runs.

Pattern-scan source: `src/cpu.c` (see "DCS-mode override" block).

## `--dcs-mode io-handled`

Skip the 5-byte patch. Prime and scribble the BT-107 cell with
`0x00000000` instead. The probe cell is shared with the watchdog
liveness cell — with `0x0000` in it, the natural DCS probe flips to
"DCS present" (see [14-dcs-probe-polarity.md](14-dcs-probe-polarity.md))
and the game initialises DCS naturally, then routes commands through
the I/O port path (`io.c:dcs2_port_read/write`).

This is the more faithful emulation on paper — no CPU code is
rewritten. But the `0x0000` prime is itself a scribble, and on some
bundles an early-boot routine reads the cell *before* the game has
had a chance to overwrite it. On those bundles, `0x0000` derails
boot. That is the real reason `io-handled` is not the default.

### What breaks under `io-handled`, and why

The three observed failure points all share the same signature:
the prime/scribble value is read by pre-XINU boot code that expects
`0xFFFF` (or at least "not zero"), and a branch misfires:

| Case                               | Behaviour                                                                                          | Root cause                                                                                                        |
|------------------------------------|----------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------------------------------------------|
| RFM v1.2 + `io-handled`            | XINU never fires. Heartbeat shows `EIP=0x00000000`, `frames=0`.                                    | Prime=0 derails a pre-XINU check in the r1 firmware; boot stalls before xinu_ready. The CPU patch is never even considered. |
| SWE1 v1.3 + `io-handled`           | `UC_ERR_INSN_INVALID` starting at `EIP≈0x001f2de8` around `exec≈10 000`.                           | Same mechanism: prime=0 causes an early-boot routine to jump into unmapped memory. Crashes in a tight error loop. |
| `rfm --update none` + `io-handled` | exec_count ≈ 1 000, immediate death with unmapped fetches at ASCII-encoded addresses.              | Same. With no bundle to overlay, the guest is already fragile; prime=0 tips it over.                              |

Note: **SWE1 v1.3 `bar4-patch` also has no usable DCS** because the
5-byte pattern is absent in the v1.3 build (`[init] DCS-mode pattern
absent — no patch applied`). With bar4-patch it still **reaches
attract and renders silently** because prime=0xFFFF lets it boot.
With io-handled it cannot even reach attract.

### Hypothesis worth testing (not yet implemented)

The prime value and the CPU patch are independent in principle.
A future `io-handled` that primes with `0x0000FFFF` until xinu_ready
and *then* switches to `0x0000` would in theory rescue the three
cases above — early-boot checks see 0xFFFF and survive, and by the
time the DCS probe runs the scribble has already flipped to 0.
Has not been tried. If it works, io-handled could become the
default.

## Symptoms of getting the mode wrong

* **`bar4-patch` + silent attract:** the pattern scan didn't find a
  match. Look for `[init] DCS-mode pattern absent` in the log.
  Only SWE1 v1.3 hits this in the current matrix. Workaround: none
  — DCS stays silent; video still works.
* **`io-handled` + CPU error loop / no XINU:** prime=0 broke a
  pre-XINU check. Look for `UC_ERR_INSN_INVALID` or `EIP=0x00000000`
  in the heartbeat. Workaround: switch to `--dcs-mode bar4-patch`.
* **Both modes silent on a bundle that should work:** the pb2kslib
  container wasn't found. Check `[snd] pb2kslib detected by shape: …`
  at boot.

## Log lines that confirm each mode

```
[sgc] watchdog suppression active: [0x00344390] primed =0x0000FFFF (BT-107, dcs-mode=bar4-patch)
[irq] XINU ready: timer injection enabled EIP=0x... exec=...
[init] DCS-mode pattern hit @0x001931e4 slot=0x0034a714 — patched (force BAR4)
```

means `bar4-patch` succeeded end-to-end.

```
[sgc] watchdog suppression active: [0x00344390] primed =0x00000000 (BT-107, dcs-mode=io-handled)
[irq] XINU ready: ...
[init] DCS-mode patch SKIPPED (--dcs-mode io-handled): game uses unmodified PCI probe; UART handlers in io.c answer the I/O path.
```

means `io-handled` cleared the pre-XINU hurdle and reached the
skip-patch decision. If you see the `[sgc]` line but no `XINU ready`
or skip line before `UC_ERR_INSN_INVALID`, you are hitting the
pre-XINU prime=0 derail described above.

## Full pass/fail matrix

See [26-testing-bundle-matrix.md](26-testing-bundle-matrix.md) for
the 22-combination results and per-bundle notes.

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
