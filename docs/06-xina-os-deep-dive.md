# XINA and XINU inside the guest

XINA is the Pinball 2000 system software shipped inside each game/update image.
It runs in the emulated x86 machine; Encore does not replace its scheduler,
resource database, shell, network stack or game tasks.

This page is for debugging guest behavior. For the emulated hardware around
it, start with [Architecture](10-architecture.md). For the timer contract, see
[CPU, PIT and IRQ0 timing](12-cpu-and-timers.md).

> [!IMPORTANT]
> “XINA”, “XINU” and “Encore” are different owners. XINU supplies the kernel
> primitives, XINA adds the Pinball 2000 platform/services, game code supplies
> rules, and Encore supplies the hardware environment in which all of that runs.

## What is preserved

Every update bundle carries four components used to construct the guest flash:

- boot data;
- the initial image/flash region;
- game code, including XINA/XINU code linked for that release; and
- a symbol table.

The exact XINA build therefore follows the selected game/update. Do not apply
SWE1 2.10 addresses to RFM, a base ROM or another update. Resolve symbols from
the matching `*_symbols.rom` and retain the exact component hashes.

```bash
python3 tools/sym_dump.py \
  updates/pin2000_50069_0210_10312025_B_10000000/50069/\
pin2000_50069_0210_symbols.rom \
  --lookup 'clkint(void)' \
  --lookup 'resched(void)' \
  --lookup 'Fatal(char const *,...)'
```

For SWE1 2.10 in the current tree, those names resolve to `0x0022c4c6`,
`0x0023b218` and `0x0024ae38`. They are evidence for that exact image, not an
ABI promised across releases.

## Boot ownership

The simplified sequence is:

```text
BIOS and board discovery
  → XINA/XINU initialization
  → persistent resource recovery
  → game-code startup
  → optional network startup and background services
  → attract/game tasks
```

Encore makes enough of the PC, PRISM, CMOS/flash, display, audio, UART and
driver-board surfaces visible for the original path to proceed. Guest errors
during this sequence often identify an emulation contract, but the error text
still comes from XINA or game code.

Resource recovery is particularly important. BAR2 stores persistent CMOS-like
state; BAR3 stores the staged update flash. Empty or malformed state can leave
XINA's resource table absent, causing repeated `Retrieve Resource` NonFatal
messages or preventing game startup. See [Savedata](09-savedata.md) and
[ROM/update loading](15-rom-loading.md) before diagnosing such output as a CPU
failure.

## Scheduler and clock

XINU installs IRQ0 as `clkint`. One accepted PIT interrupt advances the guest
by one fixed tick and can drive sleep queues, scheduling and time-based game
work. `resched`, `resume`, `nulluser` and the process table remain guest code.

Encore's default timing path delivers the natural QEMU PIT/PIC interrupt. It
does not call XINU scheduling routines directly and does not manufacture missed
ticks to catch the guest up after a host delay.

This makes three observations distinct:

| Observation | Meaning |
|---|---|
| PIT edge raised | the emulated timer requested IRQ0 |
| `clkint` entered | the guest accepted and began handling it |
| guest progress | scheduling/game work continued after the handler |

A host can raise interrupts while the guest delays acceptance; a guest can
enter nested handlers and exhaust its interrupt stack; or scheduling can stop
despite the QEMU process remaining alive. The benchmark therefore reports
delivery, interval/PDB distributions, maximum `clkint` depth and minimum XINU
IStack margin rather than treating QEMU liveness as proof of guest health.

> [!WARNING]
> Guest-load probes temporarily patch RAM to observe named XINU routines. They
> are forensic workloads, not normal emulation and not proof that the same
> addresses apply to another update.

## Fatal and NonFatal output

XINA distinguishes recoverable diagnostics from fatal monitors:

- `NonFatal` lines can describe optional hardware, bad resources or a condition
  the guest continues past;
- `Fatal` can stop normal guest progress and enter the monitor; and
- `StackFatal`/`IStackFatal` identify process or interrupt-stack failure paths.

The message is a starting point, not ownership proof. Correlate it with:

- exact game/update and savedata policy;
- the preceding UART lines;
- current EIP/flags and task/scheduler observations;
- IRQ depth and IStack margin;
- device diagnostics around the event; and
- whether the same workload survives on the current default path.

If the failed process remains alive, follow [Live crash capture](51-live-crash-capture.md)
before closing it.

## Serial shell and keyboard

COM1 carries the XINA/XINU console. In a headless diagnostic run it is attached
to the terminal; it can instead be exposed through the launcher's TCP UART
mode. The UART is bidirectional unless deliberately silenced.

For an interactive console in the current terminal:

```bash
scripts/run-qemu.sh --game swe1 --update latest --serial
```

For a localhost TCP console that another process can retain or automate:

```bash
scripts/run-qemu.sh --game swe1 --update latest \
  --uart-tcp 127.0.0.1:1234
nc 127.0.0.1 1234
```

Start with `help` or `?`: the command inventory belongs to the selected guest
build. Common commands observed in preserved builds include `ps`, `mem`,
`fatal`, `nonfatal`, `dcs`, `pdb` and `sleep 10`, but their presence and syntax
are not a cross-version API. The normal benchmark uses the guest's `sleep 10`
as one scoped wall-time check.

The emulated AT keyboard is normally presented as disconnected because cabinet
keys belong to the driver-board path. `Tab` switches desktop input into **XINA
KEYBOARD** mode; from then on ordinary function keys belong to XINA until the
mode is toggled back. See [Desktop controls](41-cli-keyboard-guide.md).

Do not expose an interactive XINA console to an untrusted network. It is a
historical maintenance surface, not a hardened modern remote shell.

## Optional volatile guest extensions

`--guest-extensions` does not rewrite ROM files. After the selected game emits
its `XINA:` startup banner, Encore identifies supported code patterns in live
RAM, installs a small payload, registers a `setip` shell command, and restores
the intercepted `netstart` prologue before entering the original routine.

`--setip <ip> <mask> <gateway>` additionally writes the three normal XINA
resources at startup. The extension checker currently classifies 24 preserved
updates as structurally supported and two early images as pre-network.

> [!NOTE]
> This is an explicit compatibility extension, not a claim that the original
> software contained Encore's command. A run without the option executes the
> unmodified guest path.

See [Optional networking](48-network.md) for topology and exposure rules and
`guest-extensions/README.md` for the payload ABI.

## Reading guest state safely

Prefer a progression from least intrusive to most intrusive:

1. retain ordinary UART and device diagnostics;
2. use lightweight timing snapshots or the normal benchmark;
3. resolve names through the exact symbol table;
4. capture a live failed process without writing guest memory;
5. use a temporary RAM probe only when the question cannot be answered
   observationally.

Attaching GDB pauses QEMU. Patching guest RAM changes the experiment. Neither
result can be mixed into a timing window without re-arming and settling after
the debugger detaches.

## Deliberate boundaries

Encore does not aim to:

- provide a source-level reimplementation of XINA;
- guarantee identical internal addresses across releases;
- make every XINU shell command a supported public API;
- repair arbitrary guest scheduler or game-code defects automatically;
- infer physical cabinet safety from guest diagnostics; or
- turn historic network/console services into Internet-facing services.

The preservation goal is narrower: run the original software against a
measured hardware model, keep compatibility interventions explicit, and retain
enough evidence to distinguish guest, emulator and host failures.

---

[Architecture](10-architecture.md) · [Troubleshooting](04-troubleshooting.md) · [Documentation](README.md)
