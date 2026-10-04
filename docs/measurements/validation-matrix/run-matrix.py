#!/usr/bin/env python3
"""Reproduce the supported boot/audio validation matrix.

Runs SWE1 and RFM in base-ROM and latest-update modes through every current
DCS engine.  The DCS comparison harness supplies identical cabinet input and
lightweight timing measurement; this wrapper adds exact game/update/engine,
display, audio-progress, live-DSP-health and fatal checks and writes one
combined Markdown report.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
COMPARISON = ROOT / "docs/measurements/dcs-engines/run-comparison.py"
ENGINES = (
    "pb2kslib", "pb2kslib-adsp", "adsp", "adsp-thread",
    "adsp-clock-thread", "adsp-hybrid-thread",
)
LIVE_ENGINES = {
    "adsp", "adsp-thread", "adsp-clock-thread", "adsp-hybrid-thread",
}
DCS_HEALTH_RE = re.compile(
    r"^\[p2k-dcs-health\] queued=(?P<queued>\d+) "
    r"runtime_resets=(?P<runtime_resets>\d+) host_boots=(?P<host_boots>\d+) "
    r"enqueued=(?P<enqueued>\d+) consumed=(?P<consumed>\d+) "
    r"dropped=(?P<dropped>\d+) pcm_frames=(?P<pcm_frames>\d+) "
    r"pcm_nonzero=(?P<pcm_nonzero>\d+) cycles=(?P<cycles>\d+)$",
    re.MULTILINE,
)
DEFAULT_CELLS = (
    ("swe1", "none", "SWE1 base"),
    ("swe1", "latest", "SWE1 latest"),
    ("rfm", "none", "RFM base"),
    ("rfm", "latest", "RFM latest"),
)


def installed_cells() -> tuple[tuple[str, str, str], ...]:
    """Return base plus every extracted update bundle, in version order."""
    cells: list[tuple[str, str, str]] = []
    for game, gid in (("swe1", "50069"), ("rfm", "50070")):
        cells.append((game, "none", f"{game.upper()} base"))
        bundles = sorted((ROOT / "updates").glob(f"pin2000_{gid}_*"))
        for bundle in bundles:
            inner = bundle / gid
            if not inner.is_dir() or not any(inner.glob("*_game.rom")):
                continue
            match = re.match(rf"pin2000_{gid}_(\d{{4}})_", bundle.name)
            version = match.group(1) if match else bundle.name
            cells.append((game, str(inner), f"{game.upper()} {version}"))
    return tuple(cells)


def artifact_cell_key(update: str, label: str) -> str:
    if update == "none":
        return "base"
    if update == "latest":
        return "latest"
    # Version numbers are not unique: distinct dated identities may share one
    # version number. Keep the full bundle identity.
    return Path(update).parent.name


def last_number(text: str, pattern: str, default: float = 0.0) -> float:
    matches = re.findall(pattern, text, flags=re.I)
    return float(matches[-1]) if matches else default


def inspect(log: Path, game: str, update: str, engine: str) -> dict[str, object]:
    text = log.read_text(encoding="utf-8", errors="replace")
    banner = re.findall(r"Game\(Williams - ([^)]+)\)", text)
    machine_game = re.findall(r"machine ready \(game=([a-z0-9]+)", text)
    blits = len(re.findall(r"GP BLT #", text))
    rendered = int(last_number(text, r"dcs-audio: decoded cmd=.*?frames=(\d+)"))
    adsp_cycles = int(last_number(text, r"dcs-adsp: run .*?cycles=(\d+)"))
    fatal = bool(re.search(
        r"\*\*\* Fatal|DCS2 board failed to initialize|stack smash|"
        r"segmentation fault|assertion .* failed|guest .*crash",
        text, re.I,
    ))
    timed = "p2k-timing #" in text
    if engine == "pb2kslib":
        engine_ok = ("pb2kslib loaded" in text
                     and "dcs-cache: using persistent PCM cache" not in text)
    elif engine == "pb2kslib-adsp":
        engine_ok = "dcs-cache: using persistent PCM cache" in text
    else:
        engine_ok = bool(re.search(
            rf"native ADSP-2104 execution selected \({re.escape(engine)}\)",
            text, re.I,
        ))

    if update == "none":
        update_ok = "[run-qemu] --update none" in text
    elif update == "latest":
        update_ok = "[run-qemu] --update latest →" in text
    else:
        update_ok = (f"[run-qemu] --update {update} →" in text
                     and str(Path(update).resolve()) in text)

    health = "n/a"
    health_ok = True
    if engine in LIVE_ENGINES:
        matches = list(DCS_HEALTH_RE.finditer(text))
        if not matches:
            health = "MISSING"
            health_ok = False
        else:
            values = {key: int(value)
                      for key, value in matches[-1].groupdict().items()}
            health_ok = (
                values["queued"] == 0
                and values["runtime_resets"] == 0
                and values["dropped"] == 0
                and values["enqueued"] == values["consumed"]
                and values["pcm_frames"] > 0
                and values["pcm_nonzero"] > 0
                and values["cycles"] > 0
            )
            health = "PASS" if health_ok else "FAIL"

    identity = banner[-1] if banner else (machine_game[-1].upper() if machine_game else "missing")
    selected = re.findall(r"^\[run-qemu\] --update \S+ → (.+)$", text, re.MULTILINE)
    resolved_update = selected[-1] if selected else ("base" if update == "none" else "missing")
    game_ok = bool(machine_game and machine_game[-1] == game)
    progress = adsp_cycles if engine in LIVE_ENGINES else rendered
    passed = bool(game_ok and update_ok and blits > 0 and timed and engine_ok
                  and progress > 0 and health_ok and not fatal)
    return {
        "game": game, "update": update, "engine": engine,
        "banner": identity, "game_ok": game_ok, "update_ok": update_ok,
        "engine_ok": engine_ok, "health": health, "blits": blits,
        "resolved_update": resolved_update,
        "rendered": rendered, "cycles": adsp_cycles,
        "fatal": fatal, "pass": passed, "log": log,
    }


def render(rows: list[dict[str, object]], duration: float, warmup: float) -> str:
    lines = [
        "# Encore validation-matrix result", "",
        f"Generated {dt.datetime.now().astimezone().isoformat(timespec='seconds')}. "
        f"Each cell ran {duration:g} s with cabinet input at 11 s and timing "
        f"windows after {warmup:g} s.", "",
        "| Game path | DCS engine | Identity | Update | Engine | GP BLTs | Audio progress | Health | Fatal | Result |",
        "|---|---|---|---|---|---:|---:|---|---|---|",
    ]
    for row in rows:
        update = str(row["update"])
        if update == "none":
            update = "base"
        elif update == "latest" and row["resolved_update"] != "missing":
            bundle = Path(str(row["resolved_update"])).parent.name
            match = re.search(r"_(\d{4})_", bundle)
            update = f"latest→{match.group(1) if match else bundle}"
        elif "/" in update:
            bundle = Path(update).parent.name
            match = re.search(r"_(\d{4})_(\d{8})_", bundle)
            update = (f"{match.group(1)}@{match.group(2)}"
                      if match else bundle)
        path = f"{str(row['game']).upper()} {update}"
        progress = (row["cycles"] if row["engine"] in LIVE_ENGINES
                    else row["rendered"])
        lines.append(
            f"| {path} | {row['engine']} | "
            f"{'ok' if row['game_ok'] else str(row['banner'])} | "
            f"{'ok' if row['update_ok'] else 'wrong/missing'} | "
            f"{'ok' if row['engine_ok'] else 'wrong/missing'} | {row['blits']} | "
            f"{progress} | {row['health']} | {'yes' if row['fatal'] else 'no'} | "
            f"{'PASS' if row['pass'] else 'FAIL'} |"
        )
    passed = sum(bool(row["pass"]) for row in rows)
    lines += ["", f"**Summary: {passed}/{len(rows)} passed.**", ""]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--duration", type=float, default=60.0)
    parser.add_argument("--warmup", type=float, default=30.0)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--parse-only", type=Path, metavar="DIR")
    parser.add_argument(
        "--engine", action="append", choices=ENGINES,
        help="DCS engine to test (repeatable; default: all current engines)",
    )
    parser.add_argument(
        "--all-updates", action="store_true",
        help="test base plus every locally extracted update bundle",
    )
    args = parser.parse_args()
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    output = (args.parse_only or args.output or Path(f"/tmp/p2k-validation-matrix-{stamp}")).resolve()
    if not args.parse_only and output.exists() and any(output.iterdir()):
        raise SystemExit(f"refusing to overwrite non-empty output directory: {output}")
    output.mkdir(parents=True, exist_ok=True)

    rows: list[dict[str, object]] = []
    engines = tuple(dict.fromkeys(args.engine or ENGINES))
    cells = installed_cells() if args.all_updates else DEFAULT_CELLS
    if not args.parse_only:
        try:
            commit = subprocess.check_output(
                ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True
            ).strip()
        except (OSError, subprocess.CalledProcessError):
            commit = "unknown"
        (output / "metadata.json").write_text(json.dumps({
            "generated": dt.datetime.now().astimezone().isoformat(timespec="seconds"),
            "repo_commit": commit,
            "repo_dirty": bool(subprocess.run(
                ["git", "status", "--porcelain"], cwd=ROOT,
                text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                check=False,
            ).stdout.strip()),
            "runner_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "comparison_sha256": hashlib.sha256(COMPARISON.read_bytes()).hexdigest(),
            "duration": args.duration,
            "warmup": args.warmup,
            "all_updates": args.all_updates,
            "engines": engines,
            "cells": cells,
        }, indent=2) + "\n")
    for game, update, label in cells:
        cell_key = artifact_cell_key(update, label)
        cell = output / f"{game}-{cell_key}"
        if not args.parse_only:
            command = [
                sys.executable, str(COMPARISON), "--game", game, "--update", update,
                "--duration", str(args.duration), "--warmup", str(args.warmup),
                "--output", str(cell), "--lightweight",
            ]
            for engine in engines:
                command.extend(("--engine", engine))
            print(f"[matrix] {label}: {' '.join(command)}", flush=True)
            completed = subprocess.run(command, cwd=ROOT)
            if completed.returncode:
                print(f"[matrix] comparison failed for {label}; retaining artifacts", file=sys.stderr)
        for engine in engines:
            log = cell / f"{engine}.log"
            if log.exists():
                rows.append(inspect(log, game, update, engine))
            else:
                rows.append({
                    "game": game, "update": update, "engine": engine,
                    "banner": "missing", "game_ok": False,
                    "update_ok": False, "engine_ok": False,
                    "health": "MISSING", "blits": 0,
                    "resolved_update": "missing",
                    "rendered": 0, "cycles": 0, "fatal": True,
                    "pass": False, "log": log,
                })

    report = render(rows, args.duration, args.warmup)
    (output / "report.md").write_text(report, encoding="utf-8")
    print("\n" + report)
    print(f"[matrix] artifacts: {output}")
    return 0 if all(bool(row["pass"]) for row in rows) else 1


if __name__ == "__main__":
    raise SystemExit(main())
