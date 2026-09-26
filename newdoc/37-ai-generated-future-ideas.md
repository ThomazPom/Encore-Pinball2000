# From ideas to evidence

Encore does not maintain a separate list of “AI-generated future features.”
The active engineering priorities, owners and completion conditions live in
the [roadmap](36-roadmap.md). This page defines how any speculative idea—human
or machine-generated—can earn a place there.

> [!IMPORTANT]
> An idea is not a requirement, a supported feature or evidence that the
> current implementation is deficient. Its author does not change the burden
> of proof.

This distinction matters in an emulator. A plausible workaround can easily
hide the real guest contract, manufacture timing, bypass original firmware or
create another permanent mode that nobody can validate.

## The promotion path

An idea moves through these states:

```text
observation → scoped hypothesis → isolated experiment → retained evidence
                                              ↘ reject/archive
retained evidence → roadmap candidate → implementation review → supported path
```

Skipping a state creates historical confusion. In particular, a branch that
boots once is still an experiment; it is not a roadmap commitment.

### 1. Observation

Record the smallest factual problem statement:

- exact game, update and savedata policy;
- Encore commit and dirty state;
- host/QEMU identity;
- normal user command or reproducible script;
- raw symptom and timestamp;
- what the baseline does; and
- what remains unknown.

“Audio seems slow” is not enough. “SWE1 2.10, clean state, default engine,
named cue X begins Y ms after the guest command in three retained captures” is
an observation that another maintainer can test.

### 2. Scoped hypothesis

State one predicted owner and one falsifiable mechanism. Name what the idea
must **not** change. Examples:

- a display optimization may not change guest-visible framebuffer contents;
- a host scheduling experiment may not create or accelerate guest PIT ticks;
- a savedata repair may not merge two writers' state;
- a physical-I/O feature may not claim electrical safety from an emulator
  fixture; and
- a network convenience may not expose historical guest services by default.

If several mechanisms could explain the observation, split them into separate
experiments rather than building one large “fix.”

### 3. Isolated experiment

Use a branch or a local patch with an explicit expiry condition. Keep the
public CLI unchanged unless exercising the experiment genuinely requires an
opt-in selector.

The experiment description must include:

| Field | Required content |
|---|---|
| Claim | one sentence describing the expected improvement |
| Baseline | current main commit and exact command |
| Variant | branch commit and exact command |
| Workload | normal, scripted, loaded, physical or synthetic |
| Metrics | distributions and health signals, not one attractive number |
| Safety boundary | data, timing, network and hardware actions excluded |
| Success | threshold that would justify roadmap review |
| Failure | result that archives the experiment |
| Artifacts | raw logs, reports, hashes and environment identity |

> [!WARNING]
> Do not tune against a broken observer. A measurement helper must first prove
> that it resolves the intended symbols, workload and update. The symbol-table
> and loaded-bench corrections documented during the tournament audit are an
> example of why this gate exists.

### 4. Matched evidence

Run baseline and variant under the same conditions. For timing work, compare at
least:

- effective guest speed;
- IRQ delivery ratio and interval distribution;
- PDB distribution and long-gap windows;
- maximum IRQ nesting depth;
- minimum XINU IStack margin;
- DCS health;
- host CPU cost; and
- crashes, assertions and guest progress.

For other subsystems, use the corresponding contract in
[Testing and validation](26-testing-validation-matrix.md). A shorter targeted
test may diagnose a mechanism; it does not replace the release matrix.

### 5. Disposition

Every experiment ends in one of three states:

- **promote** — evidence supports adding a roadmap item with an owner and
  completion condition;
- **retain as forensic tooling** — the mechanism is not a product feature, but
  its observer or reproducer is still useful; or
- **archive/reject** — preserve the conclusion and evidence, then remove the
  public selector and avoid describing it as pending work.

Git history is the archive. Active user documentation should not carry every
abandoned implementation alternative indefinitely.

## Current branch evidence

The branch inventory on 2026-09-26 illustrates the rule:

| Branch family | Current interpretation |
|---|---|
| `cleanup/strict-only-irq0` | integrated product direction: one natural IRQ0 path |
| `experiment/hotloop-deadline` | timing experiment; not a second supported delivery mode |
| `audit/main-irq0-observer` | reusable observation work, represented by current forensic tooling |
| `experiment/host-menu` | isolated UX experiment without an accepted product requirement |
| `investigate/rfm-0200-crashes` | investigation history; diagnostics survive only where still generally useful |
| `exp/pub-card-emulation` | explicitly partial experimental hardware model |
| `experiment/xuart-network-auto` and related network branches | concepts represented by the newer optional-network implementation and guide |
| old Unicorn/hotloop branches | rejected or superseded execution models, retained as history rather than feature candidates |

Branch names are not status authority. Compare each unique diff with current
main before archiving it: useful tests and observers may have survived even
when the delivery mechanism did not.

## Disposition of the former idea bank

The previous page was explicitly speculative. Its ideas are not silently
deleted; current code and the accepted roadmap give them these dispositions:

| Former idea | Evidence-based disposition |
|---|---|
| session recorder/replay | console scripts, screenshots, timed audio and video capture cover repeatable inputs and selected outputs; deterministic whole-session replay and a single archive format do not exist |
| Cabinet Lab | LPT observers, emulated fixtures and the Debian installation lab exist; powered physical qualification remains roadmap work |
| on-screen telemetry | timing, DCS and LPT telemetry exists in logs and reports, not as a supported overlay |
| host `--doctor` | launcher/installer preflights cover individual requirements; the roadmap owns a future non-destructive asset audit, not a promised all-in-one command |
| update/ROM inspector | extraction, assembly, symbol and analysis tools exist; there is no supported `--inspect-update` or launcher `--dry-run` |
| named machine profiles | the cabinet installer stores one explicit session configuration, but generic hidden option bundles are not a current product goal |
| savedata slots/history | clean, fresh and explicit savedata paths exist; concurrency locking, durability and retained history remain roadmap work |
| projection calibration | no current calibration workflow is supported; ordinary SDL scaling is not equivalent evidence |
| DCS laboratory | scripted command/audio capture and offline sound tools cover focused investigations, not a complete interactive laboratory |
| failure bundle | live crash capture preserves debugger evidence without killing QEMU; a general privacy-reviewed issue archive is not implemented |
| cabinet kiosk | Cage/Weston sessions, systemd ownership and maintenance recovery are implemented by the cabinet installer, subject to the documented hardware boundary |
| configurable controls | YAML desktop switch maps are implemented and smoke-tested |
| USB cabinet bridge | no current implementation or validation evidence |

This table is a status map, not a resurrected feature queue. An unimplemented
row still needs a present observation and success condition before it can enter
the roadmap.

## Where current ideas belong

Use the roadmap category that owns the risk:

| Idea type | Current owner |
|---|---|
| release/licensing/provenance | roadmap Priority 0 |
| savedata locking and durability | roadmap Priority 1 |
| asset verification | roadmap Priority 1 |
| timing/PDB investigation | roadmap Priority 1 and [CPU/timers](12-cpu-and-timers.md) |
| physical cabinet behavior | roadmap Priority 1 and [real LPT](46-real-lpt-passthrough.md) |
| sound fidelity | roadmap Priority 2 and [DCS sound](25-dcs-sound.md) |
| optional networking | roadmap Priority 2 and [networking](48-network.md) |
| CLI/mode removal | roadmap Priority 3 |

If an idea fits none of these, write the observation and acceptance condition
first. Do not create a parallel wishlist merely to hold it.

## Proposal template

Use this in an issue, branch note or retained experiment report:

```markdown
# Proposal: <short factual name>

## Observation
<reproduction, baseline commit, inputs, raw artifact links>

## Hypothesis
<one falsifiable mechanism and predicted owner>

## Experiment
<smallest isolated change; exact baseline/variant commands>

## Safety and non-claims
<data, timing, network, hardware and compatibility boundaries>

## Success and rejection criteria
<metrics and thresholds chosen before the run>

## Result
<matched table plus raw evidence paths>

## Disposition
<promote, retain as forensic tooling, or archive/reject>
```

An AI can help enumerate mechanisms, locate code owners, generate a test or
challenge a conclusion. It cannot replace measured guest behavior, physical
cabinet evidence, source provenance or maintainer judgment.

---

[Roadmap](36-roadmap.md) · [Known limitations](35-known-limitations.md) · [Documentation](README.md)
