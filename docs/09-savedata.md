# 09 — Savedata

Encore persists three guest-visible storage areas under `savedata/`:

| File | Guest device | Size |
|---|---|---:|
| `<game>.nvram2` | PRISM BAR2 battery SRAM | 192 KiB |
| `<game>.flash` | PRISM BAR3 update flash | 4 MiB |
| `<game>.see` | PLX serial EEPROM | 128 B |

These are hardware state, not save states. QEMU still boots the game normally;
Encore seeds each emulated device from its file and writes changed state back on
clean exit.

The runner passes the selected directory to the Encore machine as
`savedata-dir`. Encore recursively creates that exact directory when possible
and tests it with a temporary create/remove operation. If it is unavailable or
not writable, Encore falls back to `$XDG_DATA_HOME/encore/savedata` (normally
`~/.local/share/encore/savedata`) and reports the selected path. Only failure of
both locations stops startup. `--no-savedata` deliberately creates nothing.

Writes use a temporary file followed by `rename()` so an interrupted save does
not replace the last complete file. A hard kill cannot run the exit notifiers,
so stop Encore normally whenever possible.

## Start fresh and save the result

```sh
scripts/run-qemu.sh --fresh
```

This ignores the existing files for the selected game. BAR2 starts empty,
BAR3 starts erased before the selected update is installed, and the serial
EEPROM uses its built-in defaults. A clean exit replaces all three savedata
files with the newly initialized state.

## Run without loading or saving

```sh
scripts/run-qemu.sh --no-savedata
```

This sets `P2K_NO_SAVEDATA=1`. No savedata is loaded or written, and volatile
devices start from their compiled reset contents. It is useful for repeatable
tests but is not the normal playing configuration.

`--fresh` and `--no-savedata` are mutually exclusive.

## Ownership

- `qemu/p2k-bars.c`: BAR2 SRAM
- `qemu/p2k-bar3-flash.c`: update flash
- `qemu/p2k-plx-regs.c`: serial EEPROM

---

← [Documentation index](README.md) · [Project README](../README.md)
