# Encore documentation

This documentation covers the current QEMU implementation. It is intentionally
limited to material that helps run Encore, understand current code, reproduce a
result, or test a cabinet.

> [!WARNING]
> Physical-cabinet validation is pending. Desktop success does not certify a
> powered playfield.

## Run Encore

- [Quickstart](02-quickstart.md)
- [Command-line reference](03-cli-reference.md)
- [Desktop controls](41-cli-keyboard-guide.md)
- [Troubleshooting](04-troubleshooting.md)
- [XINA serial console](06-xina-os-deep-dive.md)
- [Savedata](09-savedata.md)

## Understand the implementation

- [Architecture](10-architecture.md)
- [CPU and timing](12-cpu-and-timers.md)
- [Memory map](13-memory-map.md)
- [Boot path](14-boot-recipe.md)
- [ROM and update loading](15-rom-loading.md)
- [MediaGX display](23-mediagx-and-display.md)
- [DCS sound](25-dcs-sound.md)
- [LPT board](26-lpt-board.md)
- [Compatibility support](30-compatibility-support.md)
- [`qemu/` source map](../qemu/README.md)

## Validate and connect hardware

- [Validation matrix](26-testing-validation-matrix.md)
- [Known limitations](35-known-limitations.md)
- [Roadmap](36-roadmap.md)
- [Real LPT passthrough](46-real-lpt-passthrough.md)

## Non-roadmap notes

- [AI-generated future ideas](37-ai-generated-future-ideas.md) — speculative
  brainstorming that may never be implemented.

Reference material under `references/` is evidence, not current Encore
documentation. Read it only when a current source or physical trace requires
hardware background.

---

← [Project README](../README.md)
