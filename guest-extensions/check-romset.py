#!/usr/bin/env python3
"""Verify the structural extension ABI against every preserved game ROM."""

from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]


def masked(parts: list[tuple[str, bool]]) -> re.Pattern[bytes]:
    return re.compile(b"".join(
        b"." * len(bytes.fromhex(hexbytes)) if wildcard
        else re.escape(bytes.fromhex(hexbytes))
        for hexbytes, wildcard in parts
    ), re.DOTALL)


SHELL = masked([
    ("5589e583ec04575653c745fcffffffff8b35", False),
    ("00000000", True), ("bf300000008b1d", False), ("00000000", True),
])
PUT_VALUE = masked([
    ("5589e556538b75088d450c50ff7604ff3668", False),
    ("00000000", True), ("e8", False), ("00000000", True),
    ("89c383c41085db750b5668", False), ("00000000", True),
    ("e8", False), ("00000000", True), ("89d88d65f85b5ec9c3", False),
])
NETSTART = bytes.fromhex(
    "89f0c1e01889f2c1ea1809c289f0250000ff00c1e80809d0"
    "81e600ff0000c1e60809c6"
)
FACTORY_MESSAGE = b"*** Automatic Factory Reset underway"


def factory_reset_target(data: bytes) -> int | None:
    message = data.find(FACTORY_MESSAGE)
    if message < 0:
        return None
    message_addr = 0x100000 + message
    prefix = b"\x68" + struct.pack("<I", message_addr) + b"\xe8"
    for match in re.finditer(re.escape(prefix), data):
        off = match.start()
        if off + 15 > len(data) or data[off + 10] != 0xE8:
            continue
        target = 0x100000 + off + 15 + struct.unpack_from("<i", data, off + 11)[0]
        fn = target - 0x100000
        if 0 <= fn <= len(data) - 9 and data[fn:fn + 3] == b"\x55\x89\xe5":
            return target
    return None


def main() -> int:
    supported = skipped = failed = 0
    for rom in sorted(ROOT.glob("updates/pin2000_*/*/*_game.rom")):
        data = rom.read_bytes()
        shell = list(SHELL.finditer(data))
        put = list(PUT_VALUE.finditer(data))
        netstart = [m.start() for m in re.finditer(re.escape(NETSTART), data)]
        factory = factory_reset_target(data)
        name = rom.parents[1].name
        if len(shell) == 1 and put and len(netstart) == 1 and factory is not None:
            print(f"OK    {name}")
            supported += 1
        elif b"IPAddr\0" not in data and not netstart:
            print(f"SKIP  {name} (pre-network image)")
            skipped += 1
        else:
            print(f"FAIL  {name}: shell={len(shell)} put={len(put)} "
                  f"netstart={len(netstart)} factory={factory is not None}")
            failed += 1
    print(f"\n{supported} supported, {skipped} pre-network, {failed} failed")
    return bool(failed)


if __name__ == "__main__":
    sys.exit(main())
