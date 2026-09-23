#!/usr/bin/env python3
"""Collect external CPU baselines with a shared ONNX correctness oracle."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import platform
import random
import signal
import shutil
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


def apply_model_edit(model: bytes, edit: dict[str, Any]) -> bytes:
    """Apply one hash-bound operator replacement without backend-specific edits."""
    import onnx

    fields = {"schema", "model_sha256", "node_index", "domain", "before", "after"}
    if set(edit) != fields or edit["schema"] != "onnx-node-edit/v1":
        raise ValueError("invalid ONNX node edit specification")
    if sha256(model) != edit["model_sha256"]:
        raise ValueError("edit source hash mismatch")
    proto = onnx.load_model_from_string(model)
    index = edit["node_index"]
    if type(index) is not int or not 0 <= index < len(proto.graph.node):
        raise ValueError("edit node index is outside the graph")
    node = proto.graph.node[index]
    if node.domain != edit["domain"] or node.op_type != edit["before"]:
        raise ValueError("edit precondition does not match the original operator")
    if not isinstance(edit["after"], str) or not edit["after"] or edit["before"] == edit["after"]:
        raise ValueError("edit must replace the operator")
    node.op_type = edit["after"]
    onnx.checker.check_model(proto, full_check=True)
    return proto.SerializeToString()


def checked_native_build(factory, model: bytes, feeds: dict, names: list[str],
                         expected: list[np.ndarray], rtol: float, atol: float,
                         edit: dict[str, Any] | None = None):
    """Time construction through validation, excluding reference generation."""
    started = time.perf_counter_ns()
    if edit is not None:
        model = apply_model_edit(model, edit)
    edited = time.perf_counter_ns()
    runner = factory(model, feeds)
    try:
        ready = time.perf_counter_ns()
        if runner.names != names:
            raise ValueError("replacement output names or order changed")
        runner.invoke()
        outputs = runner.outputs()
        comparison = compare_outputs(outputs, expected, rtol, atol)
        if not comparison["correct"]:
            raise ValueError(f"replacement executable failed numerical validation: {comparison}")
        elapsed = time.perf_counter_ns() - started
        return runner, {
            "wall_ns": elapsed, "ready_ns": ready - started,
            "edit_ns": edited - started if edit is not None else 0,
            "validation_ns": elapsed - (ready - started),
            "stages_ns": runner.stages_ns,
            "model_sha256": sha256(model),
            "output_digest": output_digest(names, outputs), **comparison,
        }
    except BaseException:
        if hasattr(runner, "close"):
            runner.close()
        raise


def production_identity(args: argparse.Namespace) -> dict[str, Any]:
    """Fingerprint the compiler actually used by a production update worker."""
    from benchmark_backends import tvm_identity, onnx_mlir_identity
    if args.backend == "joggle":
        from run_joggle_benchmarks import compiler_identity
        backend = compiler_identity(args)
        backend["server_sha256"] = sha256(args.joggle_server.read_bytes())
        compiler = Path(shutil.which(args.cc) or args.cc).resolve(strict=True)
        backend["host_compiler"] = {
            "path": str(compiler), "sha256": sha256(compiler.read_bytes()),
            "version": subprocess.run([str(compiler), "--version"], check=True,
                                      capture_output=True, text=True).stdout.strip(),
        }
    elif args.backend == "tvm":
        backend = tvm_identity(json.loads(args.target_json))
    elif args.backend == "onnx-mlir":
        backend = onnx_mlir_identity(args.onnx_mlir)
    else:
        raise ValueError("unsupported production compiler")
    collector = Path(__file__).resolve().parent
    revision, dirty = git_state(collector.parent)
    return {"backend": backend, "collector_revision": revision, "collector_dirty": dirty,
            "collector_sources": {
        name: sha256((collector / name).read_bytes()) for name in
        ("run_baseline_benchmarks.py", "benchmark_backends.py", "joggle_entry.py")}}


def production_update(args: argparse.Namespace) -> dict[str, Any]:
    """One full or resident-runtime rebuild; no synthetic incremental cache."""
    import onnx
    from benchmark_backends import ONNXMLIRRunner, TVMRunner, JoggleCompiler, JoggleRunner

    measurement, _, feeds = load_case(args.spec, args.inputs, args.case_id)
    spec = json.loads(args.spec.read_text())
    case = next(c for c in spec["operator_cases"] + spec["model_cases"]
                if c["id"] == args.case_id)
    resident = None
    if args.backend == "joggle":
        factory = lambda model, inputs: JoggleRunner(model, inputs, resident,
            args.joggle, args.builtin_mods, args.cc, args.case_timeout)
        retained = "resident-env-and-evaluator-plans; fresh-source-graph"
    elif args.backend == "tvm":
        import tvm  # Initialize libraries before either timing boundary.
        from tvm.relax.frontend.onnx import from_onnx  # noqa: F401
        factory = lambda model, inputs: TVMRunner(
            model, inputs, json.loads(args.target_json), measurement["threads"])
        retained = "resident-tvm-runtime; native-process-caches-unmodified"
    elif args.backend == "onnx-mlir":
        factory = lambda model, inputs: ONNXMLIRRunner(
            model, inputs, args.onnx_mlir, measurement["threads"])
        retained = "resident-host-runtime; fresh-native-compiler-subprocess"
    else:
        raise ValueError("production update requires Joggle, TVM, or ONNX-MLIR")
    original = args.model.read_bytes()
    edit = json.loads(args.edit_json.read_text())
    identity = production_identity(args)
    spec_hash = sha256(args.spec.read_bytes())
    input_index_hash = sha256((args.inputs / "index.json").read_bytes())
    # Generate the edited reference outside candidate timing. The identical
    # hash-bound edit is applied again inside every measured build.
    replacement = apply_model_edit(original, edit)
    runners = []

    def build(model, path, applied_edit=None):
        names = [out.name for out in onnx.load_model_from_string(model).graph.output]
        oracle_args = argparse.Namespace(**vars(args))
        oracle_args.model = path
        expected = isolated_reference(oracle_args, names)
        runner, record = checked_native_build(factory,
            original if applied_edit is not None else model, feeds, names, expected,
            case["rtol"], case["atol"], applied_edit)
        runners.append(runner)
        if record["model_sha256"] != sha256(model):
            raise ValueError("measured edit differs from reference model")
        return record

    try:
        setup_started = time.perf_counter_ns()
        if args.backend == "joggle":
            resident = JoggleCompiler(args.joggle_server, args.builtin_mods, args.case_timeout)
        setup_ns = time.perf_counter_ns() - setup_started
        initial = build(original, args.model) if args.worker == "update" else None
        with tempfile.TemporaryDirectory(prefix="joggle-edited-reference-") as directory:
            path = Path(directory) / "edited.onnx"
            path.write_bytes(replacement)
            result = build(replacement, path, edit)
        final_identity = production_identity(args)
        if final_identity != identity:
            raise ValueError("production compiler changed during measurement")
        if (sha256(args.spec.read_bytes()) != spec_hash or
                sha256((args.inputs / "index.json").read_bytes()) != input_index_hash):
            raise ValueError("production measurement protocol changed during measurement")
        return {"schema": "production-update-sample/v1", "backend": args.backend,
                "policy": args.worker, "case_id": args.case_id,
                "edit_delivery": "hash-bound-node-replacement",
                "edit_sha256": sha256(json.dumps(edit, sort_keys=True).encode()),
                "resident_setup_ns": setup_ns if resident is not None else None,
                "retained_state": retained if initial else "fresh-worker",
                "compiler_identity": identity, "final_compiler_identity": final_identity,
                "identity_stable": True, "benchmark_spec_sha256": spec_hash,
                "input_index_sha256": input_index_hash,
                "initial": initial, "replacement": result,
                "oracle": correctness_oracle_record(),
                "input_digest": output_digest(list(feeds), list(feeds.values()))}
    finally:
        for runner in reversed(runners):
            if hasattr(runner, "close"):
                runner.close()
        if resident is not None:
            resident.close()


def worker(args: argparse.Namespace) -> int:
    # Library initialization is outside every reported boundary.
    import onnxruntime  # noqa: F401

    if args.worker in {"update", "rebuild"}:
        print(json.dumps(production_update(args)))
        return 0
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
    if kind in {"update", "rebuild"}:
        for key in ("edit_json", "joggle", "joggle_server", "builtin_mods", "cc", "case_timeout"):
            argv.extend(["--" + key.replace("_", "-"), str(getattr(args, key))])
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


def production_population(args: argparse.Namespace) -> list[dict]:
    """Resolve a frozen edit population before any compiler worker starts."""
    manifest = json.loads(args.edit_manifest.read_text())
    if set(manifest) != {"schema", "cases"} or manifest["schema"] != "production-update-population/v1":
        raise ValueError("invalid production population manifest")
    spec = json.loads(args.spec.read_text())
    models = {case["id"]: case for case in spec["model_cases"]}
    if not manifest["cases"]:
        raise ValueError("empty production population")
    seen = set()
    for case in manifest["cases"]:
        if set(case) != {"case_id", "edit_id", "edit"}:
            raise ValueError("population case requires case_id, edit_id, edit")
        key = (case["case_id"], case["edit_id"])
        if key in seen or case["case_id"] not in models or not case["edit_id"]:
            raise ValueError("duplicate or unknown population case")
        seen.add(key)
        data = (args.model_root / (case["case_id"] + ".onnx")).read_bytes()
        if sha256(data) != models[case["case_id"]]["sha256"]:
            raise ValueError("population model differs from benchmark specification")
        case["replacement_sha256"] = sha256(apply_model_edit(data, case["edit"]))
        load_case(args.spec, args.inputs, case["case_id"])
    selected = manifest["cases"]
    if args.case_id:
        requested = set(args.case_id)
        if requested - {case["case_id"] for case in selected}:
            raise ValueError("selected model is outside the edit population")
        selected = [case for case in selected if case["case_id"] in requested]
    return selected


def production_sample_row(sample: dict, backend: str, policy: str, case: dict,
                          spec_hash: str, index_hash: str) -> dict:
    edit_hash = sha256(json.dumps(case["edit"], sort_keys=True).encode())
    if (not isinstance(sample, dict) or sample.get("schema") != "production-update-sample/v1" or
            sample.get("backend") != backend or sample.get("policy") != policy or
            sample.get("case_id") != case["case_id"] or sample.get("edit_sha256") != edit_hash or
            sample.get("benchmark_spec_sha256") != spec_hash or sample.get("input_index_sha256") != index_hash or
            sample.get("identity_stable") is not True or not sample.get("compiler_identity") or
            sample.get("compiler_identity") != sample.get("final_compiler_identity") or
            sample.get("oracle") != correctness_oracle_record()):
        raise ValueError("invalid production worker identity or protocol")
    initial = sample.get("initial")
    if policy == "update":
        if (not isinstance(initial, dict) or initial.get("correct") is not True or
                initial.get("model_sha256") != case["edit"]["model_sha256"] or
                sample.get("retained_state") in (None, "fresh-worker")):
            raise ValueError("update sample lacks a validated original build")
    elif initial is not None or sample.get("retained_state") != "fresh-worker":
        raise ValueError("rebuild sample is not a fresh worker")
    result = sample.get("replacement")
    if (not isinstance(result, dict) or result.get("correct") is not True or
            result.get("model_sha256") != case["replacement_sha256"]):
        raise ValueError("replacement model or correctness mismatch")
    for key in ("wall_ns", "ready_ns", "edit_ns", "validation_ns"):
        if type(result.get(key)) is not int or result[key] < 0:
            raise ValueError("invalid production timing")
    if (result["wall_ns"] <= 0 or result["ready_ns"] <= 0 or result["edit_ns"] > result["ready_ns"] or
            result["wall_ns"] != result["ready_ns"] + result["validation_ns"]):
        raise ValueError("inconsistent production timing boundary")
    for digest in (result.get("output_digest"), sample.get("input_digest")):
        if not isinstance(digest, str) or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
            raise ValueError("missing production input or output digest")
    row = {key: result[key] for key in
           ("model_sha256", "wall_ns", "ready_ns", "edit_ns", "validation_ns", "output_digest")}
    return row | {"input_digest": sample["input_digest"], "correct": "true",
                  "compiler_identity_sha256": sha256(json.dumps(sample["compiler_identity"], sort_keys=True).encode())}


def production_worker_timeout(policy: str, case_timeout: float,
                              worker_timeout: float | None = None) -> float:
    """Budget both builds in update workers; timing still covers only replacement."""
    if policy not in {"update", "rebuild"}:
        raise ValueError("invalid production policy")
    for value in (case_timeout, worker_timeout):
        if value is not None and (not math.isfinite(value) or value <= 0):
            raise ValueError("timeouts must be finite and positive")
    return worker_timeout if worker_timeout is not None else case_timeout * (2 if policy == "update" else 1)


def collect_updates(args: argparse.Namespace) -> int:
    """Collect repeated, fresh-worker update/rebuild pairs without synthetic timings."""
    revision, dirty = git_state(Path(__file__).resolve().parent.parent)
    if dirty and not args.allow_dirty:
        raise ValueError("commit before production collection or use --allow-dirty for integration")
    cases = production_population(args)
    timeouts = {policy: production_worker_timeout(policy, args.case_timeout, args.worker_timeout)
                for policy in ("update", "rebuild")}
    all_models = {case["id"] for case in json.loads(args.spec.read_text())["model_cases"]}
    missing_models = sorted(all_models - {case["case_id"] for case in cases})
    record_path, raw_path = args.output.with_suffix(".json"), args.output.with_suffix(".samples.jsonl")
    if any(path.exists() for path in (args.output, record_path, raw_path)):
        raise ValueError("refusing to replace production collection outputs")
    tracked = [args.spec, args.edit_manifest, args.inputs / "index.json", Path(__file__).resolve()]
    fingerprints = {str(path.resolve()): sha256(path.read_bytes()) for path in tracked}
    if json.loads((args.inputs / "index.json").read_text()).get("spec_sha256") != sha256(args.spec.read_bytes()):
        raise ValueError("inputs use a different benchmark specification")
    columns = ["backend", "case_id", "edit_id", "iteration", "policy", "order", "seed",
               "edit_sha256", "model_sha256", "input_digest", "compiler_identity_sha256",
               "wall_ns", "ready_ns", "edit_ns", "validation_ns", "output_digest", "correct", "error"]
    jobs = [(case, iteration) for case in cases for iteration in range(args.iterations)]
    rng = random.Random(args.seed)
    rng.shuffle(jobs)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    count = failures = 0
    compiler_identities = set()
    with args.output.open("x", newline="") as stream, raw_path.open("x") as raw:
        writer = csv.DictWriter(stream, fieldnames=columns, lineterminator="\n")
        writer.writeheader()
        with tempfile.TemporaryDirectory(prefix="production-edits-") as directory:
            for case, iteration in jobs:
                edit_path = Path(directory) / "edit.json"
                edit_path.write_text(json.dumps(case["edit"], sort_keys=True) + "\n")
                child = argparse.Namespace(**vars(args))
                child.edit_json = edit_path
                policies = ["update", "rebuild"]
                rng.shuffle(policies)
                for order, policy in enumerate(policies):
                    row = {"backend": args.backend, "case_id": case["case_id"], "edit_id": case["edit_id"],
                           "iteration": iteration, "policy": policy, "order": order, "seed": args.seed,
                           "edit_sha256": sha256(json.dumps(case["edit"], sort_keys=True).encode()),
                           "correct": "false", "error": ""}
                    argv = command(child, policy, case["case_id"], args.model_root / (case["case_id"] + ".onnx"))
                    sample = None
                    try:
                        sample = run_json(argv, timeouts[policy])
                        measured = production_sample_row(sample, args.backend, policy, case,
                            fingerprints[str(args.spec.resolve())], fingerprints[str((args.inputs / "index.json").resolve())])
                        row.update(measured)
                        compiler_identities.add(measured["compiler_identity_sha256"])
                    except (subprocess.SubprocessError, ValueError, KeyError, OSError) as error:
                        failures += 1
                        row["error"] = f"{type(error).__name__}: {error}"
                        sample = {"rejected_sample": sample, "failure": row["error"], "stdout": getattr(error, "stdout", ""),
                                  "stderr": getattr(error, "stderr", "")}
                    raw.write(json.dumps({"key": {key: row[key] for key in columns[:7]},
                                          "sample": sample}, sort_keys=True) + "\n")
                    raw.flush()
                    writer.writerow(row)
                    stream.flush()
                    count += 1
                    print(f"{args.backend}/{case['case_id']}/{case['edit_id']}/{iteration}/{policy}: {row['correct']}", flush=True)
    stable = len(compiler_identities) <= 1 and all(sha256(Path(path).read_bytes()) == value for path, value in fingerprints.items())
    record = {"schema": "production-update-collection/v1", "revision": revision, "dirty": dirty,
              "created_utc": datetime.now(timezone.utc).isoformat(), "host": platform.platform(),
              "population_sha256": sha256(args.edit_manifest.read_bytes()), "sources": fingerprints,
              "rows": count, "failures": failures, "stable": stable, "iterations": args.iterations,
              "selected_models": args.case_id, "seed": args.seed,
              "timeouts_seconds": {"case": args.case_timeout, "worker": timeouts},
              "missing_models": missing_models,
              "output_sha256": sha256(args.output.read_bytes()), "raw_sha256": sha256(raw_path.read_bytes()),
              "partial": bool(args.case_id) or bool(missing_models) or args.smoke or args.iterations < 10,
              "release_eligible": False}
    record_path.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")
    return 0 if stable and not failures else 1


def main(args: argparse.Namespace) -> int:
    if args.group == "updates":
        return collect_updates(args)
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
    parser.add_argument("--worker", choices=("execute", "oracle", "update", "rebuild"))
    parser.add_argument("--edit-json", type=Path)
    parser.add_argument("--edit-manifest", type=Path)
    parser.add_argument("--backend", choices=("onnxruntime", "tvm", "onnx-mlir", "joggle"), default="onnxruntime")
    parser.add_argument("--joggle", type=Path, default=root.parent / "build/joggle")
    parser.add_argument("--joggle-server", type=Path,
                        default=root.parent / "build/artifact/joggle-artifact-reactive")
    parser.add_argument("--builtin-mods", type=Path, default=root.parent / "build/modules")
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--onnx-mlir", type=Path, help="path to the ONNX-MLIR compiler executable")
    parser.add_argument("--target-json", default='{"kind":"llvm","num-cores":1}')
    parser.add_argument("--case-timeout", type=float, default=1200)
    parser.add_argument("--worker-timeout", type=float,
                        help="total production worker limit; default is case-timeout per build (two for update)")
    parser.add_argument("--spec", type=Path,
                        default=root / "manifests" / "benchmark-cases.json")
    parser.add_argument("--inputs", type=Path, required=True)
    parser.add_argument("--case-id", action="append")
    parser.add_argument("--model", type=Path)
    parser.add_argument("--iterations", type=int, default=1)
    parser.add_argument("--warmups", type=int, default=0)
    parser.add_argument("--batch", type=int, default=1)
    parser.add_argument("--group", choices=("operators", "models", "updates"))
    parser.add_argument("--operator-models", type=Path)
    parser.add_argument("--model-root", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--run-record", type=Path)
    parser.add_argument("--seed", type=int, default=20260921)
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--allow-dirty", action="store_true")
    args = parser.parse_args()
    if args.backend == "joggle":
        if args.worker not in {"update", "rebuild"} and args.group != "updates":
            parser.error("resident Joggle backend requires an update or rebuild worker")
        if not args.joggle.is_file() or not args.joggle_server.is_file() or not args.builtin_mods.is_dir():
            parser.error("resident Joggle backend requires built tools and mods")
    if args.backend == "onnx-mlir" and (not args.onnx_mlir or not args.onnx_mlir.is_file()):
        parser.error("--backend onnx-mlir requires an existing --onnx-mlir executable")
    try:
        target = json.loads(args.target_json)
    except json.JSONDecodeError:
        parser.error("--target-json must be a JSON object")
    if not isinstance(target, dict) or target.get("kind") != "llvm":
        parser.error("--target-json must describe an LLVM CPU target")
    if not math.isfinite(args.case_timeout) or args.case_timeout <= 0:
        parser.error("--case-timeout must be finite and positive")
    if args.worker_timeout is not None:
        if not math.isfinite(args.worker_timeout) or args.worker_timeout <= 0:
            parser.error("--worker-timeout must be finite and positive")
        if args.group != "updates" or args.worker:
            parser.error("--worker-timeout is only valid for production update collection")
    if args.worker:
        if not args.model or not args.case_id or len(args.case_id) != 1:
            parser.error("worker mode requires one --case-id and --model")
        if args.iterations <= 0 or args.warmups < 0 or args.batch <= 0:
            parser.error("worker counts must be positive, with non-negative warmups")
        if args.worker == "oracle" and not args.output:
            parser.error("oracle worker requires --output")
        if args.worker in {"update", "rebuild"} and args.backend == "onnxruntime":
            parser.error("production update workers require joggle, tvm, or onnx-mlir")
        if args.worker in {"update", "rebuild"} and (not args.edit_json or not args.edit_json.is_file()):
            parser.error("production workers require an existing --edit-json")
        if args.edit_json and args.worker not in {"update", "rebuild"}:
            parser.error("--edit-json is only valid for production workers")
        args.case_id = args.case_id[0]
    else:
        if not args.group or not args.output:
            parser.error("collector mode requires --group and --output")
        if args.group == "operators" and not args.operator_models:
            parser.error("operator collection requires --operator-models")
        if args.group in {"models", "updates"} and not args.model_root:
            parser.error("model collection requires --model-root")
        if args.group == "updates":
            if not args.edit_manifest or not args.edit_manifest.is_file() or args.backend == "onnxruntime":
                parser.error("update collection requires --edit-manifest and a production compiler backend")
            if args.iterations <= 0 or (args.iterations < 10 and not args.smoke):
                parser.error("update collection requires at least 10 repetitions, or --smoke")
    return args


if __name__ == "__main__":
    parsed = parse_args()
    raise SystemExit(worker(parsed) if parsed.worker else main(parsed))
