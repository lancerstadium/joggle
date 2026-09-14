#!/usr/bin/env python3
"""Check committed fixture and contract digests without generation dependencies."""

import hashlib
import json
from pathlib import Path


HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
FIXTURES = ("implementation", "policy", "numeric-format")


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    for name in FIXTURES:
        root = HERE / name
        manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
        if manifest.get("schema") != 1:
            raise ValueError(f"unexpected manifest schema: {name}")
        contract = REPO / manifest["contract"]
        if digest(contract) != manifest["contract_sha256"]:
            raise ValueError(f"contract digest changed: {name}")
        declared = set(manifest["files"])
        present = {
            path.relative_to(root).as_posix()
            for path in root.rglob("*")
            if path.is_file() and path.name != "manifest.json"
        }
        if present != declared:
            raise ValueError(f"fixture file set changed: {name}")
        for relative, expected in manifest["files"].items():
            if digest(root / relative) != expected:
                raise ValueError(f"fixture digest changed: {name}/{relative}")
    print(f"fixture records: pass ({len(FIXTURES)} fixtures)")


if __name__ == "__main__":
    main()
