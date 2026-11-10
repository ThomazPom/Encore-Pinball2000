#!/usr/bin/env python3

import importlib.util
import struct
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


SERIAL_UPDATE = load_module(
    "serial_update_bundle",
    ROOT / "scripts" / "internal" / "serial-update-bundle.py",
)
SERIAL_SENDER = load_module(
    "serial_update_sender",
    ROOT / "scripts" / "internal" / "serial-update-send.py",
)


def fixture_components(game_id=50069, major=2, minor=10):
    bootdata = bytearray(b"\xff" * SERIAL_UPDATE.BOOTDATA_SIZE)
    timestamp = b"Fri Oct 31 07:16:34 2025\0"
    bootdata[:len(timestamp)] = timestamp
    image = bytes((index * 3) & 0xFF for index in range(0x100))
    game = bytes((index * 5) & 0xFF for index in range(0x200))
    symbols = bytes((index * 7) & 0xFF for index in range(0x300))
    struct.pack_into("<I", bootdata, 0x2C, len(image) // 4)
    struct.pack_into("<I", bootdata, 0x3C, game_id)
    struct.pack_into("<I", bootdata, 0x40, major)
    struct.pack_into("<I", bootdata, 0x44, minor)
    struct.pack_into("<I", bootdata, 0x4C, len(game) // 4)
    struct.pack_into("<I", bootdata, 0x54, len(symbols) // 4)
    struct.pack_into("<I", bootdata, 32000, 1)
    sound_seed = (game_id + major + minor) & 0xFF
    sound = bytes(
        (index * 11 + sound_seed) & 0xFF
        for index in range(SERIAL_UPDATE.SOUND_FLASH_SIZE)
    )
    return {
        "bootdata": bytes(bootdata),
        "im_flsh0": image,
        "game": game,
        "symbols": symbols,
        "sf": sound,
    }


def assembled_flash(components):
    result = bytearray(b"\xff" * SERIAL_UPDATE.BAR3_SIZE)
    result[:SERIAL_UPDATE.BOOTDATA_SIZE] = components["bootdata"]
    offset = SERIAL_UPDATE.BOOTDATA_SIZE
    for kind in ("im_flsh0", "game", "symbols"):
        data = components[kind]
        result[offset:offset + len(data)] = data
        offset += len(data)
    return bytes(result)


def write_flash_inputs(root: Path, components):
    game_flash = root / "swe1.flash"
    sound_flash = root / "swe1.dcsflash"
    game_flash.write_bytes(assembled_flash(components))
    sound_flash.write_bytes(components["sf"])
    return game_flash, sound_flash


def write_bundle(root: Path, components, outer="pin2000_50069_0210_fixture"):
    inner = root / outer / "50069"
    inner.mkdir(parents=True)
    for kind, data in components.items():
        (inner / f"pin2000_50069_0210_{kind}.rom").write_bytes(data)
    return inner


class SerialUpdateBundleTests(unittest.TestCase):
    def test_input_and_reconstructed_component_hashes_are_identical(self):
        components = fixture_components()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            game_flash, sound_flash = write_flash_inputs(root, components)
            inner, identity, hashes, created = SERIAL_UPDATE.promote(
                game_flash,
                sound_flash,
                root / "installed",
                [],
                root / "state" / "swe1.update-pin",
                "swe1",
            )
            self.assertTrue(created)
            self.assertIn("-serial-", inner.parent.name)
            self.assertEqual(identity.outer_name,
                             "pin2000_50069_0210_10312025_B_10000000")
            output_paths = SERIAL_UPDATE.component_paths(inner)
            self.assertIsNotNone(output_paths)
            for kind in SERIAL_UPDATE.KINDS:
                with self.subTest(kind=kind):
                    self.assertEqual(output_paths[kind].read_bytes(), components[kind])
                    self.assertEqual(
                        SERIAL_UPDATE.sha256_file(output_paths[kind]), hashes[kind]
                    )

    def test_identical_existing_bundle_is_reused_without_copy(self):
        components = fixture_components()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = write_bundle(root / "known", components)
            game_flash, sound_flash = write_flash_inputs(root, components)
            install_root = root / "installed"
            inner, _, _, created = SERIAL_UPDATE.promote(
                game_flash,
                sound_flash,
                install_root,
                [root / "known"],
                root / "state" / "swe1.update-pin",
                "swe1",
            )
            self.assertFalse(created)
            self.assertEqual(inner, source.resolve())
            self.assertFalse(install_root.exists())
            self.assertEqual(
                (root / "state" / "swe1.update-pin").read_text().strip(),
                str(source.resolve()),
            )

    def test_partial_update_state_is_preserved_as_distinct_serial_bundle(self):
        target = fixture_components(50069, 2, 10)
        source = fixture_components(50069, 1, 0)
        mixed = dict(target)
        mixed["sf"] = source["sf"]
        self.assertNotEqual(mixed["sf"], target["sf"])

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            game_flash, sound_flash = write_flash_inputs(root, mixed)
            inner, identity, hashes, created = SERIAL_UPDATE.promote(
                game_flash,
                sound_flash,
                root / "installed",
                [],
                root / "state" / "swe1.update-pin",
                "swe1",
            )

            self.assertTrue(created)
            self.assertEqual(identity.version, "0210")
            self.assertIn("-serial-", inner.parent.name)
            output = SERIAL_UPDATE.component_paths(inner)
            for kind in ("bootdata", "im_flsh0", "game", "symbols"):
                with self.subTest(kind=kind):
                    self.assertEqual(output[kind].read_bytes(), target[kind])
            self.assertEqual(output["sf"].read_bytes(), source["sf"])
            self.assertEqual(hashes["sf"], SERIAL_UPDATE.sha256_bytes(source["sf"]))
            self.assertEqual(
                (root / "state" / "swe1.update-pin").read_text().strip(),
                str(inner.resolve()),
            )

    def test_managed_storage_keeps_only_latest_bundle_per_game(self):
        swe_first = fixture_components(50069, 2, 10)
        swe_second = dict(swe_first)
        swe_second["game"] = bytes(reversed(swe_first["game"]))
        rfm = fixture_components(50070, 2, 60)

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            install_root = root / ".serial-updates"
            unrelated = install_root / "operator-notes"
            unrelated.mkdir(parents=True)
            (unrelated / "keep").write_text("not managed\n")

            for name, components, game in (
                ("rfm", rfm, "rfm"),
                ("swe-first", swe_first, "swe1"),
                ("swe-second", swe_second, "swe1"),
            ):
                case = root / name
                case.mkdir()
                game_flash, sound_flash = write_flash_inputs(case, components)
                SERIAL_UPDATE.promote(
                    game_flash,
                    sound_flash,
                    install_root,
                    [],
                    root / "state" / f"{game}.update-pin",
                    game,
                )

            swe_dirs = list(install_root.glob("pin2000_50069_*-serial-*"))
            rfm_dirs = list(install_root.glob("pin2000_50070_*-serial-*"))
            self.assertEqual(len(swe_dirs), 1)
            self.assertEqual(len(rfm_dirs), 1)
            self.assertTrue((unrelated / "keep").is_file())
            self.assertEqual(
                (root / "state" / "swe1.update-pin").read_text().strip(),
                str((swe_dirs[0] / "50069").resolve()),
            )

    def test_reusing_known_bundle_prunes_obsolete_managed_copy(self):
        components = fixture_components()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            install_root = root / ".serial-updates"
            game_flash, sound_flash = write_flash_inputs(root, components)
            managed, _, _, created = SERIAL_UPDATE.promote(
                game_flash,
                sound_flash,
                install_root,
                [],
                root / "state" / "swe1.update-pin",
                "swe1",
            )
            self.assertTrue(created)
            self.assertTrue(managed.is_dir())

            known = write_bundle(root / "known", components)
            reused, _, _, created = SERIAL_UPDATE.promote(
                game_flash,
                sound_flash,
                install_root,
                [root / "known"],
                root / "state" / "swe1.update-pin",
                "swe1",
            )
            self.assertFalse(created)
            self.assertEqual(reused, known.resolve())
            self.assertFalse(managed.parent.exists())
            self.assertEqual(
                (root / "state" / "swe1.update-pin").read_text().strip(),
                str(known.resolve()),
            )

    def test_wrong_game_never_creates_bundle_or_pin(self):
        components = fixture_components()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            game_flash, sound_flash = write_flash_inputs(root, components)
            with self.assertRaises(ValueError):
                SERIAL_UPDATE.promote(
                    game_flash,
                    sound_flash,
                    root / "installed",
                    [],
                    root / "state" / "rfm.update-pin",
                    "rfm",
                )
            self.assertFalse((root / "installed").exists())
            self.assertFalse((root / "state" / "rfm.update-pin").exists())

    def test_historical_single_digit_minor_is_normalized(self):
        for major, minor, expected in ((0, 7, "0070"), (0, 71, "0071"),
                                       (1, 5, "0150"), (2, 1, "0201")):
            with self.subTest(major=major, minor=minor):
                self.assertEqual(
                    SERIAL_UPDATE.normalized_version(major, minor), expected
                )


class SerialUpdateProtocolTests(unittest.TestCase):
    def test_sender_defaults_to_factory_compatible_baud(self):
        args = SERIAL_SENDER.parse_args(["fixture", "--port", "5599"])
        self.assertEqual(args.baud, 115200)

    def test_newer_230400_rate_remains_explicitly_available(self):
        args = SERIAL_SENDER.parse_args(
            ["fixture", "--port", "5599", "--baud", "230400"]
        )
        self.assertEqual(args.baud, 230400)

    def test_historical_packet_layout_and_checksum(self):
        payload = bytes(range(256)) * 2
        frame = SERIAL_SENDER.make_frame(0x1234, payload)
        self.assertEqual(len(frame), 0x206)
        sequence, length, checksum = struct.unpack_from("<HHH", frame)
        self.assertEqual(sequence, 0x1234)
        self.assertEqual(length, 512)
        self.assertEqual((sum(payload) + checksum) & 0xFFFF, 0)
        self.assertEqual(frame[6:], payload)

    def test_short_packet_is_zero_padded_to_512_bytes(self):
        frame = SERIAL_SENDER.make_frame(7, b"abc")
        self.assertEqual(struct.unpack_from("<HH", frame), (7, 3))
        self.assertEqual(frame[6:9], b"abc")
        self.assertEqual(frame[9:], bytes(509))


class PreservedUpdateRoundTripTests(unittest.TestCase):
    SWE_OLD_SOURCE = (
        ROOT / "updates" / "pin2000_50069_0100_07171999_B_10000000" /
        "50069"
    )
    SWE_SOURCE = (
        ROOT / "updates" / "pin2000_50069_0210_10312025_B_10000000" /
        "50069"
    )
    RFM_OLD_SOURCE = (
        ROOT / "updates" / "pin2000_50070_0070_03291999_B_10000000" /
        "50070"
    )
    RFM_SOURCE = (
        ROOT / "updates" / "pin2000_50070_0260_08082024_B_10000000" /
        "50070"
    )

    def assert_preserved_update_round_trip(self, source, game):
        paths = SERIAL_UPDATE.component_paths(source)
        self.assertIsNotNone(paths)
        components = {kind: paths[kind].read_bytes() for kind in SERIAL_UPDATE.KINDS}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            game_flash, sound_flash = write_flash_inputs(root, components)
            inner, _, _, created = SERIAL_UPDATE.promote(
                game_flash,
                sound_flash,
                root / "installed",
                [],
                root / f"{game}.update-pin",
                game,
            )
            self.assertTrue(created)
            output = SERIAL_UPDATE.component_paths(inner)
            self.assertEqual(
                (root / f"{game}.update-pin").read_text().strip(),
                str(inner.resolve()),
            )
            for kind in SERIAL_UPDATE.KINDS:
                with self.subTest(kind=kind):
                    self.assertEqual(
                        SERIAL_UPDATE.sha256_file(output[kind]),
                        SERIAL_UPDATE.sha256_file(paths[kind]),
                    )

    @unittest.skipUnless(SWE_SOURCE.is_dir(), "private SWE1 2.10 update absent")
    def test_real_swe_update_round_trip_is_byte_identical(self):
        self.assert_preserved_update_round_trip(self.SWE_SOURCE, "swe1")

    @unittest.skipUnless(SWE_OLD_SOURCE.is_dir(), "private SWE1 1.00 update absent")
    def test_real_old_swe_update_round_trip_is_byte_identical(self):
        self.assert_preserved_update_round_trip(self.SWE_OLD_SOURCE, "swe1")

    @unittest.skipUnless(RFM_SOURCE.is_dir(), "private RFM 2.60 update absent")
    def test_real_rfm_update_round_trip_is_byte_identical(self):
        self.assert_preserved_update_round_trip(self.RFM_SOURCE, "rfm")

    @unittest.skipUnless(RFM_OLD_SOURCE.is_dir(), "private RFM 0.70 update absent")
    def test_real_old_rfm_update_round_trip_is_byte_identical(self):
        self.assert_preserved_update_round_trip(self.RFM_OLD_SOURCE, "rfm")


if __name__ == "__main__":
    unittest.main()
