# 37 — AI-generated future ideas

> [!CAUTION]
> This file is an AI-generated idea bank, not the Encore roadmap. It does not
> describe current features, promised work or release requirements. Any or all
> of these ideas may never be implemented.

The ideas below follow traits already visible in Encore: running the original
software, making pragmatic hardware substitutions, keeping meaningful choices
explicit, measuring behavior, preserving updates and savedata, and supporting
eventual use in a physical cabinet.

## Possible features

| Idea | Possible form | Intended value |
|---|---|---|
| Session recorder and replay | `--record-session <file>` captures configuration, source hashes, initial savedata, timestamped inputs, serial output, DCS events, LPT activity, timing and frame hashes; `--replay-session <file>` reproduces it. | Turns difficult bugs into portable, repeatable evidence. |
| Cabinet Lab | Live switch, lamp and coil matrices with board initialization, PDB05 cadence, gap tails and an output-inhibit mode. | Makes unpowered and first-power cabinet validation safer and easier to understand. |
| On-screen telemetry | A low-overhead toggle showing guest speed, IRQ delivery and tails, PDB gaps, FPS, DSP queue depth, audio underruns, update and sound engine. | Makes timing health visible without reading logs. |
| Host preflight | `--doctor` checks ROM/update assets, savedata/update state, QEMU build, display and audio backends, host power policy, timing, and parallel-port permissions. | Produces one actionable PASS/WARN/FAIL report before a normal run. |
| Update and ROM inspector | `--inspect-update` and `--dry-run` report identity, version, timestamps, hashes, included regions, selected sound assets and the saved-flash action without booting. | Makes the flexible update loader easier to audit. |
| Named machine profiles | `--profile desktop`, `cabinet`, `development`, or user-defined profiles that expand to visible runtime options. | Keeps repeatable host configurations without hiding their actual settings. |
| Savedata slots and history | Named score/configuration slots plus atomic snapshots before update installation. | Protects persistent state without requiring whole-machine snapshots. |
| Projection calibration | Test grid, crop, position, scaling, brightness and gamma stored per display. | Supports the reflected Pinball 2000 cabinet display arrangement. |
| DCS laboratory | Browse track IDs, send commands, inspect stop/loop behavior, monitor DSP and PCM state, and export short samples. | Gives native sound development a focused validation tool. |
| Failure bundle | One command packages logs, configuration, hashes, timing, recent inputs and screenshots while excluding ROM data. | Makes issue reports useful without distributing protected assets. |
| Cabinet kiosk mode | Controlled autostart, safe output shutdown, health monitoring, crash recovery and rotated diagnostics. | Supports unattended cabinet operation after physical validation. |
| Configurable desktop controls | Keyboard and controller mappings stored per user or profile. | Replaces hard-coded desktop keys without affecting real-cabinet input. |
| Modern cabinet bridge | A dedicated USB microcontroller presents the cabinet protocol and handles time-sensitive signaling outside the host scheduler. | Could make cabinet use practical on computers without suitable parallel ports. |

## Strongest concept: a session artifact

A recorder could produce a directory or archive such as:

```text
swe1-volume-test.encore/
├── manifest.json
├── inputs.p2k
├── serial.log
├── lpt.csv
├── timing.json
├── dcs-events.csv
├── starting-savedata/
└── screenshots/
```

Input events should be timestamped against guest ticks when possible. A first
version could replay configuration and inputs without claiming deterministic
whole-machine replay; stricter frame, audio and device assertions could be
added only after they are proven reliable.

## Ideas to postpone

- Whole-machine save states, rewind and run-ahead require serialization of
  Encore devices, native audio workers and pending external I/O. Restoring
  physical output state also needs an explicit safety design.
- Automatic runtime switching between timing or sound engines would make
  failures harder to reproduce and could conceal implementation bugs.
- Netplay, achievements and large shader collections do not directly advance
  software preservation or cabinet operation.
- A large launcher GUI should not take priority over preflight checks,
  profiles, replayable tests or cabinet inspection.

---

← [Documentation index](README.md) · [Actual roadmap](36-roadmap.md)
