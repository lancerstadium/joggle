#!/usr/bin/env python3
"""Merge backend-specific Figure 8 or 9 CSVs and validate the result."""

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


VARIANT_ORDER = {"joggle-unoptimized": 0, "joggle-optimized": 1, "onnxruntime": 2}
KIND_ORDER = {"coverage": 0, "prepare": 1, "execute": 2, "memory": 3}


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write_json(path: Path, value: dict[str, object]) -> None:
    temporary = path.with_name(f".{path.name}.tmp")
    with temporary.open("w", encoding="utf-8") as stream:
        stream.write(json.dumps(value, indent=2, sort_keys=True) + "\n")
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("figure", choices=("8", "9"))
    parser.add_argument("inputs", type=Path, nargs="+")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--allow-partial", action="store_true")
    args = parser.parse_args()
    if args.output.exists():
        raise SystemExit(f"refusing to replace {args.output}")
    record_path = args.output.with_suffix(".merge.json")
    if record_path.exists():
        raise SystemExit(f"refusing to replace {record_path}")
    root = Path(__file__).resolve().parent
    template = root / "templates" / (
        "figure-08-operators.csv" if args.figure == "8" else "figure-09-models.csv"
    )
    with template.open(newline="", encoding="utf-8") as stream:
        header = next(csv.reader(stream))
    rows = []
    for path in args.inputs:
        with path.open(newline="", encoding="utf-8") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames != header:
                raise SystemExit(f"{path}: columns differ from {template.name}")
            rows.extend(reader)
    subject = "case_id" if args.figure == "8" else "model"
    rows.sort(key=lambda row: (
        row[subject], VARIANT_ORDER.get(row["variant"], 99),
        KIND_ORDER.get(row["record_kind"], 99),
        int(row["iteration"] or -1), int(row["seed"] or -1),
    ))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    handle, temporary = tempfile.mkstemp(
        prefix=f".{args.output.name}.", suffix=".tmp", dir=args.output.parent
    )
    os.close(handle)
    temporary_path = Path(temporary)
    try:
        with temporary_path.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=header)
            writer.writeheader()
            writer.writerows(rows)
        command = [
            sys.executable, str(root / "validate_figure.py"),
            args.figure, str(temporary_path),
        ]
        if args.allow_partial:
            command.append("--allow-partial")
        subprocess.run(command, check=True)
        temporary_path.replace(args.output)
    finally:
        if temporary_path.exists():
            temporary_path.unlink()
    write_json(record_path, {
        "schema": "benchmark-merge/v1",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "figure": int(args.figure),
        "release_eligible": not args.allow_partial,
        "inputs": [{"path": str(path.resolve()), "sha256": sha256(path)}
                   for path in args.inputs],
        "output": {"path": str(args.output.resolve()), "sha256": sha256(args.output)},
        "rows": len(rows),
    })
    print(f"merged {len(rows)} rows into {args.output}; record={record_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
