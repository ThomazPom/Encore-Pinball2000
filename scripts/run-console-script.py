#!/usr/bin/env python3
"""Run XINU commands and cabinet keys against a live Encore instance."""

from __future__ import annotations

import argparse
import socket
import sys
import time
from pathlib import Path


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
    raise RuntimeError(f"timed out waiting for {token!r}")


def connect_tcp(host: str, port: int, timeout: float) -> socket.socket:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            sock = socket.create_connection((host, port), timeout=1.0)
            sock.settimeout(0.5)
            return sock
        except OSError:
            time.sleep(0.2)
    raise RuntimeError(f"timed out connecting to XINU console at {host}:{port}")


def connect_monitor(path: Path, timeout: float) -> socket.socket:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.settimeout(0.5)
            sock.connect(str(path))
            try:
                sock.recv(4096)
            except socket.timeout:
                pass
            return sock
        except OSError:
            sock.close()
            time.sleep(0.2)
    raise RuntimeError(f"timed out connecting to QEMU monitor at {path}")


def load_lines(path: Path) -> list[tuple[int, str]]:
    result = []
    for number, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.strip()
        if line and not line.startswith("#"):
            result.append((number, line))
    return result


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Execute XINU commands plus optional @wait/@key directives"
    )
    parser.add_argument("script", type=Path)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--monitor", type=Path, required=True)
    parser.add_argument("--boot-timeout", type=float, default=90.0)
    args = parser.parse_args()

    lines = load_lines(args.script)
    with connect_tcp(args.host, args.port, args.boot_timeout) as console, \
            connect_monitor(args.monitor, args.boot_timeout) as monitor:
        print(f"[console-script] waiting for XINU ({args.script})", flush=True)
        wait_for(console, b"%", args.boot_timeout, wake=True)
        print("[console-script] XINU ready", flush=True)

        for number, line in lines:
            if line.startswith("@wait "):
                delay = float(line.split(None, 1)[1])
                if delay < 0:
                    raise ValueError(f"{args.script}:{number}: negative wait")
                print(f"[console-script] wait {delay:g}s", flush=True)
                time.sleep(delay)
            elif line.startswith("@key "):
                key = line.split(None, 1)[1].strip()
                if not key or any(char.isspace() for char in key):
                    raise ValueError(f"{args.script}:{number}: @key expects one QEMU key name")
                print(f"[console-script] key {key}", flush=True)
                monitor.sendall(f"sendkey {key}\n".encode())
            elif line.startswith("@"):
                raise ValueError(f"{args.script}:{number}: unknown directive {line!r}")
            else:
                print(f"[console-script] XINU> {line}", flush=True)
                console.sendall(line.encode() + b"\r")
                response = wait_for(console, b"%", 120.0)
                sys.stdout.buffer.write(response)
                sys.stdout.flush()

    print("[console-script] complete; emulator remains running", flush=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"[console-script] ERROR: {error}", file=sys.stderr)
        raise SystemExit(2)
