# 51 — Live crash capture

When Encore is wedged or XINA has entered a fatal monitor, preserve the live
state before closing the emulator:

```sh
tools/capture-live-crash.sh
```

The tool locates a single running Encore `qemu-system-i386`, briefly attaches
GDB, dumps guest RAM and host thread backtraces, then detaches. It never kills
QEMU and issues no guest-memory write command.

If several Encore instances are running, select one explicitly:

```sh
tools/capture-live-crash.sh --pid 12345
```

Add repeatable guest disassembly ranges when useful:

```sh
tools/capture-live-crash.sh \
  --disassemble 0x227f3a:0x80 \
  --disassemble 0x24be80:0x100
```

By default the new evidence directory is created under `/tmp`. Use an existing
parent directory for permanent evidence:

```sh
tools/capture-live-crash.sh \
  --output /path/to/evidence/crash-YYYYMMDD-HHMMSS
```

The destination must not already exist. It is created mode `0700` and contains:

- `guest-ram.bin` — complete guest RAM;
- `SHA256SUMS` — hashes of guest RAM and the executing QEMU binary;
- `gdb.txt` — GDB attachment details, QEMU threads and short backtraces;
- `metadata.txt` — capture time, host, kernel, executable and Encore commit;
- `process.txt`, `command-line.txt`, `host-maps.txt` — host process context;
- `guest-disassembly.txt` — requested disassembly ranges, when supplied.

Requirements and limits:

- `gdb`, `objdump` and `sha256sum` must be installed;
- normal Linux ptrace permissions apply, so capture as the QEMU owner;
- the QEMU executable must retain the symbols used to locate machine RAM;
- attaching GDB pauses emulation briefly, so this is forensic tooling rather
  than routine recording.

After capture, preserve the emulator log alongside the directory when its
stdout/stderr destination is available in `metadata.txt`.

---

← [Troubleshooting](04-troubleshooting.md) · [Documentation index](README.md)
