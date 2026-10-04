#!/usr/bin/env python3

import importlib.util
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


SYM_DUMP = load_module("sym_dump", ROOT / "tools" / "sym_dump.py")
BENCH = load_module("bench_qemu", ROOT / "scripts" / "internal" / "bench-qemu.py")


def write_symbol_table(path: Path, entries):
    """Write the smallest real-format symbol table needed by a unit test."""
    strings = bytearray()
    encoded_entries = []
    for address, name in entries:
        offset = len(strings)
        strings.extend(name.encode("ascii") + b"\0")
        encoded_entries.append((address, offset))
    header = SYM_DUMP.MAGIC + struct.pack(
        "<III", 0, len(encoded_entries), len(strings)
    )
    body = b"".join(struct.pack("<II", *entry) for entry in encoded_entries)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + body + strings)


def write_update_bundle(root: Path, game_number: str, version: int,
                        entries=(), idle_offset=None):
    update = root / (
        f"updates/pin2000_{game_number}_{version:04d}_fixture/"
        f"{game_number}"
    )
    symbols = update / f"pin2000_{game_number}_{version:04d}_symbols.rom"
    write_symbol_table(symbols, entries)
    if idle_offset is not None:
        image = bytearray(idle_offset + 2)
        image[idle_offset:idle_offset + 2] = b"\xeb\xfe"
        (update / f"pin2000_{game_number}_{version:04d}_game.rom").write_bytes(
            image
        )
    return symbols


class SymbolTableTests(unittest.TestCase):
    SWE1_210 = (
        ROOT / "updates" / "pin2000_50069_0210_10312025_B_10000000" /
        "50069" / "pin2000_50069_0210_symbols.rom"
    )

    @unittest.skipUnless(SWE1_210.is_file(), "private SWE1 2.10 update absent")
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
        self.assertTrue(tables, "repository has no preserved symbol tables")
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
        with tempfile.TemporaryDirectory() as directory:
            fixture_root = Path(directory)
            write_update_bundle(fixture_root, "50069", 200)
            write_update_bundle(fixture_root, "50069", 210)
            write_update_bundle(fixture_root, "50070", 250)
            write_update_bundle(fixture_root, "50070", 260)
            with mock.patch.object(BENCH, "ROOT", fixture_root):
                swe1 = BENCH.symbol_rom(
                    ["--game", "swe1", "--update", "latest"]
                )
                rfm = BENCH.symbol_rom(
                    ["--game", "rfm", "--update", "latest"]
                )
        self.assertIn("50069_0210", str(swe1))
        self.assertIn("50070_0260", str(rfm))

    def test_dotted_version_and_idle_loop_are_resolved(self):
        entries = (
            (0x22D0DC, "create(void *, int, unsigned int, char *, int,...)"),
            (0x23B86C, "resume(int, Bool)"),
            (0x23B218, "resched(void)"),
            (0x232CF4, "nulluser(void)"),
        )
        idle_offset = 0x232CF4 - 0x100000 + 0x13A
        with tempfile.TemporaryDirectory() as directory:
            fixture_root = Path(directory)
            write_update_bundle(
                fixture_root, "50069", 210, entries, idle_offset
            )
            with mock.patch.object(BENCH, "ROOT", fixture_root):
                create, resume, resched, idle = BENCH.load_symbols(
                    ["--game", "swe1", "--update", "2.1"]
                )
        self.assertEqual(
            (create, resume, resched, idle),
            (0x22D0DC, 0x23B86C, 0x23B218, 0x232E2E),
        )

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
