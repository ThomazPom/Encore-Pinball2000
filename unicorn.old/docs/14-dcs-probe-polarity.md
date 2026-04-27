# 14 — The Inverted DCS Probe

This is the forensic story of one particular line of code in
`src/io.c` that carries the single biggest pile of reverse-
engineering work in the entire project. Read this if you want to
understand why the watchdog scribble is `0` in one mode and `0xFFFF`
in the other.

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## The artefact

Around line 777 of `src/cpu.c`:

```c
uint32_t scribble_val =
    (g_emu.dcs_mode_choice == ENCORE_DCS_IO_HANDLED) ? 0u
                                                     : 0x0000FFFFu;
RAM_WR32(g_emu.watchdog_flag_addr, scribble_val);
```

and a matching prime at `io.c:444`:

```c
uint32_t prime_val =
    (g_emu.dcs_mode_choice == ENCORE_DCS_IO_HANDLED) ? 0u : 0x0000FFFFu;
RAM_WR32(health_addr, prime_val);
```

Why does the "watchdog scribble" depend on `--dcs-mode`? Because the
same cell is the DCS probe.

## How the probe works

Disassembly of the DCS probe (SWE1 v1.5 at `0x001A2ABC`):

```
0x1A2ABC: cmp dword [probe_cell], 0xFFFF   ; 81 3D <cell> FF FF 00 00
0x1A2AC4: je  .absent                      ; 74 xx
0x1A2AC6: mov eax, 1                       ; device PRESENT
0x1A2ACB: ret
.absent:
          xor eax, eax                      ; device ABSENT
          ret
```

Read carefully: the probe says **"PRESENT"** when the cell is
**anything other than** `0xFFFF`. This is the opposite of the
intuitive reading. The polarity is *inverted*.

## How the watchdog callee reaches it

The outer watchdog liveness function (SWE1 v1.5 at `0x1A41F0`)
disassembles as:

```
push ebp; mov ebp, esp
call <dcs_probe>       ; = the 0x1A2ABC routine above
mov edx, eax
cmp edx, 1
jne .expired           ; probe said ABSENT → treat as watchdog expired
mov eax, [ds:0x2E9898] ; PLX BAR ptr (NULL in our emulator)
mov eax, [eax+0x4C]    ; PLX register read
test al, 0x4
jne .expired           ; bit 2 = watchdog expired
mov eax, edx
jmp .out
.expired:
xor eax, eax
.out:
ret
```

So the "watchdog alive" predicate is effectively: `dcs_probe() == 1`
AND `PLX register bit 2 == 0`. The PLX bit half we handle separately
(the PLX register read returns a constant from `bar.c`); the hard
part is getting `dcs_probe()` to return 1.

## Reconciling the two consumers

We need:

* the **DCS probe** to return 1 in `io-handled` mode so the game
  itself sees "DCS present" and runs its natural init path;
* the **watchdog callee** to also see 1 so it does not return "expired".

Both conditions are satisfied by the single cell condition
`cell != 0xFFFF`. So:

* In `io-handled` mode, scribble `0` → cell is not `0xFFFF` → both
  consumers read "alive / present".
* In `bar4-patch` mode, `cpu.c` replaces the probe's `cmp/jne` with
  `mov eax,1`, so the probe cannot fail regardless of cell value. We
  keep the cell at `0xFFFF` for historical reasons (early versions of
  Encore wrote `0xFFFF` here) and because the watchdog callee's
  structure is unchanged — the probe still gets called, and its
  patched body returns 1 — so this path still works.

## The discovery story

The first Encore prototype naively scribbled `0xFFFF` in all modes,
because the pattern scanner finds `CMP [cell], 0xFFFF` and it was
"obviously" a health-alive sentinel. This worked fine while we only
supported the `bar4-patch` mode. When `--dcs-mode io-handled` was
introduced to support the unmodified probe, the game inexplicably
skipped DCS init entirely on every bundle.

Running the game with a full UART log revealed that it *wanted* to do
DCS init but decided there was no board. The probe was returning 0.
That made zero sense until disassembling the probe and noticing the
`je .absent` condition — inverted polarity.

At that point the fix is two lines of code. The commentary inside
`cpu.c` and `io.c` is long because the conclusion is counter-intuitive
and the next person who reads it needs the full argument.

See the commit messages for reference:

```
io-handled: scribble cell=0 (not 0xFFFF) so natural DCS probe returns 1
io: document watchdog/DCS-probe coupling — scribble works in both dcs-modes
```

## Why not blacklist the cell from the scanner?

An earlier attempt tried to "blacklist" the probe cell from the
watchdog scanner in `io-handled` mode — i.e. do not scribble it, let
the game maintain its own cell value. This was then reverted:

```
io: blacklist DCS-probe cells from watchdog scan in --dcs-mode io-handled
Revert "io: blacklist DCS-probe cells from watchdog scan in …"
```

The revert was correct. Even in `io-handled` the watchdog callee
still needs the probe to answer "alive", and the game writes `0xFFFF`
into the cell during some transient operations (uninitialised, just
coincidence that the alive value IS the initial value). If Encore
does not keep the cell at a known-good "alive" value, the watchdog
will expire on those transients.

So the scribble is unconditional; only its *value* depends on the
mode.

## Tuning the probe for future work

If a future bundle exposes a probe at a different offset or with a
different cell value, the scanner's job is to find it. The `CMP
[addr], 0xFFFF` idiom (opcode bytes `81 3D`) is the fingerprint and
has been stable across the emulated bundles tested. Any new idiom would require
extending `apply_sgc_patches()` to recognise more shapes.

## Testing impact

Every one of the seven dearchived bundles was tested with both
`--dcs-mode bar4-patch` and `--dcs-mode io-handled` after this fix
landed. See the regression table in
[26-testing-bundle-matrix.md](26-testing-bundle-matrix.md). Net
result: `bar4-patch` has 100 % audio success; `io-handled` boots on
all seven but audio produces the expected output under emulation only on the bundles whose I/O
command pump is implemented.

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
