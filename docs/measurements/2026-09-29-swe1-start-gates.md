# SWE1 2.10 native Start-gate trace — 2026-09-29

This experiment identifies which native eligibility check rejects a real
emulated Start contact. It is a debugger experiment, not a timing benchmark.

## Identity and method

| Item | Value |
|---|---|
| Branch base | commit `a329a3be234a18e7707d4b2134fd9992390e3cc4` |
| Game | SWE1 2.10, `pin2000_50069_0210_10312025_B_10000000` |
| Game ROM SHA-256 | `36bf84f10bc2410ab7ec36bcafc9a65d0f7963f82bfcfe8592dc1d32917dc62d` |
| Symbol ROM SHA-256 | `0e83fc2b470bd88c407ad82fc2201b6288bc061355391ebbffd921ea22299744` |
| Display/audio | normal graphical launcher defaults; not headless |
| Cabinet input | emulated PDB, ordinary held matrix switch 13 |
| Timing mode | normal Encore default; no speed target or acceleration option |
| Persistent state | existing user savedata |

`tools/trace-start-gates.sh` launched the ordinary window and attached QEMU's
GDB stub without `-S`. The address-specific command file logs the return value
after each of the seven calls in `Game::m_credit_button_pressed()` and adds
detail inside `MultiDevice::game_start_check()`. It writes no guest memory and
changes no register.

## First run: existing post-boot state

Start closing edges 1–50 reached the native handler. Menu, Slam Tilt, credit
and audio passed; `MultiDevice::game_start_check()` rejected while an APid in
the `first_multi_device..last_multi_device` range remained active. Attempt 46
reached the per-device virtual check and identified object `0x002e2c60`, which
the exact symbol ROM names `trough_eject` with the `my_trough_eject` vtable.

Attempt 51 produced the complete success chain:

```text
START #51: native closing edge
  route       game-start path
  1 menu      PASS
  2 slam      PASS
  3 credit    PASS
  4 audio     PASS
  5 devices   PASS
  6 JTS       PASS
  7 XINA      PASS
  RESULT      ACCEPTED -> Game::m_start_a_game()
```

Attempts 52–55 were then rejected by the credit check, consistent with the
successful game start having consumed the available credit.

## Second run: explicit post-Game-Over state

The run exposed COM1 over TCP. After the booted shell returned its prompt,
`game over` was submitted and acknowledged; Start was then exercised again
without rebooting. Serial diagnostics named the active path:

```text
Trough 1::m_game_start_check()
Trough 1::hook_serve() default handler
Trough 1::m_ball_serve_proc(43)
```

Attempts 11–76 again failed first at the device gate. Attempt 73 specifically
reported `trough_eject`; attempt 77 then passed all seven gates and entered
`Game::m_start_a_game()`. The XINA-specific
`pid_recent_game_over_kickout` guard was never the first observable blocker:
the earlier device/trough work remained active until that short guard had
already cleared.

The fatal-state process dump independently exposed the active native classes:

```text
APid 2   first_mult...   sleep
APid 3   multi_devi...   sleep
APid 43  trough_ser...   rtim
```

## Conclusion

> [!IMPORTANT]
> The capricious Start behavior is not a lost host key in these runs. Every
> tested closure reached `swd_start_button_proc()`. The game deliberately
> discarded it because `MultiDevice::game_start_check()` still saw trough
> work in progress. Since the native handler does not queue the request, a
> later edge is required after the trough becomes eligible.

The empirical sequence agrees with the static call-chain reconstruction. The
usual practical explanation is therefore: the guest is waiting on its
physical-ball model, while Encore currently does not reproduce complete ball
movement in response to every coil.

## Debugger disturbance

> [!CAUTION]
> GDB breakpoints stop the emulated CPU while host wall time continues. During
> the second experiment, roughly 80 rapidly repeated Start attempts multiplied
> by several breakpoints eventually produced
> `Fatal: interval_0_25ms: exec is hung`. This is a debugger-induced watchdog
> event and must not be classified as a natural emulator crash. Future runs
> should use spaced presses (about one per second) and stop after the needed
> transition is captured.

Related implementation and interpretation:
[LPT/driver-board emulation](../26-lpt-board.md#from-a-contact-to-a-game-action)
and [XINA debugging boundaries](../06-xina-os-deep-dive.md#reading-guest-state-safely).
