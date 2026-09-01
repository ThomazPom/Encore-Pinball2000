#!/usr/bin/env python3
"""Capture a Pinball 2000 PUB card through the XINA serial console."""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import select
import socket
import struct
import termios
import time
from pathlib import Path


DUMP_LINE = re.compile(
    rb"^0x[0-9a-fA-F]{8}\s{2}"
    rb"((?:[0-9a-fA-F]{2}\s+){15}[0-9a-fA-F]{2})(?:\s{2}|$)"
)
# The original console routine performs a synchronous fprintf for every byte.
# Large single commands can starve old XINA builds; 4 KiB keeps the console
# responsive while retaining useful throughput.
CHUNK = 0x1000


class Transport:
    def send(self, data: bytes) -> None:
        raise NotImplementedError

    def receive(self, timeout: float) -> bytes:
        raise NotImplementedError

    def close(self) -> None:
        pass


class TcpTransport(Transport):
    def __init__(self, endpoint: str):
        host, port = endpoint.rsplit(":", 1)
        self.sock = socket.create_connection((host, int(port)), timeout=10)
        self.sock.setblocking(False)

    def send(self, data: bytes) -> None:
        self.sock.sendall(data)

    def receive(self, timeout: float) -> bytes:
        ready, _, _ = select.select([self.sock], [], [], timeout)
        return self.sock.recv(65536) if ready else b""

    def close(self) -> None:
        self.sock.close()


class SerialTransport(Transport):
    def __init__(self, device: str, baud: int):
        self.fd = os.open(device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(self.fd)
        speed = getattr(termios, f"B{baud}", None)
        if speed is None:
            raise ValueError(f"unsupported baud rate: {baud}")
        attrs[0] = termios.IGNPAR
        attrs[1] = 0
        attrs[2] = termios.CS8 | termios.CLOCAL | termios.CREAD
        attrs[3] = 0
        attrs[4] = speed
        attrs[5] = speed
        attrs[6][termios.VMIN] = 0
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)

    def send(self, data: bytes) -> None:
        os.write(self.fd, data)

    def receive(self, timeout: float) -> bytes:
        ready, _, _ = select.select([self.fd], [], [], timeout)
        return os.read(self.fd, 65536) if ready else b""

    def close(self) -> None:
        os.close(self.fd)


def capture_command(link: Transport, command: str, count: int,
                    timeout: float) -> bytes:
    link.send(command.encode("ascii") + b"\r")
    stream = bytearray()
    deadline = time.monotonic() + timeout
    last_data = time.monotonic()

    while time.monotonic() < deadline:
        block = link.receive(0.25)
        if block:
            stream.extend(block)
            last_data = time.monotonic()
            # XINA prints a fresh percent prompt after the dump. Waiting for a
            # short quiet interval prevents an incidental '%' in ASCII output
            # from terminating the transfer.
        elif len(stream) and time.monotonic() - last_data > 0.5 and \
                re.search(rb"(?:^|[\r\n])%\s*$", stream):
            break
    else:
        raise TimeoutError(f"serial timeout while running: {command}")

    result = bytearray()
    for line in bytes(stream).splitlines():
        match = DUMP_LINE.match(line.strip(b"\r"))
        if match:
            result.extend(bytes.fromhex(match.group(1).decode("ascii")))
    if len(result) != count:
        raise RuntimeError(
            f"{command!r}: captured {len(result)} bytes, expected {count}"
        )
    return bytes(result)


def dump_range(link: Transport, bank: str, start: int, count: int,
               timeout: float) -> bytes:
    output = bytearray()
    while len(output) < count:
        offset = start + len(output)
        length = min(CHUNK, count - len(output))
        print(f"  {bank}: 0x{offset:06x}..0x{offset + length:06x}",
              flush=True)
        output.extend(capture_command(
            link, f"pub {bank} dump {offset} {length}", length, timeout
        ))
    return bytes(output)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_layout(header: bytes) -> tuple[int, int, int, int, int]:
    if len(header) < 0x58:
        raise ValueError("short PUB header")
    game_id = struct.unpack_from("<I", header, 0x3C)[0]
    version = struct.unpack_from("<I", header, 0x44)[0]
    image_size = struct.unpack_from("<I", header, 0x2C)[0] * 4
    game_size = struct.unpack_from("<I", header, 0x4C)[0] * 4
    symbols_size = struct.unpack_from("<I", header, 0x54)[0] * 4
    if game_id not in (50069, 50070) or not all(
        0 < size <= 0x400000 for size in (image_size, game_size, symbols_size)
    ):
        raise ValueError(
            "PUB header has implausible component metadata; refusing to guess"
        )
    return game_id, version, image_size, game_size, symbols_size


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Dump and reconstruct ROM files from a PUB card via XINA"
    )
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--tcp", metavar="HOST:PORT",
                        help="Encore/XINA UART TCP endpoint")
    source.add_argument("--device", metavar="TTY",
                        help="physical serial device, e.g. /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=38400,
                        help="physical serial baud rate (default: 38400)")
    parser.add_argument("--output", type=Path, required=True,
                        help="new directory receiving raw and reconstructed data")
    parser.add_argument("--timeout", type=float, default=120,
                        help="timeout per 64-KiB command (default: 120 seconds)")
    args = parser.parse_args()

    if args.output.exists():
        parser.error(f"output already exists: {args.output}")
    args.output.mkdir(parents=True)
    link: Transport = (TcpTransport(args.tcp) if args.tcp else
                       SerialTransport(args.device, args.baud))
    try:
        # Synchronize with the monitor and discard its prompt/boot tail.
        link.send(b"\r")
        quiet_until = time.monotonic() + 1.0
        while time.monotonic() < quiet_until:
            if link.receive(0.1):
                quiet_until = time.monotonic() + 0.5

        print("Reading PUB metadata...", flush=True)
        header = dump_range(link, "game", 0, 0x100, args.timeout)
        game_id, version, image_size, game_size, symbols_size = parse_layout(header)
        total = 0x8000 + image_size + game_size + symbols_size
        if total > 0x400000:
            raise ValueError(f"declared PUB contents exceed game bank: 0x{total:x}")
        major = 1 if version < 100 else version // 100
        print(f"Detected game {game_id}, version {major}.{version % 100:02d}; "
              f"dumping 0x{total:x} bytes", flush=True)
        raw = dump_range(link, "game", 0, total, args.timeout)
    finally:
        link.close()

    # XINA stores community 1.xx updates as the minor integer (66 => 1.66),
    # while bundle filenames use the four-digit 0166 spelling.
    filename_version = version + 100 if version < 100 else version
    version_tag = f"{filename_version:04d}"
    stem = f"pin2000_{game_id}_{version_tag}"
    components = {
        f"{stem}_bootdata.rom": raw[:0x8000],
        f"{stem}_im_flsh0.rom": raw[0x8000:0x8000 + image_size],
        f"{stem}_game.rom": raw[
            0x8000 + image_size:0x8000 + image_size + game_size
        ],
        f"{stem}_symbols.rom": raw[
            0x8000 + image_size + game_size:total
        ],
    }
    (args.output / "pub-game-bank.bin").write_bytes(raw)
    print("\nReconstructed files:")
    for name, data in components.items():
        path = args.output / name
        path.write_bytes(data)
        print(f"  {sha256(data)}  {name}  ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
