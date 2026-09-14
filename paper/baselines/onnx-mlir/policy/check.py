#!/usr/bin/env python3
"""Verify the preserved ONNX-MLIR policy result without rebuilding it."""

import hashlib
import json
from pathlib import Path


HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
SOURCES = [
    HERE / "src/Accelerators/Policy/CMakeLists.txt",
    HERE / "src/Accelerators/Policy/PolicyAccelerator.cpp",
    HERE / "src/Accelerators/Policy/PolicyAccelerator.hpp",
    HERE / "src/Accelerators/Policy/Runtime/CMakeLists.txt",
    HERE / "src/Accelerators/Policy/Runtime/RuntimePolicy.c",
    HERE / "test/accelerators/Policy/CMakeLists.txt",
    HERE / "test/accelerators/Policy/measure.mlir",
]


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


def source_digest(paths: list[Path]) -> str:
    digest = hashlib.sha256()
    for path in sorted(paths):
        digest.update(path.relative_to(REPO).as_posix().encode("utf-8"))
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def file_digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def main() -> None:
    result = json.loads((HERE / "result.json").read_text(encoding="utf-8"))
    require(result.get("task") == "policy", "unexpected task")
    require(result.get("status") == "pass", "policy result does not pass")

    implementation = result["implementation"]
    require(len(SOURCES) == implementation["files"], "source-file count changed")
    require(
        sum(source_lines(path) for path in SOURCES)
        == implementation["source_lines"],
        "source-line count changed",
    )
    require(
        sum(path.stat().st_size for path in SOURCES)
        == implementation["source_bytes"],
        "source-byte count changed",
    )
    require(
        source_digest(SOURCES) == implementation["source_sha256"],
        "source digest changed",
    )

    for measurement in result["measurement"]:
        require(
            measurement["actual"] == measurement["expected"],
            "cost measurement differs from its declared result",
        )

    for fusion in result["fusion"]:
        path = HERE / fusion["ir"]
        require(file_digest(path) == fusion["ir_sha256"], "IR digest changed")
        require(
            path.read_text(encoding="utf-8").count("affine.for")
            == fusion["actual_loops"]
            == fusion["expected_loops"],
            "preserved loop count differs from its declared result",
        )
        require(fusion["maximum_absolute_error"] == 0.0, "oracle error changed")

    require(result["oracle"]["status"] == "pass", "native oracle did not pass")
    print("ONNX-MLIR policy record: pass")


if __name__ == "__main__":
    main()
