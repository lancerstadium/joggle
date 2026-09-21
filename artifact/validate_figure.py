#!/usr/bin/env python3
"""Validate non-reactive figure CSVs before statistical analysis."""

from __future__ import annotations

import argparse
import csv
from collections import Counter, defaultdict
from pathlib import Path


FAMILIES = {"definition", "analysis", "rewrite", "conversion", "emission", "vertical"}
DEMOS = {0, 1, 2, 4}


def boolean(row: dict[str, str], field: str, line: int) -> bool:
    value = row[field].lower()
    if value not in {"true", "false"}:
        raise SystemExit(f"line {line}: {field} must be true or false")
    return value == "true"


def unsigned(row: dict[str, str], field: str, line: int, *, empty: bool = False) -> int | None:
    if empty and row[field] == "":
        return None
    try:
        value = int(row[field])
    except ValueError as error:
        raise SystemExit(f"line {line}: {field} is not an integer") from error
    if value < 0:
        raise SystemExit(f"line {line}: {field} is negative")
    return value


def real(row: dict[str, str], field: str, line: int, *, empty: bool = False) -> float | None:
    if empty and row[field] == "":
        return None
    try:
        value = float(row[field])
    except ValueError as error:
        raise SystemExit(f"line {line}: {field} is not numeric") from error
    if value < 0:
        raise SystemExit(f"line {line}: {field} is negative")
    return value


def load(path: Path, template: Path) -> list[dict[str, str]]:
    with template.open(newline="", encoding="utf-8") as stream:
        expected = next(csv.reader(stream))
    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        actual = reader.fieldnames or []
        if actual != expected:
            raise SystemExit(
                f"columns differ from {template.name}:\n"
                f"expected {','.join(expected)}\nactual   {','.join(actual)}"
            )
        rows = list(reader)
    if not rows:
        raise SystemExit("CSV contains no observations")
    return rows


def unique(rows: list[dict[str, str]], fields: tuple[str, ...]) -> None:
    seen: set[tuple[str, ...]] = set()
    for line, row in enumerate(rows, start=2):
        key = tuple(row[field] for field in fields)
        if key in seen:
            raise SystemExit(f"line {line}: duplicate key {key}")
        seen.add(key)


def extension(rows: list[dict[str, str]], partial: bool) -> None:
    unique(rows, ("record_kind", "model", "system", "task", "demo_count", "seed", "sample_index"))
    samples: dict[tuple[str, str, str, int], list[int]] = defaultdict(list)
    references: Counter[tuple[str, str, str, int]] = Counter()
    task_family: dict[str, str] = {}
    for line, row in enumerate(rows, start=2):
        if row["record_kind"] not in {"sample", "reference"}:
            raise SystemExit(f"line {line}: unknown record_kind")
        if row["family"] not in FAMILIES:
            raise SystemExit(f"line {line}: unknown family {row['family']}")
        if row["task"] in task_family and task_family[row["task"]] != row["family"]:
            raise SystemExit(f"line {line}: task changes family")
        task_family[row["task"]] = row["family"]
        demos = unsigned(row, "demo_count", line)
        if demos not in DEMOS:
            raise SystemExit(f"line {line}: demo_count must be 0, 1, 2, or 4")
        unsigned(row, "seed", line)
        target_tokens = unsigned(row, "target_tokens", line)
        if target_tokens == 0:
            raise SystemExit(f"line {line}: target_tokens must be positive")
        unsigned(row, "context_tokens", line)
        key = (row["model"], row["system"], row["task"], demos)
        if row["record_kind"] == "sample":
            index = unsigned(row, "sample_index", line)
            outcomes = [boolean(row, field, line)
                        for field in ("parsed", "typed", "built", "passed")]
            if outcomes != sorted(outcomes, reverse=True):
                raise SystemExit(f"line {line}: completion phases are inconsistent")
            samples[key].append(index)
        else:
            if row["sample_index"]:
                raise SystemExit(f"line {line}: reference row has sample_index")
            real(row, "nll", line)
            references[key] += 1
    if partial:
        return
    families = Counter(task_family.values())
    if len(task_family) != 24 or families != Counter({family: 4 for family in FAMILIES}):
        raise SystemExit(f"expected 24 tasks, four per family; found {dict(families)}")
    if set(samples) != set(references):
        missing = sorted(set(samples) - set(references))
        extra = sorted(set(references) - set(samples))
        raise SystemExit(f"sample/reference conditions differ; missing={missing[:2]} extra={extra[:2]}")
    for key, indexes in samples.items():
        if sorted(indexes) != list(range(50)):
            raise SystemExit(f"condition {key}: expected sample indexes 0..49")
        if references[key] != 1:
            raise SystemExit(f"condition {key}: expected one reference row")


def footprint(rows: list[dict[str, str]], partial: bool) -> None:
    unique(rows, ("system", "system_revision", "task"))
    counts = ("source_files", "source_added", "source_deleted", "test_files",
              "test_added", "test_deleted", "zones", "registrations", "fanout",
              "cross_mod_edges")
    task_family: dict[str, str] = {}
    task_systems: dict[str, set[str]] = defaultdict(set)
    for line, row in enumerate(rows, start=2):
        if row["family"] not in FAMILIES:
            raise SystemExit(f"line {line}: unknown family {row['family']}")
        task_family[row["task"]] = row["family"]
        task_systems[row["task"]].add(row["system"])
        for field in counts:
            unsigned(row, field, line)
        passed = boolean(row, "oracle_passed", line)
        if not passed and not partial:
            raise SystemExit(f"line {line}: footprint patch failed its oracle")
    if partial:
        return
    families = Counter(task_family.values())
    if len(task_family) != 12 or families != Counter({family: 2 for family in FAMILIES}):
        raise SystemExit(f"expected 12 tasks, two per family; found {dict(families)}")
    systems = {row["system"] for row in rows}
    for task, observed in task_systems.items():
        if observed != systems:
            raise SystemExit(f"task {task}: incomplete systems {sorted(observed)}")


def operators(rows: list[dict[str, str]]) -> None:
    unique(rows, ("operator", "shape", "dtype", "system", "system_revision",
                  "variant", "iteration", "seed"))
    for line, row in enumerate(rows, start=2):
        for field in ("iteration", "prepare_ns", "latency_ns", "code_bytes", "seed"):
            unsigned(row, field, line)
        real(row, "max_abs_error", line)
        boolean(row, "correct", line)


def models(rows: list[dict[str, str]]) -> None:
    unique(rows, ("model", "model_hash", "system", "system_revision", "variant",
                  "iteration", "seed"))
    for line, row in enumerate(rows, start=2):
        supported = boolean(row, "supported", line)
        boolean(row, "correct", line)
        unsigned(row, "iteration", line)
        unsigned(row, "seed", line)
        for field in ("prepare_ns", "latency_ns", "peak_bytes", "artifact_bytes"):
            unsigned(row, field, line, empty=not supported)
        real(row, "max_abs_error", line, empty=not supported)
        if not supported and not row["reason"]:
            raise SystemExit(f"line {line}: unsupported row requires reason")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("figure", choices=("4", "5", "8", "9"))
    parser.add_argument("csv", type=Path)
    parser.add_argument("--allow-partial", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    names = {"4": "figure-04-extension.csv", "5": "figure-05-footprint.csv",
             "8": "figure-08-operators.csv", "9": "figure-09-models.csv"}
    rows = load(args.csv, root / "templates" / names[args.figure])
    if args.figure == "4":
        extension(rows, args.allow_partial)
    elif args.figure == "5":
        footprint(rows, args.allow_partial)
    elif args.figure == "8":
        operators(rows)
    else:
        models(rows)
    print(f"validated {len(rows)} rows for Figure {args.figure}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
