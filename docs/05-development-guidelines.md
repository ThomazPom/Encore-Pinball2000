# 05 — Development guidelines

This document defines the engineering rules for Encore changes. It applies to
the QEMU machine, wrapper scripts, tests, build integration and documentation.

> [!IMPORTANT]
> Current source code is the authority for current behavior. Read the owning
> code before designing a change or documenting it. A passing build does not
> prove that a hook is correctly placed, that two state machines agree, or that
> a runtime path works.

The goal is not maximum abstraction or a complete model of every physical
component. The goal is a coherent implementation that boots and runs the
supported software, remains testable, and can connect to a cabinet without
surprising interactions between subsystems.

## Core rules

Every change must preserve these rules:

1. **One owner per state.** Frontends translate; they do not copy a protocol
   state machine.
2. **Extend an existing path before creating another.** A second input,
   timing, audio or persistence pipeline requires a concrete reason.
3. **Install features from the machine that owns them.** Runtime P2K setup
   belongs in `pinball2000_init()`, not in a global QEMU constructor.
4. **Keep independent inputs independent.** Combine their resulting state at
   the device boundary; do not synthesize one host action through another.
5. **Initialize before publishing callbacks.** Locks, queues, timers and state
   must exist before a handler or worker can reach them.
6. **Make optional behavior truly optional.** Disabled diagnostics and
   experiments must not change guest behavior and must have negligible cost.
7. **Fail clearly.** Invalid configuration must not be partially accepted.
8. **Test the behavior, not merely compilation.**
9. **Update the user-facing contract with the code.**
10. **Leave no investigation debris in a production change.**

## Find the owner before editing

Use the current source map in [`qemu/README.md`](../qemu/README.md). Then trace
both callers and consumers:

```sh
rg "symbol_or_register" qemu scripts docs
```

Before adding a file, global, callback or exported function, answer:

- Which module owns the underlying hardware or state?
- Which current path already receives this event or access?
- At what point is the owner initialized?
- Which thread or QEMU context executes the callback?
- What releases the state during reset and shutdown?
- Is the behavior guest-visible, host-only, diagnostic or experimental?

If those answers are unclear, implementation has not started yet.

> [!NOTE]
> Examples below identify one good property of a file or function. They do not
> declare every line in that file to be a template.

### Current exemplar map

| Concern | Reference | Copy this property |
|---|---|---|
| Machine composition | `qemu/pinball2000.c:pinball2000_init()` | Explicit, dependency-ordered machine-local installation |
| Cross-module boundary | `qemu/p2k-internal.h` | Functions grouped and documented by owning source file |
| Shared protocol state | `qemu/p2k-dcs-core.c`, `qemu/p2k-dcs.c`, `qemu/p2k-dcs-uart.c` | One core with thin guest-access frontends |
| Cross-thread host input | `qemu/p2k-display.c:p2k_queue_host_key()` | SDL worker delegates to the QEMU main loop through a bottom half |
| Independent input sources | `qemu/p2k-lpt-board.c`, `qemu/p2k-switch-keymap.c` | Separate producer layers combined at the LPT read boundary |
| Strict local configuration | `qemu/p2k-switch-keymap.c:p2k_load_keymap()` | Parse temporary state, reject the complete invalid file, publish once |
| Parser unit test | `scripts/tests/test_console_script.py` | Fast deterministic input validation without launching QEMU |
| Runtime input test | `scripts/tests/smoke-switch-keymap.py` | QMP-driven overlap and fallback assertions against a real boot |
| Upstream-core boundary | `qemu/upstream-patches/pit-speed-target/` | Small versioned hook with an identity default outside P2K |
| Reproducible graft/build | `scripts/build-qemu.sh` | Pinned source, checksummed patch set and copy-only-if-changed machine files |

> [!IMPORTANT]
> `build-qemu.sh` automatically grafts every `qemu/p2k-*.c` file. A new file
> compiling successfully proves that its name reached the build, not that its
> initialization, ownership or runtime hook is correct.

## Runtime installation and hooks

### Machine-local installation

`qemu/pinball2000.c:pinball2000_init()` is the composition root for runtime
P2K behavior. A new device or host facility should normally expose:

```c
void p2k_install_feature(...);
```

and be called there after its dependencies are ready.

Current ordering demonstrates real dependencies:

- ROMs are loaded before ROM windows and DCS audio consume them.
- the ISA bus and PIC exist before devices receive IRQ lines;
- the LPT board is installed before the switch keymap delegates to it;
- GX state exists before the display reads it;
- locks and condition variables are initialized inside the owning installer
  before workers start.

Do not append an installer mechanically. Put it at the earliest point where
all dependencies exist and before any consumer can call it.

### `type_init()` is for type registration

The `type_init(pinball2000_register_types)` call in `pinball2000.c` registers
the QEMU machine type. It is not a general feature-startup hook.

Do not use `type_init()` to:

- open files;
- create timers or threads;
- register P2K input handlers;
- access P2K locks or device state;
- add a machine-init-done notifier for a feature used only by `pinball2000`.

Such code runs in QEMU's global process context and can affect `-M help` or
unrelated machine types. Install it explicitly from `pinball2000_init()`.

### Hook hierarchy

Choose the first workable level:

1. Call an existing P2K owner API.
2. Extend the existing P2K dispatch path.
3. Add a narrow API in `p2k-internal.h` and keep state in its owner.
4. Add a machine-local QEMU device callback.
5. Patch upstream QEMU only when the machine module cannot reach the required
   boundary.

An upstream patch must be smaller than the machine-side implementation it
enables. Its default behavior must remain identical for non-P2K machines.
Current identity weak hooks in `upstream-patches/` demonstrate that boundary.

### Do not create interception layers casually

Avoid global event filters, duplicate input handlers and callbacks that consume
events before the established owner sees them. They change ordering and often
require forwarding every unhandled case correctly.

The direct framebuffer keyboard path is the model:

```text
SDL key
  -> p2k_sdl_qcode()
  -> p2k_queue_host_key()
  -> main-loop bottom half
  -> p2k_lpt_host_key()
  -> LPT-owned switch/button state
```

Normal QEMU display input also ends at `p2k_lpt_host_key()`. A keyboard feature
should extend that common path instead of creating a second SDL filter or QEMU
input handler.

## State ownership and data flow

### One canonical state machine

`p2k-dcs-core.c` owns shared DCS command, response, echo and handshake state.
The BAR4 and UART files translate their access shapes into calls to that core.
They may own registers unique to their frontend, but not duplicate the shared
queue or protocol phase.

Use the same design whenever multiple guest-visible surfaces expose one
device:

```text
frontend A ─┐
            ├─> one protocol/state owner ─> consumer
frontend B ─┘
```

Duplicated queues or flags eventually disagree during reset, partial writes or
unusual access ordering.

### Independent producers need independent layers

The LPT switch matrix accepts built-in/numeric input and configurable keymap
input. Those producers maintain separate bit layers; the LPT read boundary ORs
them together.

This matters when two sources hold the same switch. Releasing one source must
not release the other. The general rule is:

- record each producer's own state;
- combine at the device boundary;
- log a transition only when the combined guest-visible state changes.

Do not implement a high-level action by synthesizing another host input
sequence. For example, a configured letter must not fake digit presses and
Ctrl. Synthetic input shares modifier state with the physical keyboard and
breaks overlapping holds.

### Repeat, overlap and release are part of the design

For every input feature, define and test:

- repeated key-down events;
- release without a matching press;
- two keys held simultaneously;
- two keys mapped to the same destination;
- one destination held by two different input sources;
- shutdown or reset while an input remains held.

Edge-triggered actions such as coin pulses and level-triggered actions such as
matrix switches require different semantics. Do not infer one from the other.

### Cross-module interfaces

Declare internal cross-module APIs in `p2k-internal.h`. Keep functions `static`
when they have only one owner. Public board constants belong in
`pinball2000.h`.

An exported interface should:

- express a device operation, not expose storage;
- use P2K-prefixed names;
- document ordering, ownership or locking when non-obvious;
- avoid returning writable pointers unless direct access is the intended
  contract.

Do not add an exported global simply to avoid designing an owner API.

## Device implementation

### Guest-visible behavior

For port I/O and MMIO:

- define base, size and register constants close to the implementation;
- validate access width and endianness through `MemoryRegionOps`;
- keep read and write side effects explicit;
- return deterministic values for unsupported offsets;
- distinguish latches, inputs and outputs even when addresses overlap;
- document any value derived from observed software behavior.

Magic addresses in executable code need a named constant or a nearby
explanation of the guest structure they represent. A raw address plus a story
about an old investigation is not sufficient.

### Reset and persistence

Classify all state:

- immutable ROM/configuration;
- resettable device state;
- persistent guest-visible state;
- host-only runtime state;
- diagnostic counters.

Implement the relevant reset and exit behavior. Savedata writes should be
atomic where practical, respect `--no-savedata` and `--fresh`, and never be
silently redirected to a developer-specific path.

A new persistent field needs an old-file compatibility decision and a
fresh-state test.

### Time source

Choose the clock deliberately:

- `QEMU_CLOCK_VIRTUAL` for guest-time device behavior;
- `QEMU_CLOCK_HOST` or realtime only for host pacing or presentation where
  guest pauses must not define the interval.

Never replace a guest timer with `sleep()` in the QEMU main loop. Never put
wall-clock polling in a hot MMIO or I/O callback.

Timing changes must preserve the complete guest delivery path unless the
feature explicitly owns that replacement. Measure delivery, distribution and
tail latency; a correct mean can hide a watchdog-breaking worst case.

## Threads and callbacks

Threads are justified for blocking host work or sustained work that must not
occupy the QEMU main loop. They are not a generic performance switch.

Before starting a thread:

- initialize every mutex and condition variable it can reach;
- set its run and started state under the owning lock;
- define which fields are protected by which lock;
- define a fixed lock order when more than one lock is acquired;
- register shutdown and join a joinable thread;
- ensure reset cannot free or replace data still in use.

The display input path uses an AIO bottom half to move SDL-thread input into
the QEMU main-loop context. Use this pattern when a host worker must request a
QEMU/device action:

```c
aio_bh_schedule_oneshot(qemu_get_aio_context(), callback, payload);
```

Do not call arbitrary device or QEMU APIs from a worker merely because the
call appears thread-safe in one test.

Audio callbacks and display workers must not wait on guest progress. Avoid
unbounded work while holding a lock. Copy or snapshot data under the lock, then
perform expensive formatting, decoding or file I/O after releasing it when the
state model allows.

## Configuration and command-line options

`scripts/run-qemu.sh` owns the user-facing command line. `P2K_*` environment
variables are the bridge to the machine and an advanced escape hatch.

A new option must be completed in one change:

1. parser and validation in `run-qemu.sh`;
2. `--help` text with accepted values and default;
3. environment export or QEMU argument;
4. consumer-side validation;
5. `docs/03-cli-reference.md`;
6. the relevant subsystem guide;
7. a test for invalid input and at least one functional path.

Reject a missing option value before shifting arguments. Reject unknown enums
instead of quietly selecting a fallback. Resolve paths once in the wrapper
when host path semantics are intended.

Configuration files must either use a real parser or an explicitly documented
strict subset. Parse into temporary state and publish it only after the whole
file passes. Report line numbers and reject duplicates, trailing garbage and
unknown sections unless they are intentionally supported.

Defaults must be:

- useful without hidden setup;
- identical between help, documentation and code;
- validated on a fresh run;
- changed together everywhere they are described.

## Errors and logging

Use messages according to their effect:

| API | Use |
|---|---|
| `error_report()` | Requested operation cannot be performed or configuration is invalid |
| `warn_report()` | Recoverable degradation or ignored optional data |
| `info_report()` | Bounded lifecycle/configuration information |
| gated trace/counter | Repeated protocol, timing or diagnostic detail |

Messages should name the subsystem and the consequence. Prefer:

```text
pinball2000: invalid switch keymap PATH; custom A-Z bindings disabled
```

over:

```text
bad file
```

Do not print on every MMIO access, audio callback, frame, interrupt or loop
iteration in normal operation. High-frequency diagnostics must be:

- off by default;
- gated by a documented option or environment variable;
- bounded, sampled or summarized;
- cheap when disabled;
- clearly prefixed so tools can select them;
- excluded from benchmark runs unless that exact instrumentation is measured.

Do not log success before the operation is complete. Do not downgrade a
configuration error to a verbose-only message when the user needs it to
understand disabled behavior.

## Diagnostics, experiments and leftovers

Diagnostic code may remain when it is a reusable instrument. It must have:

- a clear question it answers;
- an opt-in gate;
- no guest mutation unless mutation is the declared experiment;
- bounded output and storage;
- a documented invocation;
- an off-state fast path;
- a removal or promotion decision when the investigation ends.

An experimental runtime mode must be labelled as such in help and docs. It
must not silently become the default solely because one benchmark looked good.
Promoting it requires gameplay and regression evidence, not only idle timing.

### Event-driven guest function observation

TCG can observe a stable guest function without modifying the guest and
without polling at translated-block boundaries.  When the x86 translator sees
the resolved guest PC, it can emit a QEMU helper into that one translated
block.  The helper then runs only when the guest executes the instrumented
function:

```text
load game image
→ resolve a validated machine-code signature
→ translate the block containing that PC
→ emit gen_helper_p2k_* only at that PC
→ call the host helper only when the guest function executes
```

This is different from the benchmark IRQ probe.  The benchmark temporarily
rewrites six guest bytes through GDB and restores them after measurement.  A
translation helper changes neither guest ROM nor guest RAM, but it does require
a small maintained patch in QEMU's target translator plus a `DEF_HELPER`
declaration and implementation.

One candidate use is observing
`Resource<unsigned long>::putValue(unsigned long)`.  Its relocation-masked
machine-code body is present in every preserved SWE1 and RFM game image from
1999 through 2025.  A helper could inspect the Resource argument and ignore
everything except the stable `IPAddr`, `IPMask`, and `GW IPA` resource keys.
That would be event-driven: no timer, periodic RAM scan, or callback at every
TB boundary.  Any implementation must fail closed when signature or resource
identity validation is ambiguous.

Remove before merge:

- unconditional debug `fprintf()` in hot paths;
- temporary dumps, WAV/PCM captures, screenshots and logs;
- hard-coded `/home/...` paths, usernames, hostnames or credentials;
- ROMs, update payloads, generated binaries and cache contents;
- one-off GDB/QMP scripts that have no reusable test role;
- commented-out alternative implementations;
- duplicate includes and unused flags;
- stale comments describing behavior the code no longer has;
- “temporary”, “quick hack” or “try this” branches without a current contract;
- copied binary instruction bytes when a maintained source or patch boundary
  exists;
- test bypasses that turn a failure into a pass.

Repository tests may create artifacts under a temporary directory and retain
them on failure when the path is reported. They must clean successful runs and
must never rely on an existing developer savedata directory.

`TODO` and `FIXME` are not substitutes for design. A retained marker must name
the missing behavior and the condition that would allow it to be completed.

## Source style

### C

Follow the surrounding QEMU style:

- four-space indentation, no tabs;
- opening brace on the next line for function definitions;
- braces around multi-line conditions and bodies;
- `p2k_` for exported functions and subsystem-specific prefixes for statics;
- `s_` for file-owned static state where that convention is already used;
- fixed-width types for guest-visible registers and serialized data;
- `size_t` for host buffer sizes;
- GLib/QEMU allocation helpers where the surrounding code uses them;
- one include per dependency, no duplicate or speculative includes;
- one responsibility per source file.

Keep functions short enough that ownership and exits are visible. Split
parsing, validation, state mutation and I/O instead of combining them in one
callback.

Comments explain an invariant, hardware meaning or non-obvious choice. They
must describe current code. Investigation history, abandoned alternatives and
claims about unrelated implementations do not belong in production comments.

### Shell

New shell scripts use:

```sh
set -euo pipefail
```

Quote expansions, use arrays for command arguments, validate before mutation,
and use `mktemp` plus a trap for transient directories. Avoid `eval`, parsing
human-formatted output when a stable interface exists, and writing generated
files into the source tree.

Extend the existing argument parser instead of adding a second pre-parser.
Cache expensive builds by source/configuration checksum, not only by a
human-readable version label.

### Python

Prefer `pathlib`, explicit timeouts, temporary directories and structured
subprocess arguments. Raise an error with the artifact path when a runtime test
fails. Parser logic should have fast unit tests; QEMU integration should have a
separate ROM-backed smoke test.

Tests must terminate the emulator on success and failure. Never leave a process
for the next test to discover.

### Documentation

Verify every behavioral statement against current code first. Documentation
describes what exists, why it is useful and how to use or validate it. It does
not preserve obsolete plans or narrate old failed approaches.

Avoid repeating a linked document's contents. State the local idea, then link
to details. Use GitHub admonitions for important operational distinctions:

```md
> [!IMPORTANT]
> A concise consequence the reader must not miss.
```

When a CLI option, default, mapping, supported version or file path changes,
update every current location in the same change. Search for the old value
before committing.

## QEMU upstream patches

Files under `qemu/upstream-patches/` are the only supported way to modify
upstream QEMU core files.

Each logical patch family:

- has one directory;
- appears in `upstream-patches/series`;
- has version compatibility encoded in its variant filename;
- must select exactly one zero-fuzz variant for the chosen QEMU release;
- keeps non-P2K behavior unchanged;
- is source-range validated with `scripts/internal/qemu-patch-series.py`;
- is compiled and boot-tested before a QEMU version is called supported.

Do not teach `build-qemu.sh` to edit upstream source with line-numbered `sed`,
ad-hoc text replacement or embedded raw source. The builder owns generated P2K
Meson/Kconfig files; versioned patches own upstream entry points.

Source compatibility is not runtime validation. A patch that applies across a
range may still change timing, display, audio or interrupt behavior.

Details: [`qemu/README.md`](../qemu/README.md).

## Validation requirements

Use the smallest test that can fail for the change, then add broader tests in
proportion to risk.

### Every change

```sh
git diff --check
```

Review the final diff, not only individual edits. Confirm no unrelated files,
generated artifacts or secrets are staged.

Run checks for every affected layer:

```sh
# Shell changes
bash -n scripts/run-qemu.sh scripts/build-qemu.sh

# Python/parser changes
python3 -m unittest discover -s scripts/tests -v

# QEMU machine, build integration or upstream-patch changes
scripts/build-qemu.sh
```

Documentation-only changes do not require rebuilding QEMU. They do require
checking every referenced path, command, option and default against the current
tree.

### Device, input or configuration changes

Boot with isolated state and exercise the actual event:

```sh
scripts/run-qemu.sh --no-savedata --update 210
```

Use a reusable QMP or console-script test when the state can be observed
without human interpretation. The configurable keymap smoke test is an example:

```sh
python3 scripts/tests/smoke-switch-keymap.py
```

Test both the normal QEMU display path and direct framebuffer path when input
or display routing changes.

### Audio, timing or threading changes

Test active gameplay or a scripted equivalent, not only boot and attract mode.
Exercise repeated coin, volume and overlapping sound commands. Check for audio
distortion, visual stalls, dropped commands and guest fatal errors.

Use the repository benchmark:

```sh
scripts/run-qemu.sh --bench
```

Compare like-for-like runs on the same powered host. Record steady-state IRQ
delivery and distribution plus PDB p50, p95, p99 and worst gap. A better mean
does not compensate for a new long tail.

Run the affected rows of the validation matrix, or all rows for shared DCS,
ROM-loading, savedata or machine-startup changes:

```sh
docs/measurements/validation-matrix/run-matrix.py --all-updates
```

### Cabinet-facing changes

Desktop emulation cannot certify physical hardware behavior. Verify inactive
levels, direction, reset behavior and timing without a powered playfield first.
Do not claim cabinet validation until the relevant path has been exercised on
the cabinet.

## Review checklist

A change is not ready until every applicable answer is yes.

### Architecture

- [ ] The owning module is identified.
- [ ] Existing dispatch paths were reused where possible.
- [ ] Runtime installation occurs from `pinball2000_init()` after dependencies.
- [ ] No P2K runtime behavior leaks into unrelated QEMU machines.
- [ ] Shared state has one owner and thin frontends.
- [ ] Independent producers cannot release or overwrite one another.
- [ ] Reset, shutdown and persistence behavior are defined.

### Concurrency and timing

- [ ] Callback context and thread ownership are known.
- [ ] Locks/conditions are initialized before handlers or workers are exposed.
- [ ] Lock order is fixed and shutdown joins workers.
- [ ] Hot paths contain no blocking I/O or unbounded logging.
- [ ] The chosen QEMU clock matches guest-time or host-time intent.
- [ ] Tail latency was checked for timing-sensitive work.

### Interface and failure behavior

- [ ] CLI, environment bridge and consumer agree on values and defaults.
- [ ] Invalid input fails clearly without partial application.
- [ ] Built-in behavior and overlapping inputs were regression-tested.
- [ ] User-visible messages state the consequence.

### Maintenance

- [ ] Names and style match the owning subsystem.
- [ ] Comments describe current invariants, not investigation history.
- [ ] No duplicate state, includes, parser, event handler or worker was added.
- [ ] Debug output is removed or deliberately gated and documented.
- [ ] No generated data, local paths, credentials or binary payloads are staged.
- [ ] Documentation and source map reflect the final code.
- [ ] The PR description matches the final defaults and behavior.

### Evidence

- [ ] Static checks pass.
- [ ] QEMU rebuilds with the machine registered.
- [ ] A runtime test exercises the changed path.
- [ ] Risk-specific smoke, benchmark or matrix tests pass.
- [ ] Remaining limitations are stated without converting them into claims.

> [!WARNING]
> Green compilation CI proves only that the selected QEMU builds and registers
> the machine. It does not replace ROM-backed behavior tests, gameplay, timing
> measurements or cabinet validation.

---

← [Documentation index](README.md) · [Architecture](10-architecture.md) ·
[`qemu/` source map](../qemu/README.md)
