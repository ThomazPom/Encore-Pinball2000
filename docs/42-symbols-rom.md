# 42 — Williams symbol tables

Each extracted update used by Encore contains four files required for BAR3:

- `*_bootdata.rom`
- `*_im_flsh0.rom`
- `*_game.rom`
- `*_symbols.rom`

`symbols.rom` contains Williams guest names and addresses. It helps interpret
disassembly and traces; it is not executable game code.

`qemu/p2k-bar3-flash.c` appends this blob after the system and game images when
assembling BAR3. Encore does not resolve symbols at runtime and does not depend
on them for booting.

Inspect a table offline with:

```sh
python3 tools/sym_dump.py --list updates/<bundle>/<game-id>/*_symbols.rom
```

Available names and addresses vary by update. Record the game and update version
beside any address used in a forensic note. Correct emulation must not rely on a
particular update retaining a particular symbol.

For Encore implementation names, use [`qemu/README.md`](../qemu/README.md).

---

← [Documentation index](README.md) · [Project README](../README.md)
