#!/usr/bin/env python3
"""Compile one static TFLite model into an inspectable C artifact set."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import time
from pathlib import Path


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def invoke(
    tool: Path,
    action: str,
    functions: list[str],
    source: Path,
    modules: Path,
    args: list[str] | None = None,
) -> list[str]:
    command = [str(tool), action, *functions, str(source)]
    for value in args or []:
        command.extend(["--arg", value])
    command.extend(["-M", str(modules)])
    return command


def capture(command: list[str], output: Path) -> float:
    started = time.perf_counter()
    with output.open("wb") as stream:
        result = subprocess.run(
            command, stdout=stream, stderr=subprocess.PIPE, check=False
        )
    elapsed = time.perf_counter() - started
    if result.returncode != 0:
        message = result.stderr.decode(errors="replace")
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"{message}"
        )
    return elapsed


def query(command: list[str]) -> dict[str, object]:
    result = subprocess.run(
        command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, check=False
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"{result.stderr}"
        )
    value = json.loads(result.stdout)
    if not isinstance(value, dict):
        raise RuntimeError("query did not return an object")
    return value


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--modules", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    args = parser.parse_args()

    tool = args.tool.resolve()
    modules = args.modules.resolve()
    model = args.model.resolve()
    if not tool.is_file():
        parser.error(f"tool is not a file: {tool}")
    if not modules.is_dir():
        parser.error(f"module path is not a directory: {modules}")
    if not model.is_file():
        parser.error(f"model is not a file: {model}")

    root = args.out_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    stages: dict[str, float] = {}

    imported = root / "imported.jog"
    stages["read"] = capture(
        invoke(tool, "read", ["tflite.read"], model, modules), imported
    )
    converted = root / "converted.jog"
    stages["convert"] = capture(
        invoke(
            tool, "run", ["tflite.nn.convert"], imported, modules
        ),
        converted,
    )
    semantic = root / "semantic.jog"
    stages["dce"] = capture(
        invoke(tool, "run", ["opt.dce"], converted, modules, ["[]"]),
        semantic,
    )
    prepared = root / "prepared.jog"
    stages["prepare"] = capture(
        invoke(tool, "run", ["c.prepare"], semantic, modules), prepared
    )
    canonical = root / "canonical.jog"
    stages["clean"] = capture(
        invoke(
            tool, "run", ["bounds.fold", "opt.fold", "opt.basic"],
            prepared, modules
        ),
        canonical,
    )
    scalar = root / "scalar.jog"
    stages["scalarize"] = capture(
        invoke(
            tool, "run", ["tile.scalarize", "opt.basic"],
            canonical, modules
        ),
        scalar,
    )
    planned = root / "planned.jog"
    stages["plan"] = capture(
        invoke(tool, "run", ["mem.plan"], scalar, modules), planned
    )
    noalias = root / "noalias.jog"
    stages["noalias"] = capture(
        invoke(tool, "run", ["c.noalias"], planned, modules), noalias
    )
    placed = root / "model.jog"
    stages["place"] = capture(
        invoke(tool, "run", ["c.place"], noalias, modules, ['"static"']),
        placed,
    )

    outputs = {
        "source": root / "model.c",
        "header": root / "model.h",
        "weights": root / "model.bin",
        "api": root / "api.json",
    }
    stages["source"] = capture(
        invoke(
            tool, "emit", ["c.source"], placed, modules, ['"weights"']
        ),
        outputs["source"],
    )
    stages["header"] = capture(
        invoke(
            tool, "emit", ["c.header"], placed, modules, ['"weights"']
        ),
        outputs["header"],
    )
    stages["weights"] = capture(
        invoke(tool, "emit", ["c.data"], placed, modules),
        outputs["weights"],
    )
    stages["api"] = capture(
        invoke(
            tool, "query", ["c.api"], placed, modules, ['"weights"']
        ),
        outputs["api"],
    )

    revision = subprocess.run(
        ["git", "rev-parse", "HEAD"], check=True, text=True,
        stdout=subprocess.PIPE
    ).stdout.strip()
    record = {
        "schema": 1,
        "revision": revision,
        "model": {"bytes": model.stat().st_size, "sha256": digest(model)},
        "stats": query(
            invoke(tool, "query", ["stat.summary"], placed, modules)
        ),
        "stages_seconds": stages,
        "artifacts": {
            name: {
                "bytes": path.stat().st_size,
                "sha256": digest(path),
            }
            for name, path in outputs.items()
        },
    }
    (root / "compile.json").write_text(
        json.dumps(record, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
