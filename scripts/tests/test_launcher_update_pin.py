#!/usr/bin/env python3

import hashlib
import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LAUNCHER = ROOT / "scripts" / "run-qemu.sh"


class UpdatePinLauncherTests(unittest.TestCase):
    def test_no_savedata_keeps_pinned_software_without_writing_state(self):
        with tempfile.TemporaryDirectory(prefix="encore-pin-test-") as temporary:
            temp = Path(temporary)
            profile = temp / "savedata"
            bundle = (
                temp
                / "data"
                / "encore"
                / "serial-updates"
                / "swe1"
                / "50069"
            )
            bundle.mkdir(parents=True)
            for kind in ("bootdata", "im_flsh0", "game", "symbols", "sf"):
                (bundle / f"pin2000_50069_0210_{kind}.rom").write_bytes(b"test")

            pin = temp / "config" / "encore" / "update-pins" / "swe1.update-pin"
            pin.parent.mkdir(parents=True)
            pin.write_text(f"{bundle}\n")
            pin_hash = hashlib.sha256(pin.read_bytes()).hexdigest()

            capture = temp / "qemu-invocation"
            fake_qemu = temp / "qemu-system-i386"
            fake_qemu.write_text(
                "#!/bin/sh\n"
                "if [ \"${1:-}\" = -display ] && [ \"${2:-}\" = help ]; then\n"
                "  printf 'none\\n'\n"
                "  exit 0\n"
                "fi\n"
                "printf 'P2K_NO_SAVEDATA=%s\\n' \"${P2K_NO_SAVEDATA:-}\" "
                '> "$FAKE_QEMU_CAPTURE"\n'
                "printf '%s\\n' \"$@\" >> \"$FAKE_QEMU_CAPTURE\"\n"
            )
            fake_qemu.chmod(0o755)

            environment = os.environ.copy()
            environment.update(
                {
                    "QEMU_BIN": str(fake_qemu),
                    "FAKE_QEMU_CAPTURE": str(capture),
                    "XDG_CACHE_HOME": str(temp / "cache"),
                    "XDG_CONFIG_HOME": str(temp / "config"),
                    "XDG_DATA_HOME": str(temp / "data"),
                }
            )
            result = subprocess.run(
                [
                    str(LAUNCHER),
                    "--game",
                    "swe1",
                    "--update",
                    "auto",
                    "--savedata",
                    str(profile),
                    "--no-savedata",
                    "--display",
                    "none",
                    "--audio",
                    "none",
                    "--lpt-device",
                    "emulated",
                    "--no-cpu-shield",
                    "--uart-quiet",
                ],
                cwd=ROOT,
                env=environment,
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )

            self.assertEqual(
                result.returncode,
                0,
                f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}",
            )
            invocation = capture.read_text().splitlines()
            self.assertIn("P2K_NO_SAVEDATA=1", invocation)
            machine = invocation[invocation.index("-M") + 1]
            self.assertIn(f"update={bundle}", machine)
            self.assertEqual(hashlib.sha256(pin.read_bytes()).hexdigest(), pin_hash)
            for suffix in ("flash", "nvram2", "see", "rtc"):
                self.assertFalse(profile.joinpath(f"swe1.{suffix}").exists())


if __name__ == "__main__":
    unittest.main()
