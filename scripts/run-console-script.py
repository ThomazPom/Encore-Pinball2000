#!/usr/bin/env python3
"""Execute a state-aware Encore console script against a live guest."""

from __future__ import annotations

import argparse
import re
import socket
import sys
import time
import wave
from dataclasses import dataclass
from pathlib import Path


MAX_EXPANDED_ACTIONS = 100_000
DEFAULT_COMMAND_TIMEOUT = 120.0
POLL_INTERVAL = 0.5


@dataclass(frozen=True)
class Action:
    number: int
    text: str


def fail(path: Path, action: Action, message: str) -> ValueError:
    return ValueError(f"{path}:{action.number}: {message}")


def nonnegative_float(path: Path, action: Action, value: str, name: str) -> float:
    try:
        result = float(value)
    except ValueError as error:
        raise fail(path, action, f"{name} must be a number, got {value!r}") from error
    if result < 0:
        raise fail(path, action, f"{name} must not be negative")
    return result


def positive_float(path: Path, action: Action, value: str, name: str) -> float:
    result = nonnegative_float(path, action, value, name)
    if result == 0:
        raise fail(path, action, f"{name} must be greater than zero")
    return result


def split_directive(action: Action) -> tuple[str, str]:
    head, _, tail = action.text.partition(" ")
    return head, tail.strip()


def parse_wait_for(path: Path, action: Action) -> tuple[float, str, str, bool]:
    directive, rest = split_directive(action)
    parts = rest.split(None, 1)
    if len(parts) != 2 or "=>" not in parts[1]:
        raise fail(
            path,
            action,
            f"{directive} expects SECONDS COMMAND => EXPECTED",
        )
    timeout = positive_float(path, action, parts[0], "timeout")
    command, expected = (part.strip() for part in parts[1].split("=>", 1))
    if not command or not expected:
        raise fail(path, action, f"{directive} needs a command and expected text")
    regex = directive == "@wait-for-regex"
    if regex:
        try:
            re.compile(expected)
        except re.error as error:
            raise fail(path, action, f"invalid regular expression: {error}") from error
    return timeout, command, expected, regex


def validate_action(path: Path, action: Action) -> None:
    if not action.text.startswith("@"):
        return
    directive, rest = split_directive(action)
    if directive == "@wait":
        if not rest or len(rest.split()) != 1:
            raise fail(path, action, "@wait expects SECONDS")
        nonnegative_float(path, action, rest, "wait")
    elif directive == "@key":
        parts = rest.split()
        if not 1 <= len(parts) <= 2:
            raise fail(path, action, "@key expects KEY [HOLD_SECONDS]")
        if len(parts) == 2:
            positive_float(path, action, parts[1], "key hold")
    elif directive == "@switch":
        parts = rest.split()
        if not 1 <= len(parts) <= 2 or not re.fullmatch(r"[1-8][1-8]", parts[0]):
            raise fail(path, action, "@switch expects NUMBER_11_TO_88 [HOLD_SECONDS]")
        if len(parts) == 2:
            positive_float(path, action, parts[1], "switch hold")
    elif directive in {"@assert", "@assert-not", "@assert-regex", "@assert-not-regex"}:
        if not rest:
            raise fail(path, action, f"{directive} expects text")
        if "regex" in directive:
            try:
                re.compile(rest)
            except re.error as error:
                raise fail(path, action, f"invalid regular expression: {error}") from error
    elif directive in {"@wait-for", "@wait-for-regex"}:
        parse_wait_for(path, action)
    elif directive == "@screenshot":
        if rest and (Path(rest).name != rest or not re.fullmatch(r"[A-Za-z0-9_.-]+", rest)):
            raise fail(path, action, "@screenshot label must be a simple filename stem")
    elif directive == "@record-audio":
        parts = rest.split()
        if not 1 <= len(parts) <= 2:
            raise fail(path, action, "@record-audio expects SECONDS [LABEL]")
        positive_float(path, action, parts[0], "recording duration")
        if len(parts) == 2 and not re.fullmatch(r"[A-Za-z0-9_.-]+", parts[1]):
            raise fail(path, action, "audio label must be a simple filename stem")
    elif directive == "@echo":
        if not rest:
            raise fail(path, action, "@echo expects text")
    else:
        raise fail(path, action, f"unknown directive {directive!r}")


def parse_script(path: Path) -> list[Action]:
    source: list[Action] = []
    for number, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.strip()
        if line and not line.startswith("#"):
            source.append(Action(number, line))

    def block(index: int, nested: bool) -> tuple[list[Action], int]:
        result: list[Action] = []
        while index < len(source):
            action = source[index]
            directive, rest = split_directive(action)
            if directive == "@end":
                if rest:
                    raise fail(path, action, "@end takes no arguments")
                if not nested:
                    raise fail(path, action, "@end without matching @repeat")
                return result, index + 1
            if directive == "@repeat":
                if len(rest.split()) != 1:
                    raise fail(path, action, "@repeat expects COUNT")
                try:
                    count = int(rest, 10)
                except ValueError as error:
                    raise fail(path, action, "@repeat COUNT must be an integer") from error
                if count < 1 or count > 10_000:
                    raise fail(path, action, "@repeat COUNT must be from 1 through 10000")
                repeated, index = block(index + 1, True)
                if len(result) + count * len(repeated) > MAX_EXPANDED_ACTIONS:
                    raise fail(path, action, "expanded script exceeds 100000 actions")
                result.extend(repeated * count)
                continue
            validate_action(path, action)
            result.append(action)
            if len(result) > MAX_EXPANDED_ACTIONS:
                raise fail(path, action, "expanded script exceeds 100000 actions")
            index += 1
        if nested:
            raise ValueError(f"{path}: missing @end for @repeat")
        return result, index

    actions, _ = block(0, False)
    return actions


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
        if len(received) > 4 * 1024 * 1024:
            del received[: len(received) - 2 * 1024 * 1024]
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
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.settimeout(0.5)
        try:
            sock.connect(str(path))
            wait_for(sock, b"(qemu)", 5.0)
            return sock
        except OSError:
            sock.close()
            time.sleep(0.2)
    raise RuntimeError(f"timed out connecting to QEMU monitor at {path}")


def monitor_command(monitor: socket.socket, command: str) -> bytes:
    monitor.sendall(command.encode() + b"\n")
    return wait_for(monitor, b"(qemu)", 10.0)


def console_command(
    console: socket.socket,
    command: str,
    show_response: bool = True,
) -> bytes:
    if show_response:
        print(f"[console-script] XINU> {command}", flush=True)
    console.sendall(command.encode() + b"\r")
    response = wait_for(console, b"%", DEFAULT_COMMAND_TIMEOUT)
    if show_response:
        sys.stdout.buffer.write(response)
        sys.stdout.flush()
    return response


def response_text(response: bytes) -> str:
    return response.decode(errors="replace")


def file_snapshot(directory: Path) -> dict[Path, tuple[int, int]]:
    result: dict[Path, tuple[int, int]] = {}
    for path in directory.glob("p2k_screen_*"):
        try:
            stat = path.stat()
        except FileNotFoundError:
            continue
        result[path] = (stat.st_mtime_ns, stat.st_size)
    return result


def take_screenshot(monitor: socket.socket, directory: Path, label: str) -> Path:
    before = file_snapshot(directory)
    monitor_command(monitor, "sendkey f3")
    deadline = time.monotonic() + 8.0
    captured: Path | None = None
    while time.monotonic() < deadline:
        for path, state in file_snapshot(directory).items():
            if path not in before or before[path] != state:
                captured = path
                break
        if captured:
            break
        time.sleep(0.05)
    if not captured:
        raise RuntimeError(f"screenshot did not appear under {directory}")
    if label:
        destination = directory / f"{label}{captured.suffix}"
        captured.replace(destination)
        captured = destination
    print(f"[console-script] screenshot {captured}", flush=True)
    return captured


def read_capture_format(raw_path: Path) -> tuple[int, int]:
    format_path = Path(f"{raw_path}.format")
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        try:
            fields = format_path.read_text().split()
        except FileNotFoundError:
            time.sleep(0.05)
            continue
        if len(fields) == 2:
            rate, channels = (int(field) for field in fields)
            if rate > 0 and channels in (1, 2):
                return rate, channels
        raise RuntimeError(f"invalid audio capture format in {format_path}")
    raise RuntimeError("audio capture was requested but the DCS engine did not initialize it")


def record_audio(
    monitor: socket.socket,
    raw_path: Path,
    directory: Path,
    duration: float,
    label: str,
) -> Path:
    rate, channels = read_capture_format(raw_path)
    try:
        start = raw_path.stat().st_size
    except FileNotFoundError:
        start = 0
    hold_ms = max(1, round(duration * 1000))
    monitor_command(monitor, f"sendkey f11 {hold_ms}")
    time.sleep(duration + 0.25)
    try:
        end = raw_path.stat().st_size
    except FileNotFoundError as error:
        raise RuntimeError("DCS audio capture file was not created") from error
    frame_bytes = channels * 2
    end -= (end - start) % frame_bytes
    if end <= start:
        raise RuntimeError("DCS audio capture produced no PCM frames")
    if not label:
        label = time.strftime("p2k_audio_%Y%m%d_%H%M%S")
    destination = directory / f"{label}.wav"
    with raw_path.open("rb") as source:
        source.seek(start)
        pcm = source.read(end - start)
    with wave.open(str(destination), "wb") as output:
        output.setnchannels(channels)
        output.setsampwidth(2)
        output.setframerate(rate)
        output.writeframes(pcm)
    print(
        f"[console-script] audio {destination} "
        f"({len(pcm) // frame_bytes} frames, {rate} Hz, {channels} ch)",
        flush=True,
    )
    return destination


def run_actions(
    path: Path,
    actions: list[Action],
    console: socket.socket,
    monitor: socket.socket,
    screenshot_dir: Path,
    audio_capture: Path | None,
) -> None:
    last_response = b""
    for action in actions:
        directive, rest = split_directive(action)
        if not action.text.startswith("@"):
            last_response = console_command(console, action.text)
        elif directive == "@wait":
            delay = nonnegative_float(path, action, rest, "wait")
            print(f"[console-script] wait {delay:g}s", flush=True)
            time.sleep(delay)
        elif directive == "@key":
            parts = rest.split()
            command = f"sendkey {parts[0]}"
            if len(parts) == 2:
                command += f" {max(1, round(float(parts[1]) * 1000))}"
            print(f"[console-script] key {parts[0]}", flush=True)
            monitor_command(monitor, command)
        elif directive == "@switch":
            parts = rest.split()
            number = parts[0]
            duration = float(parts[1]) if len(parts) == 2 else 0.1
            print(f"[console-script] switch {number} for {duration:g}s", flush=True)
            monitor_command(monitor, f"sendkey {number[0]} 50")
            time.sleep(0.08)
            monitor_command(monitor, f"sendkey {number[1]} 50")
            time.sleep(0.08)
            monitor_command(monitor, f"sendkey ctrl {max(1, round(duration * 1000))}")
            time.sleep(duration + 0.15)
        elif directive in {"@assert", "@assert-not", "@assert-regex", "@assert-not-regex"}:
            if not last_response:
                raise fail(path, action, f"{directive} has no preceding XINU response")
            text = response_text(last_response)
            if "regex" in directive:
                matched = re.search(rest, text, re.MULTILINE) is not None
            else:
                matched = rest in text
            expected = directive in {"@assert", "@assert-regex"}
            if matched != expected:
                relation = "match" if expected else "exclude"
                raise fail(path, action, f"response did not {relation} {rest!r}")
            print(f"[console-script] assertion passed: {rest}", flush=True)
        elif directive in {"@wait-for", "@wait-for-regex"}:
            timeout, command, expected, regex = parse_wait_for(path, action)
            deadline = time.monotonic() + timeout
            attempts = 0
            print(
                f"[console-script] polling XINU> {command} (up to {timeout:g}s)",
                flush=True,
            )
            while True:
                attempts += 1
                last_response = console_command(console, command, show_response=False)
                text = response_text(last_response)
                matched = re.search(expected, text, re.MULTILINE) is not None if regex else expected in text
                if matched:
                    print(
                        f"[console-script] wait-for matched after {attempts} attempt(s): {expected}",
                        flush=True,
                    )
                    break
                if time.monotonic() >= deadline:
                    sys.stdout.buffer.write(last_response)
                    sys.stdout.flush()
                    raise fail(path, action, f"timed out waiting for {expected!r} from {command!r}")
                time.sleep(min(POLL_INTERVAL, max(0.0, deadline - time.monotonic())))
        elif directive == "@screenshot":
            take_screenshot(monitor, screenshot_dir, rest)
        elif directive == "@record-audio":
            if audio_capture is None:
                raise fail(path, action, "audio capture was not prepared by the launcher")
            parts = rest.split()
            duration = float(parts[0])
            label = parts[1] if len(parts) == 2 else ""
            record_audio(monitor, audio_capture, screenshot_dir, duration, label)
        elif directive == "@echo":
            print(f"[console-script] {rest}", flush=True)
        else:  # parse_script validates every directive before QEMU starts.
            raise AssertionError(directive)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Execute XINU commands and Encore automation directives"
    )
    parser.add_argument("script", type=Path)
    parser.add_argument("--check", action="store_true", help="validate and exit")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int)
    parser.add_argument("--monitor", type=Path)
    parser.add_argument("--boot-timeout", type=float, default=90.0)
    parser.add_argument("--screenshot-dir", type=Path, default=Path("/tmp"))
    parser.add_argument("--audio-capture", type=Path)
    args = parser.parse_args()

    actions = parse_script(args.script)
    if args.check:
        print(f"[console-script] valid: {len(actions)} expanded action(s)")
        return 0
    if args.port is None or args.monitor is None:
        parser.error("--port and --monitor are required unless --check is used")
    if not args.screenshot_dir.is_dir():
        raise ValueError(f"screenshot directory does not exist: {args.screenshot_dir}")

    with connect_tcp(args.host, args.port, args.boot_timeout) as console, \
            connect_monitor(args.monitor, args.boot_timeout) as monitor:
        print(f"[console-script] waiting for XINU ({args.script})", flush=True)
        wait_for(console, b"%", args.boot_timeout, wake=True)
        print("[console-script] XINU ready", flush=True)
        run_actions(
            args.script,
            actions,
            console,
            monitor,
            args.screenshot_dir,
            args.audio_capture,
        )

    print("[console-script] complete; emulator remains running", flush=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"[console-script] ERROR: {error}", file=sys.stderr)
        raise SystemExit(2)
