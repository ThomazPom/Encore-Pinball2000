# Part A — Honest cabinet/gameplay metrics

Raw audit logs captured by `P2K_DIAG=1 timeout 40 bash scripts/run-qemu.sh ...`
with `P2K_IRQ_REPLAY` unset (replay OFF). Each run is ~38 s of wall
time; the audit panel emits at 3 s and every 3 s thereafter.

| File | Game | Display | Audio | --update | Outcome |
|---|---|---|---|---|---|
| `s1_swe1_normal.log`       | swe1 | sdl  | pa   | auto | full 35 s |
| `s2_rfm_normal.log`        | rfm  | sdl  | pa   | auto | crashed at ~3 s, `cpu_io_recompile` |
| `s3_swe1_headless.log`     | swe1 | none | none | none | full 35 s |
| `s4_rfm_headless.log`      | rfm  | none | none | none | full 35 s |
| `s1b_swe1_sdl_noaudio.log` | swe1 | sdl  | none | auto | display-only sanity check |
| `s1c_swe1_audio_only.log`  | swe1 | none | pa   | auto | audio-only sanity check (crashed) |

> [!IMPORTANT]
> See `docs/12-cpu-and-timers.md#honest-cabinet-metrics-part-a` for
> the matrix interpretation and the central XINU drift answer.
