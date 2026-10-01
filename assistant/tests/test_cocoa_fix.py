# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""A local plugin must never be loaded against a changed private Qt SDK."""

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "cocoa_fix", Path(__file__).resolve().parents[1] / "tools/build_cocoa_accessibility_fix.py"
)
fix = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fix)


class CocoaFixTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.craft = self.root / "craft"
        for name in ("QtCore", "QtGui"):
            path = self.craft / f"lib/{name}.framework/Versions/A/{name}"
            path.parent.mkdir(parents=True)
            path.write_bytes(b"original-sdk")
        self.library = self.root / "platforms/libqcocoa.dylib"
        self.library.parent.mkdir()
        self.library.write_bytes(b"compiled-plugin")
        self.record = dict(
            qt_version=fix.VERSION, qt_source=fix.SOURCE, architecture="arm64",
            craft_root=str(self.craft.resolve()), sdk_sha256=fix.sdk_identity(self.craft),
            patch_sha256=fix.sha256(fix.TOOLS / "table-ownership.patch"),
            plugin_sha256=fix.sha256(self.library),
        )
        self.write_manifest()

    def write_manifest(self):
        (self.root / "manifest.json").write_text(json.dumps(self.record))

    def validate(self, version=fix.VERSION):
        with patch.object(fix.platform, "machine", return_value="arm64"), patch.object(
            fix.subprocess, "check_output", return_value=version
        ):
            return fix.validate(self.root, self.craft)

    def test_changed_sdk_or_plugin_is_rejected_even_at_the_same_version(self):
        self.assertEqual(self.validate(), self.library)
        path = self.craft / "lib/QtGui.framework/Versions/A/QtGui"
        path.write_bytes(b"rebuilt-at-same-version")
        with self.assertRaises(ValueError):
            self.validate()
        path.write_bytes(b"original-sdk")
        self.library.write_bytes(b"different-plugin")
        with self.assertRaises(ValueError):
            self.validate()

    def test_version_architecture_source_and_patch_must_match(self):
        with self.assertRaises(ValueError):
            self.validate("6.11.2")
        for key in ("qt_source", "architecture", "craft_root", "patch_sha256"):
            with self.subTest(key=key):
                original = self.record[key]
                self.record[key] = "different"
                self.write_manifest()
                with self.assertRaises(ValueError):
                    self.validate()
                self.record[key] = original


if __name__ == "__main__":
    unittest.main()
