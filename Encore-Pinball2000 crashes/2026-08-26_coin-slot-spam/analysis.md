# SWE1 fatal after repeated coin-slot input

Observed on 26 August 2026 with the SWE1 2.10 update.

## Trigger

Repeatedly pressing `C` during the ROM's boot phase can stop the guest in its
fatal monitor. The failure has not been observed after startup or during normal
play.

`C` and `F10` both drive coin slot 1. Each key-down restarts the emulated
coin-switch pulse at 60 LPT scans, so closely repeated presses can keep that
switch asserted while the ROM processes pricing and credits.

## Observed fatal

```text
*** Fatal: 26 Aug 2026 21:15:50
*** Fatal: Last[XPid 129 APid 0 (exec)] Current[XPid 67 APid 71 (coin_slot_work_)]
*** Fatal: interval_0_25ms: scheduler is hung
```

The process table identified XPid 67 as `coin_slot_work_`, consuming 100% and
being current when the fatal was reported. The non-fatal log also contained:

```text
*** NonFatal: Last[XPid 129 APid 0 (exec)] Current[XPid 67 APid 71 (coin_slot_work_)]
*** NonFatal: resched: called from interrupt handler
```

## Timing-mode result

The failure has been observed in both the normal HOTLOOP mode and `--strict`.
`--strict` disables HOTLOOP and uses the natural i8254/i8259 IRQ0 path.
Therefore this reproduction is **not HOTLOOP-specific**, and no HOTLOOP change
is justified by this evidence.

An earlier controlled `--strict` run survived 40 injected `C` presses. That was
a negative reproduction attempt, not proof that strict mode was unaffected.
The later observed strict-mode failure supersedes that provisional inference.

## Scope

Other tested cabinet keys did not reproduce the failure. This is consistent
with the implementation: flippers, action buttons, Start and service controls
change held switch states, while `C` enters the ROM's coin/pricing path through
a retriggerable 60-scan pulse. `Enter` also uses a 60-scan pulse but targets a
different service input and did not reproduce the failure in the controlled
test.

This is also a different signature from the 10 August `alp lhd` stack-guard
corruption. A common timing sensitivity remains possible, but the available
evidence does not establish a shared cause.

## Current conclusion

The reliable statement is narrow: rapidly retriggering coin slot 1 during boot
can expose a guest scheduler failure in SWE1 2.10, independently of HOTLOOP.
This is not currently considered a normal-use issue: waiting for the ROM to
finish booting before inserting credits avoids the known trigger. The root
cause—game code, the duration/retrigger semantics of the emulated coin switch,
or another board-emulation interaction—has not yet been isolated.
