# Encore guest extensions

Encore can install a small, transient extension in unused guest RAM after the
game image has been loaded.  This is not a modified update ROM: the update
files remain byte-for-byte original and the extension disappears on reset.

The host resolves the few XINU/game imports it needs from instruction shapes,
not from fixed addresses or a per-update target table.  It then writes the
payload and its import block to `0x00ff0000..0x00ffffff`.  That 64 KiB reserve
is outside both XINU's largest supported heap ceiling (`0x00dfffff`) and the
physical framebuffer (`0x00800000..0x00bfffff`).

The first extension supplies `setip <address> <mask> <gateway>` on the XINU
serial shell.  It updates the three normal persistent resources through
`Resource<unsigned long>::putValue`; `net start` or a reboot applies them.

`extension.S` is deliberately freestanding i386 code.  `build.sh` turns it
into the byte include consumed by QEMU.  Run `build.sh --check` to verify that
the committed generated include is current.
