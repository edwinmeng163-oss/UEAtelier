#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
INSTALLER_PATH = REPO_ROOT / "Tools/install_unrealmcp_to_project.py"
SPEC = importlib.util.spec_from_file_location("install_unrealmcp_to_project", INSTALLER_PATH)
assert SPEC and SPEC.loader
installer = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = installer
SPEC.loader.exec_module(installer)


class InstallerSupportTierTests(unittest.TestCase):
    def test_primary_and_maintenance_tiers(self) -> None:
        self.assertEqual(installer.classify_engine_support("5.7"), "primary")
        self.assertEqual(installer.classify_engine_support("5.8"), "primary")
        self.assertEqual(installer.classify_engine_support("5.6"), "maintenance")

    def test_unknown_or_custom_associations_are_unverified(self) -> None:
        self.assertEqual(installer.classify_engine_support("5.9"), "unverified")
        self.assertEqual(installer.classify_engine_support("{CUSTOM-GUID}"), "unverified")
        self.assertEqual(installer.classify_engine_support(""), "unverified")

    def test_project_engine_association_is_read_from_json(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            project = Path(temp_dir) / "Sample.uproject"
            project.write_text(json.dumps({"FileVersion": 3, "EngineAssociation": "5.8"}), encoding="utf-8")
            self.assertEqual(installer.read_project_engine_association(project), "5.8")


if __name__ == "__main__":
    unittest.main()
