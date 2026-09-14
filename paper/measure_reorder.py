#!/usr/bin/env python3
"""Build matched baseline, reorder, and affine-canonical C variants."""

from __future__ import annotations

import argparse
from collections import Counter
import csv
import hashlib
import re
import shutil
import subprocess
import time
from pathlib import Path


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def loop_headers(path: Path) -> Counter[str]:
    return Counter(
        line.strip() for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip().startswith("for ") and line.strip().endswith("{")
    )


def run(command: list[str], output: Path) -> float:
    start = time.perf_counter()
    with output.open("wb") as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.PIPE)
    elapsed = time.perf_counter() - start
    if result.returncode != 0:
        detail = result.stderr.decode(errors="replace")
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"{detail}"
        )
    return elapsed


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


def invocation(tool: Path, action: str, functions: list[str], source: Path,
               paths: list[Path], arguments: list[str] | None = None
               ) -> list[str]:
    command = [str(tool), action, *functions, str(source)]
    for argument in arguments or []:
        command += ["--arg", argument]
    for path in paths:
        command += ["-M", str(path)]
    return command


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--modules", type=Path, required=True)
    parser.add_argument("--examples", type=Path, required=True)
    parser.add_argument("--model", action="append", required=True,
                        metavar="NAME=CANONICAL.jog")
    parser.add_argument("--out-dir", type=Path,
                        default=Path("build-study/reorder"))
    parser.add_argument("--output", type=Path,
                        default=Path("paper/data/reorder-pilot.csv"))
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
    except ValueError as error:
        parser.error(str(error))

    revision = subprocess.run(
        ["git", "rev-parse", "HEAD"], check=True, text=True,
        stdout=subprocess.PIPE
    ).stdout.strip()
    root = args.out_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    rows: list[dict[str, object]] = []

    for model_name, canonical in selected_models:
        canonical_hash = digest(canonical)
        for variant in ["baseline", "reorder", "canon"]:
            directory = root / model_name / variant
            directory.mkdir(parents=True, exist_ok=True)
            transformed = directory / "transformed.jog"
            clean = directory / "clean.jog"
            planned = directory / "planned.jog"
            placed = directory / "placed.jog"
            source = directory / "model.c"

            if variant == "baseline":
                shutil.copyfile(canonical, transformed)
                transform_seconds = 0.0
            else:
                transform_seconds = run(invocation(
                    tool, "run", ["spatial.apply"], canonical,
                    [example_path, module_path]
                ), transformed)

            normalized = clean
            canon_seconds = 0.0
            if variant == "canon":
                normalized = directory / "normalized.jog"
            clean_seconds = run(invocation(
                tool, "run", ["bounds.fold", "opt.fold", "opt.basic"],
                transformed, [module_path]
            ), normalized)
            if variant == "canon":
                canon_seconds = run(invocation(
                    tool, "run", ["tile.canon"], normalized, [module_path]
                ), clean)
            plan_seconds = run(invocation(
                tool, "run", ["mem.plan"], clean, [module_path]
            ), planned)
            place_seconds = run(invocation(
                tool, "run", ["c.place"], planned, [module_path],
                ['"static"']
            ), placed)
            emit_seconds = run(invocation(
                tool, "emit", ["c.source"], placed, [module_path],
                ['"weights"']
            ), source)

            rows.append({
                "model": model_name,
                "revision": revision,
                "variant": variant,
                "changed": int(digest(transformed) != canonical_hash),
                "loops_changed": sum(
                    (loop_headers(canonical) - loop_headers(transformed)).values()
                ),
                "canonical_ir_bytes": canonical.stat().st_size,
                "transformed_ir_bytes": transformed.stat().st_size,
                "clean_ir_bytes": clean.stat().st_size,
                "c_bytes": source.stat().st_size,
                "canonical_ir_sha256": canonical_hash,
                "transformed_ir_sha256": digest(transformed),
                "clean_ir_sha256": digest(clean),
                "c_sha256": digest(source),
                "transform_seconds": f"{transform_seconds:.6f}",
                "clean_seconds": f"{clean_seconds:.6f}",
                "canon_seconds": f"{canon_seconds:.6f}",
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
