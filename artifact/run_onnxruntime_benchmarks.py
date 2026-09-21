#!/usr/bin/env python3
"""Collect the ONNX Runtime rows for Figures 8 and 9."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import platform
import random
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import numpy as np


NUMPY_DTYPES = {
    "float32": np.dtype("<f4"),
    "uint8": np.dtype("u1"),
    "int8": np.dtype("i1"),
    "int32": np.dtype("<i4"),
}
THREAD_ENV = {
    "OMP_NUM_THREADS": "1",
    "OPENBLAS_NUM_THREADS": "1",
    "MKL_NUM_THREADS": "1",
    "VECLIB_MAXIMUM_THREADS": "1",
    "NUMEXPR_NUM_THREADS": "1",
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def ort_session(model: bytes, measurement: dict[str, Any]):
    import onnxruntime as ort

    options = ort.SessionOptions()
    options.intra_op_num_threads = measurement["threads"]
    options.inter_op_num_threads = measurement["threads"]
    options.execution_mode = getattr(ort.ExecutionMode,
                                     measurement["reference_execution_mode"])
    options.graph_optimization_level = getattr(
        ort.GraphOptimizationLevel, measurement["reference_graph_optimization"]
    )
    options.log_severity_level = 3
    return ort.InferenceSession(
        model, sess_options=options, providers=[measurement["reference_provider"]]
    )


def load_case(
    spec_path: Path, input_root: Path, case_id: str
) -> tuple[dict[str, Any], dict[str, Any], dict[str, np.ndarray]]:
    spec = json.loads(spec_path.read_text())
    cases = spec["operator_cases"] + spec["model_cases"]
    selected = [case for case in cases if case["id"] == case_id]
    if len(selected) != 1:
        raise SystemExit(f"unknown or ambiguous case {case_id}")
    case = selected[0]
    index = json.loads((input_root / "index.json").read_text())
    records = [record for record in index["cases"] if record["id"] == case_id]
    if len(records) != 1:
        raise SystemExit(f"missing input record for {case_id}")
    record = records[0]
    tensors = {item["name"]: item for item in record["tensors"]}
    feeds = {}
    for tensor in case["inputs"]:
        item = tensors[tensor["name"]]
        path = input_root / item["path"]
        data = path.read_bytes()
        if sha256(data) != item["sha256"]:
            raise SystemExit(f"input hash mismatch: {path}")
        feeds[tensor["name"]] = np.frombuffer(
            data, dtype=NUMPY_DTYPES[tensor["dtype"]]
        ).reshape(tensor["shape"])
    return spec["measurement"], record, feeds


def output_digest(names: list[str], outputs: list[np.ndarray]) -> str:
    digest = hashlib.sha256()
    for name, value in zip(names, outputs, strict=True):
        metadata = json.dumps(
            {"name": name, "dtype": value.dtype.name, "shape": list(value.shape)},
            sort_keys=True,
            separators=(",", ":"),
        ).encode()
        data = value.tobytes(order="C")
        for part in (metadata, data):
            digest.update(len(part).to_bytes(8, "little"))
            digest.update(part)
    return digest.hexdigest()


def worker(args: argparse.Namespace) -> int:
    # Library initialization is outside every reported boundary.
    import onnxruntime  # noqa: F401

    measurement, _, feeds = load_case(args.spec, args.inputs, args.case_id)
    model = args.model.read_bytes()
    if args.worker == "memory":
        print("READY", flush=True)
        if not sys.stdin.readline():
            raise SystemExit("memory monitor closed before start")
    started = time.perf_counter_ns()
    session = ort_session(model, measurement)
    prepared = time.perf_counter_ns() - started
    if args.worker == "prepare":
        print(json.dumps({"prepare_ns": prepared}))
        return 0

    names = [item.name for item in session.get_outputs()]
    for _ in range(args.warmups):
        session.run(names, feeds)
    if args.worker == "execute":
        latencies = []
        outputs = []
        for _ in range(args.iterations):
            started = time.perf_counter_ns()
            for _ in range(args.batch):
                outputs = session.run(names, feeds)
            elapsed = time.perf_counter_ns() - started
            latencies.append(max(1, elapsed // args.batch))
        print(json.dumps({
            "latencies_ns": latencies,
            "output_digest": output_digest(names, outputs),
        }))
        return 0

    outputs = session.run(names, feeds)
    print(json.dumps({"output_digest": output_digest(names, outputs)}), flush=True)
    return 0


def command(
    args: argparse.Namespace, kind: str, case_id: str, model: Path,
    *, iterations: int = 1, warmups: int = 0,
    batch: int = 1,
) -> list[str]:
    return [
        sys.executable, str(Path(__file__).resolve()), "--worker", kind,
        "--spec", str(args.spec), "--inputs", str(args.inputs),
        "--case-id", case_id, "--model", str(model),
        "--iterations", str(iterations), "--warmups", str(warmups),
        "--batch", str(batch),
    ]


def run_json(argv: list[str]) -> dict[str, Any]:
    environment = {**os.environ, **THREAD_ENV}
    result = subprocess.run(
        argv, check=True, capture_output=True, text=True, env=environment
    )
    return json.loads(result.stdout.strip().splitlines()[-1])


def memory_run(argv: list[str]) -> tuple[int, dict[str, Any]]:
    try:
        import psutil
    except ImportError as error:
        raise SystemExit("psutil is required for memory measurements") from error
    environment = {**os.environ, **THREAD_ENV}
    process = subprocess.Popen(
        argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, env=environment,
    )
    assert process.stdin is not None and process.stdout is not None
    if process.stdout.readline().strip() != "READY":
        stderr = process.stderr.read() if process.stderr else ""
        process.kill()
        raise SystemExit(f"memory worker did not become ready: {stderr}")
    observed = psutil.Process(process.pid)
    baseline = observed.memory_info().rss
    peak = baseline
    process.stdin.write("\n")
    process.stdin.flush()
    while process.poll() is None:
        try:
            peak = max(peak, observed.memory_info().rss)
        except psutil.NoSuchProcess:
            break
        time.sleep(0.0005)
    stdout = process.stdout.read()
    stderr = process.stderr.read() if process.stderr else ""
    if process.returncode:
        raise SystemExit(f"memory worker failed ({process.returncode}): {stderr}")
    payload = json.loads(stdout.strip().splitlines()[-1])
    delta = peak - baseline
    return max(0, delta), payload


def git_state(repo: Path) -> tuple[str, bool]:
    revision = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=repo, check=True,
        capture_output=True, text=True,
    ).stdout.strip()
    dirty = bool(subprocess.run(
        ["git", "status", "--porcelain"], cwd=repo, check=True,
        capture_output=True, text=True,
    ).stdout.strip())
    return revision, dirty


def host_controls() -> dict[str, Any]:
    affinity = (sorted(os.sched_getaffinity(0))
                if hasattr(os, "sched_getaffinity") else None)
    governors = set()
    for path in Path("/sys/devices/system/cpu").glob(
        "cpu[0-9]*/cpufreq/scaling_governor"
    ):
        try:
            governors.add(path.read_text().strip())
        except OSError:
            pass
    return {
        "affinity": affinity,
        "governors": sorted(governors) if governors else None,
        "nice": os.nice(0),
    }


def row(header: list[str], common: dict[str, Any], **values: Any) -> dict[str, Any]:
    result = {name: "" for name in header}
    result.update(common)
    result.update(values)
    return result


def main(args: argparse.Namespace) -> int:
    repo = Path(__file__).resolve().parent.parent
    revision, dirty = git_state(repo)
    if dirty and not args.allow_dirty:
        raise SystemExit("refusing to benchmark a dirty tree; commit or pass --allow-dirty")
    spec_bytes = args.spec.read_bytes()
    spec = json.loads(spec_bytes)
    spec_hash = sha256(spec_bytes)
    index = json.loads((args.inputs / "index.json").read_text())
    if index.get("spec_sha256") != spec_hash:
        raise SystemExit("input index was generated from a different benchmark specification")
    input_records = {record["id"]: record for record in index["cases"]}
    cases = spec["operator_cases"] if args.group == "operators" else spec["model_cases"]
    if args.case_id:
        requested = set(args.case_id)
        cases = [case for case in cases if case["id"] in requested]
        missing = requested - {case["id"] for case in cases}
        if missing:
            raise SystemExit(f"unknown cases: {sorted(missing)}")
    random.Random(args.seed).shuffle(cases)
    measurement = spec["measurement"]
    prepare_count = 2 if args.smoke else measurement["preparation_iterations"]
    execute_count = 3 if args.smoke else measurement["execution_iterations"]
    memory_count = 2 if args.smoke else measurement["memory_iterations"]
    warmups = 1 if args.smoke else measurement["warmups"]

    try:
        import onnxruntime as ort
        import psutil
    except ImportError as error:
        raise SystemExit("onnxruntime and psutil are required") from error
    if ort.get_available_providers().count(measurement["reference_provider"]) != 1:
        raise SystemExit(f"missing provider {measurement['reference_provider']}")
    system_revision = f"onnxruntime-{ort.__version__}"
    template_name = ("figure-08-operators.csv" if args.group == "operators"
                     else "figure-09-models.csv")
    with (repo / "artifact" / "templates" / template_name).open(newline="") as stream:
        header = next(csv.reader(stream))
    if args.output.exists():
        raise SystemExit(f"refusing to replace {args.output}")
    record_path = args.run_record or args.output.with_suffix(".run.json")
    if record_path.exists():
        raise SystemExit(f"refusing to replace {record_path}")
    args.output.parent.mkdir(parents=True, exist_ok=True)

    operator_records: dict[str, dict[str, Any]] = {}
    if args.group == "operators":
        operator_index_path = args.operator_models / "index.json"
        operator_index = json.loads(operator_index_path.read_text())
        if operator_index.get("spec_sha256") != spec_hash:
            raise SystemExit("operator models were generated from a different specification")
        if not operator_index.get("runtime_verified"):
            raise SystemExit("operator models were not runtime-verified when generated")
        operator_records = {record["id"]: record for record in operator_index["cases"]}

    rows = []
    model_files = []
    for case in cases:
        case_id = case["id"]
        model = ((args.operator_models / f"{case_id}.onnx")
                 if args.group == "operators" else (args.model_root / f"{case_id}.onnx"))
        if not model.is_file():
            raise SystemExit(f"missing model {model}")
        model_hash = sha256(model.read_bytes())
        expected_hash = (operator_records[case_id]["sha256"]
                         if args.group == "operators" else case["sha256"])
        if model_hash != expected_hash:
            raise SystemExit(f"model hash mismatch: {model}")
        model_files.append({"id": case_id, "sha256": model_hash})
        common = {
            "case_spec_sha256": spec_hash,
            "system": "ONNX Runtime CPU EP",
            "system_revision": system_revision,
            "variant": "onnxruntime",
            "supported": "true",
            "reason": "",
            "input_digest": input_records[case_id]["input_digest"],
        }
        if args.group == "operators":
            common.update(case_id=case_id, family=case["family"])
        else:
            common.update(model=case_id, model_hash=case["sha256"])
        for iteration in range(prepare_count):
            payload = run_json(command(args, "prepare", case_id, model))
            rows.append(row(
                header, common, record_kind="prepare", iteration=iteration,
                prepare_ns=payload["prepare_ns"], seed=args.seed + iteration,
            ))
        payload = run_json(command(
            args, "execute", case_id, model,
            iterations=execute_count, warmups=warmups,
            batch=measurement["execution_batches"][case_id],
        ))
        for iteration, latency in enumerate(payload["latencies_ns"]):
            rows.append(row(
                header, common, record_kind="execute", iteration=iteration,
                calls_per_sample=measurement["execution_batches"][case_id],
                latency_ns=latency, max_abs_error=0, max_rel_error=0,
                output_digest=payload["output_digest"], correct="true",
                seed=args.seed + 10_000 + iteration,
            ))
        for iteration in range(memory_count):
            peak, payload = memory_run(command(
                args, "memory", case_id, model, warmups=warmups,
            ))
            rows.append(row(
                header, common, record_kind="memory", iteration=iteration,
                peak_bytes=peak, max_abs_error=0, max_rel_error=0,
                output_digest=payload["output_digest"], correct="true",
                seed=args.seed + 20_000 + iteration,
            ))

    with args.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=header)
        writer.writeheader()
        writer.writerows(rows)
    record = {
        "schema_version": 1,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "release_eligible": not args.smoke and not dirty,
        "group": args.group,
        "cases": [case["id"] for case in cases],
        "model_files": model_files,
        "output_sha256": sha256(args.output.read_bytes()),
        "benchmark_spec_sha256": spec_hash,
        "input_index_sha256": sha256((args.inputs / "index.json").read_bytes()),
        "git_revision": revision,
        "git_dirty": dirty,
        "system_revision": system_revision,
        "provider": measurement["reference_provider"],
        "graph_optimization": measurement["reference_graph_optimization"],
        "execution_mode": measurement["reference_execution_mode"],
        "threads": measurement["threads"],
        "prepare_iterations": prepare_count,
        "execution_iterations": execute_count,
        "memory_iterations": memory_count,
        "warmups": warmups,
        "execution_batches": {
            case["id"]: measurement["execution_batches"][case["id"]]
            for case in cases
        },
        "seed": args.seed,
        "thread_environment": THREAD_ENV,
        "host_controls": host_controls(),
        "host": {
            "platform": platform.platform(),
            "machine": platform.machine(),
            "processor": platform.processor(),
            "logical_cpus": os.cpu_count(),
            "python": platform.python_version(),
            "memory_bytes": psutil.virtual_memory().total,
        },
        "command": sys.argv,
    }
    record_path.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")
    print(f"wrote {len(rows)} ONNX Runtime rows to {args.output}")
    print(f"wrote run record to {record_path}")
    return 0


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser()
    parser.add_argument("--worker", choices=("prepare", "execute", "memory"))
    parser.add_argument("--spec", type=Path,
                        default=root / "manifests" / "benchmark-cases.json")
    parser.add_argument("--inputs", type=Path, required=True)
    parser.add_argument("--case-id", action="append")
    parser.add_argument("--model", type=Path)
    parser.add_argument("--iterations", type=int, default=1)
    parser.add_argument("--warmups", type=int, default=0)
    parser.add_argument("--batch", type=int, default=1)
    parser.add_argument("--group", choices=("operators", "models"))
    parser.add_argument("--operator-models", type=Path)
    parser.add_argument("--model-root", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--run-record", type=Path)
    parser.add_argument("--seed", type=int, default=20260921)
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--allow-dirty", action="store_true")
    args = parser.parse_args()
    if args.worker:
        if not args.model or not args.case_id or len(args.case_id) != 1:
            parser.error("worker mode requires one --case-id and --model")
        if args.iterations <= 0 or args.warmups < 0 or args.batch <= 0:
            parser.error("worker counts must be positive, with non-negative warmups")
        args.case_id = args.case_id[0]
    else:
        if not args.group or not args.output:
            parser.error("collector mode requires --group and --output")
        if args.group == "operators" and not args.operator_models:
            parser.error("operator collection requires --operator-models")
        if args.group == "models" and not args.model_root:
            parser.error("model collection requires --model-root")
    return args


if __name__ == "__main__":
    parsed = parse_args()
    raise SystemExit(worker(parsed) if parsed.worker else main(parsed))
