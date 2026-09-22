#!/usr/bin/env python3
"""Validate, render, and hash-bind the four evaluation figures."""

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

from merge_update_rows import validate_provider

FIGURES = {
    4: ("figure-04-extension.csv", "figure_04_extension.py"),
    5: ("figure-05-footprint.csv", "figure_05_footprint.py"),
    6: ("figure-06-update.csv", "figure_06_update.py"),
    7: ("figure-07-performance.csv", "figure_07_performance.py"),
}
SCHEMAS = {4: "extension-agent-assembly/v1", 5: "figure-05-footprint/v1",
           6: "update-assembly/v1", 7: "performance-merge/v1"}


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


def validate_record(figure: int, csv_path: Path) -> Path:
    suffix = ".merge.json" if figure == 7 else ".json"
    record_path = csv_path.with_suffix(suffix)
    record = load_json(record_path)
    if record.get("schema") != SCHEMAS[figure]:
        fail(f"Figure {figure}: wrong provenance schema")
    expected = (record.get("output", {}).get("sha256") if figure in {4, 7}
                else record.get("output_sha256", record.get("csv_sha256")))
    if expected != sha256(csv_path):
        fail(f"Figure {figure}: CSV hash differs from provenance record")
    for item in record.get("inputs", {}).values() if isinstance(record.get("inputs"), dict) else record.get("inputs", []):
        path = Path(item.get("path", ""))
        if item.get("sha256") != sha256(path):
            fail(f"Figure {figure}: input hash differs for {path}")
        if item.get("record"):
            source_record = Path(item["record"])
            if item.get("record_sha256") != sha256(source_record):
                fail(f"Figure {figure}: source record hash differs for {source_record}")
        if figure == 6:
            with path.open(newline="", encoding="utf-8") as stream:
                provider_rows = list(csv.DictReader(stream))
            validate_provider(path, provider_rows)
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
    inputs, records, outputs = [], [], []
    try:
        for figure, (csv_name, plot_name) in FIGURES.items():
            csv_path = data / csv_name
            validator = ([sys.executable, str(root / "validate_reactive.py"), str(csv_path)]
                         if figure == 6 else
                         [sys.executable, str(root / "validate_figure.py"), str(figure), str(csv_path)])
            run(validator, root.parent)
            record_path = validate_record(figure, csv_path)
            output = staging / f"figure-{figure:02d}.pdf"
            run([sys.executable, str(root / "figures" / plot_name), str(csv_path),
                 "--output", str(output)], root.parent)
            inputs.append({"figure": figure, "path": str(csv_path), "sha256": sha256(csv_path)})
            records.append({"figure": figure, "path": str(record_path), "sha256": sha256(record_path)})
            for produced in (output, output.with_suffix(".png")):
                outputs.append({"figure": figure, "format": produced.suffix[1:],
                                "path": produced.name, "sha256": sha256(produced)})
        manifest = {"schema": "evaluation-release/v2",
                    "created_utc": datetime.now(timezone.utc).isoformat(),
                    "inputs": inputs, "records": records, "outputs": outputs}
        manifest_path = staging / "release-manifest.json"
        with manifest_path.open("w", encoding="utf-8") as stream:
            stream.write(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
            stream.flush(); os.fsync(stream.fileno())
        staging.replace(args.output_dir.resolve())
    except BaseException:
        print(f"release gate failed; staged diagnostics remain at {staging}", file=sys.stderr)
        raise
    print(f"validated and rendered four figures in {args.output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
