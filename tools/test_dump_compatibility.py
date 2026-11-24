"""Focused regression checks for the IL2CPP dump contract comparator."""

from __future__ import annotations

import argparse
import copy
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from dump_compatibility import check, check_hud_metadata, read_dump


class DumpCompatibilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not hasattr(cls, "old_path"):
            raise unittest.SkipTest("Run this integration check with old_dump and new_dump arguments")
        cls.old, cls.old_metadata = read_dump(cls.old_path)
        cls.new, cls.new_metadata = read_dump(cls.new_path)
        cls.baseline_failures = {r["name"] for r in check(cls.new, cls.new) if not r["passed"]}

    def assert_added_failures(self, changed, expected):
        self.assertTrue(self.baseline_failures.isdisjoint(expected))
        failures = {r["name"] for r in check(self.new, changed) if not r["passed"]}
        self.assertEqual(failures, self.baseline_failures | expected)

    def assert_group_failures(self, dump, group, expected):
        failures = {item["name"] for item in check_hud_metadata(dump)[group] if not item["passed"]}
        self.assertEqual(failures, expected)

    def test_comparison_retains_both_historical_field_offsets(self):
        results = check(self.old, self.new)
        self.assertEqual(len(results), 100)
        use_count = next(item for item in results if item["name"] == "Crucifix.useCount")
        for key, dump in (("old", self.old), ("new", self.new)):
            offsets = [field["offset"] for field in dump[("", "Crucifix")]["fields"]
                       if field["name"] == "useCount" and not field["static"]]
            self.assertEqual(len(offsets), 1)
            self.assertEqual([field[2] for field in use_count[key]], offsets)
        self.assertEqual(use_count["passed"], use_count["old"] == use_count["new"])

    def test_current_hud_data_and_public_unity_metadata(self):
        self.assert_group_failures(self.new, "hud_data", set())
        self.assert_group_failures(self.new, "current_unity_consumers", set())

    def test_current_coordinates_do_not_require_private_bindings(self):
        changed = self.new.copy()
        for owner in ("Transform", "Camera"):
            key = ("UnityEngine", owner)
            changed[key] = dict(self.new[key])
            changed[key]["methods"] = [m for m in changed[key]["methods"] if not m["name"].endswith("_Injected")]
        self.assert_group_failures(changed, "current_unity_consumers", set())
        historical = {item["name"] for item in check(self.old, changed) if not item["passed"]}
        self.assertIn("UnityEngine.Transform.get_position_Injected/1", historical)
        self.assertIn("UnityEngine.Camera.WorldToScreenPoint_Injected/3", historical)

    def test_detects_fixed_offset_change(self):
        changed = self.new.copy()
        changed[("", "GhostAI")] = copy.deepcopy(self.new[("", "GhostAI")])
        ghost = changed[("", "GhostAI")]
        field = next(field for field in ghost["fields"] if field["offset"] == 0xF3)
        field["offset"] = 0x1AB
        self.assert_added_failures(changed, {"GhostAI", "GhostAI+0xf3"})

    def test_detects_hook_signature_change(self):
        changed = self.new.copy()
        changed[("", "GhostAI")] = copy.deepcopy(self.new[("", "GhostAI")])
        hunting = next(method for method in changed[("", "GhostAI")]["methods"]
                       if method["name"] == "Hunting")
        hunting["parameters"][0] = "int"
        self.assert_added_failures(changed, {"GhostAI.Hunting/3"})

    def test_detects_enum_value_change(self):
        changed = self.new.copy()
        ghost = changed[("", "GhostAI")]
        enum_name = next(field["type"] for field in ghost["fields"] if field["offset"] == 0x30)
        changed[("", enum_name)] = copy.deepcopy(self.new[("", enum_name)])
        values = changed[("", enum_name)]["enum_values"]
        hunting_index = next(index for index, value in enumerate(values) if value[0] == "hunting")
        values[hunting_index] = ("hunting", "999")
        self.assert_added_failures(changed, {"GhostAI+0x30 enum"})

    def test_dynamic_fields_allow_relocation_but_reject_ambiguity_and_wrong_type(self):
        changed = self.new.copy()
        for owner in ("LocalPlayer", "Crucifix"):
            changed[("", owner)] = copy.deepcopy(self.new[("", owner)])
        fields = changed[("", "LocalPlayer")]["fields"]
        stamina = next(f for f in fields if f["type"] == "PlayerStamina" and not f["static"])
        use_count = next(f for f in changed[("", "Crucifix")]["fields"] if f["name"] == "useCount")
        stamina["offset"] += 0x100
        use_count["offset"] += 0x100
        self.assert_group_failures(changed, "hud_data", set())
        fields.append(stamina.copy())
        use_count["type"] = "float"
        self.assert_group_failures(changed, "hud_data", {"LocalPlayer -> PlayerStamina", "Crucifix.useCount"})

    def test_public_unity_accessor_must_be_unambiguous_and_have_expected_return_type(self):
        changed = self.new.copy()
        changed[("UnityEngine", "Transform")] = copy.deepcopy(self.new[("UnityEngine", "Transform")])
        methods = changed[("UnityEngine", "Transform")]["methods"]
        position = next(m for m in methods if m["name"] == "get_position" and not m["parameters"])
        for edit in ("wrong return", "same-arity overload"):
            with self.subTest(edit=edit):
                if edit == "wrong return":
                    position["return"] = "Vector2"
                else:
                    position["return"] = "Vector3"
                    methods.append(position.copy())
                self.assert_group_failures(changed, "current_unity_consumers", {"UnityEngine.Transform.get_position/0"})

    def test_current_coordinates_require_component_and_vector_by_value(self):
        changed = self.new.copy()
        for owner in ("Component", "Camera", "Vector3"):
            key = ("UnityEngine", owner)
            changed[key] = copy.deepcopy(self.new[key])
        component = changed[("UnityEngine", "Component")]
        component["methods"] = [m for m in component["methods"] if m["name"] != "get_transform"]
        projection = next(m for m in changed[("UnityEngine", "Camera")]["methods"]
                          if m["name"] == "WorldToScreenPoint" and m["parameters"] == ["Vector3"])
        projection["parameters"] = ["ref Vector3"]
        z = next(f for f in changed[("UnityEngine", "Vector3")]["fields"] if f["name"] == "z")
        z["offset"] = 12
        self.assert_group_failures(changed, "current_unity_consumers", {
            "UnityEngine.Component.get_transform/0", "UnityEngine.Camera.WorldToScreenPoint/1", "UnityEngine.Vector3"})

    def test_equipment_enum_is_resolved_by_name_and_checks_hud_numbers(self):
        changed = self.new.copy()
        changed[("", "StoreItem")] = copy.deepcopy(self.new[("", "StoreItem")])
        equipment = next(f for f in changed[("", "StoreItem")]["fields"] if f["name"] == "equipmentType")
        equipment["offset"] += 0x100
        key = ("", equipment["type"])
        changed[key] = copy.deepcopy(self.new[key])
        self.assert_group_failures(changed, "hud_data", set())
        values = changed[key]["enum_values"]
        changed[key]["enum_values"] = [entry for entry in values if entry[0] != "DowsingRods"]
        self.assert_group_failures(changed, "hud_data", set())
        index = next(i for i, (name, _) in enumerate(values) if name == "EMFReader")
        values[index] = ("EMFReader", "99")
        changed[key]["enum_values"] = values
        self.assert_group_failures(changed, "hud_data", {"StoreItem.equipmentType enum"})

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
