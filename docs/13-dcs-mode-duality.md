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

`--dcs-mode` selects which of two end-to-end sound paths the guest
takes. Three underlying knobs are driven from the flag; the
polarity of the prime/scribble is **staged** around the XINU-ready
milestone so both modes can safely coexist with early-boot code:

| | BT-107 cell prime (at SGC scan) | Scribble **pre-**`xinu_ready` | Scribble **post-**`xinu_ready` | 5-byte CPU patch at `xinu_ready` |
|---|---|---|---|---|
| `bar4-patch` | `0x0000FFFF` | `0x0000FFFF` | `0x0000FFFF` | **applied** (forces `dcs_mode=1`) |
| `io-handled` | `0x0000FFFF` | `0x0000FFFF` | `0x00000000` | **skipped** |

Why the prime and the pre-XINU scribble are the same across modes:
the CMP cell the SGC scanner latches onto lives at a
*bundle-dependent* BSS/.data address. On mature bundles (RFM v1.4+
and SWE1 v1.5+) the cell ends up at ~`0x003170xx` and is read only
by the late DCS probe, so priming it to `0` pre-XINU was harmless.
On **r1-era** RFM v1.2 and **pre-release** SWE1 v1.3 the cell
falls near `0x002c8xxx` — a region that early boot code *also*
consults as a sentinel. Writing `0` there sent those bundles into
a null-EIP jump (RFM v1.2) or `UC_ERR_INSN_INVALID` at
`EIP≈0x001f2de8` (SWE1 v1.3) long before the DCS probe ever ran.

The staged scribble resolves that: every bundle boots with the
sentinel-safe `0xFFFF` in place, and `io-handled` only flips to
`0x0000` once `xinu_ready` fires. The game's own DCS probe is not
called until after that flip, so it still sees `cell != 0xFFFF`
and returns 1 = "DCS present" — exactly what `io-handled` needs.

Net result: **both modes now boot on every bundle in the matrix**,
including the three formerly-broken cases (`rfm 1.2 io-handled`,
`swe1 1.3 io-handled`, `rfm/swe1 --update none io-handled`). On
the three "pattern absent" bundles (`swe1 1.3`, `swe1 --update
none`, `rfm --update none`), `io-handled` is in fact *strictly
better* than `bar4-patch` — bar4 has no prologue to rewrite and
cannot activate DCS, whereas io-handled's post-XINU scribble lets
the game's own probe activate DCS naturally.

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

## `--dcs-mode io-handled` (default)

Skip the 5-byte CPU patch entirely. Instead, let the game's own
DCS probe return 1 naturally by making the probe cell read
`!= 0xFFFF` at the moment the probe runs.

The scribble is staged:

* Until `xinu_ready` fires, the cell is scribbled with `0x0000FFFF`
  every tick — identical to what `bar4-patch` does pre-XINU. This
  keeps any early-boot code that reads the cell as a sentinel
  happy.
* Once `xinu_ready` fires, the scribble flips to `0x00000000`. The
  game's DCS probe (which runs from game-init code after XINU) now
  sees `cell != 0xFFFF` → returns 1 ("DCS present") → game stores
  `dcs_mode=1` → BAR4 command stream starts. `sound.c` handles the
  MMIO commands exactly as in bar4-patch mode.

No CPU code is rewritten in this mode — closer to the real
hardware path. Because the pre-XINU polarity is `0xFFFF` regardless
of `--dcs-mode`, every bundle in the matrix boots in io-handled
just as reliably as in bar4-patch (see
[docs/26](26-testing-bundle-matrix.md)).

**Why this is the default.** The matrix in
[docs/26](26-testing-bundle-matrix.md) shows that `io-handled`
boots and produces audio on **12/12** shipped bundles, while
`bar4-patch` only delivers audio on **9/12** (the three "pattern
absent" cases below boot silent). io-handled is therefore strictly
dominant: more compatible *and* closer to the real hardware path
(no CPU code rewrite). The flag is kept so regression / A-B work
against the legacy patch path is still trivial.

### Where io-handled beats bar4-patch

Three bundles have no 5-byte pattern for `bar4-patch` to rewrite:

* `swe1 --update 1.3` (pre-release, prologue shape differs)
* `swe1 --update none` (base chips only, no game overlay)
* `rfm --update none` (same)

On those three, `bar4-patch` boots to a silent attract — there is
nothing to patch so `dcs_mode` stays 0. Under `io-handled` the
same bundles boot **and** produce the full DCS command stream
(30 `[dcs] WR` events in a 12 s headless window, matching the
mature bundles).

## `--dcs-mode bar4-patch` (legacy / regression)

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
This mode is retained so an A/B against the legacy patch path
remains a one-flag flip; it is **not** the default because it
silently skips DCS init on bundles where the prologue is absent.

## Symptoms of getting the mode wrong

* **`bar4-patch` + silent attract:** the pattern scan didn't find a
  match (`[init] DCS-mode pattern absent`). Affects SWE1 v1.3 and
  both `--update none` runs. Workaround: use `--dcs-mode
  io-handled`, which doesn't need the pattern and will activate
  DCS naturally on those bundles.
* **`io-handled` + CPU error loop / no XINU (historical):** was
  caused by the pre-XINU `0x0000` scribble derailing an early
  sentinel check on r1-era bundles. Fixed by staging the scribble
  (always `0xFFFF` before `xinu_ready`, flip to `0x0000` after).
  If you see this on a new build, check that the staging branch in
  `cpu.c` is still present.
* **Both modes silent on a bundle that should work:** the pb2kslib
  container wasn't found. Check `[snd] pb2kslib detected by shape: …`
  at boot.

## Log lines that confirm each mode

```
[sgc] watchdog suppression active: [0x00344390] primed =0x0000FFFF (BT-107, dcs-mode=bar4-patch)
[irq] XINU ready: timer injection enabled EIP=0x... exec=...
[init] DCS-mode pattern hit @0x001931e4 slot=0x0034a714 — patched (force BAR4)
```

means `bar4-patch` succeeded end-to-end (note: not the default).

```
[sgc] watchdog suppression active: [0x00344390] primed =0x0000FFFF (BT-107, dcs-mode=io-handled, scribble flips post-xinu_ready)
[irq] XINU ready: ...
[init] DCS-mode patch SKIPPED (--dcs-mode io-handled): game uses unmodified PCI probe; UART handlers in io.c answer the I/O path.
```

means `io-handled` reached the skip-patch decision. The prime
line shows `=0x0000FFFF` in **both** modes now — the polarity is
what flips post-XINU in the per-tick scribble, not at prime time.

## Full pass/fail matrix

See [26-testing-bundle-matrix.md](26-testing-bundle-matrix.md) for
the 22-combination results and per-bundle notes.

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
