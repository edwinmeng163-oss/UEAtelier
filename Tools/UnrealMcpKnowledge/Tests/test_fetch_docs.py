#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
import urllib.parse
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
FETCHER_PATH = REPO_ROOT / "Tools/unreal_mcp_fetch_docs.py"
SPEC = importlib.util.spec_from_file_location("unreal_mcp_fetch_docs", FETCHER_PATH)
assert SPEC and SPEC.loader
fetch_docs = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = fetch_docs
SPEC.loader.exec_module(fetch_docs)


class FetchDocsTests(unittest.TestCase):
    def test_application_version_override_replaces_existing_query_value(self) -> None:
        source = (
            "https://dev.epicgames.com/documentation/en-us/unreal-engine/"
            "blueprints-visual-scripting-in-unreal-engine?application_version=5.7"
        )
        canonical = fetch_docs.canonicalize_url(source, application_version="5.8")
        self.assertIsNotNone(canonical)
        query = urllib.parse.parse_qs(urllib.parse.urlparse(canonical).query)
        self.assertEqual(query["application_version"], ["5.8"])

    def test_text_extractor_preserves_markdown_heading_levels(self) -> None:
        extractor = fetch_docs.TextExtractor()
        extractor.feed("<h1>Top</h1><p>Body</p><h2>Details</h2><p>More</p>")
        text = extractor.text()
        self.assertIn("# Top", text)
        self.assertIn("## Details", text)

    def test_text_extractor_ignores_nested_markup_inside_skipped_regions(self) -> None:
        extractor = fetch_docs.TextExtractor()
        extractor.feed(
            '<svg><h2>Hidden</h2><a href="/hidden">Secret</a></svg>'
            '<p><a href="/visible">Visible</a></p>'
        )
        text = extractor.text()
        self.assertNotIn("##", text)
        self.assertNotIn("Hidden", text)
        self.assertNotIn("Secret", text)
        self.assertIn("Visible", text)
        self.assertEqual(extractor.links, ["/visible"])

    def test_seed_engine_version_is_required_and_validated(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            seed_path = Path(temp_dir) / "seed.json"
            seed_path.write_text(
                json.dumps(
                    {
                        "name": "mismatch",
                        "engineVersion": "5.8",
                        "sources": [
                            {
                                "id": "doc",
                                "title": "Doc",
                                "url": (
                                    "https://dev.epicgames.com/documentation/en-us/unreal-engine/"
                                    "programming-with-cplusplus-in-unreal-engine?application_version=5.7"
                                ),
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            seed, sources = fetch_docs.load_seed_file(seed_path)
            with self.assertRaises(ValueError):
                fetch_docs.resolve_application_version(seed, sources, None)
            self.assertEqual(fetch_docs.resolve_application_version(seed, sources, "5.8"), "5.8")

    def test_versioned_output_dir_does_not_reuse_another_engine_cache(self) -> None:
        seed = {
            "engineVersion": "5.7",
            "defaultOutputDir": "Saved/UnrealMcp/KnowledgeSources/UnrealEngineOfficialDocs/5.7",
        }
        output = fetch_docs.resolve_output_dir(seed, "5.8", None)
        self.assertEqual(
            output.as_posix(),
            "Saved/UnrealMcp/KnowledgeSources/UnrealEngineOfficialDocs/5.8",
        )


if __name__ == "__main__":
    unittest.main()
