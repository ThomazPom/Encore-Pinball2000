# Private serial updater bridge

Some legacy Linux GUI updaters enumerate serial ports through
`/proc/tty/drivers`, then request an exclusive kernel lock. A PTY bridge must
keep its master open, so that lock survives application shutdown and makes the
next connection fail with `EBUSY`.

This tool creates two private PTYs as a transient user service. Its clean
`LD_PRELOAD` compatibility shim exposes one synthetic serial entry, redirects
that entry to the updater-side PTY and acknowledges `TIOCEXCL` **only** for
that PTY.

The updater writes its XINA shell command with a Unix LF, closes the port, then
waits about two seconds before reopening it for binary transfer. XINA's native
receive timeout is a CPU loop: an unthrottled QEMU vCPU exhausts it before that
desktop-application handoff completes. The shim therefore holds only a write
which begins with the exact `fupdate load com1 ` prefix and ends in a lone LF.
When the first binary write arrives, it sends the command as CRLF, consumes
XINA's echoed command line, allows the measured 50 ms receiver transition to
finish, then forwards the original binary write unchanged. Later packet
writes pass straight through, and the shim does not log serial traffic.

Start Encore from one terminal:

```sh
bridge=tools/serial-updater-bridge/serial-updater-bridge.sh
scripts/run-qemu.sh --game swe1 --update auto \
  --serial-device "$("$bridge" guest-device)"
```

Launch an external Linux updater from another terminal. Keep its executable
and accompanying libraries outside this repository:

```sh
tools/serial-updater-bridge/serial-updater-bridge.sh run \
  /absolute/path/to/the/updater
```

Select `/dev/full` in that application. It is only the synthetic name visible
inside the launched process; Encore never opens the real `/dev/full` device.
The bridge and updater run as the desktop user and require no root access.

Stop the service explicitly when finished:

```sh
tools/serial-updater-bridge/serial-updater-bridge.sh stop
```

The shim is rebuilt below `$XDG_CACHE_HOME/encore/serial-updater` when its C
source changes. Runtime PTYs and the synthetic driver list live below
`$XDG_RUNTIME_DIR/encore-serial-updater` and naturally disappear at logout or
reboot.
