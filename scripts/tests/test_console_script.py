#!/usr/bin/env python3

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "internal" / "run-console-script.py"
SPEC = importlib.util.spec_from_file_location("run_console_script", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class ConsoleScriptParserTests(unittest.TestCase):
    def parse(self, source: str):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.p2k"
            path.write_text(source)
            return MODULE.parse_script(path)

    def test_nested_repeat_expands_in_order(self):
        actions = self.parse(
            """
            @repeat 2
              @key c
              @repeat 2
                @switch 13 0.2
              @end
            @end
            ps
            @assert currently
            """
        )
        self.assertEqual(
            [action.text for action in actions],
            [
                "@key c", "@switch 13 0.2", "@switch 13 0.2",
                "@key c", "@switch 13 0.2", "@switch 13 0.2",
                "ps", "@assert currently",
            ],
        )

    def test_all_directives_validate(self):
        actions = self.parse(
            """
            @wait 0
            @key ctrl 3
            @switch 88
            @assert text
            @assert-not text
            @assert-regex t.*t
            @assert-not-regex absent$
            @wait-for 2 ps => currently
            @wait-for-regex 2 ps => currently\\s+[0-9]+
            @screenshot proof
            @record-audio 1 proof
            @echo still running
            """
        )
        self.assertEqual(len(actions), 12)

    def test_invalid_switch_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "NUMBER_11_TO_88"):
            self.parse("@switch 19\n")

    def test_unclosed_repeat_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "missing @end"):
            self.parse("@repeat 2\n@key c\n")

    def test_unknown_directive_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "unknown directive"):
            self.parse("@eventually perhaps\n")


if __name__ == "__main__":
    unittest.main()
