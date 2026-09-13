#!/usr/bin/env python3

"""Measure snapshot-query memoization with a mechanically stripped control."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import platform
import re
import resource
import shutil
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path


QUERY_HELPERS = (
    "address_only",
    "affine",
    "affine_in",
    "affine_seen",
    "base_fn",
    "base_op",
    "captured",
    "contains",
    "drop_address",
    "has_op",
    "index_arg",
    "known",
    "linked",
    "nested",
    "syntax_op",
    "trip",
)


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--modules", type=Path, required=True)
    parser.add_argument("--examples", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=2)
    parser.add_argument("--revision", default="")
    return parser.parse_args()


def strip_query_memoization(source: Path, target: Path) -> None:
    if target.exists():
        shutil.rmtree(target)
    shutil.copytree(source, target)
    names = "|".join(re.escape(name) for name in QUERY_HELPERS)
    pattern = re.compile(rf"\[memo\]\n(?=(?:local )?fn (?:{names})\b)")
    removed = 0
    for path in target.rglob("*.jog"):
        text = path.read_text(encoding="utf-8")
        text, count = pattern.subn("", text)
        if count:
            path.write_text(text, encoding="utf-8")
            removed += count
    if removed != len(QUERY_HELPERS):
        raise RuntimeError(
            f"expected {len(QUERY_HELPERS)} query annotations, removed {removed}"
        )


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def usage() -> resource.struct_rusage:
    return resource.getrusage(resource.RUSAGE_CHILDREN)


def revision(args: argparse.Namespace) -> str:
    if args.revision:
        return args.revision
    result = subprocess.run(
        [
            "git",
            "-C",
            str(args.modules.parent),
            "rev-parse",
            "--short=12",
            "HEAD",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
        text=True,
    )
    return result.stdout.strip() if result.returncode == 0 else "unknown"


def run_one(
    args: argparse.Namespace,
    source_revision: str,
    variant: str,
    iteration: int,
    order: int,
    modules: Path,
) -> dict[str, object]:
    stem = f"{iteration:02d}-{order:02d}-{variant}"
    output = args.out / f"{stem}.jog"
    report = args.out / f"{stem}.json"
    command = [
        str(args.tool),
        "run",
        "spatial.block",
        str(args.input),
        "--arg",
        "2",
        "--report",
        str(report),
        "-M",
        str(args.examples),
        "-M",
        str(modules),
    ]
    before = usage()
    started = time.perf_counter()
    with output.open("wb") as stream:
        completed = subprocess.run(
            command, stdout=stream, stderr=subprocess.PIPE, check=False
        )
    wall = time.perf_counter() - started
    after = usage()
    if completed.returncode:
        raise RuntimeError(
            f"{variant} iteration {iteration} failed:\n"
            + completed.stderr.decode("utf-8", errors="replace")
        )
    record = json.loads(report.read_text(encoding="utf-8"))
    calls = record.get("calls", {})
    cached = record.get("cached", {})
    return {
        "variant": variant,
        "revision": source_revision,
        "iteration": iteration,
        "order": order,
        "wall_s": f"{wall:.6f}",
        "user_s": f"{after.ru_utime - before.ru_utime:.6f}",
        "sys_s": f"{after.ru_stime - before.ru_stime:.6f}",
        "edits": record.get("edits", 0),
        "source_calls": sum(calls.values()),
        "cached_calls": sum(cached.values()),
        "output_bytes": output.stat().st_size,
        "output_sha256": digest(output),
        "report": report.name,
    }


def main() -> None:
    args = arguments()
    if args.repeats < 1:
        raise ValueError("--repeats must be positive")
    args.tool = args.tool.resolve()
    args.input = args.input.resolve()
    args.modules = args.modules.resolve()
    args.examples = args.examples.resolve()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    source_revision = revision(args)
    control_modules = args.out / "control-modules"
    strip_query_memoization(args.modules, control_modules)

    rows: list[dict[str, object]] = []
    for iteration in range(args.repeats):
        order = ("control", "candidate")
        if iteration % 2:
            order = tuple(reversed(order))
        for position, variant in enumerate(order):
            modules = control_modules if variant == "control" else args.modules
            rows.append(
                run_one(
                    args, source_revision, variant, iteration, position, modules
                )
            )

    hashes = {str(row["output_sha256"]) for row in rows}
    sizes = {int(row["output_bytes"]) for row in rows}
    edits = {int(row["edits"]) for row in rows}
    if len(hashes) != 1 or len(sizes) != 1 or len(edits) != 1:
        raise RuntimeError("control and candidate outputs are not identical")

    fields = list(rows[0])
    with (args.out / "summary.csv").open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    manifest = {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "platform": platform.platform(),
        "machine": platform.machine(),
        "python": platform.python_version(),
        "revision": source_revision,
        "tool": str(args.tool),
        "tool_sha256": digest(args.tool),
        "input": str(args.input),
        "input_sha256": digest(args.input),
        "modules": str(args.modules),
        "examples": str(args.examples),
        "repeats": args.repeats,
        "stripped_query_helpers": list(QUERY_HELPERS),
        "verified_output_sha256": next(iter(hashes)),
    }
    (args.out / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(args.out / "summary.csv")


if __name__ == "__main__":
    main()
