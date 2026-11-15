#!/usr/bin/env python3

from collections import Counter
from pathlib import Path
import re
import unittest
from urllib.parse import unquote


ROOT = Path(__file__).resolve().parents[2]
ARCHIVE = ROOT / "docs" / "references" / "pinball2000"
SOURCES = ARCHIVE / "sources"
TOPICS = ARCHIVE / "topics"
COVERAGE = ARCHIVE / "KNOWLEDGE-COVERAGE.md"


class ReferenceKnowledgeCoverageTests(unittest.TestCase):
    def source_names(self):
        return {
            path.name
            for path in SOURCES.iterdir()
            if path.is_dir() and (path / "manifest.json").is_file()
        }

    def test_every_source_has_exactly_one_coverage_row(self):
        names = re.findall(
            r"\]\(sources/([^/]+)/README\.md\)",
            COVERAGE.read_text(encoding="utf-8"),
        )
        counts = Counter(names)
        self.assertEqual(
            sorted(name for name, count in counts.items() if count != 1),
            [],
            "each source must have exactly one coverage-ledger row",
        )
        self.assertEqual(set(names), self.source_names())

    def test_every_source_is_reachable_from_a_topic_guide(self):
        names = set()
        for topic in TOPICS.glob("*.md"):
            names.update(re.findall(
                r"\]\(\.\./sources/([^/]+)/README\.md(?:#[^)]+)?\)",
                topic.read_text(encoding="utf-8"),
            ))
        self.assertEqual(names, self.source_names())

    def test_archive_indexes_and_topics_have_no_broken_local_links(self):
        documents = [
            ARCHIVE / "README.md",
            ARCHIVE / "METHODOLOGY.md",
            ARCHIVE / "CATALOG.md",
            ARCHIVE / "KNOWLEDGE-COVERAGE.md",
            ARCHIVE / "PDF-COVERAGE.md",
            SOURCES / "README.md",
            *TOPICS.glob("*.md"),
        ]
        broken = []
        for document in documents:
            text = document.read_text(encoding="utf-8")
            for target in re.findall(r"(?<!!)\[[^]]*\]\(([^)]+)\)", text):
                if target.startswith(("http://", "https://", "mailto:", "#")):
                    continue
                relative = unquote(target.split("#", 1)[0])
                if relative and not (document.parent / relative).exists():
                    broken.append(f"{document.relative_to(ROOT)}: {target}")
        self.assertEqual(broken, [])


if __name__ == "__main__":
    unittest.main()
