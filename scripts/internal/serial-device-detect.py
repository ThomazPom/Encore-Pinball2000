#!/usr/bin/env python3
"""List host serial devices suitable for Encore COM1 passthrough.

Stable udev links win over kernel names.  Output is deliberately tab-separated
for install.sh: path, kernel target, kind, human label, owning group.
"""

from __future__ import annotations

import glob
import os
import stat
import subprocess
import sys


DIRECT_PATTERNS = (
    "/dev/ttyUSB*",
    "/dev/ttyACM*",
    "/dev/ttyS*",
    "/dev/ttyAMA*",
    "/dev/ttyTHS*",
    "/dev/ttymxc*",
)


def udev_properties(device: str) -> dict[str, str]:
    try:
        result = subprocess.run(
            ["udevadm", "info", "--query=property", f"--name={device}"],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except FileNotFoundError:
        return {}
    properties: dict[str, str] = {}
    for line in result.stdout.splitlines():
        key, separator, value = line.partition("=")
        if separator:
            properties[key] = value
    return properties


def clean(value: str) -> str:
    return " ".join(value.replace("_", " ").split())


def describe(path: str, target: str, properties: dict[str, str]) -> tuple[str, str]:
    bus = properties.get("ID_BUS", "")
    driver = properties.get("ID_USB_DRIVER", properties.get("ID_DRIVER", ""))
    target_name = os.path.basename(target)
    if bus == "usb" or target_name.startswith(("ttyUSB", "ttyACM")):
        kind = "USB serial adapter"
    elif bus == "pci" or target_name.startswith("ttyS"):
        kind = "built-in/PCI serial port"
    else:
        kind = "serial port"

    vendor = clean(
        properties.get("ID_VENDOR_FROM_DATABASE", properties.get("ID_VENDOR", ""))
    )
    model = clean(
        properties.get("ID_MODEL_FROM_DATABASE", properties.get("ID_MODEL", ""))
    )
    serial = clean(properties.get("ID_SERIAL_SHORT", ""))
    identity = " ".join(piece for piece in (vendor, model) if piece)
    if serial and serial not in identity:
        identity = f"{identity} [{serial}]" if identity else f"[{serial}]"
    if not identity and driver:
        identity = clean(driver)
    label = f"{kind} — {identity}" if identity else kind
    if path.startswith("/dev/serial/by-id/"):
        label += " (stable device ID)"
    elif path.startswith("/dev/serial/by-path/"):
        label += " (stable USB/PCI path)"
    else:
        label += " (kernel name may change)"
    return kind, label


def candidates() -> list[tuple[str, str, str, str, str]]:
    ordered: list[str] = []
    ordered.extend(sorted(glob.glob("/dev/serial/by-id/*")))
    ordered.extend(sorted(glob.glob("/dev/serial/by-path/*")))
    for pattern in DIRECT_PATTERNS:
        ordered.extend(sorted(glob.glob(pattern)))

    found: list[tuple[str, str, str, str, str]] = []
    seen_targets: set[str] = set()
    for path in ordered:
        try:
            target = os.path.realpath(path)
            device_stat = os.stat(target)
        except OSError:
            continue
        if not stat.S_ISCHR(device_stat.st_mode) or target in seen_targets:
            continue
        target_name = os.path.basename(target)
        if target_name.startswith("ttyS"):
            # serial8250 creates placeholder ttyS nodes even when firmware did
            # not report any UART.  Kernel type 0 is PORT_UNKNOWN, not a
            # detected DB9/PCI controller.
            try:
                with open(f"/sys/class/tty/{target_name}/type", encoding="ascii") as handle:
                    if handle.read().strip() == "0":
                        continue
            except OSError:
                pass
        seen_targets.add(target)
        properties = udev_properties(target)
        kind, label = describe(path, target, properties)
        try:
            import grp

            group = grp.getgrgid(device_stat.st_gid).gr_name
        except (ImportError, KeyError):
            group = str(device_stat.st_gid)
        found.append((path, target, kind, label, group))
    return found


def main() -> int:
    if len(sys.argv) != 1:
        print(f"Usage: {sys.argv[0]}", file=sys.stderr)
        return 2
    for row in candidates():
        print("\t".join(value.replace("\t", " ").replace("\n", " ") for value in row))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
