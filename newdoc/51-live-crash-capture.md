# Capture a live guest crash

`tools/capture-live-crash.sh` preserves guest RAM and host-side QEMU state
without deliberately writing guest memory or terminating the emulator. Use it
when XINA has stopped in a fatal monitor, the display is frozen, or a rare
failure is still visible in a running process.

> [!IMPORTANT]
> Capture first, experiment second. Leave the failed QEMU process running and
> use a separate terminal. Restarting, closing the window or continuing guest
> input can destroy the most useful state.

The tool briefly pauses all QEMU threads while GDB attaches, dumps RAM and
collects host backtraces. It then detaches and QEMU resumes.

## What the capture proves

A successful artifact preserves:

- the complete guest RAM `MemoryRegion` exposed by this QEMU build;
- the QEMU command line, process identity and host mappings;
- the QEMU executable path and SHA-256;
- Encore commit and dirty-state flag when the checkout is discoverable;
- the capture-tool SHA-256;
- native host thread backtraces from the attached QEMU process;
- optional raw i386 disassembly at requested RAM offsets; and
- a SHA-256 manifest covering every file in the artifact.

It does **not** automatically preserve:

- the SDL window contents;
- serial output that was not already redirected to a log;
- guest CPU registers as a structured report;
- the XINU stack as decoded frames;
- persistent flash/NVRAM/SEEPROM files outside RAM;
- deterministic replay; or
- the physical cabinet state.

`gdb.txt` contains QEMU's **host** threads and native backtraces. Do not mistake
those frames for a decoded guest stack. XINA's fatal monitor or Encore's serial
diagnostics may print guest EIP/register information separately; retain that
log alongside the capture.

## Requirements

The host needs:

- `gdb`, `objdump` and `sha256sum` in `PATH`;
- permission to inspect the QEMU process with `ptrace`;
- the same user as the QEMU process in the normal case; and
- a QEMU executable with enough debug information for GDB to resolve
  `current_machine` and the RAM `MemoryRegion` internals.

Check the tool without touching a process:

```bash
tools/capture-live-crash.sh --help
```

> [!CAUTION]
> Do not globally weaken `ptrace` policy as a first troubleshooting step. Use
> the same account, confirm the exact target PID and inspect the host's current
> policy. A managed or hardened system may intentionally forbid attachment.

## Normal workflow

### 1. Keep the failing run alive

Do not launch a special timing mode merely to make capture possible. Reproduce
the problem with the normal user command and the exact game/update/savedata
policy under investigation.

When a rare failure matters, retain normal output from the beginning. For a
headless diagnostic run, for example:

```bash
scripts/run-qemu.sh \
  --game swe1 \
  --update latest \
  --no-savedata \
  --headless 2>&1 | tee /tmp/encore-run.log
```

This example is disposable because of `--no-savedata`. Choose the savedata
policy that matches the incident and record it; do not erase a user's failing
profile merely to simplify collection.

### 2. Open another terminal

If exactly one running process is an Encore `pinball2000` QEMU, automatic
selection is safest and simplest:

```bash
tools/capture-live-crash.sh
```

The tool scans `/proc`, requires `qemu-system-i386`, and confirms that its
machine argument begins with `pinball2000`. It refuses both zero matches and
multiple matches.

If several Encore runs exist, inspect them and select the exact QEMU PID:

```bash
ps -eo pid,ppid,lstart,args | grep '[q]emu-system-i386.*pinball2000'

tools/capture-live-crash.sh --pid 12345
```

> [!WARNING]
> A backgrounded `scripts/run-qemu.sh` PID is the launcher shell, not
> necessarily QEMU. Do not assume `$!` is a valid `--pid`; verify the process
> name and full machine argument. The tool rejects a launcher PID.

### 3. Name the artifact explicitly when preserving it

Without `--output`, the default is a new timestamped directory under `/tmp`.
For evidence that must survive reboot, choose a new path on persistent storage:

```bash
tools/capture-live-crash.sh \
  --pid 12345 \
  --output /var/tmp/encore-crashes/swe1-rare-fatal-001
```

The parent must already exist. The destination must not exist: the tool refuses
to merge with or overwrite an earlier capture. It creates the directory mode
`0700` under an `077` umask.

### 4. Add only relevant disassembly ranges

If the fatal report provides a candidate address, request a bounded range:

```bash
tools/capture-live-crash.sh \
  --pid 12345 \
  --output /var/tmp/encore-crashes/swe1-rare-fatal-001 \
  --disassemble 0x227f3a:0x80 \
  --disassemble 0x24be80:0x100
```

The length defaults to `0x100` when omitted. The option is repeatable.

The disassembler treats the address as an offset in the physical RAM dump and
labels it with the same numeric value. It does not translate paging, prove that
the address contains code or resolve function names. Pinball 2000's normal
flat mapping makes many game-code addresses useful directly, but always check
the crash context and exact update.

Use the matching symbol table for named lookups:

```bash
python3 tools/sym_dump.py \
  updates/pin2000_50069_0210_10312025_B_10000000/50069/\
pin2000_50069_0210_symbols.rom \
  --addr 0x227f3a
```

## Artifact layout

A capture with requested disassembly contains:

```text
capture/
├── README.txt
├── SHA256SUMS
├── command-line.txt
├── gdb.txt
├── guest-disassembly.txt
├── guest-ram.bin
├── host-maps.txt
├── metadata.txt
└── process.txt
```

| File | Meaning |
|---|---|
| `guest-ram.bin` | complete RAM region, with offset zero corresponding to guest physical address zero |
| `gdb.txt` | attach transcript, RAM pointers/size and native QEMU thread backtraces |
| `guest-disassembly.txt` | raw i386 decoding for requested ranges only |
| `metadata.txt` | capture time/host, PID, kernel, QEMU/tool hashes, repository identity and RAM size |
| `command-line.txt` | flattened QEMU argument vector captured from `/proc` |
| `process.txt` | PID, parent, user, start/elapsed time, state and resource snapshot |
| `host-maps.txt` | QEMU host virtual-memory mappings |
| `SHA256SUMS` | integrity hashes for all files above, including optional disassembly |

Verify the artifact before analysis or transfer:

```bash
cd /var/tmp/encore-crashes/swe1-rare-fatal-001
sha256sum -c SHA256SUMS
```

The QEMU executable itself is not copied. Its hash is stored in
`metadata.txt`, so another investigator can identify or rebuild the exact
binary without the manifest depending on its original absolute path.

## After capture

The tool prints `Capture complete` only after GDB has detached, the RAM dump is
non-empty, the optional disassembly has completed and the local manifest has
been written. Return to the original run and decide whether to:

- preserve a screenshot or the already-running serial log;
- capture a second range into a **new** directory;
- continue a controlled observation; or
- close QEMU normally.

Do not use the post-attach run for timing measurements. Stopping every QEMU
thread to dump RAM introduces an intentional host pause and invalidates any
interval spanning the capture.

If GDB fails, the tool leaves the newly created directory and points to
`gdb.txt`. It does not kill QEMU. Preserve that error, fix the permission or
symbol problem, and retry into a different output directory.

## Privacy and sharing

> [!WARNING]
> `guest-ram.bin` is not a harmless log. It may contain saved game state,
> operator settings, network addresses, tournament credentials, player data or
> other guest strings. `command-line.txt`, mappings and host paths can also
> identify the machine or user.

Before sharing:

1. keep the original artifact private and immutable;
2. verify its manifest;
3. decide whether the recipient genuinely needs full RAM;
4. create a separate redacted derivative rather than editing the original;
5. record every removed/replaced file and hash the derivative; and
6. never add ROM/update payloads or a crash artifact to Git by default.

A short disassembly, exact update hash, fatal serial excerpt and metadata may
be sufficient for public triage while the full RAM stays private.

## Troubleshooting

| Symptom | Check |
|---|---|
| no running process found | QEMU may already have exited, or the selected process is not the Encore machine |
| several processes found | select the exact QEMU process with verified `--pid` |
| PID is not QEMU | use the child `qemu-system-i386` PID, not the launcher shell |
| permission denied attaching | confirm ownership, dumpability and host `ptrace` policy |
| `current_machine` or RAM fields unknown | rebuild/use the Encore QEMU with adequate debug information |
| destination exists | choose a new directory; captures never overwrite |
| no RAM image | inspect `gdb.txt`; QEMU remains running unless it failed independently |
| misleading disassembly | verify address mapping, game/update identity and that the range is code |

For fatal patterns and first-response triage, see
[Troubleshooting](04-troubleshooting.md). For timing evidence that does not
pause the process, use the observers and normal benchmark described in
[CPU, PIT and IRQ0 timing](12-cpu-and-timers.md).

---

[Documentation](README.md) · [Troubleshooting](04-troubleshooting.md) · [Testing and validation](26-testing-validation-matrix.md)
