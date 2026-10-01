#!/usr/bin/env python3
"""Summarize an Encore timer-precursor capture without attaching to QEMU."""

from __future__ import annotations

import argparse
import json
import statistics
import struct
import subprocess
from pathlib import Path


EVENT = struct.Struct("<QQQqqqqQQIIB7x")
FIELDS = (
    "seq", "irq_raised", "clkint_entered", "expire_ns", "observed_ns",
    "start_wall_ns", "end_wall_ns", "callback", "opaque",
    "clkint_depth", "max_clkint_depth", "clock_type",
)


def percentile(values: list[float], fraction: float) -> float:
    return sorted(values)[int(fraction * (len(values) - 1))]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("capture", type=Path)
    args = parser.parse_args()

    state = json.loads(
        (args.capture / "timer-precursor-state.json").read_text(
            encoding="utf-8"))
    raw = (args.capture / "timer-precursor-ring.bin").read_bytes()
    if len(raw) % EVENT.size:
        raise RuntimeError("timer ring size is not a multiple of 88 bytes")

    freeze_seq = state["p2k_timer_precursor_freeze_seq"]
    events = []
    for offset in range(0, len(raw), EVENT.size):
        event = dict(zip(FIELDS, EVENT.unpack_from(raw, offset)))
        if freeze_seq - len(raw) // EVENT.size < event["seq"] <= freeze_seq:
            events.append(event)
    events.sort(key=lambda event: event["seq"])
    if not events:
        raise RuntimeError("capture contains no valid ring events")

    base = state["load_base"]
    symbols: dict[int, str] = {}
    output = subprocess.check_output(
        ("nm", "-an", state["executable"]), text=True)
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 3:
            symbols[base + int(fields[0], 16)] = fields[2]
    for event in events:
        event["callback_name"] = symbols.get(
            event["callback"], hex(event["callback"] - base))

    callbacks = {}
    for name in sorted({event["callback_name"] for event in events}):
        selected = [event for event in events if event["callback_name"] == name]
        lateness = [(event["observed_ns"] - event["expire_ns"]) / 1000
                    for event in selected]
        duration = [(event["end_wall_ns"] - event["start_wall_ns"]) / 1000
                    for event in selected]
        callbacks[name] = {
            "events": len(selected),
            "lateness_us_p99": percentile(lateness, 0.99),
            "lateness_us_max": max(lateness),
            "duration_us_p99": percentile(duration, 0.99),
            "duration_us_max": max(duration),
        }

    pit = [event for event in events
           if event["callback_name"] == "pit_irq_timer"]
    if not pit:
        raise RuntimeError("capture contains no pit_irq_timer event")
    last_zero = max(index for index, event in enumerate(pit)
                    if event["clkint_depth"] == 0)
    rise = pit[last_zero:]
    rise_lateness = [
        (event["observed_ns"] - event["expire_ns"]) / 1000
        for event in rise
    ]
    summary = {
        "capture": str(args.capture),
        "freeze_seq": freeze_seq,
        "events": len(events),
        "ring_span_ms":
            (events[-1]["start_wall_ns"] - events[0]["start_wall_ns"]) / 1e6,
        "max_depth_at_copy": state["p2k_clkint_max_depth"],
        "min_istack_margin_at_copy": state["p2k_irq0_stack_global_min"],
        "callbacks": callbacks,
        "final_rise": {
            "span_ms": (rise[-1]["start_wall_ns"] -
                        rise[0]["start_wall_ns"]) / 1e6,
            "pit_transitions": len(rise),
            "irq0_edges": rise[-1]["irq_raised"] - rise[0]["irq_raised"],
            "clkint_entries": (rise[-1]["clkint_entered"] -
                               rise[0]["clkint_entered"]),
            "pit_lateness_us_mean": statistics.fmean(rise_lateness),
            "pit_lateness_us_max": max(rise_lateness),
        },
    }
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
