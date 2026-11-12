#!/usr/bin/env python3

import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "reference-preservation" / "reference-bundle.py"


def load_module():
    spec = importlib.util.spec_from_file_location("reference_bundle", TOOL)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


BUNDLE = load_module()


class ReferenceBundleTests(unittest.TestCase):
    def test_legacy_request_url_quotes_spaces_without_double_encoding(self):
        expected = "https://example.test/manuals/Pinball%202000.pdf?q=a%20b"
        self.assertEqual(
            BUNDLE.request_url(
                "https://example.test/manuals/Pinball 2000.pdf?q=a b"),
            expected,
        )
        self.assertEqual(BUNDLE.request_url(expected), expected)

    def test_mediawiki_import_repairs_legacy_asset_names(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            assets = root / "old-pics"
            assets.mkdir()
            (assets / "Pin2KCaps.jpg").write_bytes(b"caps")
            (assets / "Mediagxboard.jpg").write_bytes(b"board")
            (assets / "Pin2K_Lamp_Test.png").write_bytes(b"lamp")
            source = root / "source.wiki"
            source.write_text(
                "\r\n= Overview =\r\n"
                "== Hardware ==\r\n"
                "# First step\r\n"
                "[[File:Pin2KCaps.jpg_\u200e|thumb|Capacitors]]\n"
                "[[File:mediagxboard.jpg|Motherboard]]\n"
                "[[File:Pin2K Lamp Test.png|Lamp test]]\n"
                "[https://example.test/old| Historical link]\n",
                encoding="utf-8",
            )
            output = root / "bundle"
            BUNDLE.build_bundle(
                "mediawiki", "https://example.test/wiki/P2K",
                source.read_bytes(), b"<html><body>P2K</body></html>",
                BUNDLE.gather_files(assets), output, "2026-03-28",
                "Pinball 2000 Repair",
            )
            readme = (output / "README.md").read_text()
            self.assertIn("# Overview", readme)
            self.assertIn("## Hardware", readme)
            self.assertIn("1. First step", readme)
            self.assertIn("![Capacitors](assets/Pin2KCaps.jpg)", readme)
            self.assertIn("![Motherboard](assets/Mediagxboard.jpg)", readme)
            self.assertIn("![Lamp test](assets/Pin2K_Lamp_Test.png)", readme)
            self.assertIn("[Historical link](https://example.test/old)", readme)
            BUNDLE.validate_bundle(output)

    def test_dokuwiki_bundle_keeps_raw_source_and_footnote(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            assets = root / "pics"
            assets.mkdir()
            (assets / "guide.odt").write_bytes(b"odt")
            source = "====== XINA ======\nText((evidence)).\n{{:xina:guide.odt|Guide}}\n"
            output = root / "bundle"
            BUNDLE.build_bundle(
                "dokuwiki", "https://example.test/xina/p2k", source.encode(),
                b"<html></html>", BUNDLE.gather_files(assets), output,
                "2026-04-20", "XINA",
            )
            readme = (output / "README.md").read_text()
            self.assertIn("# XINA", readme)
            self.assertIn("[^1]: evidence", readme)
            self.assertIn("assets/guide.odt", readme)
            self.assertEqual((output / "raw" / "source.dokuwiki").read_text(), source)
            BUNDLE.validate_bundle(output)

    def test_static_html_bundle_copies_only_referenced_assets(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            assets = root / "assets"
            assets.mkdir()
            (assets / "board.png").write_bytes(b"png")
            (assets / "noise.png").write_bytes(b"noise")
            page = (
                "<html><head><title>P2K board</title></head><body>"
                "<h1>Board</h1><p>Evidence <strong>here</strong>.</p>"
                "<p>First line<br><br>Second paragraph</p>"
                "<img src='board.png' alt='board'>"
                "</body></html>"
            )
            output = root / "bundle"
            BUNDLE.build_bundle(
                "html", "https://example.test/p2k.html", page.encode(), None,
                BUNDLE.gather_files(assets), output, "2026-11-11",
            )
            self.assertTrue((output / "assets" / "board.png").is_file())
            self.assertFalse((output / "assets" / "noise.png").exists())
            self.assertIn("**here**", (output / "README.md").read_text())
            self.assertIn("First line\n\nSecond paragraph",
                          (output / "README.md").read_text())
            BUNDLE.validate_bundle(output)

    def test_static_html_honors_legacy_declared_encoding(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "bundle"
            page = (
                "<html><head><meta charset='iso-8859-1'></head>"
                "<body><h1>Réparation</h1><p>Café</p></body></html>"
            ).encode("iso-8859-1")
            BUNDLE.build_bundle(
                "html", "https://example.test/legacy.html", page, None,
                [], output, "2026-11-11", "Legacy Latin-1",
            )
            readme = (output / "README.md").read_text()
            self.assertIn("Réparation", readme)
            self.assertIn("Café", readme)
            self.assertEqual(
                (output / "raw" / "page.html").read_bytes(), page)
            mislabeled = (
                "<meta charset='iso-8859-1'><p>Risqué — test</p>"
            ).encode("utf-8")
            self.assertIn(
                "Risqué — test",
                BUNDLE.decode_source_bytes(mislabeled, "html"),
            )

    def test_static_html_extracts_content_and_does_not_archive_downloads(self):
        navigation = " ".join(
            f"<a href='/menu/{index}'>Navigation item {index}</a>"
            for index in range(30)
        )
        prose = "Technical evidence about Pinball 2000. " * 30
        page = (
            "<html><body><table><tr>"
            f"<td class='navigation'>{navigation}</td>"
            "<td align='left'><h1>Technical note</h1>"
            f"<p>{prose}</p>"
            "<table><tr><th>Signal</th><th>Value</th></tr>"
            "<tr><td>IRQ</td><td>5</td></tr></table>"
            "<a href='software.zip'>software</a>"
            "</td></tr></table></body></html>"
        )
        markdown, _ = BUNDLE.html_to_markdown(
            page, "https://example.test/reference/", {})
        self.assertIn("# Technical note", markdown)
        self.assertIn("Technical evidence", markdown)
        self.assertNotIn("Navigation item", markdown)
        self.assertEqual(markdown.count("| IRQ | 5 |"), 1)
        self.assertNotIn(
            "https://example.test/reference/software.zip",
            BUNDLE.html_asset_refs(page, "https://example.test/reference/"),
        )
        malformed = (
            "<html><body><td><table><table><tr><td>" + prose +
            "</td></tr></table></table></td></body></html>"
        )
        recovered, _ = BUNDLE.html_to_markdown(
            malformed, "https://example.test/", {})
        self.assertIn("Technical evidence", recovered)

        legacy_article = (
            "<html><body><h1>History</h1><p>Introduction"
            "<h2>First chapter</h2><p>First paragraph"
            "<p>Second paragraph<ul><li>Outer<ul><li>Inner</li></ul></li></ul>"
            "</body></html>"
        )
        article, _ = BUNDLE.html_to_markdown(
            legacy_article, "https://example.test/", {})
        self.assertIn("## First chapter\n\nFirst paragraph", article)
        self.assertIn("First paragraph\n\nSecond paragraph", article)
        self.assertIn("- Outer", article)
        self.assertIn("  - Inner", article)

        layout = (
            "<table class='box'>"
            "<tr><td id='pagehead'>Section one</td></tr>"
            "<tr><td id='pagebody'><p>Useful prose</p></td></tr>"
            "<tr><td id='pagehead'>Section two</td></tr>"
            "</table>"
        )
        rendered, _ = BUNDLE.html_to_markdown(
            layout, "https://example.test/", {})
        self.assertIn("### Section one", rendered)
        self.assertIn("Useful prose", rendered)
        self.assertNotIn("| Section one |", rendered)

    def test_validator_rejects_tampering_and_broken_links(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "bundle"
            BUNDLE.build_bundle(
                "html", "https://example.test/", b"<h1>Evidence</h1>", None,
                [], output, "2026-11-11", "Evidence",
            )
            (output / "README.md").write_text("# changed\n")
            with self.assertRaisesRegex(ValueError, "mismatch"):
                BUNDLE.validate_bundle(output)

    def test_manifest_records_provenance_and_roles(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "bundle"
            BUNDLE.build_bundle(
                "html", "https://example.test/reference", b"<p>Proof</p>",
                None, [], output, "2026-11-11", "Reference",
            )
            manifest = json.loads((output / "manifest.json").read_text())
            self.assertEqual(manifest["bundle"]["source_url"],
                             "https://example.test/reference")
            self.assertEqual(manifest["bundle"]["capture_mode"], "local-import")
            self.assertEqual(
                {record["role"] for record in manifest["files"]},
                {"raw", "readable"},
            )

    def test_legacy_render_base_repairs_root_relative_media_and_links(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / "screen.gif"
            image.write_bytes(b"gif")
            output = root / "bundle"
            BUNDLE.build_bundle(
                "html", "https://example.test/deep/page.html",
                ("<p><a href='legacy/help.html'>Help</a>"
                 "<img src='legacy/screen.gif'></p>").encode(),
                None, [image], output, "2026-11-11", "Legacy page",
                render_base_url="https://example.test/",
            )
            readme = (output / "README.md").read_text()
            self.assertIn("[Help](https://example.test/legacy/help.html)", readme)
            self.assertIn("assets/screen.gif", readme)
            BUNDLE.validate_bundle(output)

    def test_artifact_bundle_collapses_byte_duplicates(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = root / "manual scan.pdf"
            duplicate = root / "renamed.pdf"
            first.write_bytes(b"manual")
            duplicate.write_bytes(b"manual")
            output = root / "bundle"
            BUNDLE.import_artifacts(SimpleNamespace(
                file=[first, duplicate],
                source_url="https://example.test/manual.pdf",
                source_note="publisher download",
                captured_at="2026-11-11",
                title="Service manual",
                description="Official service evidence.",
                output=output,
            ))
            self.assertEqual(
                [path.name for path in (output / "raw").iterdir()],
                ["manual scan.pdf"],
            )
            manifest = json.loads((output / "manifest.json").read_text())
            self.assertEqual(manifest["bundle"]["kind"], "artifact")
            self.assertEqual(manifest["bundle"]["source_note"],
                             "publisher download")
            BUNDLE.validate_bundle(output)

    def test_rebuild_uses_retained_raw_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original = root / "original"
            rebuilt = root / "rebuilt"
            page = b"<html><body><p>Preserved fact</p></body></html>"
            BUNDLE.build_bundle(
                "html", "https://example.test/fact", page, None, [],
                original, "2026-11-11", "Fact",
            )
            BUNDLE.rebuild_bundle(SimpleNamespace(
                bundle=original, output=rebuilt))
            self.assertEqual(
                (rebuilt / "raw" / "page.html").read_bytes(), page)
            self.assertIn("Preserved fact", (rebuilt / "README.md").read_text())
            BUNDLE.validate_bundle(rebuilt)


if __name__ == "__main__":
    unittest.main()
