# 38 — Installing an update bundle

Encore loads an extracted Pinball 2000 update directory. It never executes the
Windows updater.

## Required files

The directory passed to Encore must contain these four files with a common game
and version prefix:

```text
*_bootdata.rom
*_im_flsh0.rom
*_game.rom
*_symbols.rom
```

`qemu/p2k-bar3-flash.c` validates those names and assembles the files into the
4 MiB update flash in that order. `pubboot.rom` and `sf.rom` may exist in an
original distribution, but the current BAR3 loader does not consume them.

## Install

Original updater EXEs are ZIP-compatible self-extracting archives:

```sh
unzip pin2000_<game>_<version>_*.exe -d updates/
```

Keep the extracted directory structure. Then either let Encore select the
highest installed version:

```sh
scripts/run-qemu.sh --game swe1 --update latest
```

or select one explicitly:

```sh
scripts/run-qemu.sh --game swe1 --update 2.10
scripts/run-qemu.sh --game swe1 --update /path/to/extracted/50069
```

Use `--update none` only when intentionally testing the base chip ROM without
an update. That mode enables the documented base-ROM compatibility mechanisms.

Encore does not distribute community update payloads. Download them from their
publisher and keep them local; see [39 — Community updates](39-community-updates.md).

---

← [Documentation index](README.md) · [Project README](../README.md)
