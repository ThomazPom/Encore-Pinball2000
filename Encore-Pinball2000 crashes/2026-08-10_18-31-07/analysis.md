# SWE1 fatal — `alp lhd` stack corruption

Observed on 10 August 2026 during a SWE1 2.10 run.

## Original fatal

```text
Trough 1::m_test_report_start(False)
Trough 1::m_test_report_start(False)
Trough 1::m_test_report_start(False)
Trough 1::m_test_report_start(False)
*** Fatal: 10 Aug 2107 18:31:07
*** Fatal: Last[XPid 23 APid -1 (alp neon)] Current[XPid 20 APid -1 (alp lhd)]
*** Fatal: Stack: bad magic (B), xpid 20 (alp lhd) limit 0x3e3ffc base 0x3e5ff8 esp 0x3e5ea0 magic 0x20
```

At the first fatal, process 20 (`alp lhd`) still had an ESP inside its assigned
8 KiB stack (`0x3e3ffc` through `0x3e5ff8`). The fatal was raised because the
stack guard contained `0x20` instead of the expected XINU magic `0xaaa9`.
This is evidence of a guest-memory overwrite, not ordinary stack exhaustion
and not a host QEMU crash.

## State after `continue`

After the fatal monitor was continued, XINU reported:

```text
*** Fatal: Stack: underflow (B), xpid 20 (alp lhd)
limit 0x3e3ffc base 0x3e5ff8 esp 0x3f3e78 magic 0x20
```

Live guest-memory inspection confirmed that PID 20's saved ESP had become
`0x3f3e78`, which belongs to PID 23's `alp neon` stack. This second failure is
damage following an attempted continuation of an already-corrupted scheduler
state. It should not be treated as an independent root cause. A guest reaching
this fatal must be rebooted rather than continued.

## Relevant observations

- The failing program is the recent community SWE1 2.10 update. Its symbols
  identify `Trough::m_test_report_start(Bool)`, `StackFatal`, `clkint`, and the
  named processes seen in the report.
- Encore's emulated driver board passes the initial device audit but does not
  yet move a ball from the trough to the shooter lane in response to coils.
  The repeated trough report may therefore be exercising a state that physical
  cabinet hardware would normally resolve.
- The clock interrupt handler keeps interrupts disabled until `iret`. The
  evidence consequently does not support a simple recursively nested HOTLOOP
  IRQ0 stack-overflow explanation.
- `lampmgr` was consuming 84% in the fatal process table. This is suspicious
  context, but it does not identify the writer that damaged `alp lhd`.

## Current conclusion

The victim is known, but the exact guest instruction that wrote over its guard
is not yet known. The leading possibilities are:

1. a SWE1 2.10 game-code bug exposed by an impossible or prolonged emulated
   trough state;
2. incomplete Encore trough/coil/ball-device emulation;
3. a timing-sensitive Encore issue, less directly supported by the current
   evidence.

## Controlled isolation plan

Repeat the same gameplay sequence with fresh, separate savedata:

1. SWE1 2.10 with normal HOTLOOP;
2. SWE1 2.10 with `--strict`;
3. official SWE1 1.50 with normal HOTLOOP.

Interpretation:

- only 2.10 fails: update/game-code interaction;
- both game versions fail around trough activity: Encore board/ball model;
- only HOTLOOP fails: timing path;
- strict and HOTLOOP both fail: not HOTLOOP-specific.

The decisive future diagnostic is a guest-physical write watch on process 20's
stack guard. It should stop at the instruction that replaces `0xaaa9`, revealing
the writer rather than only the damaged process.

## Later coin-input incident

A separate 26 August reproduction caused `interval_0_25ms: scheduler is hung`
in `coin_slot_work_` after coin-slot input was repeatedly injected during ROM
boot. It occurred in both normal HOTLOOP and `--strict`, but it did not
reproduce this report's `alp lhd` stack guard corruption. The two incidents
must remain separate until a common cause is demonstrated.
