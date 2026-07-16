# 22 — PRISM SRAM and savedata (BAR2)

BAR2 is the PRISM card's 16 MiB PCI window at `0x11000000`. The real storage is
only the first 128 KiB; Encore models the rest as empty space.

| Guest address | Behavior |
|---|---|
| `0x11000000–0x1101ffff` | Read/write SRAM, persisted as `savedata/<game>.nvram2`. |
| `0x11020000–0x11ffffff` | Reads return all ones; writes are ignored. |

`qemu/p2k-bars.c` implements this with a RAM region overlaid on a larger
all-ones region.

The game stores settings, audits and resource data in the SRAM. The DCS resource
scanner also probes beyond the physical SRAM and expects empty space to read as
all ones. Returning zero across the whole BAR causes resource and sound
initialization failures.

At startup Encore loads `savedata/<game>.nvram2` when it exists. On normal exit
it writes the complete 128 KiB back through a temporary file and rename.
`--no-savedata` disables both operations, so that run begins with zero-filled
SRAM.

BAR2 offset `4` is also used as a display/VSync latch by `qemu/p2k-vsync.c`.

See [13 — Memory map](13-memory-map.md) and [24 — VSync](24-vsync.md).

---

← [Documentation index](README.md) · [Project README](../README.md)
