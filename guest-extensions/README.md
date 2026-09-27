# Encore guest extensions

Encore can install a small, transient extension in unused guest RAM after the
game image has been loaded.  This is not a modified update ROM: the update
files remain byte-for-byte original and the extension disappears on reset.

The completed game announces itself with its `XINA:` banner through the
emulated UART before `netstart`. That one hardware event makes the host resolve
the few XINU/game imports it needs from instruction shapes, not from fixed
addresses or a per-update target table. It then writes the
payload and its import block to `0x00ff0000..0x00ffffff`.  That 64 KiB reserve
is outside both XINU's largest supported heap ceiling (`0x00dfffff`) and the
physical framebuffer (`0x00800000..0x00bfffff`).

The first extension supplies `setip <address> <mask> <gateway>` on the XINU
serial shell.  It updates the three normal persistent resources through
`Resource<unsigned long>::putValue`; `net start` or a reboot applies them.
For `--setip`, the values are applied before `netstart`. If blank CMOS causes
the game's native automatic factory reset later in that boot, a one-shot
wrapper reapplies them after the reset so the operator UI and BAR2 persistence
retain the same values. No polling or host-side savedata editing is involved.
No payload is written until every import and hook site has been resolved.

When the emulated Ethernet card is attached to QEMU's Slirp backend, Encore
also applies a smaller automatic extension before `netstart`. Every preserved
network-capable image gives unicast packets sent through XINU's common
`udpsend()` path a default TTL of 1. That is suitable for a flat cabinet LAN,
but Slirp adds a routed hop and drops those packets before NAT. Encore locates
the unique `udpsend()` instruction shape in guest RAM and changes that default
to 64. This covers the whole unicast UDP path, including DNS, JTS tournament
traffic, UDP echo and shell/network utilities; multicast keeps its separate
route-derived TTL logic.

This automatic patch is volatile and backend-scoped. It is installed only
when the card's actual peer is Slirp, does not require `--guest-extensions` or
`--setip`, and is not installed for passt or bridged/TAP networking. The
checker requires exactly one structural match in every supported update.

`extension.S` is deliberately freestanding i386 code.  `build.sh` turns it
into the byte include consumed by QEMU.  Run `build.sh --check` to verify that
the committed generated include is current.
