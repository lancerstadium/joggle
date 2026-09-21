#!/usr/bin/env python3
"""Collect the Joggle rows for the model-backed Figure 6 experiment."""

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
import tempfile
from datetime import datetime, timezone
from pathlib import Path


def command(args: list[str], cwd: Path, capture: bool = False) -> str:
    result = subprocess.run(
        args,
        cwd=cwd,
        check=True,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.STDOUT if capture else None,
    )
    return result.stdout.strip() if capture else ""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--stages", type=int, nargs="+", default=[5])
    subjects = parser.add_mutually_exclusive_group()
    subjects.add_argument("--models", type=Path, nargs="+")
    subjects.add_argument("--model-manifest", type=Path)
    parser.add_argument("--model-root", type=Path)
    parser.add_argument(
        "--edit-classes",
        choices=("operation_metadata", "value_type"),
        nargs="+",
        default=["operation_metadata", "value_type"],
    )
    parser.add_argument(
        "--scopes",
        choices=("affected", "unrelated"),
        nargs="+",
        default=["affected", "unrelated"],
    )
    parser.add_argument(
        "--sites",
        choices=("early", "middle", "late"),
        nargs="+",
        default=["early", "middle", "late"],
    )
    parser.add_argument("--warmups", type=int, default=10)
    parser.add_argument("--iterations", type=int, default=100)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--build-root", type=Path)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--allow-dirty", action="store_true")
    return parser.parse_args()


def configure(repo: Path, build: Path, persistent: bool, onnx: bool) -> Path:
    command(
        [
            "cmake",
            "-S",
            str(repo),
            "-B",
            str(build),
            "-DCMAKE_BUILD_TYPE=Release",
            "-DJOGGLE_BUILD_TESTS=OFF",
            "-DJOGGLE_BUILD_ARTIFACT=ON",
            "-DJOGGLE_EVAL_COUNTERS=ON",
            f"-DJOGGLE_BUILD_ONNX={'ON' if onnx else 'OFF'}",
            f"-DJOGGLE_EVALUATOR_PERSISTENT_PLANS={'ON' if persistent else 'OFF'}",
        ],
        repo,
    )
    command(
        ["cmake", "--build", str(build), "--target", "joggle-artifact-reactive", "-j"],
        repo,
    )
    suffix = ".exe" if sys.platform == "win32" else ""
    return build / "artifact" / f"joggle-artifact-reactive{suffix}"


def append_csv(source: Path, output: Path, write_header: bool) -> bool:
    with source.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        raise RuntimeError(f"empty result file: {source}")
    template = Path(__file__).resolve().parent / "templates/figure-06-update.csv"
    with template.open(newline="", encoding="utf-8") as stream:
        header = next(csv.reader(stream))
    with output.open("a", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=header, lineterminator="\n")
        if write_header:
            writer.writeheader()
        for row in rows:
            policy = "update" if row["policy"] == "reactive" else row["policy"]
            if policy not in {"full", "update"}:
                continue
            writer.writerow({
                "system": "Joggle", "system_revision": row["system_revision"],
                "subject": row["subject"], "subject_hash": row["subject_hash"],
                "total_ops": row["total_ops"], "affected_ops": row["affected_ops"],
                "edit_class": row["edit_class"], "edit_scope": row["edit_scope"],
                "edit_site": row["edit_site"], "policy": policy,
                "iteration": row["iteration"], "wall_ns": row["wall_ns"],
                "visited_ops": row["evaluated_ops"],
                "executed_stages": row["executed_stages"], "total_stages": row["stages"],
                "output_digest": row["output_digest"], "correct": row["correct"],
                "seed": row["seed"],
            })
    return False


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write_json(path: Path, value: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp")
    with temporary.open("w", encoding="utf-8") as stream:
        stream.write(json.dumps(value, indent=2, sort_keys=True) + "\n")
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def resolve_models(args: argparse.Namespace) -> list[dict[str, object]]:
    if args.model_manifest and not args.model_root:
        raise SystemExit("--model-manifest requires --model-root")
    if args.model_root and not args.model_manifest:
        raise SystemExit("--model-root requires --model-manifest")
    requested: list[tuple[Path, str | None, str | None]] = []
    if args.models:
        requested = [(path, None, None) for path in args.models]
    elif args.model_manifest:
        with args.model_manifest.open(newline="", encoding="utf-8") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames != ["model", "sha256"]:
                raise SystemExit("model manifest columns must be model,sha256")
            for line, row in enumerate(reader, start=2):
                name = row["model"]
                digest = row["sha256"].lower()
                if not name or len(digest) != 64 or any(
                    char not in "0123456789abcdef" for char in digest
                ):
                    raise SystemExit(f"invalid model manifest row {line}")
                requested.append((args.model_root / f"{name}.onnx", name, digest))
    models: list[dict[str, object]] = []
    seen: set[tuple[str, str]] = set()
    for path, declared_name, declared_hash in requested:
        resolved = path.resolve()
        if not resolved.is_file():
            raise SystemExit(f"model does not exist: {path}")
        digest = sha256(resolved)
        if declared_hash and digest != declared_hash:
            raise SystemExit(f"model hash mismatch: {path}")
        name = declared_name or resolved.stem
        identity = (name, digest)
        if identity in seen:
            continue
        seen.add(identity)
        models.append({"path": resolved, "name": name, "sha256": digest})
    return models


def main() -> int:
    args = parse_args()
    repo = Path(__file__).resolve().parents[1]
    status = command(["git", "status", "--porcelain"], repo, capture=True)
    if status and not args.allow_dirty:
        raise SystemExit("refusing to benchmark a dirty tree; commit or pass --allow-dirty")
    revision = command(["git", "rev-parse", "HEAD"], repo, capture=True)
    if status:
        revision += "-dirty"
    if args.resume and not args.build_root:
        raise SystemExit("--resume requires a persistent --build-root")
    if args.output.exists() and not args.resume:
        raise SystemExit(f"refusing to replace {args.output}; pass --resume")
    metadata_path = args.output.with_suffix(".json")
    if metadata_path.exists():
        raise SystemExit(f"refusing to replace completed run record {metadata_path}")
    if (
        not (args.models or args.model_manifest)
        or any(value < 1 or value > 5 for value in args.stages)
        or args.warmups < 0
        or args.iterations <= 0
        or args.seed < 0
    ):
        raise SystemExit("benchmark arguments are out of range")
    models = resolve_models(args)

    owned_temp = None
    if args.build_root:
        build_root = args.build_root.resolve()
        build_root.mkdir(parents=True, exist_ok=True)
    else:
        owned_temp = tempfile.TemporaryDirectory(prefix="joggle-artifact-")
        build_root = Path(owned_temp.name)

    onnx = bool(models)
    persistent = configure(repo, build_root / "persistent", True, onnx)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    job_records: list[dict[str, object]] = []
    jobs: list[tuple[Path, list[str]]] = []

    subjects = models

    for subject in subjects:
        for stage_count in args.stages:
            sites = args.sites
            for site in sites:
                label = f"{subject['name']}-{subject['sha256'][:12]}-{site}"
                runs = ((persistent, "full"), (persistent, "reactive"))
                for edit_class in args.edit_classes:
                    for binary, policy in runs:
                        partial = build_root / (
                            f"{label}-{stage_count}-{edit_class}-{policy}.csv"
                        )
                        invocation = [
                            str(binary),
                            "--modules",
                            str(binary.parents[1] / "modules"),
                            "--output",
                            str(partial),
                            "--revision",
                            revision,
                            "--policy",
                            policy,
                            "--edit-class",
                            edit_class,
                            "--stages",
                            str(stage_count),
                            "--warmups",
                            str(args.warmups),
                            "--iterations",
                            str(args.iterations),
                            "--seed",
                            str(args.seed),
                        ]
                        if len(args.scopes) == 1:
                            invocation.extend(["--scope", args.scopes[0]])
                        invocation.extend([
                            "--input", str(subject["path"]),
                            "--subject-hash", subject["sha256"], "--site", site,
                        ])
                        jobs.append((partial, invocation))

    random.Random(args.seed).shuffle(jobs)
    for partial, invocation in jobs:
        sidecar = partial.with_suffix(".job.json")
        expected_job = {
            "schema": "reactive-job/v1",
            "revision": revision,
            "command": invocation,
        }
        reused = False
        if args.resume and (partial.exists() or sidecar.exists()):
            if not partial.is_file() or not sidecar.is_file():
                raise SystemExit(f"incomplete cached job pair: {partial}")
            observed = json.loads(sidecar.read_text(encoding="utf-8"))
            if {key: observed.get(key) for key in expected_job} != expected_job:
                raise SystemExit(f"cached job command differs: {partial}")
            if observed.get("output_sha256") != sha256(partial):
                raise SystemExit(f"cached job hash differs: {partial}")
            reused = True
        if not reused:
            if partial.exists() or sidecar.exists():
                raise SystemExit(
                    f"refusing to replace cached job {partial}; use a fresh build root"
                )
            command(invocation, repo)
            write_json(sidecar, {**expected_job, "output_sha256": sha256(partial)})
        job_records.append({
            "path": str(partial),
            "sha256": sha256(partial),
            "reused": reused,
            "record": str(sidecar),
            "record_sha256": sha256(sidecar),
        })

    merged = args.output.with_name(f".{args.output.name}.tmp")
    with merged.open("w", encoding="utf-8"):
        pass
    write_header = True
    for partial, _invocation in jobs:
        write_header = append_csv(partial, merged, write_header)

    command(
        [
            sys.executable,
            str(repo / "artifact" / "validate_reactive.py"),
            str(merged),
            "--allow-partial",
        ],
        repo,
    )
    merged.replace(args.output)
    metadata = {
        "schema": "update-provider/v1",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "revision": revision,
        "dirty": bool(status),
        "platform": platform.platform(),
        "python": platform.python_version(),
        "cmake": command(["cmake", "--version"], repo, capture=True).splitlines()[0],
        "stages": args.stages,
        "models": [
            {"name": model["name"], "sha256": model["sha256"]}
            for model in models
        ],
        "model_manifest": (
            {
                "path": str(args.model_manifest.resolve()),
                "sha256": sha256(args.model_manifest.resolve()),
            }
            if args.model_manifest
            else None
        ),
        "sites": args.sites,
        "edit_classes": args.edit_classes,
        "scopes": args.scopes,
        "warmups": args.warmups,
        "iterations": args.iterations,
        "seed": args.seed,
        "output_sha256": sha256(args.output),
        "inputs": job_records,
        "expected_jobs": len(jobs),
    }
    write_json(metadata_path, metadata)
    if owned_temp is not None:
        owned_temp.cleanup()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
