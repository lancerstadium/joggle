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

VARIANT_ORDER = {"joggle-unoptimized": 0, "joggle-optimized": 1, "onnxruntime": 2,
                 "tvm-relax-llvm": 3}


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
            "record": str(record_path.resolve()), "record_sha256": sha256(record_path),
            "correctness_oracle": record.get("correctness_oracle", {
                "schema": "legacy-optimized-reference/v0",
                "graph_optimization": "ORT_ENABLE_ALL",
            })}


def audit_protocols(paths: list[Path], group: str, spec: dict, spec_hash: str) -> None:
    """Match native runs before combining timings from different systems."""
    records = [json.loads(path.with_suffix(".run.json").read_text()) for path in paths]
    if not records:
        return
    measurement = spec["measurement"]
    for path, record in zip(paths, records):
        if record.get("group") != group or record.get("benchmark_spec_sha256") != spec_hash:
            raise SystemExit(f"{path}: workload contract differs")
        for field in ("warmups", "execution_iterations"):
            if record.get(field) != measurement[field]:
                raise SystemExit(f"{path}: {field} differs from measurement contract")
        for case in record["cases"]:
            if record["execution_batches"].get(case) != measurement["execution_batches"].get(case):
                raise SystemExit(f"{path}: batch size differs for {case}")
        # TVM's explicit pool control was added after the original CPU runs.
        # The shared CPU controls must match; backend-specific controls remain
        # recorded in each native run rather than erased from provenance.
        controls = record["thread_environment"]
        for key in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS",
                    "VECLIB_MAXIMUM_THREADS", "NUMEXPR_NUM_THREADS"):
            if controls.get(key) != str(measurement["threads"]):
                raise SystemExit(f"{path}: mismatched thread control {key}")
        if record.get("backend", {}).get("variant") == "tvm-relax-llvm":
            if controls.get("TVM_NUM_THREADS") != str(measurement["threads"]):
                raise SystemExit(f"{path}: mismatched TVM pool control")
    for field in ("input_index_sha256", "seed", "host_controls", "cases", "model_files"):
        if any(record.get(field) != records[0].get(field) for record in records[1:]):
            raise SystemExit(f"{group}: incompatible native runs: {field}")
    candidates = [record for record in records
                  if record.get("variant") in {"joggle-unoptimized", "joggle-optimized"}]
    for field in ("git_revision", "joggle_sha256", "compiler", "compile_flags"):
        if candidates and any(record.get(field) != candidates[0].get(field)
                              for record in candidates[1:]):
            raise SystemExit(f"{group}: unmatched Joggle variants: {field}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--operators", type=Path, nargs="+", required=True)
    parser.add_argument("--models", type=Path, nargs="+", default=[])
    parser.add_argument("--allow-partial", action="store_true",
                        help="Export an explicitly incomplete, validated measurement snapshot")
    parser.add_argument("--variants", nargs="+",
                        help="Explicit comparison population, including optional external backends")
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
    policies = {json.dumps(item["correctness_oracle"], sort_keys=True) for item in audited}
    if len(policies) != 1:
        raise SystemExit("refusing to mix different numerical oracle policies")
    spec_path = root / "manifests/benchmark-cases.json"
    spec = json.loads(spec_path.read_text())
    audit_protocols(args.operators, "operators", spec, sha256(spec_path))
    audit_protocols(args.models, "models", spec, sha256(spec_path))
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
        if args.variants:
            command.extend(["--variants", *args.variants])
        subprocess.run(command, check=True)
        temporary_path.replace(args.output)
    finally:
        temporary_path.unlink(missing_ok=True)

    write_json(record_path, {
        "schema": "performance-merge/v1",
        "complete": not args.allow_partial,
        "variants": args.variants or [row["id"] for row in json.loads(
            (root / "manifests/benchmark-cases.json").read_text())["variants"]],
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "inputs": audited,
        "output": {"path": str(args.output.resolve()), "sha256": sha256(args.output)},
        "rows": len(rows),
    })
    print(f"assembled {len(rows)} Figure 7 rows in {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
