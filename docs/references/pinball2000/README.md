# Pinball 2000 knowledge archive

This directory preserves knowledge, not merely old downloads. Its purpose is to
keep the most useful Pinball 2000 facts understandable and auditable after the
small sites that carry them disappear.

> [!IMPORTANT]
> A file is not accepted because it mentions Pinball 2000. Every preserved
> source must contribute identifiable knowledge, have stated provenance and
> survive byte-level validation. The raw capture is evidence; its generated
> Markdown is only the readable view.

## Start here

| Need | Document |
|---|---|
| understand a subject without opening every capture | [Topic guides](#topic-guides) |
| understand what is accepted and why | [Preservation method](METHODOLOGY.md) |
| see accepted, queued and rejected sources | [Source catalog](CATALOG.md) |
| inspect the original evidence | [Preserved source bundles](sources/README.md) |

## Topic guides

| Subject | Guide |
|---|---|
| XINU/XINA, graphics, threading and development tools | [Architecture and development](topics/architecture-and-development.md) |
| PC, PRISM, power driver, display, conversion and repair | [Hardware and service](topics/hardware-and-service.md) |
| serial/PUB paths, flash and version evidence | [Software updates](topics/software-updates.md) |
| Ethernet, XINA services, barcode reader and tournament server | [Networking and tournament operation](topics/network-and-tournament.md) |
| Holopin, shipped games and unfinished projects | [Prototypes and history](topics/prototypes-and-history.md) |

The long-term layout is deliberately layered:

```text
pinball2000/
├── README.md
├── METHODOLOGY.md
├── CATALOG.md
├── topics/       concise fact-checked knowledge by subject
└── sources/      readable source bundles plus their raw evidence
```

The topic layer reconciles sources but never silently turns a community
recollection into a fact. The source layer retains the evidence needed to
revisit that judgement. As of the 2026-11-11 pass, the archive contains 39
validated bundles selected from the local collections and thematic English,
German and French web searches.

> [!NOTE]
> Encore does not vendor NuCore documentation, binaries or packages. Historical
> source pages may mention it in passing, but the separate NuCore ecosystem is
> outside this archive's scope.

[Encore documentation](../../README.md) · [All third-party references](../README.md)
