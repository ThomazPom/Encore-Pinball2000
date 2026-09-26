#!/usr/bin/env python3

import importlib.util
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


SYM_DUMP = load_module("sym_dump", ROOT / "tools" / "sym_dump.py")
BENCH = load_module("bench_qemu", ROOT / "scripts" / "internal" / "bench-qemu.py")


class SymbolTableTests(unittest.TestCase):
    SWE1_210 = (
        ROOT / "updates" / "pin2000_50069_0210_10312025_B_10000000" /
        "50069" / "pin2000_50069_0210_symbols.rom"
    )

    def test_swe1_210_known_names_have_exact_addresses(self):
        data, count, string_base, by_name_offset, header = SYM_DUMP.parse(
            self.SWE1_210
        )
        self.assertEqual(count, 15482)
        self.assertEqual(header["n_hdr"], count)
        self.assertEqual(string_base, 0x1E3E8)
        self.assertEqual(
            SYM_DUMP.lookup(
                data, count, string_base, by_name_offset,
                "jts_set_header(jts_header *, short, unsigned long)",
            ),
            0x20F74C,
        )
        self.assertEqual(
            SYM_DUMP.lookup(
                data, count, string_base, by_name_offset, "nulluser(void)"
            ),
            0x232CF4,
        )

    def test_every_preserved_entry_resolves_to_a_printable_name(self):
        tables = sorted(ROOT.glob("updates/*/*/*_symbols.rom"))
        self.assertEqual(len(tables), 26)
        for path in tables:
            with self.subTest(path=path):
                data, count, string_base, _, _ = SYM_DUMP.parse(path)
                for index in range(count):
                    _, name_offset = SYM_DUMP.struct.unpack_from(
                        "<II", data, SYM_DUMP.HDR + index * 8
                    )
                    name = SYM_DUMP.name_at(data, string_base, name_offset)
                    self.assertTrue(name and name.isprintable())


class LoadedBenchSymbolTests(unittest.TestCase):
    def test_latest_selection_is_game_specific(self):
        swe1 = BENCH.symbol_rom(["--game", "swe1", "--update", "latest"])
        rfm = BENCH.symbol_rom(["--game", "rfm", "--update", "latest"])
        self.assertIn("50069_0210", str(swe1))
        self.assertIn("50070_0260", str(rfm))

    def test_dotted_version_and_idle_loop_are_resolved(self):
        create, resume, resched, idle = BENCH.load_symbols(
            ["--game", "swe1", "--update", "2.1"]
        )
        self.assertEqual((create, resume, resched, idle), (
            0x22D0DC, 0x23B86C, 0x23B218, 0x232E2E,
        ))

    def test_implicit_game_or_update_is_refused(self):
        for arguments in (
            ["--update", "latest"],
            ["--game", "swe1"],
            ["--game", "auto", "--update", "latest"],
        ):
            with self.subTest(arguments=arguments):
                with self.assertRaises(RuntimeError):
                    BENCH.symbol_rom(arguments)


if __name__ == "__main__":
    unittest.main()
