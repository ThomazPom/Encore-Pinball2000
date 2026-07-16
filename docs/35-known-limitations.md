# 35 — Known Limitations

> *This list is based on emulator-only testing. Some entries may turn
> out to be cabinet-only issues — and other cabinet-only issues may
> surface that aren't listed here yet.*

Items that are incomplete, partially working, or require further
research. This is a truthful snapshot; it is not a promise of future
delivery.

---

## Legacy `.ems` files are intentionally unused

state; they are not an original-board device and do not contain service-menu flags.
Encore runs the guest and DCS paths directly, so importing that wrapper state would be
incorrect. SEEPROM (cabinet config / dip switches) persists correctly via the exit
notifier in `qemu/p2k-plx-regs.c` — see [docs/09-savedata.md](09-savedata.md).

> [!NOTE]
> NVRAM, flash, and SEEPROM all persist correctly via exit notifiers. There is no EMS
> hardware persistence gap.

---

## Base-ROM audio still uses a compatibility shim

`--update none` boots the chip-ROM game to XINU and attract mode. Base SWE1 also emits
DCS commands when the gated probe-cell compatibility shim is active. Disabling that
shim does not stop the game from booting, but the game then classifies DCS as absent
and sends no sound commands.

The remaining limitation is therefore implementation fidelity, not usability: Encore
maintains a discovered guest probe cell every 50 ms instead of obtaining the value from
a fully modelled DCS/PLX hardware path. Normal update boots do not enable this shim.
See [docs/34-probe-cell-shim.md](34-probe-cell-shim.md).

## Reference-only update bundles (below chip-ROM baseline)

The bundled chip ROMs are at a fixed baseline: **SWE1 v1.5** and
**RFM v1.6 (r2)**. An on-cabinet update can only *upgrade* flash,
never downgrade the underlying chip ROMs. Bundles below the baseline
— SWE1 v1.3 / v1.4 and RFM v1.2 / v1.4 / v1.5 — are therefore
**reference-only**:

* They ship on disk for historical interest (so you can see what
  shipped earlier and inspect the `.exe` self-extractors under
  `updates/exe-sources/`).
* Encore does not claim they reach a usable state.
* Failures under these versions are not tracked as bugs and will
  not be investigated.

RFM v1.2 in particular predates the XINU port. It targets the r1
chip hardware revision (`rfm_u100.rom` / `rfm_u101.rom`), but
Encore loads it against the r2 chips (the unconditional bank-0
preference) which is a known mismatch. Boot behaviour under these
old bundles is untested.

---

## No graphical debugger or symbolic breakpoint panel

Encore inherits QEMU's standard monitor interface (accessible via `-monitor stdio` or
`-monitor telnet:...`) but does not expose a Pinball2000-specific debugger UI. There is no
breakpoint, watchpoint, or step facility tailored to the game's symbol tables.

If you want to observe or modify guest state while running, use QEMU's `info registers`,
`x/<addr>`, and `gdbserver` support. The XINA shell on COM1 (mirrored to stderr) provides
game-level introspection.

---

## LPT driver-board handshake and passthrough need cabinet validation

Encore's emulated LPT device implements the recovered DATA/STATUS/CONTROL
edge protocol. Desktop keys update a dedicated switch matrix that opcode
`0x04` reads; they do not write guest RAM, and lamp-output rows are separate
state. What remains unproven is equivalence to the electrical Williams board,
not the old lamp/switch alias bug.

Linux `ppdev` passthrough via `--lpt-device /dev/parportN` is implemented, including
claim/negotiation, bidirectional reads, purist-mode input suppression, and forensic
tracing. It has not yet been proven against a physical Williams driver board. STATUS
polarity, IRQ7 behavior, permissions UX, electrical pacing, and long-run watchdog
behavior therefore remain validation items. See
[docs/46-real-lpt-passthrough.md](46-real-lpt-passthrough.md).

## Native DCS behavioral coverage is incomplete

`adsp` and `adsp-thread` are Encore's intended sound paths: they execute
original-format firmware and load U109/U110 plus the selected update's
`sf.rom`, including update-added speech. `pb2kslib` is only a legacy extracted-
audio fallback and cannot establish correct DCS behavior. Remaining validation
is focused on repeated volume commands, loop/stop transitions, reset and
long-running mailbox traffic in the native engines.

---

## No `make install` target or packaging recipe

Encore must be launched via `scripts/run-qemu.sh`; there is no `make install` that produces
a system-wide binary. Packaging (`.deb`, `.rpm`, Flatpak, etc.) is absent. See
[docs/43-build-system.md](43-build-system.md).

---

## CPU/bus pacing is evidence-triggered, not current homework

QEMU's i386 TCG can execute guest busy loops faster than the original 233 MHz
Cyrix MediaGX. Busy-wait
loops in the ROM firmware (iodelay-style `inb $0x80` strobes, `rep nop` chains) collapse to
near-zero wall-clock time. This is invisible in emulation (where "peripherals" are also emulated
and respond instantly) but could cause protocol violations if driving a real driver board or DCS
chip. No current emulator or bench result demonstrates that this causes a
cabinet problem.

writes. Encore's passthrough does not currently pace individual accesses.
Physical-board traces must first prove that pacing is needed; it must not be
added merely from host-CPU-speed theory. Until such evidence exists, there is
no pacing implementation task. See

> [!CAUTION]
> During first physical-board validation, record relay/watchdog behavior and a
> wire trace. A clean run closes this hypothesis; a failure correlated with
> access timing is what would open pacing work.

---

## Cross-references

* Savedata gaps: [09-savedata.md](09-savedata.md)
* Roadmap for missing features: [36-roadmap.md](36-roadmap.md)
* Build system: [43-build-system.md](43-build-system.md)
* Real LPT passthrough (legacy design): [46-real-lpt-passthrough.md](46-real-lpt-passthrough.md)

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
