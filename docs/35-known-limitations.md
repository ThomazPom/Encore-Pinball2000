# 35 — Known limitations

This page lists current gaps that affect users or cabinet validation. It does
not list retired experiments or hypothetical redesigns.

## Physical cabinet is not certified

Linux `ppdev` passthrough exists, but Encore has not completed a powered
playfield validation. Switch polarity, lamp/output behavior, worst-case bus
gaps and long gameplay runs must be observed on physical hardware.

Use [46 — Real LPT passthrough](46-real-lpt-passthrough.md) and keep playfield
power disabled during the first trace.

## Native ADSP engines are experimental

`adsp` and `adsp-thread` execute the original DSP firmware and render SPORT PCM.
They pass the automated boot/progress matrix, but manual sound behavior still
needs broader gameplay and service-menu testing. `pb2kslib` remains the default
sample engine.

## Base-ROM mode uses explicit compatibility support

`--update none` enables the accepted DCS probe-cell mechanism. It is isolated
from genuine update boots. This is expected behavior, not an accidental patch
leaking into normal runs.

## Timing modes have different purposes

Default adaptive HOTLOOP targets real-time XINU progress. `--strict` is a
diagnostic natural-PIT path and can run substantially slower on TCG.
`--with-pit` is retained for controlled comparison. Use `--bench` on the actual
host instead of inferring speed from CPU model or display smoothness.

## No project-level license file

The repository currently has no project-level license file. QEMU and bundled
reference material retain their own licenses. This should be resolved before a
formal source release.

If a reproducible problem is not listed here, capture the command, game,
update, DCS engine and log, then report it.

---

← [Documentation index](README.md) · [Project README](../README.md)
