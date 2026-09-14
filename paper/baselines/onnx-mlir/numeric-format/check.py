#!/usr/bin/env python3
"""Verify the preserved ONNX-MLIR numeric-format boundary."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path


HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
SOURCES = sorted(
    path
    for root in (HERE / "src", HERE / "test")
    for path in root.rglob("*")
    if path.is_file()
)


def source_lines(path: Path) -> int:
    count = 0
    block_comment = False
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if block_comment:
            if "*/" not in line:
                continue
            block_comment = False
            line = line.split("*/", 1)[1].strip()
        if line.startswith("/*"):
            if "*/" not in line[2:]:
                block_comment = True
            continue
        if line and not line.startswith("//"):
            count += 1
    return count


def digest(paths: list[Path]) -> str:
    value = hashlib.sha256()
    for path in paths:
        value.update(path.relative_to(REPO).as_posix().encode("utf-8"))
        value.update(b"\0")
        value.update(path.read_bytes())
        value.update(b"\0")
    return value.hexdigest()


def file_digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def main() -> None:
    result = json.loads((HERE / "result.json").read_text(encoding="utf-8"))
    require(result.get("task") == "numeric-format", "unexpected task")
    require(result.get("status") == "unsupported", "unexpected task status")
    implementation = result["implementation"]
    require(len(SOURCES) == implementation["files"], "source-file count changed")
    require(
        sum(source_lines(path) for path in SOURCES) == implementation["source_lines"],
        "source-line count changed",
    )
    require(
        sum(path.stat().st_size for path in SOURCES) == implementation["source_bytes"],
        "source-byte count changed",
    )
    require(digest(SOURCES) == implementation["source_sha256"], "source digest changed")
    for key in ("imported_ir", "materialized_ir", "lowered_ir"):
        path = HERE / result["outputs"][key]
        require(
            file_digest(path) == result["outputs"][f"{key}_sha256"],
            f"{key} digest changed",
        )
    materialized = (HERE / "materialized.mlir").read_text(encoding="utf-8")
    lowered = (HERE / "lowered.mlir").read_text(encoding="utf-8")
    require("tensor<4x!sat.int<5>>" in materialized, "nested sat type missing")
    require(materialized.count('"sat.add"') == 5, "materialized add count changed")
    require("!sat.int" not in lowered, "sat type remains after lowering")
    require(lowered.count("sat.generated") == 4, "helper count changed")
    require(result["oracle"]["maximum_absolute_error"] == 0.0, "oracle changed")
    requirements = {item["name"]: item["status"] for item in result["requirements"]}
    require(
        requirements["execute scalar and tensor cases through two targets"]
        == "unsupported",
        "unsupported boundary changed",
    )
    print("ONNX-MLIR numeric-format record: unsupported at second target")


if __name__ == "__main__":
    main()
