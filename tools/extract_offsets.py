#!/usr/bin/env python3
"""Extract member offsets from a Dumpspace snapshot.

Given a directory containing Dumpspace's ``ClassesInfo.json`` and
``StructsInfo.json``, plus a request JSON that lists CPP-prefixed
class/struct names and the properties wanted from each, emit a
response JSON with each property replaced by its integer offset.

Request schema (flat, CPP-prefixed names):

    {
      "ACharacter": ["Mesh"],
      "FVector":    ["X", "Y", "Z"]
    }

Response schema:

    {
      "ACharacter": { "Mesh": 1296 },
      "FVector":    { "X": 0, "Y": 4, "Z": 8 }
    }

A class/struct name that doesn't appear in Dumpspace collapses to
``null``. A property name that isn't present on a resolved
class/struct collapses to ``null`` for that key. Every miss prints a
one-line warning to stderr.
"""

import argparse
import json
import sys
from pathlib import Path


def build_type_index(dumpspace_dir: Path) -> dict:
    """Return {type_name: [member_entries...]} merging Classes then Structs.

    Entries come from ``ClassesInfo.json`` first, so on a (highly
    unlikely) name collision the class wins.
    """
    index: dict = {}
    for filename in ("ClassesInfo.json", "StructsInfo.json"):
        path = dumpspace_dir / filename
        if not path.is_file():
            sys.exit(f"error: missing {path}")
        with path.open("r", encoding="utf-8") as f:
            try:
                blob = json.load(f)
            except json.JSONDecodeError as exc:
                sys.exit(f"error: failed to parse {path}: {exc}")
        for entry in blob.get("data", []):
            # Each entry is {"TypeName": [member_dicts...]}
            for type_name, members in entry.items():
                index.setdefault(type_name, members)
    return index


def find_member_offset(members: list, prop_name: str):
    """Return the integer offset for ``prop_name`` within ``members``,
    or ``None`` if absent.

    Each member is a 1-key dict. Built-in meta keys
    (``__InheritInfo``, ``__MDKClassSize``) are skipped. Real member
    entries have a list value of
    ``[typeInfo, offset, size, arrayDim, (optional) bitOffset]``.
    """
    for member in members:
        if not isinstance(member, dict):
            continue
        for key, value in member.items():
            if key.startswith("__"):
                continue
            if key != prop_name:
                continue
            if isinstance(value, list) and len(value) >= 2 and isinstance(value[1], int):
                return value[1]
            return None
    return None


def extract(request: dict, type_index: dict) -> dict:
    response: dict = {}
    for type_name, prop_names in request.items():
        if not isinstance(prop_names, list):
            print(
                f"warning: entry for '{type_name}' must be a JSON array, emitting null.",
                file=sys.stderr,
            )
            response[type_name] = None
            continue

        members = type_index.get(type_name)
        if members is None:
            print(f"warning: class/struct '{type_name}' not found.", file=sys.stderr)
            response[type_name] = None
            continue

        type_out: dict = {}
        for prop in prop_names:
            if not isinstance(prop, str):
                print(
                    f"warning: non-string property name in '{type_name}' list, skipping.",
                    file=sys.stderr,
                )
                continue
            offset = find_member_offset(members, prop)
            if offset is None:
                print(
                    f"warning: property '{type_name}.{prop}' not found.",
                    file=sys.stderr,
                )
                type_out[prop] = None
            else:
                type_out[prop] = offset
        response[type_name] = type_out
    return response


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Extract member offsets from a Dumpspace snapshot."
    )
    parser.add_argument(
        "dumpspace_dir",
        type=Path,
        help="Directory containing ClassesInfo.json and StructsInfo.json.",
    )
    parser.add_argument(
        "--request",
        type=Path,
        default=Path("OffsetsRequest.json"),
        help="Request JSON file (default: ./OffsetsRequest.json).",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=None,
        help="Output file (default: OffsetsResponse.json next to --request).",
    )
    args = parser.parse_args()

    if not args.dumpspace_dir.is_dir():
        sys.exit(f"error: {args.dumpspace_dir} is not a directory")
    if not args.request.is_file():
        sys.exit(f"error: request file {args.request} not found")

    with args.request.open("r", encoding="utf-8") as f:
        try:
            request = json.load(f)
        except json.JSONDecodeError as exc:
            sys.exit(f"error: failed to parse {args.request}: {exc}")

    if not isinstance(request, dict):
        sys.exit(f"error: {args.request} must have a JSON object at the top level")

    type_index = build_type_index(args.dumpspace_dir)
    response = extract(request, type_index)

    out_path = args.out if args.out is not None else args.request.parent / "OffsetsResponse.json"
    with out_path.open("w", encoding="utf-8") as f:
        json.dump(response, f, indent=2)
        f.write("\n")

    print(f"wrote {out_path}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
