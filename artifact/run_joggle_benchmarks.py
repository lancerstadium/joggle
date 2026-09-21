#!/usr/bin/env python3
"""Collect Joggle steady-state rows for Figure 7."""

from __future__ import annotations

import argparse
import csv
import json
import os
import random
import shlex
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import numpy as np

from run_onnxruntime_benchmarks import (
    NUMPY_DTYPES,
    THREAD_ENV,
    git_state,
    host_controls,
    load_case,
    output_digest,
    sha256,
)


C_TYPES = {
    "float32": "float",
    "uint8": "uint8_t",
    "int8": "int8_t",
    "int32": "int32_t",
    "int64": "int64_t",
}


class Unsupported(Exception):
    def __init__(self, stage: str, stderr: str = "") -> None:
        super().__init__(stage)
        self.stage = stage
        self.stderr = stderr


def run_to_file(
    argv: list[str | Path], output: Path, stage: str, timeout: float
) -> None:
    try:
        with output.open("w", encoding="utf-8") as stream:
            result = subprocess.run(
                [str(value) for value in argv], stdout=stream,
                stderr=subprocess.PIPE, text=True,
                env={**os.environ, **THREAD_ENV}, timeout=timeout,
            )
    except subprocess.TimeoutExpired as error:
        raise Unsupported(f"{stage}:timeout", str(error)) from error
    if result.returncode:
        raise Unsupported(stage, result.stderr)


def mod_flags(
    names: list[str], builtin: Path, extensions: Path
) -> list[str]:
    roots = {"builtin": builtin, "extensions": extensions}
    return [part for name in names for part in ("-M", str(roots[name]))]


def prepare(
    args: argparse.Namespace, model: Path, variant: dict[str, Any], work: Path,
    flags: list[str],
) -> tuple[int, int, Path, Path, Path]:
    work.mkdir(parents=True, exist_ok=True)
    started = time.perf_counter_ns()
    current = work / "00-read.jog"
    run_to_file(
        [args.joggle, "read", "onnx.read", model,
         "-M", args.builtin_mods],
        current, "onnx.read", args.stage_timeout,
    )
    for index, stage in enumerate(variant["pipeline"], start=1):
        following = work / f"{index:02d}-stage.jog"
        command = [args.joggle, "run", *stage["functions"], current]
        for value in stage["args"]:
            command.extend(("--arg", value))
        command.extend(mod_flags(
            stage["mod_roots"], args.builtin_mods, args.extension_mods
        ))
        run_to_file(command, following, stage["functions"][-1], args.stage_timeout)
        current = following
    try:
        frontier = subprocess.run(
            [args.joggle, "query", "c.frontier", current,
             "-M", args.builtin_mods],
            capture_output=True, text=True, env={**os.environ, **THREAD_ENV},
            timeout=args.stage_timeout,
        )
    except subprocess.TimeoutExpired as error:
        raise Unsupported("c.frontier:timeout", str(error)) from error
    if frontier.returncode or frontier.stdout != "[]\n":
        raise Unsupported("c.frontier", frontier.stderr + frontier.stdout)
    header = work / "model.h"
    source = work / "model.c"
    run_to_file(
        [args.joggle, "emit", "c.header", current, "-M", args.builtin_mods],
        header, "c.header", args.stage_timeout,
    )
    run_to_file(
        [args.joggle, "emit", "c.source", current, "-M", args.builtin_mods],
        source, "c.source", args.stage_timeout,
    )
    artifact = work / "model.o"
    try:
        compiled = subprocess.run(
            [args.cc, *flags, "-c", source, "-o", artifact],
            capture_output=True, text=True, env={**os.environ, **THREAD_ENV},
            timeout=args.stage_timeout,
        )
    except subprocess.TimeoutExpired as error:
        raise Unsupported("host-compile:timeout", str(error)) from error
    if compiled.returncode:
        raise Unsupported("host-compile", compiled.stderr)
    elapsed = time.perf_counter_ns() - started
    return elapsed, artifact.stat().st_size, artifact, header, source


def c_string(value: Path) -> str:
    return json.dumps(str(value))


def make_harness(
    path: Path, inputs: list[dict[str, Any]], input_records: dict[str, dict[str, Any]],
    outputs: list[tuple[str, np.ndarray]], input_root: Path, batch: int,
) -> None:
    declarations = []
    loads = []
    frees = []
    arguments = []
    for index, tensor in enumerate(inputs):
        item = input_records[tensor["name"]]
        ctype = C_TYPES[tensor["dtype"]]
        name = f"input_{index}"
        declarations.append(f"  {ctype} *{name} = NULL;")
        loads.append(
            f"  if (!read_exact({c_string(input_root / item['path'])}, "
            f"(void **)&{name}, {item['bytes']})) return 3;"
        )
        frees.append(f"  free({name});")
        arguments.append(name)
    for index, (_, value) in enumerate(outputs):
        dtype = value.dtype.name
        if dtype not in C_TYPES:
            raise SystemExit(f"unsupported generated output dtype {dtype}")
        ctype = C_TYPES[dtype]
        name = f"output_{index}"
        declarations.append(f"  {ctype} *{name} = calloc(1, {value.nbytes});")
        loads.append(f"  if (!{name}) return 4;")
        frees.append(f"  free({name});")
        arguments.append(name)
    writes = [
        f"  if (!write_exact(output_dir, {index}, output_{index}, {value.nbytes})) return 5;"
        for index, (_, value) in enumerate(outputs)
    ]
    call = f"model_main({', '.join(arguments)});"
    code = f'''#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "model.h"

static uint64_t now_ns(void) {{
  struct timespec value;
  if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) exit(10);
  return (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
}}

static int read_exact(const char *path, void **out, size_t size) {{
  FILE *stream = fopen(path, "rb");
  if (!stream) return 0;
  void *data = malloc(size == 0 ? 1 : size);
  int ok = data && fread(data, 1, size, stream) == size && fgetc(stream) == EOF;
  fclose(stream);
  if (!ok) {{ free(data); return 0; }}
  *out = data;
  return 1;
}}

static int write_exact(const char *directory, int index, const void *data, size_t size) {{
  char path[4096];
  int length = snprintf(path, sizeof path, "%s/output-%d.bin", directory, index);
  if (length < 0 || (size_t)length >= sizeof path) return 0;
  FILE *stream = fopen(path, "wb");
  if (!stream) return 0;
  int ok = fwrite(data, 1, size, stream) == size && fclose(stream) == 0;
  return ok;
}}

int main(int argc, char **argv) {{
  if (argc != 5) return 2;
  const char *mode = argv[1];
  long warmups = strtol(argv[2], NULL, 10);
  long iterations = strtol(argv[3], NULL, 10);
  const long batch = {batch};
  const char *output_dir = argv[4];
{chr(10).join(declarations)}
{chr(10).join(loads)}
  if (strcmp(mode, "execute") == 0) {{
    for (long i = 0; i < warmups; ++i) {{ {call} }}
    uint64_t *elapsed = calloc((size_t)iterations, sizeof *elapsed);
    if (!elapsed) return 7;
    for (long i = 0; i < iterations; ++i) {{
      uint64_t begin = now_ns();
      for (long j = 0; j < batch; ++j) {{ {call} }}
      elapsed[i] = (now_ns() - begin) / (uint64_t)batch;
      if (elapsed[i] == 0) elapsed[i] = 1;
    }}
    for (long i = 0; i < iterations; ++i)
      printf("NS %llu\\n", (unsigned long long)elapsed[i]);
    free(elapsed);
{chr(10).join(writes)}
  }} else return 8;
{chr(10).join(frees)}
  return 0;
}}
'''
    path.write_text(code)


def oracle(
    model: Path, measurement: dict[str, Any], feeds: dict[str, np.ndarray]
) -> list[tuple[str, np.ndarray]]:
    from run_onnxruntime_benchmarks import ort_session

    session = ort_session(model.read_bytes(), measurement)
    names = [item.name for item in session.get_outputs()]
    return list(zip(names, session.run(names, feeds), strict=True))


def read_outputs(directory: Path, expected: list[tuple[str, np.ndarray]]) -> list[np.ndarray]:
    values = []
    for index, (_, reference) in enumerate(expected):
        data = (directory / f"output-{index}.bin").read_bytes()
        if len(data) != reference.nbytes:
            raise Unsupported("oracle:output-size")
        values.append(np.frombuffer(data, dtype=reference.dtype).reshape(reference.shape).copy())
    return values


def compare(
    actual: list[np.ndarray], expected: list[tuple[str, np.ndarray]],
    rtol: float, atol: float,
) -> tuple[float, float]:
    max_abs = 0.0
    max_rel = 0.0
    for value, (_, reference) in zip(actual, expected, strict=True):
        if value.dtype.kind in "iu":
            if not np.array_equal(value, reference):
                raise Unsupported("oracle:integer-mismatch")
            continue
        absolute = np.abs(value.astype(np.float64) - reference.astype(np.float64))
        relative = absolute / np.maximum(np.abs(reference.astype(np.float64)), atol or 1e-30)
        max_abs = max(max_abs, float(np.max(absolute, initial=0.0)))
        max_rel = max(max_rel, float(np.max(relative, initial=0.0)))
        if not np.allclose(value, reference, rtol=rtol, atol=atol, equal_nan=False):
            raise Unsupported(
                "oracle:tolerance",
                f"max_abs={max_abs:.9g}; max_rel={max_rel:.9g}; "
                f"atol={atol:.9g}; rtol={rtol:.9g}",
            )
    return max_abs, max_rel


def csv_row(header: list[str], common: dict[str, Any], **values: Any) -> dict[str, Any]:
    result = {name: "" for name in header}; result.update(common); result.update(values)
    return result


def write_rows(path: Path, header: list[str], rows: list[dict[str, Any]]) -> None:
    """Atomically publish complete per-case checkpoints."""
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp")
    with temporary.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=header)
        writer.writeheader()
        writer.writerows(rows)
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def append_jsonl(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as stream:
        stream.write(json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n")
        stream.flush()
        os.fsync(stream.fileno())


def load_checkpoint(
    path: Path, header: list[str], *, group: str, variant: str, revision: str,
    execute_count: int,
) -> tuple[list[dict[str, str]], set[str]]:
    """Load only checkpoints containing whole, internally consistent cases."""
    subject = "case_id" if group == "operators" else "model"
    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != header:
            raise SystemExit("checkpoint columns differ from the current template")
        rows = list(reader)
    grouped: dict[str, list[dict[str, str]]] = {}
    for line, row in enumerate(rows, start=2):
        case_id = row[subject]
        if not case_id:
            raise SystemExit(f"checkpoint line {line}: empty {subject}")
        if row["system"] != "Joggle" or row["variant"] != variant:
            raise SystemExit(f"checkpoint line {line}: backend or variant differs")
        if row["system_revision"] != revision:
            raise SystemExit(f"checkpoint line {line}: Git revision differs")
        grouped.setdefault(case_id, []).append(row)
    complete: set[str] = set()
    for case_id, case_rows in grouped.items():
        unsupported = [row for row in case_rows if row["supported"] == "false"]
        if len(unsupported) == 1 and len(case_rows) == 1:
            complete.add(case_id)
        elif not unsupported and len(case_rows) == execute_count:
            complete.add(case_id)
        else:
            raise SystemExit(
                f"checkpoint {case_id}: incomplete case rows; "
                "remove it or restore a complete per-case snapshot"
            )
    return rows, complete


def main(args: argparse.Namespace) -> int:
    repo = Path(__file__).resolve().parent.parent
    revision, dirty = git_state(repo)
    if dirty and not args.allow_dirty:
        raise SystemExit("refusing to benchmark a dirty tree; commit or pass --allow-dirty")
    record_path = args.run_record or args.output.with_suffix(".run.json")
    if record_path.exists():
        raise SystemExit(f"refusing to replace {record_path}")
    failure_path = args.output.with_suffix(".failures.jsonl")
    if failure_path.exists() and not args.resume:
        raise SystemExit(f"refusing to replace {failure_path}; pass --resume")
    spec_bytes = args.spec.read_bytes(); spec = json.loads(spec_bytes); spec_hash = sha256(spec_bytes)
    index = json.loads((args.inputs / "index.json").read_text())
    if index.get("spec_sha256") != spec_hash:
        raise SystemExit("input index was generated from a different benchmark specification")
    indexed = {record["id"]: record for record in index["cases"]}
    variants = {item["id"]: item for item in spec["variants"]}
    variant = variants[args.variant]
    cases = spec["operator_cases"] if args.group == "operators" else spec["model_cases"]
    operator_records: dict[str, dict[str, Any]] = {}
    if args.group == "operators":
        operator_index = json.loads((args.operator_models / "index.json").read_text())
        if operator_index.get("spec_sha256") != spec_hash:
            raise SystemExit("operator models were generated from a different specification")
        if not operator_index.get("runtime_verified"):
            raise SystemExit("operator models were not runtime-verified when generated")
        operator_records = {item["id"]: item for item in operator_index["cases"]}
    if args.case_id:
        wanted = set(args.case_id); cases = [case for case in cases if case["id"] in wanted]
        if wanted != {case["id"] for case in cases}:
            raise SystemExit("one or more requested cases are absent")
    random.Random(args.seed).shuffle(cases)
    measurement = spec["measurement"]
    execute_count = 3 if args.smoke else measurement["execution_iterations"]
    warmups = 1 if args.smoke else measurement["warmups"]
    flags = measurement["host_compile_flags"]
    template = "benchmark-operators.csv" if args.group == "operators" else "benchmark-models.csv"
    with (repo / "artifact/templates" / template).open(newline="") as stream:
        header = next(csv.reader(stream))
    if args.output.exists() and not args.resume:
        raise SystemExit(f"refusing to replace {args.output}; pass --resume for a checkpoint")
    rows: list[dict[str, Any]] = []
    completed: set[str] = set()
    if args.output.exists():
        rows, completed = load_checkpoint(
            args.output, header, group=args.group, variant=args.variant,
            revision=revision, execute_count=execute_count,
        )
        unknown = completed - {case["id"] for case in cases}
        if unknown:
            raise SystemExit(f"checkpoint contains cases outside this run: {sorted(unknown)}")
        print(f"resuming after {len(completed)} complete cases from {args.output}")
    failures = []
    if failure_path.exists():
        for line_number, line in enumerate(failure_path.read_text().splitlines(), start=1):
            try:
                failure = json.loads(line)
            except json.JSONDecodeError as error:
                raise SystemExit(
                    f"{failure_path}:{line_number}: invalid JSON: {error}"
                ) from error
            if (not isinstance(failure, dict)
                    or set(failure) != {"id", "stage", "stderr"}):
                raise SystemExit(f"{failure_path}:{line_number}: invalid failure record")
            failures.append(failure)
    coverage_ids = {
        row["case_id"] if args.group == "operators" else row["model"]
        for row in rows if row["supported"] == "false"
    }
    if coverage_ids != {failure["id"] for failure in failures}:
        raise SystemExit("failure log differs from checkpoint coverage rows")
    model_files = []
    with tempfile.TemporaryDirectory(prefix="joggle-generated-") as temporary:
        root = Path(temporary)
        for case in cases:
            case_id = case["id"]
            model = ((args.operator_models / f"{case_id}.onnx") if args.group == "operators"
                     else (args.model_root / f"{case_id}.onnx"))
            expected_hash = (operator_records[case_id]["sha256"]
                             if args.group == "operators" else case["sha256"])
            if not model.is_file() or sha256(model.read_bytes()) != expected_hash:
                raise SystemExit(f"missing or mismatched model {model}")
            model_files.append({"id": case_id, "sha256": expected_hash})
            if case_id in completed:
                continue
            measurement_spec, input_record, feeds = load_case(args.spec, args.inputs, case_id)
            expected = oracle(model, measurement_spec, feeds)
            common = {"case_spec_sha256": spec_hash, "system": "Joggle",
                      "system_revision": revision, "variant": args.variant,
                      "supported": "true", "reason": "",
                      "input_digest": input_record["input_digest"]}
            common.update({"case_id": case_id, "family": case["family"]} if args.group == "operators"
                          else {"model": case_id, "model_hash": case["sha256"]})
            try:
                first = prepare(args, model, variant, root / f"{case_id}-first", flags)
            except Unsupported as error:
                common.update(supported="false", reason=f"unsupported:{error.stage}")
                rows.append(csv_row(header, common, seed=args.seed))
                failure = {"id": case_id, "stage": error.stage,
                           "stderr": error.stderr[-4000:]}
                append_jsonl(failure_path, failure)
                failures.append(failure)
                write_rows(args.output, header, rows)
                continue
            try:
                _prepare_ns, _artifact_bytes, artifact, header_file, _ = first
                case_rows = []
                harness = root / f"{case_id}-harness.c"
                tensor_records = {item["name"]: item for item in input_record["tensors"]}
                batch = measurement["execution_batches"][case_id]
                make_harness(
                    harness, case["inputs"], tensor_records, expected, args.inputs, batch
                )
                harness_object = root / f"{case_id}-harness.o"
                executable = root / f"{case_id}-run"
                for command in (
                    [args.cc, *flags, "-I", header_file.parent, "-c", harness,
                     "-o", harness_object],
                    [args.cc, *flags, artifact, harness_object, "-lm", "-o", executable],
                ):
                    try:
                        result = subprocess.run(
                            command, capture_output=True, text=True,
                            timeout=args.stage_timeout,
                        )
                    except subprocess.TimeoutExpired as error:
                        raise Unsupported("harness-compile:timeout", str(error)) from error
                    if result.returncode:
                        raise Unsupported("harness-compile", result.stderr)
                execute_out = root / f"{case_id}-execute"
                execute_out.mkdir()
                try:
                    result = subprocess.run(
                        [executable, "execute", str(warmups), str(execute_count), execute_out],
                        capture_output=True, text=True, env={**os.environ, **THREAD_ENV},
                        timeout=args.stage_timeout,
                    )
                except subprocess.TimeoutExpired as error:
                    raise Unsupported("execute:timeout", str(error)) from error
                if result.returncode:
                    raise Unsupported("execute", result.stderr)
                latencies = [int(line.split()[1]) for line in result.stdout.splitlines()
                             if line.startswith("NS ")]
                if len(latencies) != execute_count:
                    raise Unsupported("execute:timing-count")
                actual = read_outputs(execute_out, expected)
                max_abs, max_rel = compare(actual, expected, case["rtol"], case["atol"])
                digest = output_digest([name for name, _ in expected], actual)
                for iteration, latency in enumerate(latencies):
                    case_rows.append(csv_row(
                        header, common, iteration=iteration,
                        calls_per_sample=batch, latency_ns=latency, max_abs_error=max_abs,
                        max_rel_error=max_rel, output_digest=digest,
                        correct="true", seed=args.seed + 10_000 + iteration,
                    ))
                rows.extend(case_rows)
                write_rows(args.output, header, rows)
            except Unsupported as error:
                common.update(supported="false", reason=f"unsupported:{error.stage}")
                rows.append(csv_row(header, common, seed=args.seed))
                failure = {"id": case_id, "stage": error.stage,
                           "stderr": error.stderr[-4000:]}
                append_jsonl(failure_path, failure)
                failures.append(failure)
                write_rows(args.output, header, rows)
    write_rows(args.output, header, rows)
    compiler = subprocess.run([args.cc, "--version"], capture_output=True, text=True)
    record = {"schema_version": 1, "created_utc": datetime.now(timezone.utc).isoformat(),
              "release_eligible": not args.smoke and not dirty, "group": args.group,
              "variant": args.variant, "cases": [case["id"] for case in cases],
              "model_files": model_files,
              "output_sha256": sha256(args.output.read_bytes()),
              "unsupported": failures, "benchmark_spec_sha256": spec_hash,
              "failure_log": ({"path": str(failure_path),
                               "sha256": sha256(failure_path.read_bytes())}
                              if failure_path.exists() else None),
              "input_index_sha256": sha256((args.inputs / "index.json").read_bytes()),
              "git_revision": revision, "git_dirty": dirty,
              "joggle_sha256": sha256(args.joggle.read_bytes()),
              "compiler": compiler.stdout.splitlines()[0] if compiler.stdout else str(args.cc),
              "compile_flags": flags, "pipeline": variant["pipeline"],
              "execution_iterations": execute_count,
              "warmups": warmups, "seed": args.seed,
              "stage_timeout_seconds": args.stage_timeout,
              "execution_batches": {case["id"]: measurement["execution_batches"][case["id"]]
                                      for case in cases},
              "thread_environment": THREAD_ENV, "host_controls": host_controls(),
              "command": sys.argv}
    record_path.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")
    print(f"wrote {len(rows)} {args.variant} rows to {args.output}")
    print(f"unsupported={len(failures)}; run record={record_path}")
    return 0


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec", type=Path, default=root / "manifests/benchmark-cases.json")
    parser.add_argument("--group", choices=("operators", "models"), required=True)
    parser.add_argument("--variant", choices=("joggle-unoptimized", "joggle-optimized"), required=True)
    parser.add_argument("--inputs", type=Path, required=True)
    parser.add_argument("--operator-models", type=Path)
    parser.add_argument("--model-root", type=Path)
    parser.add_argument("--joggle", type=Path, required=True)
    parser.add_argument("--builtin-mods", type=Path, required=True)
    parser.add_argument("--extension-mods", type=Path, default=Path("examples/mods"))
    parser.add_argument("--cc", type=Path, default=Path("cc"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--run-record", type=Path)
    parser.add_argument("--case-id", action="append")
    parser.add_argument("--seed", type=int, default=20260921)
    parser.add_argument("--stage-timeout", type=float, default=600.0)
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument(
        "--resume", action="store_true",
        help="resume an output CSV containing complete per-case checkpoints",
    )
    parser.add_argument("--allow-dirty", action="store_true")
    args = parser.parse_args()
    if args.group == "operators" and not args.operator_models:
        parser.error("operator collection requires --operator-models")
    if args.group == "models" and not args.model_root:
        parser.error("model collection requires --model-root")
    if args.stage_timeout <= 0:
        parser.error("--stage-timeout must be positive")
    return args


if __name__ == "__main__":
    raise SystemExit(main(parse_args()))
