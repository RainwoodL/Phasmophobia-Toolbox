#!/usr/bin/env python3
"""Compare the IL2CPP dump contracts used by Phasmophobia Toolbox without loading the game.

Python 3.10+, standard library only. Inputs are read-only. Optional JSON output
is written only to --output. Exit 1 means a checked contract needs review;
exit 2 means an input or parse error. Obfuscated identifiers are normalized,
so this verifies metadata shape, not the semantics of an obfuscated field.
"""

from __future__ import annotations

import argparse
import bisect
import hashlib
import json
import re
from collections import Counter
from pathlib import Path


GAME_TYPES = (
    "GhostAI", "LevelController", "PCPropGrab", "Network", "Network.PlayerSpot",
    "Player", "LocalPlayer", "NetworkedHeadMountedSocket", "PlayerSanity",
    "Difficulty", "GameController", "LevelStats", "Crucifix", "WhiteSage",
    "OuijaBoard", "Equipment", "StoreItem", "PhotonObjectInteract", "Prop",
    "PCCanvas", "LightSwitch", "Door", "Drawer", "PlayerStamina", "LevelValues",
)

# Namespace, class, method, parameter count. Keep this manifest aligned with
# FindMethod / Resolve calls in payload; there are no fixed method RVAs.
METHODS = (
    ("", "PCPropGrab", "Update", 0),
    ("", "PlayerSanity", "Update", 0),
    ("", "GhostAI", "Hunting", 3),
    ("", "Crucifix", "CrucifixUseNetworked", 2),
    ("", "WhiteSage", "UseNetworked", 2),
    ("", "OuijaBoard", "PlayMessageSequence", 2),
    ("", "Network", "get_Instance", 0),
    ("", "LevelStats", "get_Instance", 0),
    ("", "LevelValues", "get_Instance", 0),
    ("", "GameController", "GetAveragePlayerInsanity", 0),
    ("UnityEngine", "Component", "GetComponentInParent", 2),
    ("UnityEngine", "Component", "get_transform", 0),
    ("UnityEngine", "Transform", "get_position_Injected", 1),
    ("UnityEngine", "Camera", "WorldToScreenPoint_Injected", 3),
    ("UnityEngine", "Camera", "get_scaledPixelWidth", 0),
    ("UnityEngine", "Camera", "get_scaledPixelHeight", 0),
    ("UnityEngine", "Camera", "get_pixelWidth", 0),
    ("UnityEngine", "Camera", "get_pixelHeight", 0),
    ("UnityEngine.AI", "NavMeshAgent", "get_speed", 0),
    ("Photon.Pun", "MonoBehaviourPun", "get_photonView", 0),
    ("Photon.Pun", "PhotonView", "get_IsMine", 0),
    ("Photon.Pun", "PhotonNetwork", "get_IsMasterClient", 0),
    ("Photon.Pun", "PhotonNetwork", "get_InRoom", 0),
    ("Photon.Pun", "PhotonNetwork", "get_MasterClient", 0),
    ("Photon.Pun", "PhotonNetwork", "RaiseEvent", 4),
    ("Photon.Realtime", "Player", "get_ActorNumber", 0),
    ("Photon.Realtime", "LoadBalancingClient", "OnEvent", 1),
    ("Photon.Realtime", "RaiseEventOptions", ".ctor", 0),
    ("ExitGames.Client.Photon", "EventData", "get_CustomData", 0),
    ("ExitGames.Client.Photon", "EventData", "get_Sender", 0),
)

NAMED_FIELDS = (
    ("", "LevelStats", "timeSpentInvestigating"),
    ("", "Crucifix", "useCount"),
    ("", "StoreItem", "equipmentType"), ("", "StoreItem", "tier"),
    ("", "Network.PlayerSpot", "photonPlayer"),
    ("", "Network.PlayerSpot", "player"),
    ("", "Network.PlayerSpot", "accountName"),
    ("", "NetworkedHeadMountedSocket", "modelHeadSpot"),
    ("", "Difficulty", "sanityMonitor"),
    ("Photon.Realtime", "Player", "actorNumber"),
    ("Photon.Realtime", "Player", "IsLocal"),
    ("Photon.Realtime", "Player", "nickName"),
    ("Photon.Realtime", "RaiseEventOptions", "Receivers"),
    ("ExitGames.Client.Photon", "EventData", "Code"),
)

# Class, field type, expected number of instance fields for runtime type lookup.
TYPED_FIELDS = (
    ("LevelController", "GhostAI", 1), ("GhostAI", "NavMeshAgent", 1),
    ("Network", "LocalPlayer", 1), ("Network", "Player", 1),
    ("Network", "List<Network.PlayerSpot>", 1),
    ("LocalPlayer", "Camera", 1), ("LocalPlayer", "PlayerStamina", 1),
    ("Player", "PlayerSanity", 1),
    ("Player", "NetworkedHeadMountedSocket", 1),
    ("PCPropGrab", "PCCanvas", 1), ("PCPropGrab", "Collider", 2),
    ("PCPropGrab", "List<PhotonObjectInteract>", 1),
    ("PCPropGrab", "int", 1), ("Equipment", "StoreItem", 1),
    ("PhotonObjectInteract", "Prop", 1),
)

# Fixed or fallback offsets actually used in payload. A same-type replacement
# at the same offset is indistinguishable here and still needs runtime checks.
FIXED_FIELDS = (
    ("GhostAI", 0x30, "GhostAI.<obf>"), ("GhostAI", 0xF3, "bool"),
    ("Player", 0x28, "bool"), ("Player", 0x88, "Transform"),
    ("Network", 0x38, "List<Network.PlayerSpot>"),
    ("LevelValues", 0x38, "Difficulty"), ("PlayerSanity", 0x30, "float"),
    ("PlayerStamina", 0x40, "float"), ("PCCanvas", 0x88, "PCCanvas.<obf>"),
    ("PCPropGrab", 0x50, "List<PhotonObjectInteract>"),
)

ABI_TYPES = (("Photon.Pun", "PhotonMessageInfo"),
             ("ExitGames.Client.Photon", "SendOptions"))
ENUM_FIELDS = (("GhostAI", 0x30), ("PCCanvas", 0x88),
               ("StoreItem", 0x18), ("StoreItem", 0x1C))

HEADER = re.compile(r"(?m)^[^\r\n]*// TypeDefIndex:\s*(\d+)[^\r\n]*\r?$")
TYPE = re.compile(r"\b(class|struct|enum|interface)\s+([^\s:]+)")
NAMESPACE = re.compile(r"(?m)^// Namespace:([^\r\n]*)\r?$")
FIELD = re.compile(r"^\s+([^;\r\n]+); // (0x[0-9A-Fa-f]+)\s*$", re.M)
METHOD = re.compile(r"^\s+([^()\r\n]+)\(([^\r\n]*)\)\s*(?:\{ \}|;)\s*$", re.M)
ENUM_VALUE = re.compile(r"^\s+public const \S+ (\S+) = ([^;\r\n]+);\s*$", re.M)
MODIFIERS = {"public", "private", "protected", "internal", "static", "readonly",
             "const", "virtual", "override", "sealed", "abstract", "extern", "unsafe", "new"}


def normalized(value: str) -> str:
    return re.sub(r"[^\x00-\x7f]+", "<obf>", value)


def split_parameters(value: str) -> list[str]:
    if not value.strip():
        return []
    # Method contracts above use simple types, but handle generic type commas.
    parts, start, depth = [], 0, 0
    for index, char in enumerate(value):
        if char in "<([":
            depth += 1
        elif char in ">)]":
            depth -= 1
        elif char == "," and depth == 0:
            parts.append(value[start:index])
            start = index + 1
    parts.append(value[start:])
    return [part.strip().split(" = ", 1)[0].rsplit(" ", 1)[0] for part in parts]


def read_dump(path: Path) -> tuple[dict, dict]:
    data = path.read_bytes()
    text = data.decode("utf-8-sig")
    headers = list(HEADER.finditer(text))
    namespaces = list(NAMESPACE.finditer(text))
    positions = [match.start() for match in namespaces]
    result = {}
    for index, header in enumerate(headers):
        declaration = TYPE.search(header.group())
        if not declaration:
            continue
        namespace_index = bisect.bisect_right(positions, header.start()) - 1
        if namespace_index < 0:
            raise ValueError("Type header appears before the first namespace")
        namespace = namespaces[namespace_index].group(1).strip()
        end = headers[index + 1].start() if index + 1 < len(headers) else len(text)
        body = text[header.end():end]
        fields = []
        for field in FIELD.finditer(body):
            tokens = field.group(1).split()
            qualifiers = []
            while tokens and tokens[0] in MODIFIERS:
                qualifiers.append(tokens.pop(0))
            if len(tokens) < 2:
                continue
            fields.append({"type": " ".join(tokens[:-1]), "name": tokens[-1],
                           "offset": int(field.group(2), 16), "static": "static" in qualifiers})
        methods = []
        for method in METHOD.finditer(body):
            tokens = method.group(1).split()
            qualifiers = []
            while tokens and tokens[0] in MODIFIERS:
                qualifiers.append(tokens.pop(0))
            if len(tokens) < 2:
                continue
            parameters = split_parameters(method.group(2))
            methods.append({"name": tokens[-1], "return": " ".join(tokens[:-1]),
                            "parameters": parameters, "static": "static" in qualifiers})
        result[(namespace, declaration.group(2))] = {
            "kind": declaration.group(1), "index": int(header.group(1)),
            "fields": fields, "methods": methods,
            "enum_values": [(match.group(1), match.group(2)) for match in ENUM_VALUE.finditer(body)]
            if declaration.group(1) == "enum" else [],
        }
    if not result:
        raise ValueError("No IL2CPP TypeDefIndex declarations found")
    return result, {"path": str(path.resolve()), "bytes": len(data),
                    "sha256": hashlib.sha256(data).hexdigest(), "types": len(result)}


def field_shape(fields: list[dict]) -> list[tuple]:
    return [(normalized(f["type"]), normalized(f["name"]), f["offset"], f["static"])
            for f in fields]


def method_shape(method: dict) -> str:
    prefix = "static " if method["static"] else ""
    return normalized(f"{prefix}{method['return']} {method['name']}({', '.join(method['parameters'])})")


def check(old: dict, new: dict) -> list[dict]:
    checks = []

    def record(category, name, before, after, valid=True):
        checks.append({"category": category, "name": name,
                       "passed": bool(before and after and before == after and valid),
                       "old": before, "new": after})

    for name in GAME_TYPES:
        record("game_field_layout", name,
               field_shape(old.get(("", name), {}).get("fields", [])),
               field_shape(new.get(("", name), {}).get("fields", [])))
    for namespace, owner, name, count in METHODS:
        selected = []
        for dump in (old, new):
            selected.append([method_shape(m) for m in dump.get((namespace, owner), {}).get("methods", [])
                             if m["name"] == name and len(m["parameters"]) == count])
        record("method_signature", f"{namespace + '.' if namespace else ''}{owner}.{name}/{count}",
               *selected, valid=len(selected[0]) == len(selected[1]) == 1)
    for namespace, owner, name in NAMED_FIELDS:
        selected = []
        for dump in (old, new):
            selected.append(field_shape([f for f in dump.get((namespace, owner), {}).get("fields", [])
                                         if f["name"] == name and not f["static"]]))
        record("named_field", f"{namespace + '.' if namespace else ''}{owner}.{name}", *selected,
               valid=len(selected[0]) == len(selected[1]) == 1)
    for owner, field_type, count in TYPED_FIELDS:
        selected = []
        for dump in (old, new):
            selected.append([hex(f["offset"]) for f in dump.get(("", owner), {}).get("fields", [])
                             if f["type"] == field_type and not f["static"]])
        record("typed_field", f"{owner} -> {field_type}", *selected,
               valid=len(selected[0]) == len(selected[1]) == count)
    for owner, offset, field_type in FIXED_FIELDS:
        selected = []
        for dump in (old, new):
            selected.append([normalized(f["type"]) for f in dump.get(("", owner), {}).get("fields", [])
                             if f["offset"] == offset and not f["static"]])
        record("fixed_field", f"{owner}+{hex(offset)}", *selected,
               valid=selected[0] == selected[1] == [field_type])
    for namespace, name in ABI_TYPES:
        record("value_type_layout", f"{namespace}.{name}",
               field_shape(old.get((namespace, name), {}).get("fields", [])),
               field_shape(new.get((namespace, name), {}).get("fields", [])),
               valid=old.get((namespace, name), {}).get("kind") == new.get((namespace, name), {}).get("kind") == "struct")
    for owner, offset in ENUM_FIELDS:
        selected = []
        for dump in (old, new):
            matches = [f for f in dump.get(("", owner), {}).get("fields", [])
                       if f["offset"] == offset and not f["static"]]
            enum = dump.get(("", matches[0]["type"]), {}) if len(matches) == 1 else {}
            selected.append(enum.get("enum_values", []))
        record("enum_values", f"{owner}+{hex(offset)} enum", *selected)
    return checks


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("old_dump", type=Path)
    parser.add_argument("new_dump", type=Path)
    parser.add_argument("--output", type=Path, help="Optional detailed JSON report")
    args = parser.parse_args()
    try:
        old, old_meta = read_dump(args.old_dump)
        new, new_meta = read_dump(args.new_dump)
        checks = check(old, new)
    except (OSError, UnicodeError, ValueError) as exc:
        parser.error(str(exc))
    failures = [item for item in checks if not item["passed"]]
    index_changes = [{"class": name, "old": old[("", name)]["index"], "new": new[("", name)]["index"]}
                     for name in GAME_TYPES if ("", name) in old and ("", name) in new
                     and old[("", name)]["index"] != new[("", name)]["index"]]
    report = {"scope": "Static metadata contract comparison; does not validate runtime field semantics or hook execution.",
              "old_dump": old_meta, "new_dump": new_meta,
              "summary": {"passed": len(checks) - len(failures), "total": len(checks),
                          "categories": dict(Counter(c["category"] for c in checks))},
              "type_index_changes": index_changes, "checks": checks}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"{len(checks) - len(failures)}/{len(checks)} metadata checks passed")
    for item in failures:
        print(f"REVIEW [{item['category']}] {item['name']}")
        print("  old:", json.dumps(item["old"], ensure_ascii=True))
        print("  new:", json.dumps(item["new"], ensure_ascii=True))
    print(f"TypeDefIndex changes (informational): {len(index_changes)}")
    if args.output:
        print(f"Report: {args.output.resolve()}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
