# Console scripting

`--script` drives a live XINA console and selected cabinet inputs while the
normal graphical emulator remains open. It is intended for repeatable demos,
state checks, screenshots and bounded audio samples—not for replacing the
normal interactive launch or the benchmark harness.

```bash
scripts/run-qemu.sh --game swe1 --update latest \
  --script scripts/demos/start-game.p2k
```

`--console-script` is a compatibility alias. Syntax is validated before QEMU
starts; after the last action the emulator remains running until the user
closes it. Use `@key f1` as the final action when the script should request the
emulator's normal shutdown key instead.

> [!IMPORTANT]
> Script mode owns a private localhost UART and Unix QEMU monitor. Do not
> combine it with `--serial`, `--uart-tcp`, `--monitor` or `--headless`.

## File format

A script is UTF-8 text. Empty lines and lines whose first non-space character
is `#` are ignored. Any other line not starting with `@` is sent as one XINA
console command followed by carriage return.

```text
# Wait until game state is visible, then inspect it.
@wait-for 45 game info => m_game_over True
game info
@assert m_game_over
```

Assertions operate on the response of the most recent XINA command or polling
action. They do not search the complete serial transcript.

## Directives

| Directive | Meaning |
|---|---|
| `@wait SECONDS` | sleep for a non-negative duration |
| `@key KEY [HOLD_SECONDS]` | send a QEMU monitor key; optional hold must be positive |
| `@switch CR [HOLD_SECONDS]` | pulse matrix switch column/row `11` through `88`; default hold 0.1 s |
| `@assert TEXT` | require literal text in the last response |
| `@assert-not TEXT` | require literal text to be absent |
| `@assert-regex REGEX` | require a multiline Python regular-expression match |
| `@assert-not-regex REGEX` | require no such match |
| `@wait-for SECONDS COMMAND => TEXT` | poll a command until literal text appears |
| `@wait-for-regex SECONDS COMMAND => REGEX` | poll until a multiline regex matches |
| `@screenshot [LABEL]` | trigger the emulator screenshot key and retain the new image |
| `@record-audio SECONDS [LABEL]` | retain just that interval as a WAV file |
| `@echo TEXT` | print a labelled progress message |
| `@repeat COUNT` … `@end` | repeat a block; nesting is allowed |

Labels are simple filename stems containing letters, digits, underscore, dot
or dash—never a path. Repeat counts are 1–10,000 and the fully expanded script
may contain at most 100,000 actions.

### Polling instead of guessed sleeps

Prefer a condition when the guest exposes one:

```text
@wait-for 60 game info => m_game_over True
@wait-for-regex 10 ps => currently\s+[0-9]+
```

Polling uses a 0.5-second interval and preserves its final response for a
following assertion. Each console command has a 120-second response timeout;
the directive's own timeout bounds how long the condition may remain false.

Use `@wait` only for a real dwell time, a deliberately paced input or a state
that cannot be queried.

### Inputs

`@key` accepts QEMU monitor key names. Its optional hold is converted to
milliseconds; without it the monitor's normal key press is used.

`@switch 13 0.08` reproduces Encore's numeric desktop switch chord: column
digit, row digit, then Ctrl for the requested hold. It includes fixed settling
delays, so it is not a precision timing generator. Use the physical/cabinet
validation path for electrical or exact pulse claims.

Matrix switches remain inputs, not forced game state. Native SWE1 discards a
Start edge when pricing, audio readiness, Slam Tilt, active multi-device work,
the ball audit or its game-specific recent-kickout guard rejects it; it does
not retry that edge when the condition later clears. The coin door is not a
direct Start gate. In SWE1, `m_game_over True` may persist after Start is
accepted while the shooter-lane/serve transition is pending; a state such as
`m_players 1` is stronger evidence of acceptance. The detailed native chain is
documented in [LPT/driver-board emulation](26-lpt-board.md#from-a-contact-to-a-game-action).

### Screenshots and audio

Screenshots are written to `P2K_SCREENSHOT_DIR` when set, otherwise `/tmp`.
The runner detects the newly created `p2k_screen_*` file and renames it to the
label while preserving its extension.

Audio capture is prepared automatically when the script contains a directive
beginning with `@record-audio`. It requires an available audio backend. The
runner asks the emulator to gate raw signed 16-bit PCM with F11, then writes
only the new complete frames into `LABEL.wav` in the screenshot directory.
Rate and channel count come from the emulator's companion format file.

> [!NOTE]
> A screenshot or WAV proves what this run produced. Keep the command,
> game/update identity, commit and script with evidence; the filename alone is
> not a reproducible result.

## Repeatable example

```text
@wait-for 45 game info => m_game_over True
@repeat 5
  @key c
  @wait 1
@end
@repeat 13
  @switch 13 0.08
  @wait 0.15
@end
@screenshot start-result
game info
@assert-regex m_game_over\s+(True|False)
```

The bundled `scripts/demos/start-game.p2k` is an executable example, not a
promise that every update enters a game after the same number of coin/start
pulses.

## Validate without launching

The underlying parser exposes a check-only mode:

```bash
python3 scripts/internal/run-console-script.py \
  scripts/demos/start-game.p2k --check
```

It reports the expanded action count and rejects unknown directives, invalid
numbers/regexes/labels, unmatched blocks and excessive expansion with source
line numbers. The launcher's `--script` path always performs this check first.

Parser regression tests are fast and ROM-free:

```bash
python3 -m unittest scripts.tests.test_console_script
```

## Runtime lifecycle and failure behavior

The launcher creates a private temporary directory, chooses a free localhost
TCP port, creates a Unix monitor socket, starts QEMU, and waits up to 90 seconds
for the XINA `%` prompt. It sends carriage returns periodically while waiting
to wake the console.

On script error it prints the file and source line where possible, exits with
status 2 and the launcher terminates its QEMU child. On successful script
completion, control returns to the launcher and QEMU stays open. Closing or
interrupting the launcher kills the child and removes its temporary UART,
monitor and raw-audio artifacts; retained screenshots/WAV files remain in the
chosen output directory.

Common failures:

- **prompt timeout:** wrong/unsupported update, boot failure or guest console
  not reaching `%`;
- **assertion has no preceding response:** put a console command or wait-for
  before the assertion;
- **audio backend unavailable:** choose a working backend or remove the audio
  directive;
- **no screenshot/audio produced:** confirm the custom QEMU build and output
  directory permissions;
- **manual UART/monitor conflict:** remove the mutually exclusive option and
  let script mode create private endpoints.

## Evidence boundary

Console scripts use ordinary XINA commands and Encore's normal input paths,
but they still change guest state. Results after coin, switch, key or console
actions are scripted observations, not untouched natural-boot evidence. For
IRQ/timing claims use the dedicated [validation and benchmark protocol](26-testing-validation-matrix.md);
for a crash preserve the [live-capture procedure](51-live-crash-capture.md).

---

[XINA console](06-xina-os-deep-dive.md) · [Desktop controls](41-cli-keyboard-guide.md) · [Validation](26-testing-validation-matrix.md) · [Documentation](README.md)
