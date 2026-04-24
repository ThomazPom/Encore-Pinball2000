# Web archive: PinWiki — Pinball 2000 Repair (extracts)

> **Mirror policy:** External pages may disappear without notice. This file
> reproduces the technically relevant extracts from the source page below
> for the sole purpose of preserving Pinball 2000 hardware-repair
> reference material that informs Encore's emulation choices. Original
> copyright remains with the PinWiki contributors.
>
> - **Source:** <https://pinwiki.com/wiki/index.php?title=Pinball_2000_Repair>
> - **Mirrored on:** 2026-04-24
> - **License:** PinWiki content (community wiki — see source for details)
> - **Scope of this mirror:** technical / electrical extracts only.
>   Sections about commercial third-party replacement products, schematics
>   PDFs (large binaries) and motherboard model variants beyond chipset
>   compatibility are intentionally omitted; consult the live page for
>   those.

## System overview

Pinball 2000 (P2K) was Williams' last pinball platform. It is a hybrid of
conventional pinball with a video aspect: a 19" arcade monitor (CGA
resolution) reflected by a coated playfield glass produces the
"Pepper's Ghost" illusion of holographic images on the playfield.

The host computer is a commercial-off-the-shelf PC built around the
Cyrix MediaGX (an integrated graphics + CPU SoC for its era). Two custom
boards complete the system:

- the **PRISM** PCI card carries the masked ROMs (64 MB total — the
  densest possible 16-bit ROM configuration of the era) and bootstraps
  the PC without a hard disk;
- a **custom audio amp** drives stereo speakers + subwoofer.

Optional peripherals: SMC8416T family Ethernet card and a barcode
reader (used for online bookkeeping and barcode "login" tournaments).
Three SMC8416 variants work: SMC8416T, SMC8416BT, SMC8416BTA.

Only two games shipped: **Revenge from Mars** and **Star Wars Episode 1**.
Wizard Blocks and Playboy were in development when Williams ceased
pinball production.

## Chipset compatibility (critical)

Only motherboards with the **CX5520** chipset operate the P2K software
correctly. The similar CX5510 and CX5530 are *not* compatible.

The Cyrix MediaGX (grey ceramic PGA) physically fits an Intel/AMD
Socket 7 socket but the pinout is completely different: mixing a
MediaGX CPU with a Socket 7 board (or vice-versa) will likely destroy
the silicon. Cyrix 6x86 CPUs/boards are also incompatible with Cyrix GX.

CPUs observed in the wild: Cyrix GXm-266GP 2.9 V and GXm-233GP 2.9 V.

## Power Driver Board — LED indicators

> Encore relevance: LED1 is the **Blanking / Watchdog** indicator. This
> is the host-visible signal of the very same blanking/watchdog logic
> that Encore's `--lpt-device` raw I/O backend has to keep alive in real-
> cabinet mode. See [`docs/48-lpt-protocol-references.md`](../../48-lpt-protocol-references.md).

LEDs in **bold** are normally off when the coin door is open (interlock
safety).

| LED | Purpose | Notes |
|----|--------|------|
| LED1 | Blanking / Watchdog | Lit when blanking-OK + watchdog fed |
| LED2 | Health | Blinks at boot. Williams planned more uses; never shipped |
| LED3 | 18 VAC Lamp Matrix A | 18 V DC for the "A" matrix |
| **LED4** | 50 V DC Lower Right Flipper | |
| **LED5** | 50 V DC Lower Left Flipper | |
| **LED6** | 50 V DC Upper Right Flipper | No production game used this |
| **LED7** | 50 V DC Upper Left Flipper | No production game used this |
| LED8 | 18 VAC Lamp Matrix B | 18 V DC for the "B" matrix |
| LED9 | Incoming 50 VAC for Solenoids | Feeds the 4 solenoid buses |
| LED10 | 20 VAC for Flash Lamps | |
| **LED11** | 50 V DC Solenoid Power Bus 1 | |
| **LED12** | 50 V DC Solenoid Power Bus 2 | |
| **LED13** | 50 V DC Solenoid Power Bus 3 | |
| **LED14** | 50 V DC Solenoid Power Bus 4 | |
| LED15 | 20 V DC | |
| LED16 | 12 VAC (green) | 5 V on-board logic is generated from this |
| LED17 | 5 V DC | |

## Documentation pointers (not mirrored here)

The PinWiki page lists several large PDFs that we deliberately do not
mirror in-tree (size + license uncertainty). Fetch them directly from
PinWiki if needed:

- Williams Pinball 2000 Schematic Manual `16-10882` (Feb 1999) — OCR PDF
- Pinball 2000 Safety Manual `16-10878` (Feb 1999)
- Cyrix 586-GXM-AV motherboard manual
- InformTech 586-GXM+ motherboard manual
- GCT ST-MGXm motherboard manual
- Dataexpert MGX7520 motherboard manual

> Suggested archival workflow: download those PDFs into a *local-only*
> archive folder (do not commit binaries here), record the SHA-256
> alongside the URL in `docs/references/binary-sources.md` if/when that
> index file is added.
