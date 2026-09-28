#!/usr/bin/env python3
"""Verify the structural extension ABI against every preserved game ROM."""

import argparse
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
UDP_TTL = bytes.fromhex(
    "83c40c6685c0750666c74606ffffbb010000008b450825f0000000"
    "3de0000000752e"
)
RESOURCE_NAMES = {
    "dns": b"DNSIPA\0",
    "tourney_ip": b"TS_IPA\0",
    "tournament": b"GmTour\0",
    "free_play": b"CrdFPl\0",
}
BASE = 0x100000


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


def rel32_target(data: bytes, instruction: int) -> int:
    return BASE + instruction + 5 + struct.unpack_from(
        "<i", data, instruction + 1
    )[0]


def named_resource(data: bytes, name: bytes) -> tuple[int, int] | None:
    name_offsets = [m.start() for m in re.finditer(re.escape(name), data)]
    if len(name_offsets) != 1:
        return None
    name_address = BASE + name_offsets[0]
    name_push = b"\x68" + struct.pack("<I", name_address)
    references = [m.start() for m in re.finditer(re.escape(name_push), data)]
    constructors = [off for off in references
                    if off + 20 <= len(data) and
                    data[off + 5] == 0x68 and
                    data[off + 10] == 0x68 and
                    data[off + 15] == 0xE8]
    if len(constructors) != 1:
        return None
    constructor = constructors[0]
    resource = struct.unpack_from("<I", data, constructor + 11)[0]
    target = rel32_target(data, constructor + 15)
    if not BASE <= resource <= BASE + len(data) - 4:
        return None
    if not BASE <= target < BASE + len(data):
        return None
    return resource, target


def extension_resources(data: bytes, netstart: int) -> dict[str, int] | None:
    if (data[netstart + 9] != 0x68 or
            data[netstart + 14] != 0xE8):
        return None
    get_value = rel32_target(data, netstart + 14)
    resolved = {key: named_resource(data, name)
                for key, name in RESOURCE_NAMES.items()}
    if any(value is None for value in resolved.values()):
        return None
    resources = {key: value[0] for key, value in resolved.items()}
    constructors = {key: value[1] for key, value in resolved.items()}
    resource_push = (b"\x68" + struct.pack("<I", resources["dns"]) +
                     b"\xe8")
    get_calls = [m.start() for m in re.finditer(re.escape(resource_push), data)
                 if rel32_target(data, m.start() + 5) == get_value]
    if len(get_calls) != 1:
        return None
    if len(set(resources.values())) != len(resources):
        return None
    if constructors["dns"] != constructors["tourney_ip"]:
        return None
    if constructors["tournament"] != constructors["free_play"]:
        return None
    if constructors["dns"] == constructors["tournament"]:
        return None
    return resources


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    roms = sorted(ROOT.glob("updates/pin2000_*/*/*_game.rom"))
    if not roms:
        print("FAIL  no preserved update game ROMs found", file=sys.stderr)
        return 2
    supported = skipped = failed = 0
    for rom in roms:
        data = rom.read_bytes()
        shell = list(SHELL.finditer(data))
        put = list(PUT_VALUE.finditer(data))
        netstart = [m.start() for m in re.finditer(re.escape(NETSTART), data)]
        udp_ttl = [m.start() for m in re.finditer(re.escape(UDP_TTL), data)]
        factory = factory_reset_target(data)
        resources = None
        if len(netstart) == 1 and netstart[0] >= 0x2D:
            resources = extension_resources(data, netstart[0] - 0x2D)
        name = rom.parents[1].name
        if (len(shell) == 1 and put and len(netstart) == 1 and
                len(udp_ttl) == 1 and factory is not None and
                resources is not None):
            print(f"OK    {name}")
            supported += 1
        elif b"IPAddr\0" not in data and not netstart:
            print(f"SKIP  {name} (pre-network image)")
            skipped += 1
        else:
            print(f"FAIL  {name}: shell={len(shell)} put={len(put)} "
                  f"netstart={len(netstart)} udp_ttl={len(udp_ttl)} "
                  f"factory={factory is not None} "
                  f"resources={resources is not None}")
            failed += 1
    print(f"\n{supported} supported, {skipped} pre-network, {failed} failed")
    return bool(failed)


if __name__ == "__main__":
    sys.exit(main())
