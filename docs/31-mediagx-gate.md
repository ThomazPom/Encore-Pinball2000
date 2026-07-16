# 31 — MediaGX instructions used by the display driver

The Williams display software executes CPU instructions specific to the Cyrix
MediaGX. Stock QEMU does not implement them, so Encore adds them to the i386 TCG
decoder.

## Current implementation

The build applies
`qemu/upstream-patches/0001-i386-tcg-cyrix-mediagx-shim.patch`. Its helpers
are enabled only when the `pinball2000` machine calls
`p2k_mediagx_enable_extensions()` in `qemu/p2k-mediagx-gate.c`. Other QEMU
machine types retain normal x86 decoding.

| Opcode | MediaGX operation | Encore behavior |
|---|---|---|
| `0F 3B` | `BB1_RESET` | Reset buffer-1 pointer to its base. |
| `0F 3C` | `CPU_WRITE` | Write a MediaGX internal register. |
| `0F 3D` | `CPU_READ` | Read a MediaGX internal register. |

The guest must also enable scratchpad memory through GCR register `B8`, modeled
by `qemu/p2k-cyrix-ccr.c`. Unsupported MediaGX opcodes raise an invalid-opcode
exception and are logged.

Modern x86 reused part of this opcode area for SSE. Enabling MediaGX decoding
globally would break ordinary QEMU guests, which is why the machine gate exists.
This is required CPU support, not a guest patch and not planned for removal.

## Relevant source

- `qemu/p2k-mediagx-gate.c`: machine gate and internal-register state
- `qemu/p2k-cyrix-ccr.c`: guest enable bit
- `qemu/upstream-patches/0001-i386-tcg-cyrix-mediagx-shim.patch`: decoder
- `qemu/p2k-gp-blt.c`: graphics BLT engine

---

← [Documentation index](README.md) · [Project README](../README.md)
