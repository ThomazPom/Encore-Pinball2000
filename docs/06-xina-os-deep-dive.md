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
West's graphics/XINA design credit. The
[Gerwiki Pinball 2000 XINA page](https://www.gerwiki.de/xina/p2k) republishes an
older PinRepair field reference that describes XINA as an application/platform
layer built over PC-XINU, the small operating system associated with Douglas
Comer.

The historical account says PC-XINU was attractive as a compact multithreaded
embedded base that Williams could adapt for real-time work, with licensing
terms more compatible with the product than the kernel changes a Linux route
would then have required. That explains the architecture; it does not mean the
preserved game contains an off-the-shelf, unmodified PC-XINU release. XINA and
the linked game add the device, resource, service and game layers visible here.

Treat that page as a historical secondary source. It is valuable for command
names and original-cabinet context, but it combines RFM XINA 1.18, physical-PC
procedures and later community networking notes. Claims in this guide are
therefore retained only when the selected guest, symbols or current Encore
implementation corroborate them.

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

### Command map

| Area | Primarily observational examples | Commands with obvious state/effect risk |
|---|---|---|
| kernel/runtime | `conf`, `ps`, `mem stat`, `sem`, `queue`, `timerq`, `stack history`, `devs`, `pty stat` | `kill`, `zombie`, arbitrary `sleep` in scripts |
| errors/audits | `errors`, `fatal`, `nonfatal`, `eventlog stats`, `hstd`, `printout` | reset/flush forms offered by individual commands |
| resources/storage | `resources`, `reslist`, selected info/view forms | `cmos ... reset`, `bootdata`, `fupdate` |
| display/audio | `gx`, `bitmap info`, `dcs version`, manager list/info forms | `fb`, DCS reset/raw/volume commands, manager on/off/debug forms |
| cabinet hardware | `pdb`, `dipsw`, switch counters/status | `drive`, `lamp`, switch test/break and mechanism commands |
| game | `game info`, price/high-score/replay info | `credit`, `pinevents`, `attack_mars`, `scenemgr`, replay mutation |
| network | `ether info`, `netstat`, `routes`, `httpd stats` | `net start`, route changes, IGMP join/leave and exposed services |
| raw forensics/update | bounded `dump` of a known address | arbitrary memory reads, `pub ... dump`, firmware/update operations |

“Observational” does not mean harmless in every build: ask the command for its
usage first, use disposable savedata, and do not assume a subcommand named
`info` avoids all device traffic.

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

The historical source describes HTTP and Telnet administration and shared
factory credentials. That is evidence of the era's trust model, not a setup
recommendation. Keep services localhost-only unless a specific trusted-LAN
test requires otherwise. Also, never use a second `net start` to apply new
addresses: preserved builds create another set of network processes rather
than reconfiguring the first, which has reproduced an IStack failure. Save the
resource change and reboot instead; see [Optional networking](48-network.md).

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
