#!/usr/bin/env python3
"""Measure structural block budgets without changing the backend pipeline."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import subprocess
import time
from pathlib import Path


ACCUMULATOR = re.compile(r"^\s*var acc(?:_[A-Za-z0-9]+)* =", re.MULTILINE)


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def run(command: list[str], output: Path) -> float:
    start = time.perf_counter()
    with output.open("wb") as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.PIPE)
    elapsed = time.perf_counter() - start
    if result.returncode != 0:
        detail = result.stderr.decode(errors="replace")
        raise RuntimeError(f"command failed ({result.returncode}): "
                           f"{' '.join(command)}\n{detail}")
    return elapsed


def values(text: str, minimum: int, name: str) -> list[int]:
    parsed = [int(item.strip()) for item in text.split(",") if item.strip()]
    if not parsed or any(item < minimum for item in parsed):
        raise argparse.ArgumentTypeError(
            f"{name} requires comma-separated integers >= {minimum}"
        )
    if len(set(parsed)) != len(parsed):
        raise argparse.ArgumentTypeError(f"{name} contains a duplicate")
    return parsed


def models(items: list[str]) -> list[tuple[str, Path]]:
    out: list[tuple[str, Path]] = []
    names: set[str] = set()
    for item in items:
        name, separator, raw_path = item.partition("=")
        if not separator or not name or not raw_path:
            raise ValueError("models use NAME=CANONICAL.jog")
        if (not re.fullmatch(r"[A-Za-z0-9._-]+", name) or
                name in {".", ".."}):
            raise ValueError(f"unsafe model name: {name}")
        if name in names:
            raise ValueError(f"duplicate model name: {name}")
        path = Path(raw_path).resolve()
        if not path.is_file():
            raise ValueError(f"model does not exist: {path}")
        names.add(name)
        out.append((name, path))
    return out


def command(tool: Path, action: str, function: str, source: Path,
            modules: Path, *arguments: str) -> list[str]:
    out = [str(tool), action, function, str(source)]
    for argument in arguments:
        out += ["--arg", argument]
    out += ["-M", str(modules)]
    return out


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--modules", type=Path, required=True)
    parser.add_argument("--examples", type=Path, required=True)
    parser.add_argument("--model", action="append", required=True,
                        metavar="NAME=CANONICAL.jog")
    parser.add_argument("--factors", default="4,7")
    parser.add_argument("--budgets", default="0,500,1500,1000000")
    parser.add_argument("--out-dir", type=Path,
                        default=Path("build-study/block-frontier"))
    parser.add_argument("--output", type=Path,
                        default=Path("paper/data/block-frontier-pilot.csv"))
    args = parser.parse_args()

    tool = args.tool.resolve()
    module_path = args.modules.resolve()
    example_path = args.examples.resolve()
    if not tool.is_file():
        parser.error(f"tool does not exist: {tool}")
    if not module_path.is_dir() or not example_path.is_dir():
        parser.error("module and example paths must be directories")
    try:
        selected_models = models(args.model)
        factors = values(args.factors, 2, "factors")
        budgets = values(args.budgets, 0, "budgets")
    except (ValueError, argparse.ArgumentTypeError) as error:
        parser.error(str(error))

    revision = subprocess.run(
        ["git", "rev-parse", "HEAD"], check=True, text=True,
        stdout=subprocess.PIPE
    ).stdout.strip()
    root = args.out_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    rows: list[dict[str, object]] = []
    factor_arg = json.dumps(factors, separators=(",", ":"))

    for model_name, canonical in selected_models:
        canonical_hash = digest(canonical)
        canonical_bytes = canonical.stat().st_size
        for budget in budgets:
            directory = root / model_name / str(budget)
            directory.mkdir(parents=True, exist_ok=True)
            blocked = directory / "blocked.jog"
            clean = directory / "clean.jog"
            planned = directory / "planned.jog"
            placed = directory / "placed.jog"
            source = directory / "model.c"

            block_command = command(
                tool, "run", "spatial.block", canonical, module_path,
                factor_arg, str(budget)
            )
            block_command += ["-M", str(example_path)]
            block_seconds = run(block_command, blocked)
            clean_seconds = run([
                str(tool), "run", "bounds.fold", "opt.fold", "opt.basic",
                str(blocked), "-M", str(module_path)
            ], clean)
            plan_seconds = run(command(
                tool, "run", "mem.plan", clean, module_path
            ), planned)
            place_seconds = run(command(
                tool, "run", "c.place", planned, module_path,
                json.dumps("static")
            ), placed)
            emit_seconds = run(command(
                tool, "emit", "c.source", placed, module_path,
                json.dumps("weights")
            ), source)

            clean_text = clean.read_text(errors="strict")
            blocked_hash = digest(blocked)
            rows.append({
                "model": model_name,
                "revision": revision,
                "factors": factor_arg,
                "budget": budget,
                "changed": int(blocked_hash != canonical_hash),
                "accumulators": len(ACCUMULATOR.findall(clean_text)),
                "canonical_ir_bytes": canonical_bytes,
                "blocked_ir_bytes": blocked.stat().st_size,
                "clean_ir_bytes": clean.stat().st_size,
                "c_bytes": source.stat().st_size,
                "canonical_ir_sha256": canonical_hash,
                "blocked_ir_sha256": blocked_hash,
                "c_sha256": digest(source),
                "block_seconds": f"{block_seconds:.6f}",
                "clean_seconds": f"{clean_seconds:.6f}",
                "plan_seconds": f"{plan_seconds:.6f}",
                "place_seconds": f"{place_seconds:.6f}",
                "emit_seconds": f"{emit_seconds:.6f}",
            })

    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


if __name__ == "__main__":
    main()
