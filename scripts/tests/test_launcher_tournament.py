#!/usr/bin/env python3

import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LAUNCHER = ROOT / "scripts" / "run-qemu.sh"


def launcher_result(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(LAUNCHER), *arguments, "--help"],
        cwd=ROOT,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


class TournamentOptionTests(unittest.TestCase):
    def test_documented_forms_are_accepted(self):
        for arguments in (
            ("--tournament", "10.0.2.2"),
            ("--tournament", "10.0.2.2", "on"),
            ("--tournament", "10.0.2.2", "off"),
            ("--tournament", "10.0.2.2", "no-free"),
            ("--tournament", "10.0.2.2", "on", "no-free"),
            ("--tournament", "10.0.2.2", "off", "no-free"),
        ):
            with self.subTest(arguments=arguments):
                result = launcher_result(*arguments)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_invalid_address_is_rejected(self):
        result = launcher_result("--tournament", "300.0.2.2")
        self.assertEqual(result.returncode, 2)
        self.assertIn("expected an IPv4 address", result.stderr)

    def test_unknown_profile_token_is_not_silently_ignored(self):
        result = launcher_result("--tournament", "10.0.2.2", "free")
        self.assertEqual(result.returncode, 2)
        self.assertIn("Unknown arg: free", result.stderr)


if __name__ == "__main__":
    unittest.main()
