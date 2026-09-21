#!/usr/bin/env python3
"""Build both evaluator variants and collect the reactive-update matrix."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import platform
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
    parser.add_argument("--nodes", type=int, nargs="+", default=[1000])
    parser.add_argument("--affected", type=int, nargs="+", default=[1, 8, 64])
    parser.add_argument("--fanout", type=int, nargs="+", default=[1])
    parser.add_argument("--stages", type=int, nargs="+", default=[5])
    parser.add_argument("--models", type=Path, nargs="+")
    parser.add_argument(
        "--edit-classes",
        choices=("no_op", "operation_metadata", "value_type"),
        nargs="+",
        default=["no_op", "operation_metadata", "value_type"],
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
        default=["late"],
    )
    parser.add_argument("--warmups", type=int, default=10)
    parser.add_argument("--iterations", type=int, default=100)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--build-root", type=Path)
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
        reader = csv.reader(stream)
        rows = list(reader)
    if not rows:
        raise RuntimeError(f"empty result file: {source}")
    with output.open("a", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        if write_header:
            writer.writerow(rows[0])
        writer.writerows(rows[1:])
    return False


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main() -> int:
    args = parse_args()
    repo = Path(__file__).resolve().parents[1]
    status = command(["git", "status", "--porcelain"], repo, capture=True)
    if status and not args.allow_dirty:
        raise SystemExit("refusing to benchmark a dirty tree; commit or pass --allow-dirty")
    revision = command(["git", "rev-parse", "HEAD"], repo, capture=True)
    if status:
        revision += "-dirty"
    invalid_generator = not args.models and (
        any(node <= 1 for node in args.nodes)
        or any(value <= 0 for value in args.affected)
        or any(value <= 0 for value in args.fanout)
    )
    if (
        invalid_generator
        or any(value < 1 or value > 5 for value in args.stages)
        or args.warmups < 0
        or args.iterations <= 0
        or args.seed < 0
    ):
        raise SystemExit("benchmark arguments are out of range")

    owned_temp = None
    if args.build_root:
        build_root = args.build_root.resolve()
        build_root.mkdir(parents=True, exist_ok=True)
    else:
        owned_temp = tempfile.TemporaryDirectory(prefix="joggle-artifact-")
        build_root = Path(owned_temp.name)

    persistent = configure(repo, build_root / "persistent", True, bool(args.models))
    no_plans = configure(repo, build_root / "no-plans", False, bool(args.models))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.unlink(missing_ok=True)
    write_header = True
    commands: list[list[str]] = []

    models = []
    seen_models: set[tuple[str, str]] = set()
    if args.models:
        for path in args.models:
            resolved = path.resolve()
            if not resolved.is_file():
                raise SystemExit(f"model does not exist: {path}")
            digest = sha256(resolved)
            identity = (resolved.stem, digest)
            if identity in seen_models:
                continue
            seen_models.add(identity)
            models.append(
                {
                    "path": resolved,
                    "name": resolved.stem,
                    "sha256": digest,
                }
            )
    subjects = models
    if not subjects:
        subjects = []
        generated: set[tuple[int, int, int]] = set()
        for nodes in args.nodes:
            for affected in args.affected:
                if affected >= nodes:
                    continue
                for fanout in args.fanout:
                    effective = min(fanout, max(1, affected - 1))
                    key = (nodes, affected, effective)
                    if key in generated:
                        continue
                    generated.add(key)
                    subjects.append(
                        {"nodes": nodes, "affected": affected, "fanout": effective}
                    )
    if not subjects:
        raise SystemExit("the requested matrix contains no valid subjects")

    for subject in subjects:
        for stage_count in args.stages:
            sites = args.sites if models else [None]
            for site in sites:
                if models:
                    label = f"{subject['name']}-{subject['sha256'][:12]}"
                else:
                    label = (
                        f"{subject['nodes']}-{subject['affected']}-"
                        f"{subject['fanout']}"
                    )
                if site:
                    label += f"-{site}"
                runs = ((persistent, "all"), (no_plans, "no-plan-cache"))
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
                        if (
                            edit_class != "no_op"
                            and len(args.scopes) == 1
                        ):
                            invocation.extend(["--scope", args.scopes[0]])
                        if models:
                            invocation.extend(
                                [
                                    "--input",
                                    str(subject["path"]),
                                    "--subject-hash",
                                    subject["sha256"],
                                    "--site",
                                    site,
                                ]
                            )
                        else:
                            invocation.extend(
                                [
                                    "--total-nodes",
                                    str(subject["nodes"]),
                                    "--affected-nodes",
                                    str(subject["affected"]),
                                    "--fanout",
                                    str(subject["fanout"]),
                                ]
                            )
                        command(invocation, repo)
                        commands.append(invocation)
                        write_header = append_csv(
                            partial, args.output, write_header
                        )

    command(
        [
            sys.executable,
            str(repo / "artifact" / "validate_reactive.py"),
            str(args.output),
        ],
        repo,
    )
    metadata = {
        "schema": "reactive-update/v2",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "revision": revision,
        "dirty": bool(status),
        "platform": platform.platform(),
        "python": platform.python_version(),
        "cmake": command(["cmake", "--version"], repo, capture=True).splitlines()[0],
        "nodes": args.nodes,
        "affected": args.affected,
        "fanout": args.fanout,
        "stages": args.stages,
        "models": [
            {"name": model["name"], "sha256": model["sha256"]}
            for model in models
        ],
        "sites": args.sites if models else [],
        "edit_classes": args.edit_classes,
        "scopes": args.scopes,
        "warmups": args.warmups,
        "iterations": args.iterations,
        "seed": args.seed,
        "commands": commands,
    }
    args.output.with_suffix(".json").write_text(
        json.dumps(metadata, indent=2) + "\n", encoding="utf-8"
    )
    if owned_temp is not None:
        owned_temp.cleanup()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
