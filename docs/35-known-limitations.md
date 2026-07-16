# 35 — Known limitations

This page lists current gaps that affect users or cabinet validation.

## Physical cabinet is not certified

Linux `ppdev` passthrough exists, but Encore has not completed a powered
playfield validation. Switch polarity, lamp/output behavior, worst-case bus
gaps and long gameplay runs must be observed on physical hardware.

> [!WARNING]
> Keep playfield power disabled during the first real-port trace.

Details: [real LPT passthrough](46-real-lpt-passthrough.md).

## Native ADSP engines are experimental

`adsp` and `adsp-thread` execute the original DSP firmware and render SPORT PCM.
They pass the automated boot/progress matrix, but manual sound behavior still
needs broader gameplay and service-menu testing. `pb2kslib` remains the default
sample engine.

## No project-level license file

The repository currently has no project-level license file. QEMU and bundled
reference material retain their own licenses. This should be resolved before a
formal source release.

If a reproducible problem is not listed here, capture the command, game,
update, DCS engine and log, then report it.

---

← [Documentation index](README.md) · [Project README](../README.md)
