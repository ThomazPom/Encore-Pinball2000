#!/usr/bin/env python3
"""Reproduce the supported boot/audio validation matrix.

Runs SWE1 and RFM in base-ROM and latest-update modes through all three
DCS engines.  The existing DCS comparison harness supplies identical cabinet
input and timing measurement; this wrapper adds boot, display, DCS and fatal
checks and writes one combined Markdown report.
"""

from __future__ import annotations

import argparse
import datetime as dt
from pathlib import Path
import re
import subprocess
import sys


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
COMPARISON = ROOT / "docs/measurements/dcs-engines/run-comparison.py"
ENGINES = ("pb2kslib", "adsp", "adsp-thread")
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


def last_number(text: str, pattern: str, default: float = 0.0) -> float:
    matches = re.findall(pattern, text, flags=re.I)
    return float(matches[-1]) if matches else default


def inspect(log: Path, game: str, update: str, engine: str) -> dict[str, object]:
    text = log.read_text(encoding="utf-8", errors="replace")
    banner = re.findall(r"Game\(Williams - ([^)]+)\)", text)
    machine_game = re.findall(r"machine ready \(game=([a-z0-9]+)", text)
    blits = len(re.findall(r"GP BLT #", text))
    dcs_pairs = len(re.findall(
        r"dcs-audio: \[[^]]+\] (?:process_cmd|execute_mixer)", text, re.I
    ))
    rendered = int(last_number(text, r"dcs-audio: decoded cmd=.*?frames=(\d+)"))
    adsp_cycles = int(last_number(text, r"dcs-adsp: run .*?cycles=(\d+)"))
    fatal = bool(re.search(r"\*\*\* Fatal|DCS2 board failed to initialize|stack smash", text, re.I))
    timed = "p2k-timing #" in text
    engine_ok = (
        (engine == "pb2kslib" and "pb2kslib loaded" in text)
        or (engine.startswith("adsp") and re.search(
            r"dcs-adsp.*(?:original assets ready|native ADSP-2104 execution selected)",
            text, re.I,
        ))
    )
    identity = banner[-1] if banner else (machine_game[-1].upper() if machine_game else "missing")
    progress = rendered if engine == "pb2kslib" else adsp_cycles
    passed = bool(identity != "missing" and blits > 0 and timed and engine_ok
                  and progress > 0 and not fatal)
    return {
        "game": game, "update": update, "engine": engine,
        "banner": identity, "blits": blits,
        "dcs": dcs_pairs, "rendered": rendered, "cycles": adsp_cycles,
        "fatal": fatal, "pass": passed, "log": log,
    }


def render(rows: list[dict[str, object]], duration: float, warmup: float) -> str:
    lines = [
        "# Encore validation-matrix result", "",
        f"Generated {dt.datetime.now().astimezone().isoformat(timespec='seconds')}. "
        f"Each cell ran {duration:g} s with cabinet input at 11 s and timing "
        f"windows after {warmup:g} s.", "",
        "| Game path | DCS engine | Game identity | GP BLTs | Host-decoded DCS events | Decoded frames / DSP cycles | Fatal | Result |",
        "|---|---|---|---:|---:|---:|---|---|",
    ]
    for row in rows:
        update = str(row["update"])
        if update == "none":
            update = "base"
        elif "/" in update:
            bundle = Path(update).parent.name
            match = re.search(r"_(\d{4})_", bundle)
            update = match.group(1) if match else bundle
        path = f"{str(row['game']).upper()} {update}"
        progress = row["rendered"] if row["engine"] == "pb2kslib" else row["cycles"]
        lines.append(
            f"| {path} | {row['engine']} | {row['banner']} | {row['blits']} | "
            f"{row['dcs']} | {progress} | {'yes' if row['fatal'] else 'no'} | "
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
        "--all-updates", action="store_true",
        help="test base plus every locally extracted update bundle",
    )
    args = parser.parse_args()
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    output = (args.parse_only or args.output or Path(f"/tmp/p2k-validation-matrix-{stamp}")).resolve()
    output.mkdir(parents=True, exist_ok=True)

    rows: list[dict[str, object]] = []
    cells = installed_cells() if args.all_updates else DEFAULT_CELLS
    for game, update, label in cells:
        cell_key = "base" if update == "none" else label.split()[-1]
        cell = output / f"{game}-{cell_key}"
        if not args.parse_only:
            command = [
                sys.executable, str(COMPARISON), "--game", game, "--update", update,
                "--duration", str(args.duration), "--warmup", str(args.warmup),
                "--output", str(cell),
            ]
            print(f"[matrix] {label}: {' '.join(command)}", flush=True)
            completed = subprocess.run(command, cwd=ROOT)
            if completed.returncode:
                print(f"[matrix] comparison failed for {label}; retaining artifacts", file=sys.stderr)
        for engine in ENGINES:
            log = cell / f"{engine}.log"
            if log.exists():
                rows.append(inspect(log, game, update, engine))
            else:
                rows.append({
                    "game": game, "update": update, "engine": engine,
                    "banner": "missing", "blits": 0, "dcs": 0,
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
