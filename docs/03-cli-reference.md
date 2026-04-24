# 03 - CLI Reference

This page is a compact map of the options accepted by the current
command-line parser. The implementation lives in `src/main.c`:

* `parse_args()` handles command-line syntax, `-v` shortcuts, `-h`, and
  the `--config` pre-pass.
* `apply_option()` is the shared option table used by both CLI parsing
  and `encore.yaml`.

If this page and `src/main.c` disagree, `src/main.c` wins.

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is still in progress; see
> [42-cabinet-testing-call.md](42-cabinet-testing-call.md).

## Syntax

Most options use separate arguments:

```sh
./build/encore --game swe1 --update latest
./build/encore --roms ./roms --savedata ./savedata
```

Current parser notes:

* Short verbosity forms are `-v`, `-vv`, `-vvv`, and so on.
* Long options with values use `--flag value`.
* `--flag=value` is not split by the current parser; use the space
  form even if older help text or comments imply equals-form support.
* `--config FILE` is loaded before the rest of the command line, so
  later CLI flags override config values.
* With no CLI arguments, `./encore.yaml` is auto-loaded if present.

Config-file details are in [04-config-yaml.md](04-config-yaml.md).

## Source Of Truth

For accepted options, `src/main.c` `apply_option()` is authoritative.
`parse_args()` defines the CLI syntax around that table, including
`--config`, `-h`, and the compact `-v` forms.

`./build/encore --help` is still useful as a runtime cheat sheet, but it
is not the parser source of truth. If help text, comments, this page, and
`apply_option()` disagree, trust `apply_option()`.

## Common Invocations

```sh
./build/encore --game swe1
./build/encore --game rfm --update latest
./build/encore --headless --serial-tcp 4444
./build/encore --lpt-device none
```

Real-cabinet examples change as hardware testing improves. Use
[19-real-lpt-passthrough.md](19-real-lpt-passthrough.md) for the current
LPT setup and testing guidance.

## Accepted Options

| Option | Value | Purpose |
|---|---|---|
| `--game` | `swe1`, `rfm`, `auto` | Select the game family. |
| `--roms` | path | Override the ROM directory. |
| `--savedata` | path | Override the save-data directory. |
| `--update` | path, version token, `latest`, `none` | Select or disable update loading. |
| `--no-savedata` | none | Skip save-data load and save. |
| `--dcs-mode` | `io-handled`, `bar4-patch` | Select the DCS bring-up path. |
| `--fullscreen` | none | Start SDL fullscreen. |
| `--flipscreen` | none | Start with vertical display flip enabled. |
| `--bpp` | `16`, `32` | Select output texture bit depth. |
| `--splash-screen` | `none`, path | Override or disable the startup splash. |
| `--serial-tcp` | port | Bridge emulated COM1 over localhost TCP. |
| `--keyboard-tcp` | port | Inject keyboard bytes over localhost TCP. |
| `--headless` | none | Skip SDL display and audio init. |
| `--lpt-device` | path, `0xBASE`, `none`, `emu` | Select real LPT passthrough or force emulation. |
| `--lpt-trace` | file | Capture passthrough LPT bus cycles to CSV. |
| `--lpt-managed-dir` | none | Legacy raw-LPT direction-management diagnostic. |
| `--lpt-purist` | none | Back-compat alias; currently a no-op. |
| `--lpt-bus-pace` | `auto`, microseconds | Control real-LPT bus pacing. |
| `--cabinet-purist` | none | Skip selected bring-up shims while real LPT is open. |
| `--config` | file | Load a small `key: value` config file. |
| `--verbose` | optional level as next argument | Set verbose logging; use `--verbose 2`, not `--verbose=2`. |
| `-v`, `-vv`, `-vvv` | none | Increase verbose logging by count of `v`s. |
| `-h`, `--help` | none | Print runtime help. |

## Option Groups

### Game, ROMs, Updates

Use `--game`, `--roms`, `--savedata`, and `--update` for boot input
selection. The detailed update resolver rules are intentionally kept in
[09-update-loader.md](09-update-loader.md), because that path evolves
with ROM/update handling.

Related docs:

| Topic | Document |
|---|---|
| ROM loading | [08-rom-loading-pipeline.md](08-rom-loading-pipeline.md) |
| Update loading | [09-update-loader.md](09-update-loader.md) |
| Save data | [10-savedata.md](10-savedata.md) |
| Community updates | [47-community-updates.md](47-community-updates.md) |

### Display And Local UI

Use `--fullscreen`, `--flipscreen`, `--bpp`, and `--splash-screen` for
startup display behaviour. Runtime keyboard controls are documented
separately so this page does not duplicate the live cheat sheet.

Related docs:

| Topic | Document |
|---|---|
| Display pipeline | [11-display-pipeline.md](11-display-pipeline.md) |
| Keyboard guide | [36-cli-keyboard-guide.md](36-cli-keyboard-guide.md) |
| Splash screen | [49-splash-screen.md](49-splash-screen.md) |

### Headless And Network Console

Use `--headless` for non-SDL runs, usually together with
`--serial-tcp`. `--keyboard-tcp` is available for keyboard-byte
injection, but serial is the more reliable control path for scripted
XINA sessions.

Related docs:

| Topic | Document |
|---|---|
| Runtime environment | [41-build-env-and-runtime.md](41-build-env-and-runtime.md) |
| Keyboard guide | [36-cli-keyboard-guide.md](36-cli-keyboard-guide.md) |

### DCS Audio

Use `--dcs-mode` only when comparing sound bring-up paths or debugging
regressions. The default is chosen in code; this page avoids restating
bundle-specific claims that belong in the DCS docs and regression
matrix.

Related docs:

| Topic | Document |
|---|---|
| Sound pipeline | [12-sound-pipeline.md](12-sound-pipeline.md) |
| DCS mode details | [13-dcs-mode-duality.md](13-dcs-mode-duality.md) |
| Regression matrix | [26-testing-bundle-matrix.md](26-testing-bundle-matrix.md) |

### Real LPT / Cabinet

Use `--lpt-device` to select the passthrough backend:

| Form | Meaning |
|---|---|
| `/dev/parportN` | Linux ppdev backend. |
| `0xBASE` | Raw `inb` / `outb` backend. |
| `none`, `emu` | Force emulated LPT path. |

The real-cabinet flags are intentionally documented in the LPT docs,
where protocol and safety notes can stay together:

| Option | Read |
|---|---|
| `--lpt-device` | [19-real-lpt-passthrough.md](19-real-lpt-passthrough.md) |
| `--lpt-trace` | [19-real-lpt-passthrough.md](19-real-lpt-passthrough.md) |
| `--lpt-managed-dir` | [19-real-lpt-passthrough.md](19-real-lpt-passthrough.md) |
| `--lpt-bus-pace` | [19-real-lpt-passthrough.md](19-real-lpt-passthrough.md#bus-pacing---lpt-bus-pace) |
| `--cabinet-purist` | [19-real-lpt-passthrough.md](19-real-lpt-passthrough.md) |
| Protocol references | [48-lpt-protocol-references.md](48-lpt-protocol-references.md) |

### Config And Logging

`--config FILE` and `encore.yaml` feed the same `apply_option()` path as
the CLI, so supported keys track the accepted long options. Boolean
flags become `key: true` in config files.

Logging uses either `--verbose LEVEL` or short `-v` forms. Higher levels
are intended for diagnostics and can perturb timing-sensitive tests.

Related docs:

| Topic | Document |
|---|---|
| Config loader | [04-config-yaml.md](04-config-yaml.md) |
| Build/runtime environment | [41-build-env-and-runtime.md](41-build-env-and-runtime.md) |

---

[Back to documentation index](README.md) | [Back to project README](../README.md)
