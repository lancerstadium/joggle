#!/usr/bin/env python3
"""Build matched unfused and fused C variants from one canonical model."""

from __future__ import annotations

import argparse
import csv
import json
import shutil
import subprocess
from pathlib import Path

from measure_reorder import digest, invocation, run


def query(command: list[str]) -> dict[str, object]:
    result = subprocess.run(
        command, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True
    )
    value = json.loads(result.stdout)
    if not isinstance(value, dict):
        raise RuntimeError("stat.summary did not return an object")
    return value


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--modules", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    tool = args.tool.resolve()
    modules = args.modules.resolve()
    model = args.model.resolve()
    for path, kind in ((tool, "file"), (model, "file"),
                       (modules, "directory")):
        valid = path.is_file() if kind == "file" else path.is_dir()
        if not valid:
            parser.error(f"{path} is not a {kind}")

    revision = subprocess.run(
        ["git", "rev-parse", "HEAD"], check=True, text=True,
        stdout=subprocess.PIPE
    ).stdout.strip()
    root = args.out_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    canonical_hash = digest(model)
    rows: list[dict[str, object]] = []

    transforms = {
        "baseline": [],
        "fused": ["tile.fuse"],
    }
    for variant, functions in transforms.items():
        directory = root / variant
        directory.mkdir(parents=True, exist_ok=True)
        transformed = directory / "transformed.jog"
        clean = directory / "clean.jog"
        planned = directory / "planned.jog"
        placed = directory / "placed.jog"
        source = directory / "model.c"

        if functions:
            transform_seconds = run(invocation(
                tool, "run", functions, model,
                [modules]
            ), transformed)
        else:
            shutil.copyfile(model, transformed)
            transform_seconds = 0.0
        clean_seconds = run(invocation(
            tool, "run", ["bounds.fold", "opt.fold", "opt.basic"],
            transformed, [modules]
        ), clean)
        plan_seconds = run(invocation(
            tool, "run", ["mem.plan"], clean, [modules]
        ), planned)
        place_seconds = run(invocation(
            tool, "run", ["c.place"], planned, [modules], ['"static"']
        ), placed)
        emit_seconds = run(invocation(
            tool, "emit", ["c.source"], placed, [modules], ['"weights"']
        ), source)
        stats = query(invocation(
            tool, "query", ["stat.summary"], placed, [modules]
        ))
        rows.append({
            "variant": variant,
            "revision": revision,
            "canonical_ir_sha256": canonical_hash,
            "transformed_ir_sha256": digest(transformed),
            "placed_ir_sha256": digest(placed),
            "c_sha256": digest(source),
            "loops": stats["loops"],
            "memory_slots": stats["mem_slots"],
            "memory_elements": stats["mem_elems"],
            "canonical_ir_bytes": model.stat().st_size,
            "transformed_ir_bytes": transformed.stat().st_size,
            "placed_ir_bytes": placed.stat().st_size,
            "c_bytes": source.stat().st_size,
            "transform_seconds": f"{transform_seconds:.6f}",
            "clean_seconds": f"{clean_seconds:.6f}",
            "plan_seconds": f"{plan_seconds:.6f}",
            "place_seconds": f"{place_seconds:.6f}",
            "emit_seconds": f"{emit_seconds:.6f}",
        })

    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


if __name__ == "__main__":
    main()
