# 06 — XINA and its serial console

XINA is the Williams runtime that starts after the PRISM loader. It contains the
XINU scheduler, device services, service menus and the game. Encore runs this
original guest software; it does not replace its scheduler or processes.

The boot log identifies both layers, for example:

```text
XINA: V1.36
XINU: V7
```

## Open the console

For a console in the same terminal:

```sh
scripts/run-qemu.sh --serial
```

For a TCP console:

```sh
scripts/run-qemu.sh --uart-tcp 127.0.0.1:1234
telnet 127.0.0.1 1234
```

The XINA prompt is `%`. Some commands print the next prompt only after another
character arrives; Encore's console wrapper handles that when running automated
benchmarks.

Start with `help` because the available commands depend on the game and update.
Common useful commands observed in supported builds are:

| Command | Use |
|---|---|
| `ps` | Show XINU processes and states. |
| `mem` | Show guest memory use. |
| `fatal` | Show recorded fatal errors. |
| `nonfatal` | Show recorded non-fatal errors. |
| `dcs` | Show DCS commands for that build. |
| `pdb` | Show power-driver-board commands. |
| `sleep 10` | Sleep for ten guest seconds; useful for measuring guest clock speed. |

Do not assume a command or subcommand exists until `help` on the selected update
shows it.

## What Encore supplies to XINA

- IRQ0 clock delivery from the selected timing mode
- PRISM PCI configuration and BAR windows
- persistent BAR2 SRAM
- update flash in BAR3
- DCS communication and audio
- display registers, framebuffer and VSync
- an emulated or real LPT driver-board connection
- COM1 transport for this console

Encore does not patch `nulluser`, `prnull`, XINU queues or scheduler variables.
Adaptive HOTLOOP changes IRQ0 delivery at the emulated-machine boundary.

`--update none` is the one mode that enables the accepted base-ROM DCS probe
compatibility mechanism. Normal update boots do not use it.

For the boot sequence see [14 — Boot recipe](14-boot-recipe.md). For update
files see [38 — Installing an update](15-rom-loading.md).

---

← [Documentation index](README.md) · [Project README](../README.md)
