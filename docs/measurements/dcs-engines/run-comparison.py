#!/usr/bin/env python3
"""Run and summarize the repeatable Pinball 2000 DCS-engine workload.

The default run matches the July 2026 comparison:
  * SWE1 update 2.10, no savedata, headless WAV audio, verbose diagnostics
  * start cabinet input 11 seconds after launching the wrapper
  * F4 (open coin door), three credits, then 20 alternating volume presses
  * stop each engine after 90 seconds
  * summarize full diagnostic windows beginning at wall >= 30 seconds

Only the Python standard library is required.
"""

from __future__ import annotations

import argparse
import datetime as dt
import os
from pathlib import Path
import re
import signal
import socket
import subprocess
import sys
import time


ENGINES = ("pb2kslib", "pb2kslib-adsp", "adsp", "adsp-thread")
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
RUNNER = ROOT / "scripts" / "run-qemu.sh"


def field(line: str, name: str, cast=float):
    match = re.search(rf"(?:^|\s){re.escape(name)}=([-+]?[0-9]+(?:\.[0-9]+)?)", line)
    if not match:
        raise ValueError(f"missing {name}= in: {line.rstrip()}")
    return cast(match.group(1))


def monitor_commands(sock_path: Path) -> None:
    deadline = time.monotonic() + 10.0
    while not sock_path.exists():
        if time.monotonic() >= deadline:
            raise RuntimeError(f"QEMU monitor did not appear: {sock_path}")
        time.sleep(0.05)

    commands = ["sendkey f4"]
    commands.extend("sendkey c" for _ in range(3))
    commands.extend(f"sendkey {'up' if i % 2 == 0 else 'down'}" for i in range(20))

    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as monitor:
        monitor.settimeout(2.0)
        monitor.connect(str(sock_path))
        try:
            monitor.recv(4096)
        except socket.timeout:
            pass
        for index, command in enumerate(commands):
            monitor.sendall((command + "\n").encode("ascii"))
            # Give switch pulses time to be observed. Credits are deliberately
            # slower than the volume mash; the first command opens the door.
            if index == 0:
                time.sleep(0.50)
            elif index <= 3:
                time.sleep(0.30)
            else:
                time.sleep(0.10)


def run_engine(engine: str, args: argparse.Namespace, output: Path) -> Path:
    log_path = output / f"{engine}.log"
    sock_path = Path(f"/tmp/p2k-dcs-comparison-{os.getpid()}-{engine}.mon")
    sock_path.unlink(missing_ok=True)
    command = [
        str(RUNNER),
        "--game", args.game,
        "--update", args.update,
        "--no-savedata",
        "--display", "none",
        "--uart-quiet",
        "--audio", "wav",
        "--dcs-engine", engine,
        "--monitor", f"unix:{sock_path},server=on,wait=off",
        "-v",
    ]
    if args.strict:
        command.append("--strict")
    if args.with_pit:
        command.append("--with-pit")

    print(f"[comparison] {engine}: {args.duration:.0f}s -> {log_path}", flush=True)
    started = time.monotonic()
    with log_path.open("w", encoding="utf-8") as log:
        proc = subprocess.Popen(
            command,
            cwd=ROOT,
            stdin=subprocess.DEVNULL,
            stdout=log,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        try:
            remaining = args.input_delay - (time.monotonic() - started)
            if remaining > 0:
                time.sleep(remaining)
            if proc.poll() is not None:
                raise RuntimeError(f"{engine} exited before cabinet input; inspect {log_path}")
            monitor_commands(sock_path)

            remaining = args.duration - (time.monotonic() - started)
            if remaining > 0:
                try:
                    proc.wait(timeout=remaining)
                except subprocess.TimeoutExpired:
                    pass
            if proc.poll() is None:
                # Signal the wrapper's process group. Its trap terminates QEMU,
                # allowing QEMU's exit notifier to emit the final audit panel.
                os.killpg(proc.pid, signal.SIGTERM)
                try:
                    proc.wait(timeout=8.0)
                except subprocess.TimeoutExpired:
                    os.killpg(proc.pid, signal.SIGKILL)
                    proc.wait()
        finally:
            if proc.poll() is None:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()
            sock_path.unlink(missing_ok=True)
    return log_path


def find_log(directory: Path, engine: str) -> Path:
    candidates = (
        directory / f"{engine}.log",
        directory / f"stats-210-{engine}.log",
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(f"no log for {engine} in {directory}")


def summarize(log_path: Path, warmup: float) -> dict[str, float | int | str]:
    lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
    timing = [line for line in lines if "p2k-timing #" in line]
    snaps = [
        line for line in timing
        if " snap |" in line and field(line, "wall") >= warmup
    ]
    if not snaps:
        raise ValueError(f"{log_path}: no complete timing windows at wall >= {warmup}s")

    raised = sum(field(line, "current_irq0_raised", int) for line in snaps)
    serviced = sum(field(line, "current_clkint_entered", int) for line in snaps)
    current = [field(line, "current_delivery") for line in snaps]
    final_timing = next((line for line in reversed(timing) if " exit |" in line), timing[-1])

    cadence = [line for line in lines if "p2k-clkint-entry " in line]
    if not cadence:
        cadence = [line for line in lines if "p2k-clkint-hotloop " in line]
    final_cadence = (next((line for line in reversed(cadence) if " exit |" in line), cadence[-1])
                     if cadence else None)
    hotloop = [line for line in lines if "p2k-clkint-hotloop " in line]
    final_hotloop = (next((line for line in reversed(hotloop) if " exit |" in line), hotloop[-1])
                     if hotloop else None)

    pdb_windows = []
    snap_wall = None
    for line in lines:
        if "p2k-timing #" in line and " snap |" in line:
            snap_wall = field(line, "wall")
        elif "p2k-pdb05 snap | pdb05_wall_delta" in line:
            if snap_wall is not None and snap_wall >= warmup:
                pdb_windows.append(line)

    return {
        "engine": log_path.stem,
        "cumulative": field(final_timing, "delivery"),
        "raised": raised,
        "serviced": serviced,
        "weighted": 100.0 * serviced / raised,
        "window_mean": sum(current) / len(current),
        "window_min": min(current),
        "window_max": max(current),
        "window_last": current[-1],
        "windows": len(current),
        "gap_us": field(final_hotloop, "gap_ns") / 1000.0 if final_hotloop else None,
        "measured_hz": field(final_hotloop, "measured_hz") if final_hotloop else None,
        "jitter_n": field(final_cadence, "n", int) if final_cadence else None,
        "jitter_mean": field(final_cadence, "mean_us") if final_cadence else None,
        "jitter_min": field(final_cadence, "min_us") if final_cadence else None,
        "jitter_max": field(final_cadence, "max_us") if final_cadence else None,
        "jitter_stddev": field(final_cadence, "stddev_us") if final_cadence else None,
        "pdb_n": sum(field(line, "n", int) for line in pdb_windows),
        "pdb_p95_worst": max((field(line, "p95", int) for line in pdb_windows), default=0),
        "pdb_p99_worst": max((field(line, "p99", int) for line in pdb_windows), default=0),
        "pdb_window_worst": max((field(line, "max", int) for line in pdb_windows), default=0),
        "pdb_run_worst": max((field(line, "max_total", int) for line in pdb_windows), default=0),
    }


def report(rows: list[dict[str, float | int | str]], warmup: float) -> str:
    out = [
        f"Full `snap` windows at wall >= {warmup:g} s; partial `exit` window excluded.",
        "",
        "| Engine | Cumulative delivery | Current weighted | Window mean | Current range | Last full | Windows | Gap | Last measured Hz |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for row in rows:
        gap = f"{row['gap_us']:.1f} us" if row["gap_us"] is not None else "—"
        measured = f"{row['measured_hz']:.0f}" if row["measured_hz"] is not None else "—"
        out.append(
            f"| {row['engine']} | {row['cumulative']:.1f}% | {row['weighted']:.2f}% | "
            f"{row['window_mean']:.2f}% | {row['window_min']:.1f}–{row['window_max']:.1f}% | "
            f"{row['window_last']:.1f}% | {row['windows']} | {gap} | {measured} |"
        )
    out.extend([
        "",
        "| Engine | Jitter samples | Mean | Minimum | Stddev | Maximum |",
        "|---|---:|---:|---:|---:|---:|",
    ])
    for row in rows:
        if row["jitter_n"] is None:
            out.append(f"| {row['engine']} | — | — | — | — | — |")
        else:
            out.append(
                f"| {row['engine']} | {row['jitter_n']:,} | {row['jitter_mean']:.0f} us | "
                f"{row['jitter_min']:.0f} us | {row['jitter_stddev']:.0f} us | "
                f"{row['jitter_max'] / 1000.0:.1f} ms |"
            )
    out.extend([
        "",
        "| Engine | PDB05 samples | Worst p95 | Worst p99 | Worst steady window | Worst observed |",
        "|---|---:|---:|---:|---:|---:|",
    ])
    for row in rows:
        if row["pdb_n"]:
            values = (f"{row['pdb_p95_worst']:.0f} us", f"{row['pdb_p99_worst']:.0f} us",
                      f"{row['pdb_window_worst']:.0f} us", f"{row['pdb_run_worst']:.0f} us")
        else:
            values = ("—", "—", "—", "—")
        out.append(f"| {row['engine']} | {row['pdb_n']:,} | " + " | ".join(values) + " |")
    out.extend([
        "",
        "`Current weighted` is sum(current services) / sum(current raises), not a mean of percentages.",
        "`Last measured Hz` is the final short adaptive-controller sample, not a run-wide mean.",
    ])
    return "\n".join(out) + "\n"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--duration", type=float, default=90.0, help="seconds per engine (default: 90)")
    parser.add_argument("--input-delay", type=float, default=11.0, help="seconds before F4/credits/volume input")
    parser.add_argument("--warmup", type=float, default=30.0, help="ignore snap windows before this wall time")
    parser.add_argument("--game", default="swe1")
    parser.add_argument("--update", default="0210")
    parser.add_argument("--strict", action="store_true",
                        help="run every selected engine with natural PIT timing")
    parser.add_argument("--with-pit", action="store_true",
                        help="run every selected engine with HOTLOOP plus natural PIT")
    parser.add_argument("--output", type=Path, help="artifact directory (default: timestamped /tmp directory)")
    parser.add_argument("--parse-only", type=Path, metavar="DIR", help="summarize existing logs without running QEMU")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.strict and args.with_pit:
        raise SystemExit("--strict and --with-pit are mutually exclusive")
    if args.duration <= args.input_delay:
        raise SystemExit("--duration must be greater than --input-delay")
    if args.warmup >= args.duration and not args.parse_only:
        raise SystemExit("--warmup must be less than --duration")

    if args.parse_only:
        output = args.parse_only.resolve()
        logs = [find_log(output, engine) for engine in ENGINES]
    else:
        stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
        output = (args.output or Path(f"/tmp/p2k-dcs-comparison-{stamp}")).resolve()
        output.mkdir(parents=True, exist_ok=True)
        logs = [run_engine(engine, args, output) for engine in ENGINES]

    rows = []
    for engine, log in zip(ENGINES, logs):
        row = summarize(log, args.warmup)
        row["engine"] = engine
        rows.append(row)
    rendered = report(rows, args.warmup)
    report_path = output / "report.md"
    report_path.write_text(rendered, encoding="utf-8")
    print("\n" + rendered, end="")
    print(f"[comparison] logs and report: {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
