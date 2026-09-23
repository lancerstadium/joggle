#!/usr/bin/env python3
"""Assemble provider outputs into the CSV consumed by Figure 6."""

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


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def validate_provider(path: Path, rows: list[dict[str, str]]) -> Path:
    record_path = path.with_suffix(".json")
    record = json.loads(record_path.read_text(encoding="utf-8"))
    if (record.get("schema") != "update-provider/v1" or record.get("dirty")
            or record.get("output_sha256") != sha256(path)):
        raise SystemExit(f"{path}: invalid provider record")
    if any(row["path"] == "matched" for row in rows):
        if record.get("workload") != "compiler-pipeline/v1":
            raise SystemExit(f"{path}: matched results require a compiler pipeline, "
                             "not metadata-propagation diagnostics")
        if record.get("visited_ops_unit") != "subject-operation-visits":
            raise SystemExit(f"{path}: visited_ops must count subject operations, "
                             "not evaluator instructions")
    return record_path


def production_rows(paths: list[Path], cases: list[dict], spec: Path,
                    input_index: Path, population: Path) -> tuple[list[dict], list[dict]]:
    """Reconcile each CSV coordinate with its original, hash-bound worker record."""
    from run_baseline_benchmarks import production_sample_row

    lookup = {(c["case_id"], c["edit_id"]): c for c in cases}
    rows, records = [], []
    seen, identities, inputs = set(), {}, {}
    protocol = {str(p.resolve()): sha256(p) for p in (spec, input_index, population)}
    for path in paths:
        record_path, raw_path = path.with_suffix(".json"), path.with_suffix(".samples.jsonl")
        record = json.loads(record_path.read_text())
        if (record.get("schema") != "production-update-collection/v1" or
                record.get("dirty") is not False or record.get("stable") is not True or
                record.get("population_sha256") != sha256(population) or
                record.get("output_sha256") != sha256(path) or
                record.get("raw_sha256") != sha256(raw_path) or
                any(record.get("sources", {}).get(p) != h for p, h in protocol.items())):
            raise ValueError(f"{path}: invalid production collection provenance")
        with path.open(newline="") as stream:
            collected = list(csv.DictReader(stream))
        samples = [json.loads(line) for line in raw_path.read_text().splitlines()]
        if len(collected) != len(samples) or len(collected) != record.get("rows") or not collected:
            raise ValueError(f"{path}: CSV/raw population mismatch")
        failures = 0
        for row, raw in zip(collected, samples, strict=True):
            key = (row["backend"], row["case_id"], row["edit_id"], row["iteration"], row["policy"])
            if (key in seen or row["backend"] not in {"joggle", "tvm", "onnx-mlir"} or
                    row["policy"] not in {"update", "rebuild"} or
                    not 0 <= int(row["iteration"]) < record["iterations"] or
                    int(row["order"]) not in (0, 1) or int(row["seed"]) != record["seed"]):
                raise ValueError(f"{path}: invalid or duplicate production coordinate {key}")
            seen.add(key)
            if set(raw["key"]) != {"backend", "case_id", "edit_id", "iteration", "policy", "order", "seed"} or any(
                    row[k] != str(v) for k, v in raw["key"].items()):
                raise ValueError(f"{path}: CSV/raw coordinate mismatch")
            case = lookup[(row["case_id"], row["edit_id"])]
            edit_hash = hashlib.sha256(json.dumps(case["edit"], sort_keys=True).encode()).hexdigest()
            if row["edit_sha256"] != edit_hash:
                raise ValueError(f"{path}: edit differs from frozen population")
            sample = raw["sample"]
            stages = {}
            retained = ""
            if row["correct"] == "true":
                checked = production_sample_row(sample, row["backend"], row["policy"], case,
                                                sha256(spec), sha256(input_index))
                compiler = sample["compiler_identity"]
                if (compiler.get("collector_dirty") is not False or
                        compiler.get("collector_revision") != record.get("revision")):
                    raise ValueError("worker and collection revisions differ")
                if row["error"] or any(row[k] != str(v) for k, v in checked.items()):
                    raise ValueError(f"{path}: CSV differs from worker result")
                identity = checked["compiler_identity_sha256"]
                if identities.setdefault(row["backend"], identity) != identity:
                    raise ValueError("mixed compiler identities within one backend")
                input_key = (row["case_id"], row["edit_id"])
                if inputs.setdefault(input_key, checked["input_digest"]) != checked["input_digest"]:
                    raise ValueError("paired systems used different inputs")
                stages = sample["replacement"]["stages_ns"]
                if (not isinstance(stages, dict) or any(not isinstance(k, str) or
                        type(v) is not int or v < 0 for k, v in stages.items()) or
                        sum(stages.values()) > int(row["ready_ns"]) - int(row["edit_ns"])):
                    raise ValueError("invalid or overlapping backend stage timings")
                retained = sample["retained_state"]
            elif row["correct"] == "false":
                failures += 1
                if not row["error"] or sample.get("failure") != row["error"] or any(
                        row[k] for k in ("wall_ns", "ready_ns", "edit_ns", "validation_ns",
                                         "model_sha256", "input_digest", "output_digest", "compiler_identity_sha256")):
                    raise ValueError("failure row contains measurements or lacks its diagnostic")
            else:
                raise ValueError("invalid correctness status")
            rows.append(dict(row, retained_state=retained, stages_ns=json.dumps(stages, sort_keys=True)))
        if failures != record.get("failures"):
            raise ValueError(f"{path}: failure count mismatch")
        records.append({"path": str(path.resolve()), "sha256": sha256(path),
                        "record": str(record_path.resolve()), "record_sha256": sha256(record_path),
                        "raw": str(raw_path.resolve()), "raw_sha256": sha256(raw_path)})
    # Every selected edit/repetition must retain both policies, including failures.
    pairs = {}
    for row in rows:
        key = tuple(row[k] for k in ("backend", "case_id", "edit_id", "iteration"))
        pairs.setdefault(key, []).append(row)
    for pair in pairs.values():
        if ({r["policy"] for r in pair} != {"update", "rebuild"} or
                {r["order"] for r in pair} != {"0", "1"} or len({r["seed"] for r in pair}) != 1):
            raise ValueError("incomplete or inconsistent update/rebuild pair")
    rows.sort(key=lambda r: (r["case_id"], r["edit_id"], r["backend"], int(r["iteration"]), r["policy"]))
    return rows, records


def merge_production(args: argparse.Namespace) -> int:
    from run_baseline_benchmarks import production_population

    if not args.allow_partial:
        raise SystemExit("production release assembly is not enabled; use --allow-partial for audited samples")
    cases = production_population(argparse.Namespace(spec=args.spec, inputs=args.inputs_dir,
        model_root=args.model_root, edit_manifest=args.population, case_id=None))
    rows, records = production_rows(args.inputs, cases, args.spec,
                                    args.inputs_dir / "index.json", args.population)
    stage_names = sorted({name for row in rows for name in json.loads(row["stages_ns"])})
    for row in rows:
        stages = json.loads(row["stages_ns"])
        # Empty means the backend did not report that stage, not zero work.
        row.update({"stage_" + name + "_ns": stages.get(name, "") for name in stage_names})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    payload = {"schema": "production-update-assembly/v1", "inputs": records,
               "created_utc": datetime.now(timezone.utc).isoformat(),
               "output_sha256": sha256(args.output), "rows": len(rows),
               "partial": True, "release_eligible": False,
               "failures": sum(row["correct"] != "true" for row in rows)}
    with args.output.with_suffix(".json").open("x") as stream:
        json.dump(payload, stream, indent=2, sort_keys=True)
        stream.write("\n")
    print(f"assembled {len(rows)} audited production samples in {args.output}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", type=Path, nargs="+")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--allow-partial", action="store_true")
    parser.add_argument("--production", action="store_true")
    parser.add_argument("--spec", type=Path, default=Path(__file__).resolve().parent / "manifests/benchmark-cases.json")
    parser.add_argument("--population", type=Path, default=Path(__file__).resolve().parent / "manifests/production-node-edits.json")
    parser.add_argument("--inputs-dir", type=Path)
    parser.add_argument("--model-root", type=Path)
    args = parser.parse_args()
    record_path = args.output.with_suffix(".json")
    if args.output.exists() or record_path.exists():
        raise SystemExit("refusing to replace an existing output or record")
    if args.production:
        if args.inputs_dir is None or args.model_root is None:
            parser.error("--production requires --inputs-dir and --model-root")
        return merge_production(args)
    root = Path(__file__).resolve().parent
    with (root / "templates/figure-06-update.csv").open(newline="", encoding="utf-8") as stream:
        header = next(csv.reader(stream))
    rows = []
    records = []
    for path in args.inputs:
        with path.open(newline="", encoding="utf-8") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames != header:
                raise SystemExit(f"{path}: columns differ from Figure 6 schema")
            provider_rows = list(reader)
        provider_record = validate_provider(path, provider_rows)
        rows.extend(provider_rows)
        records.append({"path": str(path.resolve()), "sha256": sha256(path),
                        "record": str(provider_record.resolve()),
                        "record_sha256": sha256(provider_record)})
    rows.sort(key=lambda row: (
        row["path"], row["subject"], row["system"], row["edit_class"],
        row["edit_scope"], row["edit_site"], row["policy"],
        row["stage"], int(row["iteration"]), int(row["seed"]),
    ))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    handle, temporary = tempfile.mkstemp(prefix=f".{args.output.name}.", dir=args.output.parent)
    os.close(handle); temporary_path = Path(temporary)
    try:
        with temporary_path.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=header)
            writer.writeheader(); writer.writerows(rows)
        validation = [sys.executable, str(root / "validate_reactive.py"),
                      str(temporary_path)]
        if args.allow_partial:
            validation.append("--allow-partial")
        subprocess.run(validation, check=True)
        temporary_path.replace(args.output)
    finally:
        temporary_path.unlink(missing_ok=True)
    payload = {"schema": "update-assembly/v1",
               "created_utc": datetime.now(timezone.utc).isoformat(),
               "inputs": records, "output_sha256": sha256(args.output),
               "rows": len(rows), "partial": args.allow_partial}
    with record_path.open("w", encoding="utf-8") as stream:
        stream.write(json.dumps(payload, indent=2, sort_keys=True) + "\n")
        stream.flush(); os.fsync(stream.fileno())
    print(f"assembled {len(rows)} Figure 6 rows in {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
