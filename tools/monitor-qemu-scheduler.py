#!/usr/bin/env python3
"""Sample one QEMU thread's Linux scheduler accounting with low overhead.

The output distinguishes time spent executing on a CPU from time spent ready
but waiting in the run queue.  It is intentionally external to QEMU so a rare
main-loop delay can be classified without adding work to the emulated IRQ path.
"""

from __future__ import annotations

import argparse
import csv
import os
import re
import signal
import subprocess
import sys
import time
from pathlib import Path


stop = False


def request_stop(_signum: int, _frame: object) -> None:
    global stop
    stop = True


def read_schedstat(stream: object) -> tuple[int, int, int]:
    stream.seek(0)
    fields = stream.read().split()
    if len(fields) < 3:
        raise RuntimeError("unexpected schedstat contents")
    return int(fields[0]), int(fields[1]), int(fields[2])


def executable_base_and_symbols(pid: int) -> tuple[int, dict[str, int]]:
    executable = Path(os.readlink(f"/proc/{pid}/exe"))
    base = None
    with open(f"/proc/{pid}/maps", encoding="ascii") as maps:
        for line in maps:
            fields = line.split()
            if len(fields) >= 6 and int(fields[2], 16) == 0 and \
                    Path(fields[5]) == executable:
                base = int(fields[0].split("-", 1)[0], 16)
                break
    if base is None:
        raise RuntimeError("could not find QEMU's zero-offset mapping")

    wanted = {"p2k_clkint_depth", "p2k_clkint_max_depth"}
    symbols: dict[str, int] = {}
    output = subprocess.check_output(("nm", "-a", str(executable)), text=True)
    for line in output.splitlines():
        match = re.match(r"^([0-9a-fA-F]+)\s+\S\s+(\S+)$", line)
        if match and match.group(2) in wanted:
            symbols[match.group(2)] = base + int(match.group(1), 16)
    missing = wanted - symbols.keys()
    if missing:
        raise RuntimeError(f"missing QEMU symbols: {', '.join(sorted(missing))}")
    return base, symbols


def current_cpu(stat_stream: object) -> int:
    stat_stream.seek(0)
    # /proc/<tid>/stat's comm field is parenthesized and may contain spaces.
    fields = stat_stream.read().rsplit(")", 1)[1].split()
    return int(fields[36])  # field 39 (processor), after pid+comm were removed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pid", required=True, type=int)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--interval-ms", type=float, default=1.0)
    parser.add_argument("--report-us", type=int, default=500)
    parser.add_argument("--qemu-pid", type=int,
                        help="also sample Encore IRQ0 depth and host power")
    args = parser.parse_args()

    if args.interval_ms <= 0 or args.report_us < 0:
        parser.error("interval and report threshold must be non-negative")

    schedstat_path = Path(f"/proc/{args.pid}/task/{args.pid}/schedstat")
    task_stat_path = Path(f"/proc/{args.pid}/task/{args.pid}/stat")
    ac_path = Path("/sys/class/power_supply/AC/online")
    qemu_memory = None
    qemu_symbols: dict[str, int] = {}
    if args.qemu_pid is not None:
        _, qemu_symbols = executable_base_and_symbols(args.qemu_pid)
        qemu_memory = os.open(f"/proc/{args.qemu_pid}/mem", os.O_RDONLY)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    signal.signal(signal.SIGINT, request_stop)
    signal.signal(signal.SIGTERM, request_stop)

    interval_ns = int(args.interval_ms * 1_000_000)
    report_ns = args.report_us * 1000
    next_sample = time.monotonic_ns()

    with schedstat_path.open("r", encoding="ascii") as schedstat, \
            task_stat_path.open("r", encoding="ascii") as task_stat, \
            args.output.open("w", newline="", encoding="utf-8") as output:
        writer = csv.writer(output)
        writer.writerow(("wall_ns", "sample_gap_us", "cpu_delta_us",
                         "runqueue_delta_us", "timeslice_delta", "host_cpu",
                         "host_khz", "energy_preference", "ac_online",
                         "clkint_depth", "max_clkint_depth"))
        previous_wall = time.monotonic_ns()
        previous_cpu, previous_wait, previous_slices = read_schedstat(schedstat)

        while not stop:
            next_sample += interval_ns
            delay_ns = next_sample - time.monotonic_ns()
            if delay_ns > 0:
                time.sleep(delay_ns / 1_000_000_000)
            now = time.monotonic_ns()
            try:
                cpu, wait, slices = read_schedstat(schedstat)
            except (FileNotFoundError, ProcessLookupError):
                break

            wall_delta = now - previous_wall
            cpu_delta = cpu - previous_cpu
            wait_delta = wait - previous_wait
            slice_delta = slices - previous_slices
            if (wall_delta >= 2 * interval_ns or cpu_delta >= report_ns or
                    wait_delta >= report_ns):
                host_cpu = current_cpu(task_stat)
                cpufreq = Path(
                    f"/sys/devices/system/cpu/cpu{host_cpu}/cpufreq")
                try:
                    host_khz = (cpufreq / "scaling_cur_freq").read_text().strip()
                except OSError:
                    host_khz = ""
                try:
                    energy_preference = (
                        cpufreq / "energy_performance_preference"
                    ).read_text().strip()
                except OSError:
                    energy_preference = ""
                try:
                    ac_online = ac_path.read_text().strip()
                except OSError:
                    ac_online = ""
                depth = max_depth = ""
                if qemu_memory is not None:
                    depth = int.from_bytes(os.pread(qemu_memory, 4,
                        qemu_symbols["p2k_clkint_depth"]), "little")
                    max_depth = int.from_bytes(os.pread(qemu_memory, 4,
                        qemu_symbols["p2k_clkint_max_depth"]), "little")
                writer.writerow((now, wall_delta // 1000, cpu_delta // 1000,
                                 wait_delta // 1000, slice_delta, host_cpu,
                                 host_khz, energy_preference, ac_online,
                                 depth, max_depth))
                output.flush()

            previous_wall = now
            previous_cpu = cpu
            previous_wait = wait
            previous_slices = slices

    if qemu_memory is not None:
        os.close(qemu_memory)

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, PermissionError) as exc:
        print(f"scheduler monitor: {exc}", file=sys.stderr)
        raise SystemExit(1)
