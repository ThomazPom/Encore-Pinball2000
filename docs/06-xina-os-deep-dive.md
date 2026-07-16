# 06 — XINA and its serial console

XINA is the Williams runtime that starts after the PRISM loader. Its XINU
scheduler, device services, service menus and game execute as guest code.

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

> [!TIP]
> Run `help` on the selected update before relying on a command or subcommand.

## What Encore supplies to XINA

- IRQ0 clock delivery from the selected timing mode
- PRISM PCI configuration and BAR windows
- persistent BAR2 SRAM
- update flash in BAR3
- DCS communication and audio
- display registers, framebuffer and VSync
- an emulated or real LPT driver-board connection
- COM1 transport for this console

Details: [boot path](14-boot-recipe.md) and
[ROM/update loading](15-rom-loading.md).

---

← [Documentation index](README.md) · [Project README](../README.md)
