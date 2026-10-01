#!/usr/bin/env python3
"""Persist Encore's in-memory timer precursor ring when it freezes."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import time
from pathlib import Path


SYMBOL_SIZES = {
    "p2k_timer_precursor_events": 8192 * 88,
    "p2k_timer_precursor_freeze_seq": 8,
    "p2k_timer_precursor_seq": 8,
    "p2k_timer_precursor_frozen": 1,
    "p2k_timer_precursor_enabled": 1,
    "p2k_clkint_max_depth": 4,
    "p2k_irq0_stack_global_min": 4,
    "p2k_audit_irq0_raised": 8,
    "p2k_audit_clkint_entered": 8,
}


def executable_and_base(pid: int) -> tuple[Path, int]:
    executable = Path(os.readlink(f"/proc/{pid}/exe"))
    with open(f"/proc/{pid}/maps", encoding="ascii") as maps:
        for line in maps:
            fields = line.split()
            if len(fields) >= 6 and int(fields[2], 16) == 0 and \
                    Path(fields[5]) == executable:
                return executable, int(fields[0].split("-", 1)[0], 16)
    raise RuntimeError("could not find the executable's zero-offset mapping")


def symbol_offsets(executable: Path) -> dict[str, int]:
    wanted = set(SYMBOL_SIZES)
    found: dict[str, int] = {}
    output = subprocess.check_output(("nm", "-a", str(executable)), text=True)
    for line in output.splitlines():
        match = re.match(r"^([0-9a-fA-F]+)\s+\S\s+(\S+)$", line)
        if match and match.group(2) in wanted:
            found[match.group(2)] = int(match.group(1), 16)
    missing = wanted - found.keys()
    if missing:
        raise RuntimeError(f"missing QEMU symbols: {', '.join(sorted(missing))}")
    return found


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pid", required=True, type=int)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--poll-ms", type=float, default=10.0)
    parser.add_argument("--capture-tool", type=Path)
    args = parser.parse_args()
    if args.poll_ms <= 0:
        parser.error("--poll-ms must be positive")
    if args.output.exists():
        parser.error(f"output already exists: {args.output}")

    executable, base = executable_and_base(args.pid)
    offsets = symbol_offsets(executable)
    memory = os.open(f"/proc/{args.pid}/mem", os.O_RDONLY)
    frozen_address = base + offsets["p2k_timer_precursor_frozen"]

    while os.pread(memory, 1, frozen_address) == b"\0":
        time.sleep(args.poll_ms / 1000.0)

    args.output.mkdir(parents=True, mode=0o700)
    values: dict[str, int | str] = {
        "pid": args.pid,
        "executable": str(executable),
        "load_base": base,
        "captured_wall_ns": time.monotonic_ns(),
    }
    for name, size in SYMBOL_SIZES.items():
        data = os.pread(memory, size, base + offsets[name])
        if name == "p2k_timer_precursor_events":
            (args.output / "timer-precursor-ring.bin").write_bytes(data)
        else:
            values[name] = int.from_bytes(data, "little")
    os.close(memory)
    (args.output / "timer-precursor-state.json").write_text(
        json.dumps(values, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    if args.capture_tool:
        subprocess.run((str(args.capture_tool), "--pid", str(args.pid),
                        "--output", str(args.output / "guest-capture")),
                       check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
