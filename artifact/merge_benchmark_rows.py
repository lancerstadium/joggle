#!/usr/bin/env python3
"""Assemble the operator and model measurements consumed by Figure 7."""

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


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def header(path: Path) -> list[str]:
    with path.open(newline="", encoding="utf-8") as stream:
        return next(csv.reader(stream))


def write_json(path: Path, value: dict[str, object]) -> None:
    temporary = path.with_name(f".{path.name}.tmp")
    with temporary.open("w", encoding="utf-8") as stream:
        stream.write(json.dumps(value, indent=2, sort_keys=True) + "\n")
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def convert(path: Path, kind: str, expected: list[str]) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != expected:
            raise SystemExit(f"{path}: columns differ from benchmark-{kind}s.csv")
        raw = list(reader)
    result = []
    for row in raw:
        subject = row["case_id"] if kind == "operator" else row["model"]
        subject_hash = row["case_spec_sha256"] if kind == "operator" else row["model_hash"]
        result.append({
            "subject_kind": kind, "subject": subject, "subject_hash": subject_hash,
            "family": row.get("family", ""), "system": row["system"],
            "system_revision": row["system_revision"], "variant": row["variant"],
            "supported": row["supported"], "reason": row["reason"],
            "iteration": row["iteration"], "calls_per_sample": row["calls_per_sample"],
            "latency_ns": row["latency_ns"], "max_abs_error": row["max_abs_error"],
            "max_rel_error": row["max_rel_error"], "input_digest": row["input_digest"],
            "output_digest": row["output_digest"], "correct": row["correct"],
            "seed": row["seed"],
        })
    return result


def audited_input(path: Path) -> dict[str, object]:
    record_path = path.with_suffix(".run.json")
    record = json.loads(record_path.read_text(encoding="utf-8"))
    if (not record.get("release_eligible") or record.get("git_dirty")
            or record.get("output_sha256") != sha256(path)):
        raise SystemExit(f"{path}: run record is not release eligible")
    return {"path": str(path.resolve()), "sha256": sha256(path),
            "record": str(record_path.resolve()), "record_sha256": sha256(record_path)}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--operators", type=Path, nargs="+", required=True)
    parser.add_argument("--models", type=Path, nargs="+", default=[])
    parser.add_argument("--allow-partial", action="store_true",
                        help="Export an explicitly incomplete, validated measurement snapshot")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not args.models and not args.allow_partial:
        parser.error("--models is required for a complete Figure 7 release")
    if args.output.exists():
        raise SystemExit(f"refusing to replace {args.output}")
    record_path = args.output.with_suffix(".merge.json")
    if record_path.exists():
        raise SystemExit(f"refusing to replace {record_path}")

    root = Path(__file__).resolve().parent
    operator_header = header(root / "templates/benchmark-operators.csv")
    model_header = header(root / "templates/benchmark-models.csv")
    output_header = header(root / "templates/figure-07-performance.csv")
    inputs = [*args.operators, *args.models]
    audited = [audited_input(path) for path in inputs]
    rows = []
    for path in args.operators:
        rows.extend(convert(path, "operator", operator_header))
    for path in args.models:
        rows.extend(convert(path, "model", model_header))
    rows.sort(key=lambda row: (
        row["subject_kind"], row["family"], row["subject"],
        VARIANT_ORDER.get(row["variant"], 99), int(row["iteration"] or -1),
    ))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    handle, temporary = tempfile.mkstemp(
        prefix=f".{args.output.name}.", suffix=".tmp", dir=args.output.parent
    )
    os.close(handle)
    temporary_path = Path(temporary)
    try:
        with temporary_path.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=output_header)
            writer.writeheader()
            writer.writerows(rows)
        command = [sys.executable, str(root / "validate_figure.py"), "7", str(temporary_path)]
        if args.allow_partial:
            command.append("--allow-partial")
        subprocess.run(command, check=True)
        temporary_path.replace(args.output)
    finally:
        temporary_path.unlink(missing_ok=True)

    write_json(record_path, {
        "schema": "performance-merge/v1",
        "complete": not args.allow_partial,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "inputs": audited,
        "output": {"path": str(args.output.resolve()), "sha256": sha256(args.output)},
        "rows": len(rows),
    })
    print(f"assembled {len(rows)} Figure 7 rows in {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
