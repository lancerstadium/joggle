#!/usr/bin/env python3
"""Validate, render, and bind the complete six-figure evaluation release."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


FIGURES = {
    4: ("figure-04-extension.csv", "figure_04_extension.py"),
    5: ("figure-05-footprint.csv", "figure_05_footprint.py"),
    6: ("figure-06-model-update.csv", "figure_06_model_update.py"),
    7: ("figure-07-scaling.csv", "figure_07_scaling.py"),
    8: ("figure-08-operators.csv", "figure_08_operators.py"),
    9: ("figure-09-models.csv", "figure_09_models.py"),
}


def fail(message: str) -> None:
    raise SystemExit(message)


def sha256(path: Path) -> str:
    if not path.is_file():
        fail(f"missing release input: {path}")
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"invalid release record {path}: {error}")
    if not isinstance(value, dict):
        fail(f"release record is not an object: {path}")
    return value


def require_hash(path: Path, expected: Any, where: str) -> None:
    if not isinstance(expected, str) or sha256(path) != expected:
        fail(f"{where}: hash differs for {path}")


def row_count(path: Path) -> int:
    with path.open(newline="", encoding="utf-8") as stream:
        return sum(1 for _row in csv.DictReader(stream))


def validate_provenance(figure: int, csv_path: Path) -> Path:
    if figure == 4:
        record_path = csv_path.with_suffix(".json")
        record = load_json(record_path)
        if record.get("schema") != "extension-assembly/v1":
            fail("Figure 4: wrong assembly record schema")
        require_hash(csv_path, record.get("output", {}).get("sha256"), "Figure 4")
        for name, item in record.get("inputs", {}).items():
            path = Path(item.get("path", ""))
            require_hash(path, item.get("sha256"), f"Figure 4 {name}")
        return record_path
    if figure == 5:
        record_path = csv_path.with_suffix(".json")
        record = load_json(record_path)
        if record.get("schema") != "figure-05-footprint/v1":
            fail("Figure 5: wrong collection record schema")
        require_hash(csv_path, record.get("output_sha256"), "Figure 5")
        if len(record.get("cases", [])) != 36:
            fail("Figure 5: expected 36 audited system/task cases")
        return record_path
    if figure in {6, 7}:
        record_path = csv_path.with_suffix(".json")
        record = load_json(record_path)
        if record.get("schema") != "reactive-update/v2" or record.get("dirty"):
            fail(f"Figure {figure}: run record is dirty or has the wrong schema")
        require_hash(csv_path, record.get("csv_sha256"), f"Figure {figure}")
        jobs = record.get("jobs", [])
        if not isinstance(jobs, list) or record.get("expected_jobs") != len(jobs):
            fail(f"Figure {figure}: incomplete job inventory")
        if figure == 6:
            manifest = Path(__file__).resolve().parent / "manifests/reactive-models.csv"
            with manifest.open(newline="", encoding="utf-8") as stream:
                expected_models = [
                    {"name": row["model"], "sha256": row["sha256"]}
                    for row in csv.DictReader(stream)
                ]
            required = {
                "models": expected_models,
                "sites": ["early", "middle", "late"],
                "edit_classes": ["no_op", "operation_metadata", "value_type"],
                "scopes": ["affected", "unrelated"],
                "stages": [5],
                "warmups": 10,
                "iterations": 100,
                "expected_jobs": 15 * 3 * 3 * 4,
            }
            expected_rows = 15 * 3 * 5 * 100 * 4
        else:
            required = {
                "models": [],
                "nodes": [1000, 10000, 100000, 1000000],
                "affected": [1, 8, 64, 512],
                "fanout": [1],
                "sites": [],
                "edit_classes": ["operation_metadata"],
                "scopes": ["affected"],
                "stages": [5],
                "warmups": 10,
                "iterations": 100,
                "expected_jobs": 16 * 4,
            }
            expected_rows = 16 * 100 * 4
        mismatched = [key for key, value in required.items() if record.get(key) != value]
        if mismatched:
            fail(f"Figure {figure}: frozen population differs in {mismatched}")
        if row_count(csv_path) != expected_rows:
            fail(f"Figure {figure}: expected {expected_rows} rows")
        for index, job in enumerate(jobs):
            require_hash(
                Path(job.get("path", "")), job.get("sha256"),
                f"Figure {figure} job {index}",
            )
        return record_path
    record_path = csv_path.with_suffix(".merge.json")
    record = load_json(record_path)
    if (record.get("schema") != "benchmark-merge/v1"
            or record.get("figure") != figure
            or not record.get("release_eligible")):
        fail(f"Figure {figure}: merge record is not release eligible")
    require_hash(csv_path, record.get("output", {}).get("sha256"), f"Figure {figure}")
    inputs = record.get("inputs", [])
    if not isinstance(inputs, list) or len(inputs) != 3:
        fail(f"Figure {figure}: expected three backend inputs")
    for index, item in enumerate(inputs):
        path = Path(item.get("path", ""))
        require_hash(path, item.get("sha256"), f"Figure {figure} input {index}")
        run_record = load_json(path.with_suffix(".run.json"))
        if not run_record.get("release_eligible") or run_record.get("git_dirty"):
            fail(f"Figure {figure}: backend input {path} is not release eligible")
        require_hash(path, run_record.get("output_sha256"), f"Figure {figure} backend")
    return record_path


def run(argv: list[str], cwd: Path) -> None:
    subprocess.run(argv, cwd=cwd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--data-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    if args.output_dir.exists():
        fail(f"refusing to replace {args.output_dir}")
    data = args.data_dir.resolve()
    root = Path(__file__).resolve().parent
    parent = args.output_dir.resolve().parent
    parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=f".{args.output_dir.name}.", dir=parent))
    inputs = []
    records = []
    outputs = []
    try:
        for figure, (csv_name, plot_name) in FIGURES.items():
            csv_path = data / csv_name
            sha256(csv_path)
            validator = (
                [sys.executable, str(root / "validate_figure.py"), str(figure), str(csv_path)]
                if figure in {4, 5, 8, 9}
                else [sys.executable, str(root / "validate_reactive.py"), str(csv_path)]
            )
            run(validator, root.parent)
            record_path = validate_provenance(figure, csv_path)
            output = staging / f"figure-{figure:02d}.pdf"
            run(
                [sys.executable, str(root / "figures" / plot_name),
                 str(csv_path), "--output", str(output)],
                root.parent,
            )
            png = output.with_suffix(".png")
            inputs.append({"figure": figure, "path": str(csv_path),
                           "sha256": sha256(csv_path)})
            records.append({"figure": figure, "path": str(record_path.resolve()),
                            "sha256": sha256(record_path)})
            outputs.extend([
                {"figure": figure, "format": "pdf", "path": output.name,
                 "sha256": sha256(output)},
                {"figure": figure, "format": "png", "path": png.name,
                 "sha256": sha256(png)},
            ])
        manifest = {
            "schema": "evaluation-release/v1",
            "created_utc": datetime.now(timezone.utc).isoformat(),
            "inputs": inputs,
            "records": records,
            "outputs": outputs,
        }
        manifest_path = staging / "release-manifest.json"
        with manifest_path.open("w", encoding="utf-8") as stream:
            stream.write(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
            stream.flush()
            os.fsync(stream.fileno())
        staging.replace(args.output_dir.resolve())
    except BaseException:
        print(
            f"release gate failed; staged diagnostics remain at {staging}",
            file=sys.stderr,
        )
        raise
    print(f"validated and rendered six figures in {args.output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
