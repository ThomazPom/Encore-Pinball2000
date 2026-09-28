#!/usr/bin/env python3

import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VALIDATION = ROOT / "scripts" / "internal" / "network-validation.sh"


def bash_result(expression: str) -> bool:
    result = subprocess.run(
        ["bash", "-c", f'source "$1"; {expression}', "bash", str(VALIDATION)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return result.returncode == 0


class NetworkValidationTests(unittest.TestCase):
    def test_ipv4_accepts_bounded_dotted_decimal(self):
        for value in ("0.0.0.0", "10.0.2.15", "255.255.255.255",
                      "001.002.003.004"):
            with self.subTest(value=value):
                self.assertTrue(bash_result(f"valid_ipv4 {value!r}"))

    def test_ipv4_rejects_trailing_and_overflow_forms(self):
        for value in ("", "1.2.3", "1.2.3.4.", "1..2.3", "256.0.0.1",
                      "18446744073709551616.0.0.1"):
            with self.subTest(value=value):
                self.assertFalse(bash_result(f"valid_ipv4 {value!r}"))

    def test_netmask_must_be_contiguous(self):
        for value in ("0.0.0.0", "255.255.0.0", "255.255.255.255"):
            with self.subTest(value=value):
                self.assertTrue(bash_result(f"valid_ipv4_netmask {value!r}"))
        for value in ("255.0.255.0", "255.255.127.0", "255.255.255.1"):
            with self.subTest(value=value):
                self.assertFalse(bash_result(f"valid_ipv4_netmask {value!r}"))

    def test_gateway_must_share_selected_subnet(self):
        self.assertTrue(bash_result(
            "ipv4_same_subnet 10.0.2.15 10.0.2.2 255.255.255.0"
        ))
        self.assertFalse(bash_result(
            "ipv4_same_subnet 10.0.2.15 10.0.3.2 255.255.255.0"
        ))


if __name__ == "__main__":
    unittest.main()
