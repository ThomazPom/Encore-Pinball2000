# SWE1 fatal after a second `net start`

Observed on 30 August 2026 while testing live application of XINA network
adjustments.

## Fatal signature

```text
*** Fatal: 30 Aug 2188 12:23:29
*** Fatal: Last[XPid 47 APid -1 (autotick)] Current[XPid 121 APid -1 (lampmgr)]
*** Fatal: IStack: overflow (A) xpid 121 (lampmgr)
limit 0x3e9ffc base 0x3ebff8 esp 0x3ea00c magic 0xaaa9
```

`lampmgr` consumed 8,464 bytes in an 8,192-byte interrupt stack.  The guard
still contained the expected `0xaaa9`, so this is a real downward stack
overflow rather than the unrelated stack-guard overwrite previously observed
in `alp lhd`.

## Duplicate network stack

The process table contained two complete sets of network processes.  The first
set occupied XPids 18–26 and the second XPids 107–115:

```text
httpd telnetd echod echod tcpout tcpinp tcptimer ip slowtimer
httpd telnetd echod echod tcpout tcpinp tcptimer ip slowtimer
```

This confirms that XINA's `net start` command calls `netstart()` again; it does
not stop or reconfigure the existing stack.  Reverse analysis of `x_net`
confirms that its accepted operations are `start`, `monitor`, and interface
display.  There is no `net stop` operation.

The duplicate stack increases the process count and creates another set of
timers, queues, semaphores and daemons.  It is therefore a major confounding
factor in this experiment.  The evidence does not yet prove which network
process, if any, caused `lampmgr` to exhaust its stack, but automatically
issuing `net start` after adjustment changes is unsafe.

## Comparison with archived Encore crashes

This is not the 10 August `alp lhd` failure: that incident reported
`Stack: bad magic (B)` with an in-range ESP and a damaged guard.

This is also not the 26 August coin-input failure: that incident reported
`interval_0_25ms: scheduler is hung` in `coin_slot_work_`.

The current failure is a third signature: a genuine `lampmgr` interrupt-stack
overflow observed in an intentionally abnormal double-network-stack state.

## Consequence for live network reconfiguration

Event-driven detection of `IPAddr`, `IPMask`, and `GW IPA` changes remains
technically feasible.  Applying them cannot use a bare `net start` while the
current stack is alive.  A safe implementation first needs one of:

1. a verified guest-side teardown for the active network processes and state;
2. a narrow in-place reconfiguration of interface address, mask and routes;
3. a deliberate guest reboot after saving the adjustments.
