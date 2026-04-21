# 13 — DCS Mode Duality (`--dcs-mode`)

One of the more interesting knobs in Encore is how the guest reaches
its sound subsystem. There are two distinct paths, corresponding to
two distinct decisions the game's own init code tries to make. The
flag `--dcs-mode` selects which of those two paths we support.

## The game's internal decision

During early init the game probes for a DCS-2 board. The probe
function disassembles as:

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

On real hardware with a DCS-2 board, the probe returns 1, the CMP
succeeds, `dcs_mode` is stored as 1, and downstream code takes the
"BAR4 MMIO" path to send commands via the PCI window.

Without a DCS-2 board, the probe returns 0, `dcs_mode` stays 0, and
the game falls back to a legacy "I/O port UART" path using ports
`0x138..0x13F`.

Encore can serve either path — but **not transparently the same
way**. Hence the flag.

## `--dcs-mode bar4-patch` (default)

At the `xinu_ready` transition, `cpu.c` runs a pattern scan for the
exact 5-byte prologue:

```
83 F8 01        cmp eax, 1
75 21           jne +0x21
A3 ?? ?? ?? ??  mov [dcs_mode_slot], eax
```

The 5 bytes (CMP + JNE, not the MOV) are replaced with:

```
B8 01 00 00 00  mov eax, 1
```

Now the MOV stores 1 unconditionally. The game takes the BAR4 path.
`sound.c` handles the ensuing command stream.

Why is this the default? Because it **works on every dearchived
bundle**. The 5-byte pattern is stable across SWE1 and RFM versions
1.2 through 2.6. The bundle-specific difference — which address the
MOV stores to — is irrelevant because we leave the MOV untouched.

The key source line in `cpu.c`:

```c
uint8_t patch[5] = { 0xB8, 0x01, 0x00, 0x00, 0x00 };
uc_mem_write(uc, a, patch, 5);
memcpy(r + a, patch, 5);    /* keep RAM mirror in sync */
```

## `--dcs-mode io-handled`

Skip the patch. Let the game run its unmodified probe. The probe's
behaviour now depends on what the probe cell contains — see
[14-dcs-probe-polarity.md](14-dcs-probe-polarity.md) for the exact
polarity math.

Short version: the probe cell is shared with the watchdog liveness
cell we need to scribble every tick. By scribbling `0` (not `0xFFFF`),
we flip the probe to "DCS present" *and* keep the watchdog alive. The
game then runs its own code path to write `dcs_mode = 1` and proceeds
naturally.

Downstream, commands flow through the I/O port path
(`io.c:dcs2_port_read/write`). Encore's I/O handlers answer the
reset/handshake fully; the remaining sample-playback stream is
delegated the same way: `sound_process_cmd()` downstream.

### Why is io-handled not the default?

Two reasons:

1. On bundles whose natural probe returns 0 (even after scribble),
   the game skips DCS init entirely and stays silent. We have only
   verified clean probe-returns-1 behaviour on a subset of bundles.
2. The I/O port handshake pump in `io.c` currently answers the
   reset + handshake parts but does not fully emulate every mixer /
   multi-word command in the same way that the BAR4 path does. Some
   samples play; some don't.

When the I/O path becomes feature-complete, `io-handled` will become
the default because it is the more faithful emulation (no CPU
code patching).

## Interaction with the watchdog scanner

The scanner (`src/io.c:apply_sgc_patches`) finds a
`CMP [cell], 0xFFFF` idiom and decides "cell is the watchdog health
register". It then primes the cell and schedules a per-tick write.

The cell it finds is **the same cell the DCS probe reads**. That
overlap is not accidental — it is exactly what lets Encore serve both
modes from a single scribble policy:

| `--dcs-mode` | Scribble value | CMP patch |
|---|---:|---|
| bar4-patch | `0x0000FFFF` | yes, 5-byte `mov eax,1` |
| io-handled | `0x00000000` | no (probe runs unmodified) |

In bar4-patch, the CMP is gone, so the cell value is irrelevant — we
keep `0xFFFF` for backward-compat logging; in io-handled, the cell
value matters and must be anything except `0xFFFF`.

## Symptoms of getting the mode wrong

* **bar4-patch + dead audio:** the pattern scan didn't find a match.
  Look for `[init] DCS-mode pattern absent` in the log. Likely cause
  is the bundle has an unusual build that doesn't use the standard
  probe. Workaround: none currently; the DCS will stay silent.
* **io-handled + dead audio:** the natural probe returned 0. Look for
  `[hb] DM: … dcs_mode=0` in the heartbeat. Workaround: switch back
  to `bar4-patch`.
* **Both modes silent:** the pb2kslib container wasn't found or the
  loaded bundle has no matching samples. Check `[snd] pb2kslib
  detected by shape: …` at boot.

## Log lines that confirm each mode

```
[init] DCS-mode pattern hit @0x001931e4 slot=0x0034a714 — patched (force BAR4)
```

means `bar4-patch` succeeded.

```
[init] DCS-mode patch SKIPPED (--dcs-mode io-handled): game uses unmodified PCI probe; UART handlers in io.c answer the I/O path.
```

means `io-handled` is live.

```
[sgc] watchdog suppression active: [0x00344390] primed =0x0000FFFF (BT-107, dcs-mode=bar4-patch)
```

confirms the scribble policy and the resolved health-cell address.
