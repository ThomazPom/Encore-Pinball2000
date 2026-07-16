# 09 — Savedata

Encore persists three guest-visible storage areas under `savedata/`:

| File | Guest device | Size |
|---|---|---:|
| `<game>.nvram2` | PRISM BAR2 battery SRAM | 128 KiB |
| `<game>.flash` | PRISM BAR3 update flash | 4 MiB |
| `<game>.see` | PLX serial EEPROM | 128 B |

These are hardware state, not save states. QEMU still boots the game normally;
Encore seeds each emulated device from its file and writes changed state back on
clean exit.

Writes use a temporary file followed by `rename()` so an interrupted save does
not replace the last complete file. A hard kill cannot run the exit notifiers,
so stop Encore normally whenever possible.

## Fresh run

```sh
scripts/run-qemu.sh --no-savedata
```

This sets `P2K_NO_SAVEDATA=1`. No savedata is loaded or written, and volatile
devices start from their compiled reset contents. It is useful for repeatable
tests but is not the normal playing configuration.

To reset one game permanently, stop Encore and remove that game's three files.
The next normal run will create new state as the guest initializes the devices.

## Ownership

- `qemu/p2k-bars.c`: BAR2 SRAM
- `qemu/p2k-bar3-flash.c`: update flash
- `qemu/p2k-plx-regs.c`: serial EEPROM

---

← [Documentation index](README.md) · [Project README](../README.md)
