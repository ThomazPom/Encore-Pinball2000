#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import stat
import types
import unittest
from pathlib import Path
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[1] / "internal" / "serial-device-detect.py"
SPEC = importlib.util.spec_from_file_location("serial_device_detect", SCRIPT)
assert SPEC and SPEC.loader
DETECT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DETECT)


class SerialDeviceDetectTests(unittest.TestCase):
    def test_prefers_by_id_deduplicates_and_rejects_unknown_uart(self) -> None:
        paths = {
            "/dev/serial/by-id/*": ["/dev/serial/by-id/usb-Test_Adapter_A1"],
            "/dev/serial/by-path/*": ["/dev/serial/by-path/pci-usb-port0"],
            "/dev/ttyUSB*": ["/dev/ttyUSB0"],
            "/dev/ttyS*": ["/dev/ttyS0"],
        }
        targets = {
            "/dev/serial/by-id/usb-Test_Adapter_A1": "/dev/ttyUSB0",
            "/dev/serial/by-path/pci-usb-port0": "/dev/ttyUSB0",
            "/dev/ttyUSB0": "/dev/ttyUSB0",
            "/dev/ttyS0": "/dev/ttyS0",
        }

        def fake_glob(pattern: str) -> list[str]:
            return paths.get(pattern, [])

        def fake_stat(_path: str) -> types.SimpleNamespace:
            return types.SimpleNamespace(st_mode=stat.S_IFCHR | 0o660, st_gid=0)

        properties = {
            "ID_BUS": "usb",
            "ID_VENDOR": "Test_Vendor",
            "ID_MODEL": "USB_RS232",
            "ID_SERIAL_SHORT": "A1",
        }
        with (
            mock.patch.object(DETECT.glob, "glob", side_effect=fake_glob),
            mock.patch.object(DETECT.os.path, "realpath", side_effect=lambda path: targets[path]),
            mock.patch.object(DETECT.os, "stat", side_effect=fake_stat),
            mock.patch.object(DETECT, "udev_properties", return_value=properties),
            mock.patch("builtins.open", mock.mock_open(read_data="0\n")),
        ):
            result = DETECT.candidates()

        self.assertEqual(len(result), 1)
        path, target, kind, label, group = result[0]
        self.assertEqual(path, "/dev/serial/by-id/usb-Test_Adapter_A1")
        self.assertEqual(target, "/dev/ttyUSB0")
        self.assertEqual(kind, "USB serial adapter")
        self.assertIn("Test Vendor USB RS232 [A1]", label)
        self.assertIn("stable device ID", label)
        self.assertEqual(group, "root")

    def test_kernel_name_is_clearly_marked_unstable(self) -> None:
        kind, label = DETECT.describe(
            "/dev/ttyACM0",
            "/dev/ttyACM0",
            {"ID_BUS": "usb", "ID_MODEL": "CDC_ACM"},
        )
        self.assertEqual(kind, "USB serial adapter")
        self.assertIn("CDC ACM", label)
        self.assertIn("kernel name may change", label)


if __name__ == "__main__":
    unittest.main()
