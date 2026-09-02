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
               timeout: float, chunk: int = CHUNK,
               checkpoint: Path | None = None) -> bytes:
    output = bytearray(
        checkpoint.read_bytes()
        if checkpoint and checkpoint.exists() else b""
    )
    if len(output) > count:
        raise ValueError(f"checkpoint larger than requested {bank} range: "
                         f"{len(output)} > {count}")
    if output:
        print(f"  {bank}: resuming at 0x{start + len(output):06x}",
              flush=True)
    while len(output) < count:
        offset = start + len(output)
        length = min(chunk, count - len(output))
        print(f"  {bank}: 0x{offset:06x}..0x{offset + length:06x}",
              flush=True)
        block = capture_command(
            link, f"pub {bank} dump {offset} {length}", length, timeout
        )
        output.extend(block)
        if checkpoint:
            with checkpoint.open("ab") as stream:
                stream.write(block)
                stream.flush()
                os.fsync(stream.fileno())
    return bytes(output)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_layout(header: bytes) -> tuple[int, int, int, int, int, int]:
    if len(header) < 0x58:
        raise ValueError("short PUB header")
    game_id = struct.unpack_from("<I", header, 0x3C)[0]
    version_major = struct.unpack_from("<I", header, 0x40)[0]
    version_minor = struct.unpack_from("<I", header, 0x44)[0]
    image_size = struct.unpack_from("<I", header, 0x2C)[0] * 4
    game_size = struct.unpack_from("<I", header, 0x4C)[0] * 4
    symbols_size = struct.unpack_from("<I", header, 0x54)[0] * 4
    if game_id not in (50069, 50070) or version_major not in (1, 2) or not all(
        0 < size <= 0x400000 for size in (image_size, game_size, symbols_size)
    ):
        raise ValueError(
            "PUB header has implausible component metadata; refusing to guess"
        )
    return (game_id, version_major, version_minor, image_size, game_size,
            symbols_size)


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
    parser.add_argument("--with-sound", action="store_true",
                        help="also dump and compare the sound1/sound8 views")
    parser.add_argument("--sound-only", action="store_true",
                        help="read metadata then dump only sound "
                             "(implies --with-sound)")
    parser.add_argument("--chunk", type=lambda value: int(value, 0),
                        default=CHUNK,
                        help="bytes per console command (default: 0x1000; "
                             "larger values are intended for emulators)")
    parser.add_argument("--resume", action="store_true",
                        help="resume checkpointed raw banks in an existing "
                             "output dir")
    args = parser.parse_args()

    if args.output.exists() and not args.resume:
        parser.error(f"output already exists: {args.output}")
    args.output.mkdir(parents=True, exist_ok=args.resume)
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
        if not 0 < args.chunk <= 0x10000:
            parser.error("--chunk must be between 1 and 0x10000")
        header = dump_range(link, "game", 0, 0x100, args.timeout,
                            args.chunk)
        (game_id, version_major, version_minor, image_size, game_size,
         symbols_size) = parse_layout(header)
        total = 0x8000 + image_size + game_size + symbols_size
        if total > 0x400000:
            raise ValueError(f"declared PUB contents exceed game bank: 0x{total:x}")
        display_minor = (
            str(version_minor)
            if version_major == 1 and version_minor < 10
            else f"{version_minor:02d}"
        )
        print(f"Detected game {game_id}, version "
              f"{version_major}.{display_minor}; "
              f"dumping 0x{total:x} bytes", flush=True)
        raw = None if args.sound_only else dump_range(
            link, "game", 0, total, args.timeout, args.chunk,
            args.output / "pub-game-bank.bin"
        )
        sound1 = sound8 = None
        if args.with_sound or args.sound_only:
            print("Dumping 1 MiB sound1 view...", flush=True)
            sound1 = dump_range(link, "sound1", 0, 0x100000, args.timeout,
                                args.chunk,
                                args.output / "pub-sound1-bank.bin")
            print("Dumping 1 MiB sound8 view...", flush=True)
            sound8 = dump_range(link, "sound8", 0, 0x100000, args.timeout,
                                args.chunk,
                                args.output / "pub-sound8-bank.bin")
    finally:
        link.close()

    # Bootdata stores major/minor separately. Factory 1.x releases encode
    # tenths (1.5 => minor 5 => 0150), while community 2.x releases encode
    # hundredths directly (2.01 => minor 1 => 0201).
    filename_minor = (
        version_minor * 10
        if version_major == 1 and version_minor < 10 else version_minor
    )
    filename_version = version_major * 100 + filename_minor
    version_tag = f"{filename_version:04d}"
    stem = f"pin2000_{game_id}_{version_tag}"
    if raw is not None:
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
        print("\nReconstructed files:")
        for name, data in components.items():
            path = args.output / name
            path.write_bytes(data)
            print(f"  {sha256(data)}  {name}  ({len(data)} bytes)")
    if sound1 is not None and sound8 is not None:
        sound_name = f"{stem}_sf.rom"
        (args.output / sound_name).write_bytes(sound1)
        relation = "identical aliases" if sound1 == sound8 else "DIFFERENT"
        print("\nSound views:")
        print(f"  {sha256(sound1)}  sound1  ({len(sound1)} bytes)")
        print(f"  {sha256(sound8)}  sound8  ({len(sound8)} bytes)")
        print(f"  relation: {relation}")
        if sound1 != sound8:
            raise RuntimeError("sound1 and sound8 differ; refusing to choose "
                               "one as the reconstructed _sf.rom")
        print(f"  reconstructed: {sound_name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
