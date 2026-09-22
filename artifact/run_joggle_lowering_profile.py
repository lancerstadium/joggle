#!/usr/bin/env python3
"""Collect the production-lowering calibration rows for Figure 6."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path

from joggle_entry import signature_command

STAGES = (
    "decode", "infer_convert", "c_prepare", "scalar_lowering",
    "storage_plan", "storage_place", "c_emit",
)


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def run_file(argv: list[str | Path], output: Path, timeout: float) -> int:
    begin = time.perf_counter_ns()
    try:
        with output.open("w", encoding="utf-8") as stream:
            result = subprocess.run(
                [str(value) for value in argv], stdout=stream,
                stderr=subprocess.PIPE, text=True, timeout=timeout,
            )
    except subprocess.TimeoutExpired as error:
        raise SystemExit(f"stage timed out: {' '.join(map(str, argv))}: {error}") from error
    elapsed = time.perf_counter_ns() - begin
    if result.returncode:
        raise SystemExit(
            f"stage failed ({result.returncode}): {' '.join(map(str, argv))}\n{result.stderr}"
        )
    return max(elapsed, 1)


def capture(argv: list[str | Path], timeout: float) -> str:
    try:
        result = subprocess.run(
            [str(value) for value in argv], capture_output=True, text=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as error:
        raise SystemExit(f"command timed out: {' '.join(map(str, argv))}: {error}") from error
    if result.returncode:
        raise SystemExit(
            f"command failed ({result.returncode}): {' '.join(map(str, argv))}\n"
            f"{result.stderr}{result.stdout}"
        )
    return result.stdout


def stage_run(
    tool: Path, modules: Path, current: Path, following: Path,
    functions: list[str], timeout: float, args: list[str] | None = None,
) -> int:
    command: list[str | Path] = [tool, "run", *functions, current]
    for value in args or []:
        command.extend(("--arg", value))
    command.extend(("-M", modules))
    return run_file(command, following, timeout)


def sequence_run(
    tool: Path, modules: Path, current: Path, following: Path,
    functions: list[str], timing: Path, timeout: float,
) -> tuple[int, dict[str, int]]:
    wall_ns = run_file(
        [tool, "run", *functions, current, "--timing", timing, "-M", modules],
        following, timeout,
    )
    profile = json.loads(timing.read_text(encoding="utf-8"))
    steps = profile.get("steps")
    if not isinstance(steps, list) or [step.get("function") for step in steps] != functions:
        raise SystemExit("lowering timing report does not match the requested sequence")
    measured = {step["function"]: int(step["total_ns"]) for step in steps}
    internal_ns = sum(measured.values())
    if internal_ns > wall_ns:
        raise SystemExit("lowering timing report exceeds subprocess wall time")
    measured["driver"] = wall_ns - internal_ns
    return wall_ns, measured


def rebuild(
    args: argparse.Namespace, model: Path, inputs: list[dict[str, object]], work: Path,
) -> tuple[dict[str, int], int, int, str]:
    work.mkdir(parents=True)
    timings: dict[str, int] = {}
    current = work / "00-read.jog"
    timings["decode"] = run_file(
        [args.joggle, "read", "onnx.read", model, "-M", args.builtin_mods],
        current, args.stage_timeout,
    )
    following = work / "01-specialize.jog"
    timings["infer_convert"] = run_file(
        signature_command(
            args.joggle, current, args.builtin_mods, inputs,
        ),
        following, args.stage_timeout,
    )
    current = following
    following = work / "02-prepared.jog"
    _wall_ns, measured = sequence_run(
        args.joggle, args.builtin_mods, current, following,
        [
            "onnx.nn.infer", "onnx.nn.convert", "c.prepare",
        ],
        work / "02-lowering-timing.json", args.stage_timeout,
    )
    timings["infer_convert"] += (
        measured["driver"] + measured["onnx.nn.infer"] +
        measured["onnx.nn.convert"]
    )
    timings["c_prepare"] = measured["c.prepare"]
    current = following
    following = work / "03-scalar.jog"
    timings["scalar_lowering"] = stage_run(
        args.joggle, args.builtin_mods, current, following,
        ["tile.scalarize"], args.stage_timeout,
    )
    current = following
    following = work / "04-storage.jog"
    timings["storage_plan"] = stage_run(
        args.joggle, args.builtin_mods, current, following,
        ["mem.plan", "c.noalias"], args.stage_timeout,
    )
    current = following
    following = work / "05-place.jog"
    timings["storage_place"] = stage_run(
        args.joggle, args.builtin_mods, current, following,
        ["c.place"], args.stage_timeout, ['"static"'],
    )
    current = following

    frontier = capture(
        [args.joggle, "query", "c.frontier", current, "-M", args.builtin_mods],
        args.stage_timeout,
    )
    if frontier != "[]\n":
        raise SystemExit(f"C frontier is not empty for {model}: {frontier}")
    summary = json.loads(capture(
        [args.joggle, "query", "stat.summary", current, "-M", args.builtin_mods],
        args.stage_timeout,
    ))
    total_ops = int(summary["ops"])
    header = work / "model.h"
    source = work / "model.c"
    timings["c_emit"] = run_file(
        [args.joggle, "emit", "c.header", current, "-M", args.builtin_mods],
        header, args.stage_timeout,
    ) + run_file(
        [args.joggle, "emit", "c.source", current, "-M", args.builtin_mods],
        source, args.stage_timeout,
    )
    object_file = work / "model.o"
    capture([args.cc, "-O3", "-std=c11", "-c", source, "-o", object_file], args.stage_timeout)
    artifact_bytes = header.stat().st_size + source.stat().st_size
    digest = hashlib.sha256(header.read_bytes() + b"\0" + source.read_bytes()).hexdigest()
    return timings, total_ops, artifact_bytes, digest


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--model-manifest", type=Path, default=root / "manifests/reactive-models.csv")
    parser.add_argument("--benchmark-spec", type=Path, default=root / "manifests/benchmark-cases.json")
    parser.add_argument("--model-root", type=Path, required=True)
    parser.add_argument("--model", action="append")
    parser.add_argument("--joggle", type=Path, required=True)
    parser.add_argument("--builtin-mods", type=Path, required=True)
    parser.add_argument("--cc", type=Path, default=Path("cc"))
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--iterations", type=int, default=10)
    parser.add_argument("--seed", type=int, default=20260922)
    parser.add_argument("--stage-timeout", type=float, default=600.0)
    parser.add_argument("--allow-dirty", action="store_true")
    args = parser.parse_args()
    if args.warmups < 0 or args.iterations <= 0 or args.stage_timeout <= 0:
        parser.error("warmups, iterations, and timeout are out of range")
    return args


def main() -> int:
    args = parse_args()
    repo = Path(__file__).resolve().parents[1]
    dirty = bool(capture(["git", "status", "--porcelain"], args.stage_timeout))
    if dirty and not args.allow_dirty:
        raise SystemExit("refusing to profile a dirty tree; commit or pass --allow-dirty")
    revision = capture(["git", "rev-parse", "HEAD"], args.stage_timeout).strip()
    if dirty:
        revision += "-dirty"
    record_path = args.output.with_suffix(".json")
    if args.output.exists() or record_path.exists():
        raise SystemExit("refusing to replace an existing output or record")

    with args.model_manifest.open(newline="", encoding="utf-8") as stream:
        models = list(csv.DictReader(stream))
    benchmark = json.loads(args.benchmark_spec.read_text(encoding="utf-8"))
    benchmark_cases = {case["id"]: case for case in benchmark["model_cases"]}
    if args.model:
        wanted = set(args.model)
        models = [row for row in models if row["model"] in wanted]
        if {row["model"] for row in models} != wanted:
            raise SystemExit("one or more requested models are absent from the manifest")
    template = repo / "artifact/templates/figure-06-update.csv"
    with template.open(newline="", encoding="utf-8") as stream:
        header = next(csv.reader(stream))
    rows: list[dict[str, object]] = []
    with tempfile.TemporaryDirectory(prefix="joggle-lowering-profile-") as temporary:
        work_root = Path(temporary)
        for model_row in models:
            name, expected_hash = model_row["model"], model_row["sha256"]
            if name not in benchmark_cases:
                raise SystemExit(f"model has no fixed-shape benchmark case: {name}")
            case = benchmark_cases[name]
            if case["sha256"] != expected_hash:
                raise SystemExit(f"model hashes differ between manifests: {name}")
            model = args.model_root / f"{name}.onnx"
            if not model.is_file() or sha256(model) != expected_hash:
                raise SystemExit(f"missing or mismatched model: {model}")
            for warmup in range(args.warmups):
                rebuild(args, model, case["inputs"], work_root / name / f"warmup-{warmup}")
            for iteration in range(args.iterations):
                timings, total_ops, artifact_bytes, digest = rebuild(
                    args, model, case["inputs"],
                    work_root / name / f"iteration-{iteration}"
                )
                for stage in STAGES:
                    rows.append({
                        "path": "production", "system": "Joggle",
                        "system_revision": revision, "subject": name,
                        "subject_hash": expected_hash, "total_ops": total_ops,
                        "affected_ops": 0, "edit_class": "", "edit_scope": "",
                        "edit_site": "", "policy": "full", "stage": stage,
                        "iteration": iteration, "wall_ns": timings[stage],
                        "visited_ops": 0, "executed_stages": 1,
                        "total_stages": len(STAGES), "artifact_bytes": artifact_bytes,
                        "output_digest": digest, "correct": "true", "seed": args.seed,
                    })
            print(f"profiled {name}: {args.iterations} rebuilds", flush=True)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary_output = args.output.with_name(f".{args.output.name}.tmp")
    with temporary_output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=header, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
        stream.flush()
        os.fsync(stream.fileno())
    temporary_output.replace(args.output)
    payload = {
        "schema": "update-provider/v1",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "system": "Joggle", "revision": revision, "dirty": dirty,
        "path": "production", "models": [row["model"] for row in models],
        "warmups": args.warmups, "iterations": args.iterations,
        "stages": list(STAGES), "seed": args.seed,
        "entry_specialization": "opt.signature(main, benchmark input types)",
        "benchmark_spec_sha256": sha256(args.benchmark_spec),
        "output_sha256": sha256(args.output), "command": sys.argv,
    }
    record_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    print(f"wrote {len(rows)} production rows to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
