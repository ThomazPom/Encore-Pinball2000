#!/usr/bin/env python3
"""Boot Encore, run a XINU sleep test, and summarize live timing diagnostics."""

from __future__ import annotations

import re
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
RUNNER = ROOT / "scripts" / "run-qemu.sh"
MARKER = b"__P2K_BENCH_DONE__"
WARMUP_MARKER = b"__P2K_BENCH_WARM__"


def field(line: str, name: str, default: str = "n/a") -> str:
    match = re.search(rf"(?:^|[ |]){re.escape(name)}=([^ |]+)", line)
    return match.group(1) if match else default


def last_line(lines: list[str], needle: str, extra: str = "") -> str:
    return next(
        (line for line in reversed(lines) if needle in line and extra in line), ""
    )


def max_field(lines: list[str], needle: str, name: str) -> str:
    values: list[int] = []
    for line in lines:
        if needle not in line:
            continue
        match = re.search(rf"(?:^|[ |]){re.escape(name)}=(\d+)", line)
        if match:
            values.append(int(match.group(1)))
    return str(max(values)) if values else "n/a"


def connect_console(port: int, process: subprocess.Popen[bytes], timeout: float) -> socket.socket:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Encore exited before XINU became ready (status {process.returncode})")
        try:
            sock = socket.create_connection(("127.0.0.1", port), timeout=1.0)
            sock.settimeout(1.0)
            return sock
        except OSError:
            time.sleep(0.2)
    raise RuntimeError("timed out connecting to Encore's XINU console")


def wait_for(sock: socket.socket, token: bytes, timeout: float, wake: bool = False) -> bytes:
    deadline = time.monotonic() + timeout
    received = bytearray()
    next_wake = 0.0
    while time.monotonic() < deadline:
        if wake and time.monotonic() >= next_wake:
            sock.sendall(b"\r")
            next_wake = time.monotonic() + 1.0
        try:
            chunk = sock.recv(65536)
        except socket.timeout:
            continue
        if not chunk:
            raise RuntimeError("XINU console disconnected")
        received.extend(chunk)
        if token in received:
            return bytes(received)
    raise RuntimeError(f"timed out waiting for XINU response {token!r}")


def pick_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def requested_speed(arguments: list[str]) -> float:
    for index, argument in enumerate(arguments):
        if argument == "--speed-target" and index + 1 < len(arguments):
            return float(arguments[index + 1])
    return 100.0


def main() -> int:
    forwarded = sys.argv[1:]
    target_speed = requested_speed(forwarded)
    port = pick_port()
    artifact = Path(tempfile.mkdtemp(prefix="p2k-bench-"))
    log_path = artifact / "encore.log"
    command = [
        "bash", str(RUNNER), *forwarded,
        "--no-savedata", "--uart-quiet",
        "--uart-tcp", f"127.0.0.1:{port}", "-v",
    ]

    print(f"[bench] artifact={artifact}", flush=True)
    print(f"[bench] launching: {' '.join(command)}", flush=True)
    with log_path.open("wb") as log:
        launch_started = time.monotonic()
        process = subprocess.Popen(
            command, cwd=ROOT, stdin=subprocess.DEVNULL,
            stdout=log, stderr=subprocess.STDOUT, start_new_session=True,
        )
        try:
            with connect_console(port, process, 30.0) as console:
                wait_for(console, b"%", 45.0, wake=True)
                # Advance a full 30 seconds of *guest* time before measuring.
                # HOTLOOP resets its cumulative jitter statistics at this
                # boundary, and subsequent rolling audit windows no longer
                # contain firmware/DCS initialization behavior.  Guest-time
                # warmup remains correct even on a host whose delivery is
                # slower than wall time.
                console.sendall(b"sleep 30\recho __P2K_BENCH_WARM__\r")
                wait_for(console, WARMUP_MARKER,
                         max(120.0, 45.0 * 100.0 / target_speed))
                boot_wall = time.monotonic() - launch_started
                # Snapshot the log boundary before the measured phase. The
                # child writes directly to this file descriptor, so a fresh
                # read captures every completed audit line up to the marker.
                boot_lines = log_path.read_text(
                    encoding="utf-8", errors="replace"
                ).splitlines()
                started = time.monotonic()
                console.sendall(b"sleep 10\recho __P2K_BENCH_DONE__\r")
                wait_for(console, MARKER,
                         max(45.0, 15.0 * 100.0 / target_speed))
                sleep_wall = time.monotonic() - started
                time.sleep(6.2)  # ensure at least two fresh 3-second audit snapshots
        except Exception as error:
            print(f"[bench] ERROR: {error}", file=sys.stderr)
            return_code = 1
        else:
            return_code = 0
        finally:
            if process.poll() is None:
                try:
                    signal_pid = process.pid
                    # The wrapper execs QEMU; signal the process group defensively.
                    import os
                    os.killpg(signal_pid, signal.SIGTERM)
                    process.wait(timeout=8)
                except (ProcessLookupError, subprocess.TimeoutExpired):
                    if process.poll() is None:
                        import os
                        os.killpg(process.pid, signal.SIGKILL)
                        process.wait()

    if return_code:
        print(f"[bench] log={log_path}", file=sys.stderr)
        return return_code

    lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
    timing = last_line(lines, "p2k-timing #", " snap |")
    drift = last_line(lines, "p2k-xinu-drift snap")
    hotloop = last_line(lines, "p2k-clkint-hotloop snap")
    lpt = last_line(lines, "p2k-lpt-hz snap")
    pdb = last_line(lines, "p2k-pdb05 snap", "pdb05_wall_delta")

    boot_timing = last_line(boot_lines, "p2k-timing #", " snap |")
    boot_lpt = last_line(boot_lines, "p2k-lpt-hz snap")
    boot_data_match = re.search(r"data=\d+ \(\+([0-9.]+)/s\)", boot_lpt)
    boot_irq_jitter_max = max_field(
        boot_lines, "p2k-clkint-hotloop snap", "max_us"
    )
    boot_pdb_max = max_field(
        [line for line in boot_lines if "pdb05_wall_total" in line],
        "p2k-pdb05 snap", "max_total",
    )

    if not timing:
        print(f"[bench] ERROR: timing snapshots missing; log={log_path}", file=sys.stderr)
        return 1

    effective = 1000.0 / sleep_wall
    data_match = re.search(r"data=\d+ \(\+([0-9.]+)/s\)", lpt)
    delta_match = re.search(r"delta:.*drift=([0-9.]+)", drift)
    print()
    print("Encore self-diagnostic")
    print(f"  Requested speed:       {target_speed:.2f}%")
    print("  Boot/warmup phase:")
    print(f"    Wall duration:        {boot_wall:.3f} s")
    print(f"    IRQ0 delivery total:  {field(boot_timing, 'delivery')}")
    print(f"    IRQ0 delivery end:    {field(boot_timing, 'current_delivery')}")
    print(f"    IRQ0 jitter worst:    {boot_irq_jitter_max} us")
    print(f"    LPT DATA rate end:    "
          f"{boot_data_match.group(1) + '/s' if boot_data_match else 'n/a'}")
    print(f"    PDB05 worst:          {boot_pdb_max} us")
    print("  Steady-state phase:")
    print("    Warmup completed:     30.000 s guest time")
    print(f"    XINU sleep 10 wall:   {sleep_wall:.3f} s ({effective:.2f}% real-time)")
    print(f"    Measured clock speed: {field(timing, 'current_speed')}")
    print(f"    Current IRQ0 delivery: {field(timing, 'current_delivery')}")
    print(f"    Current IRQ0 counts: {field(timing, 'current_clkint_entered')} / "
          f"{field(timing, 'current_irq0_raised')}")
    print(f"    Current XINU drift:  {delta_match.group(1) + 'x' if delta_match else 'n/a'}")
    if hotloop:
        print(f"    HOTLOOP adaptive:    {field(hotloop, 'adaptive')}")
        print(f"    HOTLOOP gap:         {field(hotloop, 'gap_ns')} ns")
        print(f"    HOTLOOP measured:    {field(hotloop, 'measured_hz')} Hz")
        jitter = re.search(
            r"jitter:.*mean_us=(\d+).*min_us=(\d+).*max_us=(\d+).*stddev_us=(\d+)",
            hotloop,
        )
        if jitter:
            print(f"    IRQ0 jitter µs:      mean={jitter.group(1)} min={jitter.group(2)} "
                  f"max={jitter.group(3)} stddev={jitter.group(4)}")
    else:
        print("    HOTLOOP:             disabled (--strict)")
    print(f"    LPT DATA rate:       {data_match.group(1) + '/s' if data_match else 'n/a'}")
    if pdb:
        print(f"    PDB05 gaps:          p50={field(pdb, 'p50')} p95={field(pdb, 'p95')} "
              f"p99={field(pdb, 'p99')} worst={field(pdb, 'max')}")
    print(f"  Log:                   {log_path}")

    delivery_text = field(timing, "current_delivery", "0%")
    try:
        delivery = float(delivery_text.rstrip("%"))
    except ValueError:
        delivery = 0.0
    speed_low = target_speed * 0.95
    speed_high = target_speed * 1.05
    if delivery < 95.0 or delivery > 105.0 or not (speed_low <= effective <= speed_high):
        print(f"  RESULT: ABNORMAL — achieved speed is outside "
              f"{speed_low:.2f}–{speed_high:.2f}% or IRQ0 delivery is unhealthy")
        return 2
    print("  RESULT: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
