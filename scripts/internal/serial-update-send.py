#!/usr/bin/env python3
"""Send an Encore update bundle through XINA's native COM1 fupdate protocol.

This is a deterministic test client, not a product UI. It exercises the
historical 518-byte protocol so the emulator can be validated without treating
the received wire bytes as the resulting flash contents.
"""

from __future__ import annotations

import argparse
import os
import select
import socket
import stat
import struct
import sys
import termios
import time
import tty
from pathlib import Path


KINDS = ("bootdata", "im_flsh0", "game", "symbols", "sf")
PAYLOAD_SIZE = 512


BAUD_RATES = {
    baud: getattr(termios, f"B{baud}")
    for baud in (9600, 19200, 38400, 57600, 115200, 230400)
}


class SerialStream:
    """Small socket-like wrapper around a raw host serial device."""

    def __init__(self, path: Path, baud: int = 9600) -> None:
        mode = path.stat().st_mode
        if not stat.S_ISCHR(mode):
            raise ValueError(f"serial device is not a character device: {path}")
        self.path = path
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self.timeout: float | None = None
        self.original = termios.tcgetattr(self.fd)
        tty.setraw(self.fd, termios.TCSANOW)
        self.set_baud(baud)

    def __enter__(self) -> "SerialStream":
        return self

    def __exit__(self, *_args: object) -> None:
        self.close()

    def close(self) -> None:
        if self.fd < 0:
            return
        try:
            termios.tcsetattr(self.fd, termios.TCSANOW, self.original)
        finally:
            os.close(self.fd)
            self.fd = -1

    def gettimeout(self) -> float | None:
        return self.timeout

    def settimeout(self, timeout: float | None) -> None:
        self.timeout = timeout

    def set_baud(self, baud: int) -> None:
        speed = BAUD_RATES[baud]
        attributes = termios.tcgetattr(self.fd)
        attributes[4] = speed
        attributes[5] = speed
        termios.tcsetattr(self.fd, termios.TCSANOW, attributes)

    def _wait(self, readable: bool) -> None:
        readers = [self.fd] if readable else []
        writers = [] if readable else [self.fd]
        ready = select.select(readers, writers, [], self.timeout)
        if not ready[0] and not ready[1]:
            raise TimeoutError(f"serial device timed out: {self.path}")

    def recv(self, count: int) -> bytes:
        while True:
            self._wait(readable=True)
            try:
                return os.read(self.fd, count)
            except BlockingIOError:
                continue

    def sendall(self, data: bytes) -> None:
        view = memoryview(data)
        while view:
            self._wait(readable=False)
            try:
                written = os.write(self.fd, view)
            except BlockingIOError:
                continue
            view = view[written:]


def unique_component(bundle: Path, kind: str) -> Path:
    matches = sorted(bundle.glob(f"*_{kind}.rom"))
    if len(matches) != 1:
        raise ValueError(
            f"expected exactly one *_{kind}.rom in {bundle}, got {len(matches)}"
        )
    return matches[0]


def make_frame(sequence: int, payload: bytes = b"") -> bytes:
    if len(payload) > PAYLOAD_SIZE:
        raise ValueError("payload exceeds 512 bytes")
    checksum = (-sum(payload)) & 0xFFFF
    return struct.pack(
        "<HHH512s", sequence & 0xFFFF, len(payload), checksum, payload
    )


def recv_exact(stream: socket.socket | SerialStream, count: int) -> bytes:
    result = bytearray()
    while len(result) < count:
        block = stream.recv(count - len(result))
        if not block:
            raise ConnectionError("COM1 connection closed while waiting for reply")
        result.extend(block)
    return bytes(result)


def recv_protocol_reply(stream: socket.socket | SerialStream) -> bytes:
    """Find ACK/NAK even when asynchronous XINA diagnostics precede it."""
    diagnostic = bytearray()
    while True:
        first = recv_exact(stream, 1)
        if first[0] in (0x06, 0x15):
            if diagnostic:
                sys.stdout.buffer.write(diagnostic)
                sys.stdout.buffer.flush()
            return first + recv_exact(stream, 3)
        diagnostic.extend(first)


def drain_until_quiet(
    stream: socket.socket | SerialStream,
    quiet: float = 0.25,
    max_total: float = 2.0,
) -> bytes:
    previous = stream.gettimeout()
    result = bytearray()
    deadline = time.monotonic() + max_total
    try:
        while time.monotonic() < deadline:
            stream.settimeout(min(quiet, max(0.001, deadline - time.monotonic())))
            try:
                block = stream.recv(65536)
            except TimeoutError:
                break
            if not block:
                break
            result.extend(block)
    finally:
        stream.settimeout(previous)
    return bytes(result)


def wait_for_prompt(
    stream: socket.socket | SerialStream,
    timeout: float,
    ready_markers: tuple[bytes, ...] = (
        b"Game(",
        b"MultiDev: End All Multiballs Now!",
    ),
) -> bytes:
    """Release boot-time COM1 polls and wait for the post-game-init prompt.

    XINA exposes an early shell prompt before the selected game has completed
    initialization.  fupdate rejects work in that interval, so a bare first
    `% ` is not a sufficient readiness signal.
    """
    deadline = time.monotonic() + timeout
    output = bytearray()
    next_newline = 0.0
    previous = stream.gettimeout()
    try:
        stream.settimeout(0.25)
        while time.monotonic() < deadline:
            now = time.monotonic()
            marker_at = max(output.find(marker) for marker in ready_markers)
            # The runner seeds the special boot-time synchronous reads. Do
            # not inject live console input during initialization. Once game
            # startup is visible, a newline is safe and requests a fresh
            # prompt if the boot log did not naturally end with one.
            if marker_at >= 0 and now >= next_newline:
                stream.sendall(b"\r\n")
                next_newline = now + 1.0
            try:
                block = stream.recv(65536)
            except TimeoutError:
                continue
            if not block:
                raise ConnectionError("COM1 connection closed before XINA prompt")
            output.extend(block)
            sys.stdout.buffer.write(block)
            sys.stdout.buffer.flush()
            marker_at = max(output.find(marker) for marker in ready_markers)
            prompt_at = output.rfind(b"% ")
            if marker_at >= 0 and prompt_at > marker_at:
                trailing = drain_until_quiet(stream, 0.5)
                output.extend(trailing)
                sys.stdout.buffer.write(trailing)
                sys.stdout.buffer.flush()
                return bytes(output)
    finally:
        stream.settimeout(previous)
    raise TimeoutError(f"XINA prompt did not appear within {timeout:g} seconds")


def send_frame(
    stream: socket.socket | SerialStream,
    sequence: int,
    payload: bytes = b"",
) -> None:
    stream.sendall(make_frame(sequence, payload))
    reply = recv_protocol_reply(stream)
    if reply[0] != 0x06:
        raise RuntimeError(
            f"XINA rejected sequence 0x{sequence & 0xffff:04x}: {reply.hex()}"
        )


def send_image(
    stream: socket.socket | SerialStream,
    data: bytes,
    sequence: int,
    label: str,
) -> int:
    total = (len(data) + PAYLOAD_SIZE - 1) // PAYLOAD_SIZE
    for index, offset in enumerate(range(0, len(data), PAYLOAD_SIZE), start=1):
        send_frame(stream, sequence, data[offset:offset + PAYLOAD_SIZE])
        sequence = (sequence + 1) & 0xFFFF
        if index == total or index % 256 == 0:
            print(f"[serial-send] {label}: {index}/{total} packets", flush=True)
    return sequence


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Send a five-component update bundle through XINA COM1"
    )
    parser.add_argument("bundle", type=Path, help="inner 50069/50070 bundle directory")
    parser.add_argument("--host", default="127.0.0.1")
    transport = parser.add_mutually_exclusive_group(required=True)
    transport.add_argument("--port", type=int, help="raw COM1 TCP port")
    transport.add_argument(
        "--device",
        type=Path,
        help="raw host serial device, for example /dev/serial/by-id/...",
    )
    parser.add_argument(
        "--baud",
        default=115200,
        type=int,
        choices=(9600, 19200, 38400, 57600, 115200, 230400),
        help=(
            "guest fupdate baud rate (default: 115200, accepted by factory "
            "and current updates; 230400 is only accepted by newer updates)"
        ),
    )
    parser.add_argument(
        "--mode",
        choices=("full", "game", "sfonly"),
        default="full",
        help=(
            "XINA update scope: full sends game flash plus sound flash; "
            "game sends only BAR3; sfonly sends only DCS sound flash"
        ),
    )
    parser.add_argument("--connect-timeout", default=90.0, type=float)
    parser.add_argument("--prompt-timeout", default=180.0, type=float)
    parser.add_argument(
        "--reply-timeout", default=300.0, type=float,
        help=(
            "maximum wait for one XINA ACK/NAK; the first DCS data packet "
            "can wait while the sound flash is prepared"
        ),
    )
    parser.add_argument(
        "--boot-newlines", default=48, type=int,
        help="initial CR/LF pairs used to release XINA's synchronous COM1 boot reads",
    )
    parser.add_argument(
        "--assume-ready", action="store_true",
        help="skip game-initialization detection (only for an already-ready guest)",
    )
    parser.add_argument(
        "--start-delay", default=0.0, type=float,
        help=(
            "diagnostic delay after the command (normally zero: XINA's "
            "native receive timeout is CPU-loop based and expires quickly)"
        ),
    )
    return parser.parse_args(argv)


def main() -> int:
    args = parse_args()
    try:
        paths = {kind: unique_component(args.bundle, kind) for kind in KINDS}
        game_image = b"".join(paths[kind].read_bytes() for kind in KINDS[:-1])
        sound_image = paths["sf"].read_bytes()
        if len(sound_image) != 0x100000:
            raise ValueError(
                f"sound flash must be 1048576 bytes, got {len(sound_image)}"
            )

        if args.device is not None:
            stream = SerialStream(args.device, 9600)
        else:
            deadline = time.monotonic() + args.connect_timeout
            while True:
                try:
                    stream = socket.create_connection(
                        (args.host, args.port), timeout=5
                    )
                    break
                except OSError:
                    if time.monotonic() >= deadline:
                        raise
                    time.sleep(0.25)
        with stream:
            stream.settimeout(args.reply_timeout)
            if args.boot_newlines:
                stream.sendall(b"\r\n" * args.boot_newlines)
            # The bounded startup burst releases synchronous COM1 diagnostic
            # reads. wait_for_prompt drains all resulting text before any
            # binary frame is sent, so BEL/prompt bytes cannot mask an ACK.
            if args.assume_ready:
                time.sleep(1.0)
                diagnostic = drain_until_quiet(stream, 0.5)
                sys.stdout.buffer.write(diagnostic)
                sys.stdout.buffer.flush()
            else:
                wait_for_prompt(stream, args.prompt_timeout)
            # XINA's optional argument changes the accepted packet stream:
            #   no suffix -> game flash only, then 0xffff
            #   sf        -> game flash, 0xfffe, sound flash, then 0xffff
            #   sfonly    -> 0xfffe, sound flash, then 0xffff
            # Sending 0xfffe after the suffix-less command is ACKed at the
            # transport layer but then rejected semantically as an unexpected
            # packet, which the update UI reports as FATAL ERROR.
            suffix = {"full": " sf", "game": "", "sfonly": " sfonly"}[
                args.mode
            ]
            command = (
                f"fupdate load com1 {args.baud}{suffix}\r\n"
            ).encode("ascii")
            stream.sendall(command)
            if isinstance(stream, SerialStream):
                stream.set_baud(args.baud)
            time.sleep(args.start_delay)
            diagnostic = drain_until_quiet(stream, 1.0)
            if diagnostic:
                sys.stdout.buffer.write(diagnostic)
                sys.stdout.buffer.flush()

            sequence = 0
            if args.mode != "sfonly":
                sequence = send_image(stream, game_image, sequence, "BAR3")
            if args.mode != "game":
                send_frame(stream, 0xFFFE)
                print("[serial-send] DCS transition acknowledged", flush=True)
                sequence = send_image(stream, sound_image, sequence, "DCS")
            send_frame(stream, 0xFFFF)
            print(
                f"[serial-send] final packet acknowledged; next sequence={sequence}",
                flush=True,
            )
    except (OSError, ValueError, RuntimeError, ConnectionError) as error:
        print(f"serial-send: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
