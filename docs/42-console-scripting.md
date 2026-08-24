# 42 — Console scripting

`scripts/run-qemu.sh --script FILE` boots Encore normally, waits for the XINU
prompt and executes the file. Ordinary non-comment lines are XINU commands;
lines beginning with `@` are host-side automation directives.

```sh
scripts/run-qemu.sh --script scripts/demos/start-game.p2k
```

The launcher validates the complete file before starting QEMU. Blank lines and
lines whose first non-space character is `#` are ignored. The emulator remains
open when a successful script ends; use `@key f1` when a script should also shut
it down.

## Directives

| Syntax | Action |
|---|---|
| `@wait SECONDS` | Wait for host wall-clock time. |
| `@key KEY [HOLD_SECONDS]` | Send a QEMU key, optionally held for a duration. |
| `@switch NN [HOLD_SECONDS]` | Close matrix switch `11..88`, then reopen it. The default pulse is 0.1 seconds. |
| `@repeat COUNT` … `@end` | Repeat a block. Blocks may be nested. |
| `@assert TEXT` | Require the preceding XINU response to contain text. |
| `@assert-not TEXT` | Require the preceding response not to contain text. |
| `@assert-regex REGEX` | Require a regular-expression match in the preceding response. |
| `@assert-not-regex REGEX` | Require no regular-expression match. |
| `@wait-for SECONDS COMMAND => TEXT` | Run a XINU command repeatedly until its response contains text or time expires. |
| `@wait-for-regex SECONDS COMMAND => REGEX` | Poll until the response matches a regular expression. |
| `@screenshot [LABEL]` | Capture the current game image. A label renames the output. |
| `@record-audio SECONDS [LABEL]` | Capture the selected DCS mix into a WAV file for that duration. |
| `@echo TEXT` | Print a progress message. |

Screenshots and WAV files use `--screenshot-dir`, which defaults to `/tmp`.
Audio capture requires an available audio backend; the launcher reports an
error before boot if none can be selected.

> [!TIP]
> `@switch 13 3` holds Start for three seconds. It performs the same `13`, then
> Ctrl-hold gesture available in the game window and updates the same emulated
> switch matrix.

## State-based example

```text
# Wait until the game exposes its state command.
@wait-for 30 game info => m_game_over True
# The shell precedes completion of the trough/device startup audit.
@wait 5

@repeat 8
  @key c
  @wait 1
@end

@switch 13 0.38
@wait-for 30 game info => m_players 1
@assert-regex m_players\s+1
@screenshot game-started
@record-audio 2 game-started
```

`@assert*` examines the latest XINU command response. `@wait-for*` both updates
that response and supplies its own timeout, so it is preferable to a long fixed
delay when the guest exposes a useful state command.

To check syntax without booting:

```sh
python3 scripts/internal/run-console-script.py my-session.p2k --check
```

> [!NOTE]
> Matrix switches are inputs, not forced game state. A valid Start pulse can
> still be rejected because of pricing, door, ball-trough or game conditions.
> SWE1 may keep `m_game_over True` after accepting Start while it waits for the
> shooter-lane/serve transition. `m_players 1` is the reliable acceptance test.

Details: [desktop controls](41-cli-keyboard-guide.md),
[LPT board](26-lpt-board.md) and [CLI reference](03-cli-reference.md).

---

← [Documentation index](README.md) · [Project README](../README.md)
