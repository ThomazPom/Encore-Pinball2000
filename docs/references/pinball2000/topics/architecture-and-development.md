# Architecture and development

Pinball 2000 is not simply a PC running a monolithic game executable. The
preserved first-hand accounts describe a layered system built under severe
memory, bus and schedule constraints.

## The software stack

| Layer | Preserved account |
|---|---|
| PC and boot | PCI POST code hands control to initialization for the MediaGX platform and XINU |
| kernel | XINU supplies lightweight pre-emptive processes and networking |
| pinball environment | XINA provides a C++ game framework substantially shaped by APPLE's proven API concepts |
| game and presentation | game rules, DisplayManager/Displayables, display effects, lamps, switches, coils and sound |
| development path | GCC/Cygwin, TFTP boot, GDB, serial console and extensible shell commands |

The strongest source is the direct account by Tom Uban and Graham West in the
[Williams software history](../sources/wms-software-history/README.md). The
[development-lore series](../sources/pinball2000-development-lore-part-1/README.md)
adds the implementation trade-offs from another member of the software team.

> [!IMPORTANT]
> “XINA is based on XINU” and “XINA is a port of APPLE” are compatible claims,
> not alternatives: XINU is the kernel base; APPLE supplied much of the pinball
> programming model that XINA re-expressed and extended in C++.

## Graphics constraints and choices

The first-hand record explains several apparently odd properties of the shipped
system:

- the display is 640x240 on a roughly 15 kHz low-resolution CRT;
- pixels use RGB555-style 15-bit colour stored in 16 bits, avoiding shared
  palette conflicts;
- the machine had only 8 MiB of RAM in the described development context;
- 64 MiB of masked ROM included roughly 4 MiB reserved for a bootable early
  game image, leaving about 60 MiB for art;
- compressed images reduce both storage and slow PCI-ROM reads;
- Displayables are composed by Z order, while DisplayEffects add priorities,
  suspension and thresholds.

Read [part 2](../sources/pinball2000-development-lore-part-2/README.md),
[part 3](../sources/pinball2000-development-lore-part-3/README.md) and
[part 5](../sources/pinball2000-development-lore-part-5/README.md) for the
author's complete reasoning.

## Concurrency and lifetime

XINU made pre-emption the default, unlike the familiar cooperative behavior of
the older WPC framework. The team retained cooperative “APPLE-like” processes
where that made game programming safer, while system work could remain
pre-emptive. Allocation ownership was tied to processes so terminating a game
process could reclaim its remaining memory. Display objects created a need for
carefully detached lifetimes.

This distinction matters when interpreting Encore failures: a symptom involving
interrupt delivery or process starvation must not be simplified into “the game
is single-threaded.” See [part 4](../sources/pinball2000-development-lore-part-4/README.md).

## What remains testimony rather than specification

Exact implementation details remembered decades later—library versions,
individual ownership and some memory-layout figures—remain class B evidence.
Encore should prefer observed guest behavior, schematics and decompiled bytes
when a recollection conflicts with them.

[Hardware and service](hardware-and-service.md) ·
[Software updates](software-updates.md) ·
[All preserved sources](../sources/README.md)
