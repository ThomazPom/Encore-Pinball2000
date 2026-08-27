#!/usr/bin/env python3
"""ROM-backed functional smoke test for configurable switch bindings."""

from __future__ import annotations

import json
import os
from pathlib import Path
import re
import socket
import subprocess
import tempfile
import time


ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "scripts/run-qemu.sh"


def key_events(*items: tuple[str, bool]) -> dict:
    return {
        "execute": "input-send-event",
        "arguments": {
            "events": [{
                "type": "key",
                "data": {
                    "down": down,
                    "key": {"type": "qcode", "data": name},
                },
            } for name, down in items],
        },
    }


def wait_for_socket(path: Path, process: subprocess.Popen) -> None:
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"emulator exited early ({process.returncode})")
        if path.exists():
            return
        time.sleep(0.05)
    raise RuntimeError("QMP socket did not appear")


def qmp_run(path: Path, commands: list[dict]) -> None:
    with socket.socket(socket.AF_UNIX) as sock:
        sock.connect(str(path))
        stream = sock.makefile("rwb", buffering=0)
        greeting = json.loads(stream.readline())
        if "QMP" not in greeting:
            raise RuntimeError(f"invalid QMP greeting: {greeting}")
        for command in [{"execute": "qmp_capabilities"}, *commands]:
            stream.write(json.dumps(command).encode() + b"\n")
            while True:
                response = json.loads(stream.readline())
                if "error" in response:
                    raise RuntimeError(f"QMP rejected {command}: {response}")
                if "return" in response:
                    break


def run_case(directory: Path, name: str, yaml: str,
             commands: list[dict]) -> str:
    keymap = directory / f"{name}.yaml"
    qmp = directory / f"{name}.qmp"
    log_path = directory / f"{name}.log"
    keymap.write_text(yaml)
    command = [
        "bash", str(RUNNER), "--game", "swe1", "--no-savedata",
        "--update", "210",
        "--display", "none", "--audio", "none", "--uart-quiet", "-v",
        "--switch-keymap", str(keymap), "--",
        "-qmp", f"unix:{qmp},server=on,wait=off",
    ]
    with log_path.open("w") as log:
        process = subprocess.Popen(
            command, cwd=ROOT, env=os.environ.copy(),
            stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        try:
            wait_for_socket(qmp, process)
            qmp_run(qmp, commands)
            process.wait(timeout=10)
        except Exception:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            raise
    return log_path.read_text(errors="replace")


def require(text: str, pattern: str) -> None:
    if not re.search(pattern, text, re.MULTILINE | re.DOTALL):
        raise AssertionError(f"missing log sequence: {pattern}")


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="p2k-keymap-smoke-") as temporary:
        directory = Path(temporary)
        valid = run_case(
            directory,
            "valid",
            "switches:\n  a: 13\n  b: 14\n  c: 13\n",
            [
                key_events(("a", True)),
                key_events(("b", True)),
                key_events(("c", True)),
                key_events(("f12", True), ("f12", False)),
                key_events(("c", False), ("b", False)),
                key_events(("f12", True), ("f12", False)),
                key_events(("a", False)),
                key_events(("f12", True), ("f12", False)),
                key_events(("f1", True)),
            ],
        )
        require(
            valid,
            r"matrix switch 13 PRESSED.*matrix switch 14 PRESSED.*"
            r"switch1=0x0c.*matrix switch 14 released.*switch1=0x04.*"
            r"matrix switch 13 released.*switch1=0x00",
        )

        ctrl_independent = run_case(
            directory,
            "ctrl-independent",
            "switches:\n  a: 13\n",
            [
                key_events(("a", True)),
                key_events(("1", True), ("1", False)),
                key_events(("3", True), ("3", False)),
                key_events(("ctrl", True)),
                key_events(("a", False)),
                key_events(("f12", True), ("f12", False)),
                key_events(("ctrl", False)),
                key_events(("f12", True), ("f12", False)),
                key_events(("f1", True)),
            ],
        )
        require(
            ctrl_independent,
            r"matrix switch 13 PRESSED.*switch1=0x04.*"
            r"matrix switch 13 released.*switch1=0x00",
        )

        invalid = run_case(
            directory,
            "invalid",
            "switches:\n  c: 13 trailing-garbage\n  C: 14\n",
            [
                key_events(("c", True)),
                key_events(("c", False)),
                key_events(("f1", True)),
            ],
        )
        require(invalid, r"invalid switch keymap")
        require(invalid, r"coin slot 1 pulse fired")
        if re.search(r"loaded \d+ switch key binding", invalid):
            raise AssertionError("invalid keymap was partially loaded")

    print("switch-keymap smoke test: PASS")


if __name__ == "__main__":
    main()
