"""Focused regression checks for the IL2CPP dump contract comparator."""

from __future__ import annotations

import argparse
import copy
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from dump_compatibility import check, read_dump


class DumpCompatibilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not hasattr(cls, "old_path"):
            raise unittest.SkipTest("Run this integration check with old_dump and new_dump arguments")
        cls.old, cls.old_metadata = read_dump(cls.old_path)
        cls.new, cls.new_metadata = read_dump(cls.new_path)

    def test_current_dumps_match_all_checked_contracts(self):
        results = check(self.old, self.new)
        self.assertEqual(len(results), 100)
        self.assertTrue(all(result["passed"] for result in results))

    def test_detects_fixed_offset_change(self):
        changed = self.new.copy()
        changed[("", "GhostAI")] = copy.deepcopy(self.new[("", "GhostAI")])
        ghost = changed[("", "GhostAI")]
        field = next(field for field in ghost["fields"] if field["offset"] == 0xF3)
        field["offset"] = 0x1AB
        failures = {result["name"] for result in check(self.old, changed) if not result["passed"]}
        self.assertIn("GhostAI", failures)
        self.assertIn("GhostAI+0xf3", failures)

    def test_detects_hook_signature_change(self):
        changed = self.new.copy()
        changed[("", "GhostAI")] = copy.deepcopy(self.new[("", "GhostAI")])
        hunting = next(method for method in changed[("", "GhostAI")]["methods"]
                       if method["name"] == "Hunting")
        hunting["parameters"][0] = "int"
        failures = {result["name"] for result in check(self.old, changed) if not result["passed"]}
        self.assertEqual(failures, {"GhostAI.Hunting/3"})

    def test_detects_enum_value_change(self):
        changed = self.new.copy()
        ghost = changed[("", "GhostAI")]
        enum_name = next(field["type"] for field in ghost["fields"] if field["offset"] == 0x30)
        changed[("", enum_name)] = copy.deepcopy(self.new[("", enum_name)])
        values = changed[("", enum_name)]["enum_values"]
        hunting_index = next(index for index, value in enumerate(values) if value[0] == "hunting")
        values[hunting_index] = ("hunting", "999")
        failures = {result["name"] for result in check(self.old, changed) if not result["passed"]}
        self.assertEqual(failures, {"GhostAI+0x30 enum"})

    def test_cli_returns_nonzero_for_signature_change_and_parse_error(self):
        text = self.new_path.read_text(encoding="utf-8-sig")
        original = "private void Hunting(bool "
        self.assertEqual(text.count(original), 1)
        with tempfile.TemporaryDirectory(prefix="toolsbox-dump-test-") as directory:
            changed_path = Path(directory) / "changed.cs"
            changed_path.write_text(text.replace(original, "private void Hunting(int ", 1), encoding="utf-8")
            command = [sys.executable, str(Path(__file__).with_name("dump_compatibility.py")),
                       str(self.old_path), str(changed_path)]
            changed = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertEqual(changed.returncode, 1, changed.stderr)
            self.assertIn("99/100 metadata checks passed", changed.stdout)
            self.assertIn("REVIEW [method_signature] GhostAI.Hunting/3", changed.stdout)
            changed_path.write_text("not an IL2CPP dump\n", encoding="utf-8")
            invalid = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertEqual(invalid.returncode, 2, invalid.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("old_dump", type=Path)
    parser.add_argument("new_dump", type=Path)
    args = parser.parse_args()
    DumpCompatibilityTests.old_path = args.old_dump
    DumpCompatibilityTests.new_path = args.new_dump
    unittest.main(argv=[sys.argv[0]], verbosity=2)
