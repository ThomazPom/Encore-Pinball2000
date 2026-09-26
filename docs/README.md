# Encore documentation

Start with the path that matches what you are doing. The documentation is
organized by task first, then by implementation subsystem and evidence.

> [!TIP]
> New desktop user: follow [Quickstart](02-quickstart.md). For a dedicated
> boot-to-game host, use [Cabinet installation](01-cabinet-installation.md)
> only after a normal launch works.

> [!WARNING]
> Desktop and virtual-machine success do not certify a powered playfield.
> Physical-cabinet validation remains explicitly incomplete; follow the staged
> real-LPT procedure before connecting hardware.

## Run and operate

| Need | Guide |
|---|---|
| launch now, choose a game/update, manage state | [Quickstart](02-quickstart.md) |
| understand every public launcher option | [Command-line reference](03-cli-reference.md) |
| diagnose boot, display, sound or preparation | [Troubleshooting](04-troubleshooting.md) |
| install a dedicated cabinet session | [Cabinet installation](01-cabinet-installation.md) |
| use desktop cabinet keys or XINA keyboard mode | [Desktop controls](41-cli-keyboard-guide.md) |
| automate XINA commands, switches and captures | [Console scripting](42-console-scripting.md) |
| understand or safely reset persistent state | [Persistent cabinet state](09-savedata.md) |
| connect a real driver board | [Real LPT passthrough](46-real-lpt-passthrough.md) |

## Understand the emulator

| Subject | Guide |
|---|---|
| system ownership and data flow | [Architecture](10-architecture.md) |
| reset, protected-mode entry and guest startup | [Boot recipe](14-boot-recipe.md) |
| address spaces, ports and apertures | [Memory and I/O map](13-memory-map.md) |
| CPU gates, i8254, IRQ0 and stack safety | [CPU and timers](12-cpu-and-timers.md) |
| base ROMs, updates, BAR3 and software selection | [ROM loading](15-rom-loading.md) |
| guest XINU/XINA behavior and serial console | [XINA/XINU deep dive](06-xina-os-deep-dive.md) |
| MediaGX, blits, video capture and presentation | [MediaGX and display](23-mediagx-and-display.md) |
| DCS transports, engines, caches and host audio | [DCS sound](25-dcs-sound.md) |
| cabinet switch/lamp/solenoid protocol | [LPT driver board](26-lpt-board.md) |
| optional guest networking and host transports | [Optional network card](48-network.md) |

Source-oriented maps continue in the [`qemu/` implementation
guide](../qemu/README.md) and [guest-extension guide](../guest-extensions/README.md).

## Validate and preserve evidence

| Question | Guide |
|---|---|
| what does each validation layer prove? | [Testing and validation](26-testing-validation-matrix.md) |
| what combinations are supported? | [Compatibility and support](30-compatibility-support.md) |
| what is still incomplete or unproven? | [Known limitations](35-known-limitations.md) |
| how do we turn an observation into a claim? | [Evidence-promotion protocol](37-ai-generated-future-ideas.md) |
| how do I preserve a still-live failure? | [Live crash capture](51-live-crash-capture.md) |
| how are QEMU patches and changes maintained? | [Development guidelines](05-development-guidelines.md) |
| what does the published archive contain? | [Release process](47-release-process.md) |

Retained measurement tooling lives under `measurements/`; it is narrower than
the support contract. The [DCS comparison harness](measurements/dcs-engines/README.md)
compares engine timing under a controlled workload. Historical reference code
and archived pages under [`references/`](references/README.md) are evidence,
not current implementation authority.

## Game code and preservation

| Subject | Guide |
|---|---|
| local update inventory, provenance and redistribution | [Update provenance](47-community-updates.md) |
| attributable RFM/SWE1 software changes | [Game-code changelogs](50-game-changelogs.md) |
| guest JTS surface and missing tournament server | [Tournament-server research](49-tournament-server.md) |

The fact that Encore can execute an asset does not establish a right to
redistribute it. Keep code, third-party ROM/update material, savedata and
forensic captures as separate evidence/licensing classes.

## Project direction

- [Roadmap](36-roadmap.md) lists evidence-backed next work and release gates.
- [Known limitations](35-known-limitations.md) is the current unsupported or
  incomplete behavior ledger.
- [From ideas to evidence](37-ai-generated-future-ideas.md) explains how an
  experiment graduates—or does not graduate—into those documents.

> [!IMPORTANT]
> Current code and reproducible measurements outrank old investigation prose.
> If a document and implementation disagree, record the exact version and
> open a focused correction rather than silently choosing the nicer story.

---

[Project README](../README.md) · [Quickstart](02-quickstart.md) · [CLI reference](03-cli-reference.md) · [Troubleshooting](04-troubleshooting.md)
