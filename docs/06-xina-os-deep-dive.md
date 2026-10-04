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

## Historical identity and software layers

The preserved game images identify the platform literally as **“XINA - Xina
Is Not Apple”**, identify the underlying kernel as **XINU V7**, and credit Tom
Uban with the concept, architecture and XINA system design, alongside Graham
West's graphics/XINA design credit. An older PinRepair field reference,
republished by the [GerWiki Pinball 2000 XINA
page](https://www.gerwiki.de/xina/p2k), describes XINA as an
application/platform layer built over PC-XINU, the small operating system
associated with Douglas Comer. It also preserves command research credited in
part to Jim Hicks.

The historical account says PC-XINU was attractive as a compact multithreaded
embedded base that Williams could adapt for real-time work, with licensing
terms more compatible with the product than the kernel changes a Linux route
would then have required. That explains the architecture; it does not mean the
preserved game contains an off-the-shelf, unmodified PC-XINU release. XINA and
the linked game add the device, resource, service and game layers visible here.

This chapter incorporates the durable technical content of that field
reference so the external page is provenance, not a required part of Encore's
manual. The raw page consulted on 2026-10-04 had SHA-256
`42940493dd16a4d5cf469ebee3c3258730d74e935e953f4c83a51f77aaeda370`.
It remains a historical secondary source: it combines RFM XINA 1.18,
physical-PC procedures and later community networking notes. Its claims below
are labelled or corrected whenever the selected guest, symbols, decompilation
or current Encore implementation provide stronger evidence.

```text
RFM / SWE1 game rules and game-specific shell commands
                              │
                              ▼
       XINA platform: resources, devices, managers, shell,
          update services, diagnostics and network daemons
                              │
                              ▼
       XINU kernel: processes, priorities, queues, semaphores,
             sleep/timer queues, memory and interrupt entry
                              │
                              ▼
          Encore's emulated x86 / PRISM / cabinet hardware
```

This layering explains why a shell command may reveal several owners at once.
For example, `ps` is a XINA command exposing XINU process state; `pdb` asks
XINA's driver-board service about a device Encore implements; `game info`
prints state owned by the linked game code.

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

### The typed resource system

The symbols expose a typed `Resource<T>` framework rather than an unstructured
settings file. Preserved instantiations include booleans and integers as well
as pricing, earnings, high-score, timestamp, audit, error and game-specific
records. Constructors register identity/default data; accessors retrieve or
lock values; `putValue`/`atomicPut` paths update them.

That one framework connects several apparently unrelated surfaces:

- operator adjustments, pricing, audits and high scores;
- network address, mask and gateway;
- recorded hardware/game errors;
- persistent time and player/game records; and
- startup recovery from CMOS-like storage.

`resources` and `reslist` inspect the live registry. `cmos`, `cmos_buffer`,
`bootdata` and `rtc` reach storage or boot-adjacent state and require more
care. A clean `--no-savedata` run can legitimately report a corrupt or empty
fatal/nonfatal database even while the guest otherwise runs: that observation
belongs to the disposable persistence policy, not automatically to CPU or
device emulation.

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

### Processes, priorities and memory in practice

XINU uses a fixed-capacity process table, not one host thread per guest task.
In both verified builds, `conf` reports room for 130 processes and 200
semaphores. Those are table capacities: the RFM snapshot had 25 live entries
and reported a lifetime high-water mark of 34, while the other slots remained
free.

`ps` exposes the scheduler model directly. Each row includes a process ID,
name, state, priority, stack address range, used/allocated process-stack bytes,
waited semaphore and pending message. A normal snapshot can contain:

- `curr` for the shell executing the command;
- `ready` for runnable work, including the priority-zero `prnull` idle task;
- `sleep` for timed waits;
- `wait` for semaphore waits; and
- `recv` for a task waiting for a message.

The observed names cross the software layers: kernel maintenance
(`grimreaper`), XINA services (`sys_clock`, `Update mgr`, `lampmgr`, `dispmgr`)
and game tasks all share the same scheduler. Their priorities and states are a
transient snapshot, not a fixed boot contract.

> [!IMPORTANT]
> The stack use printed by `ps` is each **process stack**. It is not XINU's
> shared **interrupt stack (IStack)**. A healthy-looking list of 8 KiB process
> stacks does not rule out nested IRQ0 entries exhausting the IStack; use the
> benchmark's maximum `clkint` depth and minimum IStack margin for that question.

In the RFM 1.50 sample, `mem stat` reported 4 MiB of guest RAM and separated
text, data, BSS, allocated process stacks, heap and remaining memory. That
number is a XINU policy, not the size of Encore's physical RAM backing:
`sizmem()` returns the raw value `0x400` for 4 MiB in RFM 1.50 and 1.60,
`0x800` for 8 MiB in RFM 1.80, then `0x400` again in every preserved RFM
build surveyed from 1.90 through 2.60. See
[RFM 1.80's 8 MiB boundary](50-game-changelogs.md#why-rfm-180-needs-8-mib)
for the exact binary evidence.

Commands such as `sem`, `queue` and `timerq` reveal the corresponding
synchronization and timed-wait structures. Together these are useful for
answering “what is blocked or consuming memory now?”; they do not by
themselves prove why an interrupt was late or nested.

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

The prompt is `%`. This is a XINA command shell, not DOS, Unix or a general
host shell. Use lower-case command names and start with `help` or `?`; entering
a command without its required arguments normally prints that build's usage.
The inventory and syntax belong to the selected guest image, not to Encore.

On an original cabinet, the documented access path was a 5-pin DIN PC/AT
keyboard in the game PC and a terminal connected to COM1 at 9600 baud, 8 data
bits, no parity and one stop bit. Keyboard detection could place the game in
shell mode; the serial link could capture the same diagnostics. Encore keeps
those two guest-facing ideas but replaces the physical setup with desktop key
routing and selectable terminal/TCP serial backends.

### Verified command inventories

On 2026-09-26, read-only normal boots captured `help` and `conf` from two exact
local bundles:

| Guest | Reported XINA | Commands | XINU table sizes reported by `conf` |
|---|---|---:|---|
| RFM 1.50 (`0150_07252000`) | 1.18, built 2000-06-26 | 99 | 130 processes, 200 semaphores, 59 devices |
| SWE1 2.10 (`0210_10312025`) | 1.38, built 2024-09-27 | 87 | 130 processes, 200 semaphores, 59 devices |

RFM 1.50's complete live inventory was:

```text
? attack_mars audio bitmap bootdata bpool bs clear cmos cmos_buffer conf
continue credit dcs deffmgr devs dgstat dipsw dispmgr diverter down drive
droptgt dump echo enter errors escape ether eventlog exit fatal fb flags
flapgate flip flipramp fupdate game gx help history hstd httpd ifstat igmp
info kevents kill lamp lampmgr leffmgr lockpost loops martians mem midas mon
multi net netstat nonfatal nslookup pal pdb pinevents ping pool price_current
price_dyn price_table printout ps pty pub queue ramps rasys reboot replay
reslist resources route routes rtc scenemgr sem sleep stack start switch term
time timerq up updtmgr vdai zc zombie
```

SWE1 2.10 exposed the same set except for these 12 RFM-specific mechanism or
scene commands:

```text
attack_mars diverter droptgt flapgate flipramp lockpost loops martians pal
ramps rasys scenemgr
```

This proves both why the historical list is useful and why it must not be
presented as a universal XINA API. Even a newer XINA can expose fewer commands
when the linked game does not register RFM's objects.

### XINA 1.18 field-command atlas

This is the local, normalized replacement for the historical online command
reference. It covers the complete 99-command RFM 1.50 inventory above without
pretending that the set or syntax is stable across games. The descriptions
combine four evidence levels:

- **field** — the historical XINA 1.18 reference and original-cabinet usage;
- **live** — `help`, usage or output observed in a preserved update;
- **static** — matching symbols and decompiled guest code; and
- **Encore** — behavior seen against the current emulated hardware.

Names and short forms are normalized to lower case. Brackets indicate an
optional argument; a slash separates alternatives. Before using a modifying
form, ask that exact guest for its own usage and use disposable savedata.

#### Shell, XINU kernel and memory

| Command | Useful forms and actual role | Evidence or correction |
|---|---|---|
| `?`, `help` | List the commands registered by this game image. | Live inventories are authoritative; the 99-command claim is RFM 1.50-specific. |
| `clear` | `clear cmos`; clears persistent CMOS state. | The old reference called the function unknown despite documenting the destructive operand. |
| `continue`, `exit` | Continue or terminate a command script. | Script control, not host-shell job control. |
| `echo`, `history` | Echo script/console text; show recently entered commands. | Console conveniences. |
| `conf`, `devs` | Show compiled XINU table limits and registered device slots. | A device-table entry proves registration, not successful emulation or initialization. |
| `dump` | `dump <address> [count]`; read guest memory. | Raw forensic primitive; a wrong address can still fault the guest. |
| `kevents` | Toggle logging for resource, process, semaphore and hook events. | Diagnostic state changes can create substantial serial load. |
| `kill`, `zombie` | Terminate a PID; `zombie <pid>` forces the selected process into XINU's zombie path. | Static code resolves the historically “unknown” `zombie`; both can destroy essential tasks. |
| `mem` | `stat`, `free`, per-PID usage and allocation listings. | Reports XINU's managed memory, not host RSS. |
| `mon`, `reboot` | Enter the monitor/reboot path; `mon` emits the more extensive diagnostic path. | Both interrupt normal gameplay. |
| `pool`, `bpool` | `pool stat` summarizes fixed-block pools; `bpool` reports buffer-pool block sizes, counts, semaphore, current use and high-water use. | Both “unknowns” are resolved by static code. |
| `ps` | Show each XINU process, state, priority, stack range/use, semaphore and message. | Process stacks are not the shared interrupt stack. |
| `pty` | `pty stat [N]`; inspect pseudo-terminal slots used by network/console services. | It does not describe the physical UART alone. |
| `queue` | `queue sleep/ready`; dump XINU's sleep or ready queue links. | Resolved from `x_queue_dump`; observational unless the guest is already corrupt. |
| `sem` | Show semaphore table/state. | Pair with `ps` and queues when diagnosing a wait. |
| `sleep` | Delay the current script/shell task by guest ticks. | Not a host sleep and not a general timing benchmark. |
| `stack` | `stack history`; inspect recorded process-stack events/history. | Does not replace Encore's IStack margin probe. |
| `term` | Toggle terminal behavior including output, caps/control handling and swapping. | Changes the guest console path and can make the shell appear lost. |
| `time`, `timerq` | Show guest date/time; dump XINU's timer queue through `tqwrite`. | Static code resolves the historically “unknown” `timerq`. RTC persistence is documented under [savedata writes](09-savedata.md#when-files-are-written). |

#### Persistence, errors, audits and commercial state

| Command | Useful forms and actual role | Evidence or correction |
|---|---|---|
| `bootdata` | Select/view current, ROM or flash boot image, optionally verifying it. | Boot-selection mutation; not an ordinary information command. |
| `cmos`, `cmos_buffer` | View headers/utilization; flash, reset or disable storage paths depending on the command. | These operate on XINA's persistent stores and can invalidate the evidence under study. |
| `errors`, `fatal`, `nonfatal` | Show summarized, fatal and recoverable error records. | A recorded guest error does not by itself assign fault to Encore. |
| `eventlog` | Dump, flush, count or classify event-log buffers. | `flush` is destructive; capture raw output first. |
| `flags` | List local, global or static game flags. | Game-state observation, not CPU flags. |
| `hstd` | Print high-score-to-date tables. | Reads game/accounting records. |
| `price_current`, `price_dyn`, `price_table` | Inspect current coin value, dynamic pricing and the price/coin table. | Read-only historical forms as documented; values are backed by native resources. |
| `printout` | Emit audits, adjustments, high scores, hourly/daily data, pricing and error reports. | Serial equivalent of operator/report output. |
| `replay` | Inspect buckets/checks or reset/add/boost replay state. | Several forms mutate awards and replay history. |
| `reslist`, `resources` | Dump the typed resource registry and active resource state. | Core tools for proving native setting names and persistence. |
| `rtc` | `rtc dump`; show RTC registers. | Encore now models the RTC register/calendar contract and persistent year; see [savedata writes](09-savedata.md#when-files-are-written). |
| `vdai` | `vdai info`; report whether the audit/accounting interface is enabled and healthy, plus readout, abort and error counters. | Static code resolves another historical “unknown”; it is not a generic video diagnostic. |

#### Display, sound and effect managers

| Command | Useful forms and actual role | Evidence or correction |
|---|---|---|
| `audio` | Initialize or inspect audio policy; adjust quiet/min/default/max/current volume. | Platform audio policy above the DCS device itself. |
| `bitmap` | Show main allocation and waste information for bitmap memory. | Guest graphics-memory accounting. |
| `dcs` | Play a track/raw command, change track volume/pan, inspect signals/version, quiet or reset DCS. | Directly exercises the sound path; see [DCS sound](25-dcs-sound.md). |
| `deffmgr` | List display effects/names, inspect an entry, toggle debug/logging or unrequest an effect. | Decompiled `DeffManager` code resolves the old “unknown” label. |
| `dispmgr` | List displayables/locks, inspect an entry, enable/disable the manager and inspect/clear rendered-frame counters. | Decompiled `DisplayManager` code confirms this is the composition/render manager. |
| `fb` | Clear/test the framebuffer, draw bars/borders/pillars, inspect vsyncs, flip or change H/V sync. | Hardware-facing and stateful; `fb flip` was the field technician's glass-off display flip. |
| `gx` | Dump MediaGX/CX5520 configuration-register groups and IDs. | Original-chipset diagnostic; emulation fidelity determines the returned values. |
| `info` | Show the linked game's flipper information/display diagnostic. | `x_info` calls `show_flipper_info_deff`; the historical “function unknown” was misleading. |
| `lamp` | Test/set lamp, blink, effect and mask duties; dump layers; configure saver timing. | Can energize physical outputs on real/passthrough hardware. |
| `lampmgr` | List lamp matrices and toggle manager debug/log/activation. | Manager-level view above individual `lamp` writes. |
| `leffmgr` | List lamp effects/names, toggle debug/logging or unrequest an effect by address. | Static manager code resolves the historical “unknown”. |
| `updtmgr` | List or request manager updates spanning scenes/backgrounds/sound/music. | “Update” here is runtime content scheduling, not ROM installation. |

#### Cabinet, game and RFM-specific mechanisms

| Command | Useful forms and actual role | Evidence or correction |
|---|---|---|
| `attack_mars` | Start/stop the RFM mode or manipulate its ramp/multiball/mode flags. | RFM development/practice command, not a generic XINA service. |
| `bs` | Enable/disable Ball Search or its debugging. | Static `BallSearch` calls resolve the historical “unknown”. |
| `credit` | Initialize, inspect or decrement current credits. | Mutates normal pricing/game state. |
| `dipsw` | Show DIP-switch value. | Hardware/configuration observation. |
| `diverter`, `droptgt`, `flapgate`, `flipramp`, `lockpost`, `martians` | `info`, `debugon`, `debugoff` for named RFM mechanisms. | Game-specific mechanism objects; not present in SWE1 2.10's command set. |
| `down`, `enter`, `escape`, `start`, `up` | Inject the corresponding coin-door/operator action. | Software actions routed inside the guest; do not confuse them with every physical switch contact. |
| `drive` | Select outputs 0–47, list them, switch them off or toggle high-power drive. | Potentially dangerous on physical hardware. |
| `flip` | Assign player/computer control, enable/disable or drive flippers, with debug support. | Direct playfield behavior, not screen flipping (`fb flip`). |
| `game` | Inspect/name/collect state or trigger tilt/game-over paths. | State-changing debug forms can bypass natural switch sequences. |
| `loops`, `ramps` | Inspect or debug the corresponding RFM shot/mechanism groups. | The old reference left their purpose unknown even though the object names and forms constrain it. |
| `multi` | List multiball/multi-state flags. | Game-state diagnostic. |
| `pal` | Toggle an RFM `pal` facility. | Purpose remains unresolved; no stronger claim is made from the two-word interface alone. |
| `pdb` | Show Power Driver Board status and faults. | Exercises Encore's emulated PDB contract; output is stronger than command presence. |
| `pinevents` | Enable/disable logging for game lifecycle, multiball/audit, ball-time, tilt and update-event families. | A tracing switchboard, not a command that directly synthesizes all those events. |
| `rasys` | Toggle debugging for the RFM ramp/award subsystem. | The exact expansion remains unproven; it is game-specific and omitted from SWE1 2.10. |
| `scenemgr` | Inspect/select/start/stop/reset RFM scenes and award shots/switches. | Development control over game modes; see the Question Mark example below. |
| `switch` | Inspect callbacks/timers/counters; enable test/trace; break on a switch number. | Diagnostic controls can materially alter timing and log volume. |
| `zc` | Run `ZeroCrossDebug`, reporting the zero-cross subsystem state. | Static code resolves the historical “unknown”; it concerns lamp/AC timing, not CPU zero flags. |

#### Network, update and external protocols

| Command | Useful forms and actual role | Evidence or correction |
|---|---|---|
| `ether` | Show Ethernet interface statistics. | Requires an initialized NIC for meaningful data. |
| `dgstat` | Dump up to 16 XINU datagram endpoints with device, local/foreign ports, mode, transport and peer address. | Static code resolves the historical “unknown”; this is UDP endpoint state. |
| `fupdate` | Load firmware over COM1/COM2 at a chosen baud rate; enable/disable the updater. | Firmware path, not the normal Pinball 2000 game-update installer. |
| `httpd` | List configured hyperlinks or HTTP-server statistics. | Command presence does not prove that the daemon is running or reachable. |
| `ifstat` | Show a selected interface, such as `ifstat 1`. | Encore uses its output only as a fallback to learn XINA's active address in automatic NAT. |
| `igmp` | Join or leave an IPv4 multicast group. | Multicast behavior is separate from the unicast TTL correction. |
| `midas` | Inspect/enable a serial accounting protocol, monitor/debug it, send ACK/NACK/RVI/EOT, and simulate cash-door, denomination/token and high-score records. | Guest hooks tie it to coin-door, coin-collection and high-score events; it was not actually “unknown”. |
| `net` | Start networking, toggle packet monitoring or select an interface. | **Never use a second `net start` to reconfigure a running stack**; save resources and reboot. |
| `netstat` | Show live guest network endpoints/state. | An empty valid table is not a NIC failure. |
| `nslookup`, `ping` | Exercise name resolution or ICMP reachability. | `nslookup` needs a valid native DNS resource; ping success does not validate UDP/JTS. |
| `pub` | Dump game or sound address ranges through the Prism Update Board service. | Low-level update-board forensic surface; addresses and side effects are build-dependent. |
| `route`, `routes` | Add/delete a route; display the routing table. | The historical add form includes destination, mask, gateway, metric and TTL. |

> [!IMPORTANT]
> “Observational” does not mean harmless in every build. Manager dumps may take
> locks, network/status commands may touch devices, and verbose traces can
> perturb the timing being diagnosed. On a real or passthrough cabinet,
> output-driving commands can also energize hardware.

> [!NOTE]
> The largest improvements over the historical page are not cosmetic. The
> number 99 is now scoped to one measured build; twelve RFM-only commands are
> separated from XINA proper; many formerly “unknown” facilities are given
> code-backed roles; process stacks are distinguished from the IRQ IStack; and
> network TTL, duplicate `net start` behavior and command/device liveness are
> described from captures rather than inferred from command names.

The live samples also illustrate important interpretation limits:

- `ps` reports process stack ranges and use, not the separate IRQ IStack
  margin measured by Encore's benchmark;
- `devs` listed `ETHER`, UDP/TCP and PTY device slots even with no active
  network backend, so table presence is not proof of a working NIC;
- `netstat` and `routes` returned valid empty tables without networking; and
- `pdb` reported an emulated board and fuse state, while `dcs version` still
  reported that a version was unavailable—command existence is not device
  success.

The normal benchmark uses the guest's `sleep 10` only as one scoped wall-time
check; it does not treat the shell as a stable automation ABI.

### Game-specific commands can expose preserved development paths

The shell is also a window into game code that was not intended as an ordinary
operator interface. RFM 1.50's `scenemgr` can inspect, select, start, stop and
reset its scene objects. The historical XINA 1.18 reference lists 12 of them,
including `Question Mark` at index 7; for example, its documented forced-start
form is `scenemgr resetall stopall start 7`. That is a real RFM command and
scene, not a feature supplied by Encore.

SWE1 has a separate `QuestionMarkScene`. Its class, start/end code, display
effects, audio records and audit strings already exist in the preserved 1.50
game. A distinct tournament/community update named
`pin2000_50069_0200_02262016_B_10000000.exe` is historically attested as a
rewritten 1.30 build made to expose that “Questionmark Mission” for testing.
Its updater has not yet been recovered, but the build identity and stated
purpose should not be confused with a hypothetical Encore patch.

There is also a separate myPinballs SWE1 2.0 dated 2025. Its published notes
say that four extra sample modes were added for testing, and its locally
preserved binary scene table corroborates the description: compared with
1.50, it changes Question Mark's availability metadata to the same values used
by the other three late sample scenes—Destroyer Droid, Hover Tank and Watto's
Chance.

> [!NOTE]
> Two unrelated updates therefore carry the version `2.00`: the missing 2016
> tournament/community Questionmark test build and the preserved 2025
> myPinballs release. The later binary independently provides strong evidence
> for Question Mark as the fourth test scene. Neither fact proves that the
> inherited scene was a fully finished mission. A preserved project report
> records the opposite for SWE1 2.10: forcing the normal scene selector to
> index 13 produced an immediate score and `JEDI` letter over a black screen,
> with no playable rules or presentation. Code presence, test availability and
> complete player-facing rules are therefore three different claims. See the
> [game-code changelog](50-game-changelogs.md#question-mark-and-community-test-scenes)
> for the version-by-version evidence.

> [!WARNING]
> On a real or passthrough cabinet, `drive`, `lamp`, `switch`, mechanism and
> firmware commands can affect physical outputs or persistent state. Do not
> experiment with them on a powered playfield. Even under emulation, commands
> such as `net start`, `kill`, `fupdate`, `bootdata` and CMOS reset can create
> misleading failures or destroy the state being investigated.

### Historical AT-keyboard function keys

The XINA 1.18 field reference documents this original keyboard layer:

| Key delivered to XINA | Historical action |
|---|---|
| `F1` | help |
| `F2` | flip screen |
| `F3` | toggle shell |
| `F4` | Start |
| `F5` | Escape/service-panel action |
| `F6` | Down |
| `F7` | Up |
| `F8` | Enter |

Encore normally begins in **CABINET KEYS**, where its host/cabinet mapping owns
those keys differently. Press `Tab` to enter **XINA KEYBOARD** mode; plain
function keys then reach the guest. In that mode, `Alt+F1`, `Alt+F2` and
`Alt+F3` preserve Encore's quit, flipscreen and screenshot actions. This
separation avoids confusing a historical XINA key table with Encore's default
desktop controls. See [Desktop controls](41-cli-keyboard-guide.md) for the
complete mapping.

Do not expose an interactive XINA console to an untrusted network. It is a
historical maintenance surface, not a hardened modern remote shell.

### Network services are guest processes

The shell inventory includes `ether`, `net`, `ifstat`, `netstat`, `route`,
`routes`, `ping`, `nslookup`, `igmp` and `httpd`. XINU's device table reserves
Ethernet, UDP, 16 datagram endpoints, TCP, 16 TCP endpoints and 16 PTYs. These
are real guest facilities, not host commands proxied by the launcher.

The historical cabinet recipe required the explicitly supported **SMC EtherEZ
SMC8416T**, a 16-bit ISA 10-Mbit/s card in the spare motherboard slot. One
later forum excerpt says `8415T`; the field reference, the guest's own output
and Encore's device target agree on `8416T`, so the former is treated as a
typo. Some physical motherboards also needed onboard audio disabled to avoid
an IRQ conflict. Those BIOS and ISA installation steps describe original
hardware; they are not Encore setup requirements.

Once configured with static address, mask and gateway resources, the original
stack could provide Telnet shell and HTTP administration. The preserved field
credentials were username `Pin2000` and password `Manager`, case-sensitive and
shared across cabinets. That is evidence of the era's trusted-LAN model, not a
safe modern default. Never expose either service to the Internet; Encore's
`--http-port` deliberately binds to host loopback unless the user asks for a
different forward.

The field guide's wireless recipe simply put an Ethernet-to-wireless bridge
between the cabinet and router. Later JTS instructions added UDP/TCP port 2069
rules and an OpenWrt/DD-WRT firewall rule that rewrote the cabinet's IP TTL to
64. Community discussion correctly localized a routing problem but sometimes
called TTL a packet-size setting and did not establish the guest's exact
value.

Encore closed that evidence gap. Packet captures showed unicast UDP leaving
XINA's common `udpsend()` path at **TTL 1**: correct for a flat LAN, but consumed
by Slirp's one router hop before NAT. Under a Slirp-backed network only, Encore
therefore changes the unique live-RAM instruction from 1 to 64 before
`netstart`. Update files, savedata and multicast TTL behavior remain unchanged.
This replaces the old router firmware/firewall workaround and applies to DNS
and other unicast UDP as well as JTS. Tournament traffic has an additional
reply-port peculiarity documented in [Tournament/JTS](49-tournament-server.md);
it must not be confused with the stack-wide TTL defect.

Finally, never use a second `net start` to apply new addresses. Preserved
builds create another set of network processes rather than reconfiguring the
first, which has reproduced an IStack failure. Save the native resources and
reboot instead; see [Optional networking](48-network.md) for current topology,
security and validation details.

## Optional volatile guest extensions

`--guest-extensions` does not rewrite ROM files. After the selected game emits
its `XINA:` startup banner, Encore identifies supported code patterns in live
RAM, installs a small payload, registers `setip` and `setdns` shell commands,
and restores the intercepted `netstart` prologue before entering the original
routine.

`--setip <ip> <mask> <gateway>` additionally writes the three normal XINA
network resources at startup. Independent `--dns <address>` writes the native
`DNSIPA` resource without changing those three values. The command
`--tournament <ip> [on|off] [no-free]` writes the native `TS_IPA`, `GmTour`
and `CrdFPl` resources; it does not supply a server or COM2 reader. The
extension checker requires unique resource constructors, verifies the IP and
Yes/No type relationships, and currently classifies 24 preserved updates as
structurally supported and two early images as pre-network.

> [!NOTE]
> This is an explicit compatibility extension, not a claim that the original
> software contained Encore's command. A run without the option does not add
> that command or the startup resource wrapper. Independently, an actual
> Slirp-backed NIC triggers the narrow automatic `udpsend()` TTL patch needed
> for routed UDP; passt and bridge leave that code unchanged.

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
