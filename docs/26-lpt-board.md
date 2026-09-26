# 26 — LPT driver-board interface

Pinball 2000 uses a three-port parallel interface for its playfield driver
board. Encore can model that protocol in software, pass it to Linux `ppdev`,
overlay safe keyboard closures on a real board, or expose deliberate failure
targets. DCS sound is a separate device and never travels through this LPT
path.

```text
guest DATA/STATUS/CTRL at 378h
               │
               ▼
        LPT policy and trace
          │            │
          │            └── disconnected open bus / no device
          ▼
   emulated state machine  ←── optional keyboard switch layers
          │
          └─────────────── or Linux ppdev / physical board
```

The exact I/O placement is also listed in the
[memory map](13-memory-map.md). Host-key assignments and AT-keyboard routing
are documented separately in [Desktop controls](41-cli-keyboard-guide.md).

### Implementation owners

| Concern | Primary source |
|---|---|
| board selection, protocol, ppdev and input router | `qemu/p2k-lpt-board.c` |
| configurable A–Z matrix bindings | `qemu/p2k-switch-keymap.c` |
| i8042 connect/disconnect and installation order | `qemu/pinball2000.c` |
| public selection and validation | `scripts/run-qemu.sh` |
| `lp` group/runtime preparation | `scripts/internal/runtime-packages.sh` |
| ROM-backed switch smoke | `scripts/tests/smoke-switch-keymap.py` |
| cadence and PDB05 measurement | `qemu/p2k-timing-audit.c`, `scripts/internal/bench-qemu.py` |
| temporary kernel open-bus fixture | `tools/test-disconnected-vport.sh` |

## Selecting the board source

The default is `--lpt-device auto`.

| Selection | Guest-visible result | Failure/fallback policy |
|---|---|---|
| `auto` | recognized physical board, otherwise software board | scans accessible `/dev/parport0` through `/dev/parport31`; silently unsuitable ports are skipped |
| `emulated` | software protocol and keyboard-backed cabinet | never opens a physical port |
| `required` | recognized physical board | exits if the scan finds none; never falls back |
| `/dev/parportN` | authoritative raw physical connection | open/claim failure is fatal; an unrecognized or silent cable remains connected for guest diagnosis |
| `disconnected` | installed three-port open bus | every read is `0xff`, every write is discarded |
| `none` | no guest LPT region | diagnostic only; the game cannot reach its switch matrix and is not expected to boot |

With `--game auto`, a recognized physical board can select SWE1 or RFM from
two deliberately narrow switch signatures. A forced game remains forced; a
mismatch only warns. An explicit physical path that produces no recognized
signature defaults automatic game selection to SWE1 but preserves raw
passthrough instead of concealing the cable/board failure behind emulation.

On a normal desktop with no recognized board, `auto` reports the miss and
continues with `emulated`. Use explicit `emulated` when it is important that a
run never touches host parallel hardware.

> [!WARNING]
> Auto-detection is an active hardware probe: it claims accessible ppdev
> devices and issues driver-board write/read cycles before game ROM loading.
> Treat a connected powered playfield as live hardware. Use `emulated` for
> laptop/desktop testing and follow the powered-cabinet procedure in
> [Real LPT passthrough](46-real-lpt-passthrough.md).

## Guest port contract

The default region is `0x378–0x37a`; `--lpt-ioport` moves all three bytes.
XINA also knows the conventional `0x278` and `0x3bc` bases. Encore accepts
another non-zero address for investigation but warns that the guest may never
probe it.

| Base offset | Name | Software-board behavior |
|---:|---|---|
| `+0` | DATA | writes replace the data latch; ungated reads echo it; gated reads return the selected input/status value |
| `+1` | STATUS | reads return the default driver-board signature `0x87` |
| `+2` | CONTROL | writes drive edge detection; reads echo the last control byte |

Only byte accesses are implemented. The guest creates a transaction through
two control edges:

1. CONTROL bit 2 rises: capture the current DATA byte as the opcode;
2. the guest writes the request data;
3. CONTROL bit 0 falls: dispatch the captured opcode with current DATA.

When CONTROL bits 0 and 3 are both set, a DATA read is routed to the input
function named by the captured opcode. Otherwise DATA remains an ordinary
latch/echo. Physical passthrough forwards DATA, STATUS and CONTROL directly;
CONTROL bit 5 is additionally mirrored through `PPDATADIR` so reverse reads
sample the external board.

The Linux physical path opens the character device read/write, claims it with
`PPCLAIM`, requests IEEE-1284 compatibility/SPP mode on a best-effort basis,
and releases it on exit. Explicit ppdev passthrough is Linux-only.

## Software-board commands

The model implements the command shapes the current guest exercises. Output
and input state are deliberately separate, so an illuminated lamp cannot be
fed back as a phantom closed switch.

| Opcode | Direction | Current semantic effect |
|---:|---|---|
| `0x00` | read | physical group 8: coin-slot contacts |
| `0x01` | read | physical group 10: flippers/actions and coin-door interlock |
| `0x02` | read | high-nibble status `0xf0` |
| `0x03` | read | physical group 9: service/menu controls and the bounded Enter pulse |
| `0x04` | read | selected 8×8 playfield-matrix row |
| `0x05` | write | select/strobe a one-hot matrix column; record one PDB05 timing event |
| `0x06`, `0x07` | write | retain output data used by the following row/control operations |
| `0x08` | write | select and retain one of eight lamp/output rows |
| `0x09–0x0d` | write | update the guest-observed auxiliary flags and control bits |
| `0x0f` | read | return retained flag/control bits |
| `0x10`, `0x11` | read | alternate bit-6 ready/busy data between `0xff` and `0x00` |
| `0x12`, `0x13` | read | return zero |

The ready/busy alternation matters: returning a permanently ready value makes
the guest interpret stale auxiliary slots as new switch transitions. The
software model retains all eight lamp rows for diagnostics, but it does not
simulate coils, lamp brightness or playfield mechanics.

The emulated coin door starts closed so normal play is enabled. Cabinet keys
change contact state, not high-level game state: a coin contact does not
guarantee a credit, and Start does not force the guest into a game.

## Input layers

There are two independent software matrix layers:

- built-in cabinet controls and the numeric `NN` + Ctrl selector;
- strict YAML A–Z bindings loaded once during machine initialization.

Their row bits are ORed. Per-switch hold counts keep overlapping bindings
correct: releasing one of two keys mapped to the same switch does not reopen
the contact until both have been released. Repeated key-down events are
deduplicated.

The default keymap is created at
`$XDG_CONFIG_HOME/encore/switch-keymap.yaml` (normally
`~/.config/encore/switch-keymap.yaml`) unless `--switch-keymap PATH` selects
another file. Its accepted grammar is a deliberately strict YAML subset:

```yaml
switches:
  a: 13
  x: 28
```

Only one indented A–Z key and a two-digit matrix number whose digits are each
1–8 are accepted per entry. Tabs, duplicate letters, extra top-level content,
trailing text or an empty map reject the complete file; Encore never installs
a valid-looking partial subset. Restart after editing because the map is not
reloaded live.

The full key table and cabinet/XINA keyboard toggle are in
[Desktop controls](41-cli-keyboard-guide.md).

### From a contact to a game action

The desktop Start controls close standard matrix switch 13. Internally its
column is retained in slot 1 because the board protocol's one-hot decoder is
one-based; moving it to array slot 0 would make the guest scan a different
contact.

Seeing that closure proves the LPT/input path, not that the guest accepted a
new game. Pricing/credits, DCS readiness, Slam Tilt, trough/device audits and
game-specific state still participate. The repository's
`scripts/demos/start-game.p2k` uses real coin and switch closures, then queries
`game info`; `m_players 1` is the reliable acceptance signal for its current
SWE1 workflow. See [Console scripting](42-console-scripting.md) before turning
a repeated Start mash into a protocol assertion.

## Physical-only and hybrid input

Pure physical mode makes the external board authoritative. Keyboard cabinet
closures are blocked, while F1/F2/F3 remain host quit/display/capture actions.
Tab can connect XINA's AT keyboard but does not enable software cabinet
switches.

`--lpt-input hybrid` is accepted only with `auto`, `required` or an explicit
ppdev path, and only takes effect when a physical board is actually selected.
All guest writes, outputs, keepalive and base reads still hit the real board.
For gated input reads, Encore then clears the active-low bits corresponding to
keyboard closures. This can add a closure but cannot reopen a switch already
closed by the cabinet.

The physical coin-door interlock remains authoritative in hybrid mode, so F4
is intentionally ignored. Other configured/numeric cabinet closures remain
available. If `auto` falls back to software emulation, hybrid state is cleared
and ordinary emulated input is used.

> [!IMPORTANT]
> Hybrid input is an experimental convenience, not electrical validation. It
> cannot certify inactive levels, direction changes, keepalive behavior or
> safe output wiring on a real cabinet.

## Host access preparation

For selections that may use ppdev, runtime preparation checks membership of
the Linux `lp` group. With an available privilege helper it can add the
runtime user, then re-enter the launcher once through `sg lp` so the refreshed
membership applies without logging out. An explicit `/dev/parportN` must also
be an existing character device and readable/writable in the active session.
Printer-class `/dev/usb/lpN` nodes are not ppdev register interfaces and are
not accepted by the launcher.

The QEMU side refuses to fall back after an explicit physical request. Common
failures are a missing `ppdev` device, another process holding `PPCLAIM`, or
device permissions that do not match the active login session. See
[Troubleshooting](04-troubleshooting.md) before changing the guest model.

## Diagnostics and measurement

`--lpt-trace FILE` opens an existing-parent output path in append mode and
flushes one line for every guest read/write:

```text
1790022615.959396 R 00=00
1790022616.009683 R 01=87
1790022616.009884 W 00=04
```

The first field is host wall-clock seconds with microsecond precision; offsets
are relative to the selected LPT base. Tracing runs before backend dispatch,
so it also records physical and disconnected traffic.

> [!CAUTION]
> This trace is intentionally exhaustive and synchronous. A three-second
> software-board boot produced about 94,000 lines (2.4 MiB) on the validation
> host. Do not enable it during timing measurements; bound the run and inspect
> a copy instead of leaving an append target active indefinitely.

F12 prints compact door/control state plus ordered lamp and switch rows 1–8.
It is useful for checking key overlap and retained outputs without enabling a
per-access trace.

The timing observer exposes three software-model counters: DATA writes,
CONTROL writes and dispatched transactions. Each dispatched opcode `0x05`
also feeds the PDB05 cadence distribution. `--bench` reports `DATA/s`,
`PDB05/s`, percentiles and worst gaps from an unpatched normal run. Those
values describe guest protocol service, not electrical latency inside a
physical driver board; pure physical mode bypasses the software counters.

## Verification checklist

For an emulated-board change:

1. rebuild the custom QEMU graft;
2. run `scripts/tests/smoke-switch-keymap.py` to exercise overlapping layers,
   repeated key-down handling and all-or-nothing parsing;
3. inject held cabinet and numeric switches through QMP and inspect F12 rows;
4. use a short trace to verify DATA/CONTROL edge order and signature reads;
5. run the normal benchmark without tracing and inspect PDB05 cadence/tails;
6. test `auto`, `emulated`, `disconnected` and `none` as separate policies.

For a physical change, additionally verify explicit-path failure, automatic
game identification, direction changes and input polarity on unpowered test
equipment before connecting a powered playfield. This documentation refresh
host had no `/dev/parport*`, so current physical behavior is code-reviewed but
not recertified on hardware here.

On a Linux lab host with no existing parallel-port configuration,
`tools/test-disconnected-vport.sh` can load a temporary `parport_pc` port at
ISA `0x278`, expose it through ppdev with no board attached, launch the
explicit physical path, then unload the temporary modules on exit. It refuses
to alter a host that already has a parallel-port configuration. This tests the
kernel/ioctl path and cable-failure diagnosis; it still does not validate real
switches or outputs.

A deterministic desktop run that cannot touch physical LPT hardware is:

```bash
scripts/run-qemu.sh --game swe1 --update 2.10 \
  --lpt-device emulated --no-savedata
```

For current controls see [Desktop controls](41-cli-keyboard-guide.md); for the
full cross-mode test plan see the
[Validation matrix](26-testing-validation-matrix.md).

---

← [DCS sound](25-dcs-sound.md) ·
[Documentation index](README.md) · [Validation matrix](26-testing-validation-matrix.md)
