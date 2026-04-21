# 04 — YAML Config Loader

Encore ships a deliberately tiny `key: value` config loader. It is not
a general-purpose YAML parser; it only understands the subset that
makes sense for our CLI, and everything else is a silent skip or a
loud warning.

Implemented in `src/main.c` `load_config_file()` and
`apply_option()`.

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## Grammar

```
line        := comment | blank | "key:" | "key: value"
comment     := optional whitespace "#" …rest-of-line…
blank       := optional whitespace
key         := non-whitespace, no ':'
value       := optional ' or " quoted; inner whitespace preserved
```

The grammar is strictly line-oriented: one option per line, no nesting,
no sequences, no anchors, no tags. If you need structure, use multiple
config files with `--config`.

## Accepted keys

Any flag listed in [03-cli-reference.md](03-cli-reference.md) with its
leading `--` stripped. Bare flags (`--headless`) become `headless:` or
`headless: true`; valued flags (`--roms /x`) become `roms: /x`.

### Boolean shortcuts

These strings, case-insensitive, are treated as "flag on" (no value):

```
true yes on 1
```

These are treated as "flag off" (line skipped):

```
false no off 0
```

Flags cannot currently be explicitly turned off — Encore's options are
monotonic (once on, on). The `false`/`no`/`off` handling just gives you
a polite way to leave a line documented but inactive in the file.

### Quoted values

Surrounding single or double quotes are stripped. This matters for
paths with trailing whitespace or leading hyphens. Escape sequences
(`\n`, `\t`, `\"`) are **not** interpreted — the quote stripping is
purely cosmetic.

## Example — `encore.yaml`

```yaml
# Daily driver for SWE1
game: swe1
update: "2.1"

# Paths
roms:     "./roms"
savedata: "./savedata"

# Sound
dcs-mode: bar4-patch         # bar4-patch | io-handled

# Display
fullscreen: false            # SKIPPED (monotonic flags)
bpp: 32

# Headless regression knob — flip to 'yes' for CI
headless:    no
serial-tcp: 4444
```

Running `./build/encore` (no CLI args, file present in CWD)
auto-loads it:

```
[config] no CLI args — auto-loading ./encore.yaml
[config] loaded 6 setting(s) from ./encore.yaml
```

## Precedence

1. Hard-coded defaults inside `apply_option()`.
2. The first `--config FILE` seen on the command line (pre-pass).
3. `./encore.yaml` auto-loaded when **and only when** `argc == 1`.
4. Remaining CLI flags, applied left-to-right.

The pre-pass for `--config` means you can override a config entry on
the command line *even if* the config appears before the override on
the argument list. `encore --config prod.yaml --headless` always ends
up with `headless=true`.

## Unknown keys

```
[config] encore.yaml: unknown key 'foo'
```

A warning is printed and the line is skipped. Encore does not refuse
to start over an unknown config key — the assumption is that a newer
config file may reference options that older Encore builds do not yet
understand, and bailing out would break forward compatibility.

## What YAML does *not* support

* Nested mappings (no `sound: { mode: bar4-patch }`).
* Lists (no `roms: [./a, ./b]`).
* Multi-line scalars (no `| pipe` or `> fold`).
* References or aliases.
* Booleans spelled `True` with a capital in non-English locales. Only
  the literal strings above are recognised.

If you need any of that, pre-process the file with a real YAML tool and
feed the reduced flat file to Encore.

## Implementation notes

The parser trims every line through `trim()` (leading/trailing space
and CRLF), strips `#` to end-of-line as a comment, splits on the first
colon, and delegates to the same `apply_option()` used by the CLI
parser. This guarantees that CLI and config behaviours stay in sync
forever — both paths share the exact same 90-line function.

The single caveat is bare CLI flags. On the command line, `--headless`
passes `value == NULL` to `apply_option()`; in the config file,
`headless: true` triggers the boolean-shortcut dance above, which
resets the value pointer to `NULL` before calling the same function.
The two paths converge.

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
