#!/usr/bin/env python3

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "docs/measurements/validation-matrix/run-matrix.py"
SPEC = importlib.util.spec_from_file_location("validation_matrix", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class ValidationMatrixParserTests(unittest.TestCase):
    def inspect(self, body: str, game: str = "swe1", update: str = "latest",
                engine: str = "adsp-hybrid-thread"):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / f"{engine}.log"
            log.write_text(body)
            return MODULE.inspect(log, game, update, engine)

    @staticmethod
    def live_log(engine: str = "adsp-hybrid-thread") -> str:
        return "\n".join((
            "[run-qemu] --update latest → /tmp/update/50069",
            "pinball2000: machine ready (game=swe1, ram=16 MiB)",
            "pinball2000: GP BLT #0: ok",
            "p2k-timing #1 snap | wall=3.0s",
            f"dcs-adsp: native ADSP-2104 execution selected ({engine}); pb2kslib disabled",
            "dcs-adsp: run pc=1234 op=123456 cycles=123456 sport=1",
            "[p2k-dcs-health] queued=0 runtime_resets=0 host_boots=2 "
            "enqueued=97 consumed=97 dropped=0 pcm_frames=1000 "
            "pcm_nonzero=200 cycles=123456",
        )) + "\n"

    def test_every_current_engine_is_in_the_default_matrix(self):
        self.assertEqual(
            MODULE.ENGINES,
            (
                "pb2kslib", "pb2kslib-adsp", "adsp", "adsp-thread",
                "adsp-clock-thread", "adsp-hybrid-thread",
            ),
        )

    def test_installed_update_artifact_keys_do_not_collide(self):
        keys = [
            f"{game}-{MODULE.artifact_cell_key(update, label)}"
            for game, update, label in MODULE.installed_cells()
        ]
        self.assertEqual(len(keys), len(set(keys)))

    def test_healthy_live_engine_passes(self):
        row = self.inspect(self.live_log())
        self.assertTrue(row["pass"])
        self.assertEqual(row["health"], "PASS")

    def test_wrong_game_identity_fails(self):
        row = self.inspect(self.live_log().replace("game=swe1", "game=rfm"))
        self.assertFalse(row["pass"])
        self.assertFalse(row["game_ok"])

    def test_missing_or_unhealthy_live_report_fails(self):
        without_health = self.live_log().replace(
            "[p2k-dcs-health] queued=0 runtime_resets=0 host_boots=2 "
            "enqueued=97 consumed=97 dropped=0 pcm_frames=1000 "
            "pcm_nonzero=200 cycles=123456\n",
            "",
        )
        missing = self.inspect(without_health)
        self.assertFalse(missing["pass"])
        self.assertEqual(missing["health"], "MISSING")

        unhealthy = self.inspect(self.live_log().replace("dropped=0", "dropped=1"))
        self.assertFalse(unhealthy["pass"])
        self.assertEqual(unhealthy["health"], "FAIL")

    def test_wrong_update_fails(self):
        row = self.inspect(self.live_log().replace("--update latest", "--update 0210"))
        self.assertFalse(row["pass"])
        self.assertFalse(row["update_ok"])

    def test_both_sample_engines_require_decoded_audio(self):
        common = "\n".join((
            "[run-qemu] --update none → update discovery disabled",
            "pinball2000: machine ready (game=swe1, ram=16 MiB)",
            "pinball2000: GP BLT #0: ok",
            "p2k-timing #1 snap | wall=3.0s",
            "dcs-audio: pb2kslib loaded 10 entries from /tmp/test.pb2k",
            "dcs-audio: decoded cmd=0x003a name=bong frames=123 peak=1 rms=1",
        )) + "\n"
        fixed = self.inspect(common, update="none", engine="pb2kslib")
        self.assertTrue(fixed["pass"])

        cached = self.inspect(
            common + "dcs-cache: using persistent PCM cache /tmp/cache.pb2k\n",
            update="none", engine="pb2kslib-adsp",
        )
        self.assertTrue(cached["pass"])

        silent = self.inspect(
            common.replace("frames=123", "frames=0"),
            update="none", engine="pb2kslib",
        )
        self.assertFalse(silent["pass"])


if __name__ == "__main__":
    unittest.main()
