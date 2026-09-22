#!/usr/bin/env python3
"""Collect external CPU baselines with a shared ONNX correctness oracle."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import platform
import random
import signal
import subprocess
import sys
import tempfile
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
    "TVM_NUM_THREADS": "1",
}

# Correctness follows the submitted graph, independently of the optimized
# implementation used for timing. In particular, QDQ-to-QLinearConv fusion can
# introduce int32 overflow that is absent from the floating-point Conv graph.
CORRECTNESS_ORACLE = {
    "schema": "onnx-graph-semantics/v1",
    "graph_optimization": "ORT_DISABLE_ALL",
    "provider": "CPUExecutionProvider",
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def ort_session(model: bytes, measurement: dict[str, Any], *, semantic: bool = False):
    import onnxruntime as ort

    options = ort.SessionOptions()
    options.intra_op_num_threads = measurement["threads"]
    options.inter_op_num_threads = measurement["threads"]
    options.execution_mode = getattr(ort.ExecutionMode,
                                     measurement["reference_execution_mode"])
    options.graph_optimization_level = getattr(
        ort.GraphOptimizationLevel,
        CORRECTNESS_ORACLE["graph_optimization"] if semantic
        else measurement["reference_graph_optimization"]
    )
    options.log_severity_level = 3
    provider = CORRECTNESS_ORACLE["provider"] if semantic else measurement["reference_provider"]
    return ort.InferenceSession(model, sess_options=options, providers=[provider])


def correctness_oracle_record() -> dict[str, str]:
    import onnxruntime as ort

    return {**CORRECTNESS_ORACLE, "runtime": f"onnxruntime-{ort.__version__}"}


def compare_outputs(
    actual: list[np.ndarray], expected: list[np.ndarray], rtol: float, atol: float,
) -> dict[str, Any]:
    result = {"correct": True, "reason": "", "max_abs_error": 0.0,
              "max_rel_error": 0.0}
    if len(actual) != len(expected):
        return {**result, "correct": False, "reason": "oracle:output-count"}
    for value, reference in zip(actual, expected, strict=True):
        if value.shape != reference.shape or value.dtype != reference.dtype:
            return {**result, "correct": False, "reason": "oracle:type-shape"}
        if value.dtype.kind in "iub":
            if not np.array_equal(value, reference):
                result.update(correct=False, reason="oracle:integer-mismatch")
            continue
        if not np.all(np.isfinite(value)) or not np.all(np.isfinite(reference)):
            return {**result, "correct": False, "reason": "oracle:nonfinite"}
        absolute = np.abs(value.astype(np.float64) - reference.astype(np.float64))
        relative = absolute / np.maximum(np.abs(reference.astype(np.float64)), atol or 1e-30)
        result["max_abs_error"] = max(result["max_abs_error"], float(np.max(absolute, initial=0.0)))
        result["max_rel_error"] = max(result["max_rel_error"], float(np.max(relative, initial=0.0)))
        if not np.allclose(value, reference, rtol=rtol, atol=atol, equal_nan=False):
            result.update(correct=False, reason="oracle:tolerance")
    return result


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


def isolated_reference(args: argparse.Namespace, names: list[str]) -> list[np.ndarray]:
    """Keep the semantic session out of the candidate runtime's process."""
    with tempfile.TemporaryDirectory(prefix="joggle-oracle-") as directory:
        output = Path(directory) / "outputs.npz"
        argv = [sys.executable, str(Path(__file__).resolve()), "--worker", "oracle",
                "--spec", str(args.spec), "--inputs", str(args.inputs),
                "--case-id", args.case_id, "--model", str(args.model),
                "--output", str(output)]
        # Inherit the candidate worker's process group so the outer timeout
        # also reaps this child. The oracle itself launches no descendants.
        result = subprocess.run(argv, capture_output=True, text=True, check=True,
                                timeout=args.case_timeout, env={**os.environ, **THREAD_ENV})
        record = json.loads(result.stdout.strip().splitlines()[-1])
        reference_names = record["names"]
        if len(set(reference_names)) != len(reference_names) or set(reference_names) != set(names):
            raise ValueError("oracle output names differ from candidate outputs")
        with np.load(output, allow_pickle=False) as arrays:
            return [arrays[f"arr_{reference_names.index(name)}"] for name in names]


def worker(args: argparse.Namespace) -> int:
    # Library initialization is outside every reported boundary.
    import onnxruntime  # noqa: F401

    measurement, _, feeds = load_case(args.spec, args.inputs, args.case_id)
    model = args.model.read_bytes()
    if args.worker == "oracle":
        session = ort_session(model, measurement, semantic=True)
        names = [item.name for item in session.get_outputs()]
        np.savez(args.output, *session.run(names, feeds))
        print(json.dumps({"names": names, "pid": os.getpid()}))
        return 0
    stages_ns = {}
    if args.backend in {"tvm", "onnx-mlir"}:
        from benchmark_backends import ONNXMLIRRunner, TVMRunner
        if args.backend == "tvm":
            runner = TVMRunner(model, feeds, json.loads(args.target_json), measurement["threads"])
        else:
            import atexit
            runner = ONNXMLIRRunner(model, feeds, args.onnx_mlir, measurement["threads"])
            atexit.register(runner.close)
        names = runner.names
        invoke = runner.invoke
        get_outputs = runner.outputs
        stages_ns = runner.stages_ns
    else:
        session = ort_session(model, measurement)
        names = [item.name for item in session.get_outputs()]
        outputs = []
        def invoke():
            nonlocal outputs
            outputs = session.run(names, feeds)
        def get_outputs():
            return outputs
    for _ in range(args.warmups):
        invoke()
    if args.worker == "execute":
        latencies = []
        outputs = []
        for _ in range(args.iterations):
            started = time.perf_counter_ns()
            for _ in range(args.batch):
                invoke()
            elapsed = time.perf_counter_ns() - started
            latencies.append(max(1, elapsed // args.batch))
        outputs = get_outputs()
        spec = json.loads(args.spec.read_text())
        case = next(case for case in spec["operator_cases"] + spec["model_cases"]
                    if case["id"] == args.case_id)
        comparison = compare_outputs(outputs, isolated_reference(args, names),
                                     case["rtol"], case["atol"])
        print(json.dumps({
            "latencies_ns": latencies,
            "output_digest": output_digest(names, outputs),
            "preparation_ns": stages_ns,
            **comparison,
        }))
        return 0

    raise SystemExit(f"unknown worker {args.worker}")


def command(
    args: argparse.Namespace, kind: str, case_id: str, model: Path,
    *, iterations: int = 1, warmups: int = 0,
    batch: int = 1,
) -> list[str]:
    argv = [
        sys.executable, str(Path(__file__).resolve()), "--worker", kind,
        "--spec", str(args.spec), "--inputs", str(args.inputs),
        "--case-id", case_id, "--model", str(model),
        "--iterations", str(iterations), "--warmups", str(warmups),
        "--batch", str(batch),
        "--backend", args.backend, "--target-json", args.target_json,
    ]
    if args.onnx_mlir:
        argv.extend(["--onnx-mlir", str(args.onnx_mlir)])
    return argv


def run_json(argv: list[str], timeout: float = 1200) -> dict[str, Any]:
    environment = {**os.environ, **THREAD_ENV}
    # A compiler worker may launch opt, llc, and a linker. Reap the complete
    # process group on timeout so they cannot interfere with subsequent cases.
    with subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True, env=environment, start_new_session=True) as process:
        try:
            stdout, stderr = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            if os.name == "posix":
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            else:
                process.kill()
            stdout, stderr = process.communicate()
            raise subprocess.TimeoutExpired(argv, timeout, output=stdout, stderr=stderr)
        if process.returncode:
            raise subprocess.CalledProcessError(process.returncode, argv, output=stdout, stderr=stderr)
    return json.loads(stdout.strip().splitlines()[-1])


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
    source_paths = [Path(__file__).resolve(),
                    Path(__file__).resolve().with_name("benchmark_backends.py")]
    collector_sources = {path.name: sha256(path.read_bytes()) for path in source_paths}
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
    execute_count = 3 if args.smoke else measurement["execution_iterations"]
    warmups = 1 if args.smoke else measurement["warmups"]

    try:
        import onnxruntime as ort
    except ImportError as error:
        raise SystemExit("onnxruntime is required") from error
    if ort.get_available_providers().count(measurement["reference_provider"]) != 1:
        raise SystemExit(f"missing provider {measurement['reference_provider']}")
    backend = {"system": "ONNX Runtime CPU EP", "variant": "onnxruntime",
               "system_revision": f"onnxruntime-{ort.__version__}"}
    if args.backend == "tvm":
        from benchmark_backends import tvm_identity
        backend = tvm_identity(json.loads(args.target_json))
    elif args.backend == "onnx-mlir":
        from benchmark_backends import onnx_mlir_identity
        backend = onnx_mlir_identity(args.onnx_mlir)
    if backend.get("source_dirty") and not args.allow_dirty:
        raise SystemExit("refusing a modified external compiler; use a clean baseline checkout")
    system_revision = backend["system_revision"]
    template_name = ("benchmark-operators.csv" if args.group == "operators"
                     else "benchmark-models.csv")
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
    correctness_failures = []
    execution_failures = []
    preparation = {}
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
            "system": backend["system"],
            "system_revision": system_revision,
            "variant": backend["variant"],
            "supported": "true",
            "reason": "",
            "input_digest": input_records[case_id]["input_digest"],
        }
        if args.group == "operators":
            common.update(case_id=case_id, family=case["family"])
        else:
            common.update(model=case_id, model_hash=case["sha256"])
        try:
            payload = run_json(command(
                args, "execute", case_id, model,
                iterations=execute_count, warmups=warmups,
                batch=measurement["execution_batches"][case_id],
            ), args.case_timeout)
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
            reason = "timeout" if isinstance(error, subprocess.TimeoutExpired) else "backend-error"
            detail = error.stderr or ""
            if isinstance(detail, bytes):
                detail = detail.decode(errors="replace")
            execution_failures.append({"id": case_id, "reason": reason, "stderr": detail})
            rows.append(row(header, common, supported="false",
                            reason="unsupported:" + reason, seed=args.seed))
            print(f"{case_id}: {reason}", flush=True)
            continue
        preparation[case_id] = payload.get("preparation_ns", {})
        if not payload["correct"]:
            correctness_failures.append({
                "id": case_id, "reason": payload["reason"],
                "max_abs_error": payload["max_abs_error"],
                "max_rel_error": payload["max_rel_error"],
                "output_digest": payload["output_digest"],
            })
            rows.append(row(header, common, supported="false",
                            reason="unsupported:" + payload["reason"], seed=args.seed))
            continue
        for iteration, latency in enumerate(payload["latencies_ns"]):
            rows.append(row(
                header, common, iteration=iteration,
                calls_per_sample=measurement["execution_batches"][case_id],
                latency_ns=latency, max_abs_error=payload["max_abs_error"],
                max_rel_error=payload["max_rel_error"],
                output_digest=payload["output_digest"], correct="true",
                seed=args.seed + 10_000 + iteration,
            ))

    with args.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=header)
        writer.writeheader()
        writer.writerows(rows)
    sources_unchanged = all(sha256(path.read_bytes()) == collector_sources[path.name]
                            for path in source_paths)
    record = {
        "schema_version": 1,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "release_eligible": (not args.smoke and not dirty
                             and not backend.get("source_dirty") and sources_unchanged),
        "group": args.group,
        "cases": [case["id"] for case in cases],
        "model_files": model_files,
        "output_sha256": sha256(args.output.read_bytes()),
        "benchmark_spec_sha256": spec_hash,
        "input_index_sha256": sha256((args.inputs / "index.json").read_bytes()),
        "git_revision": revision,
        "git_dirty": dirty,
        "collector_sources": collector_sources,
        "collector_sources_unchanged": sources_unchanged,
        "system_revision": system_revision,
        "backend": backend,
        "provider": measurement["reference_provider"] if args.backend == "onnxruntime" else "llvm-cpu",
        "graph_optimization": (measurement["reference_graph_optimization"] if args.backend == "onnxruntime"
                               else "O3" if args.backend == "onnx-mlir" else "default"),
        "correctness_oracle": correctness_oracle_record(),
        "correctness_execution": "isolated-process",
        "correctness_failures": correctness_failures,
        "execution_failures": execution_failures,
        "preparation_ns": preparation,
        "execution_mode": (measurement["reference_execution_mode"]
                           if args.backend == "onnxruntime" else "native-c-abi"
                           if args.backend == "onnx-mlir" else "stateful-vm"),
        "threads": measurement["threads"],
        "execution_iterations": execute_count,
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
        },
        "command": sys.argv,
    }
    record_path.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")
    print(f"wrote {len(rows)} {backend['system']} rows to {args.output}")
    print(f"wrote run record to {record_path}")
    return 0


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser()
    parser.add_argument("--worker", choices=("execute", "oracle"))
    parser.add_argument("--backend", choices=("onnxruntime", "tvm", "onnx-mlir"), default="onnxruntime")
    parser.add_argument("--onnx-mlir", type=Path, help="path to the ONNX-MLIR compiler executable")
    parser.add_argument("--target-json", default='{"kind":"llvm","num-cores":1}')
    parser.add_argument("--case-timeout", type=float, default=1200)
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
    if args.backend == "onnx-mlir" and (not args.onnx_mlir or not args.onnx_mlir.is_file()):
        parser.error("--backend onnx-mlir requires an existing --onnx-mlir executable")
    try:
        target = json.loads(args.target_json)
    except json.JSONDecodeError:
        parser.error("--target-json must be a JSON object")
    if not isinstance(target, dict) or target.get("kind") != "llvm":
        parser.error("--target-json must describe an LLVM CPU target")
    if args.case_timeout <= 0:
        parser.error("--case-timeout must be positive")
    if args.worker:
        if not args.model or not args.case_id or len(args.case_id) != 1:
            parser.error("worker mode requires one --case-id and --model")
        if args.iterations <= 0 or args.warmups < 0 or args.batch <= 0:
            parser.error("worker counts must be positive, with non-negative warmups")
        if args.worker == "oracle" and not args.output:
            parser.error("oracle worker requires --output")
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
