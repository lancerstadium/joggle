#!/usr/bin/env python3
"""Validate the reactive-update CSV before analysis or plotting."""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from pathlib import Path


POLICIES = {"full", "suffix", "reactive", "whole-mod", "no-plan-cache"}
EDIT_CLASSES = {"operation_metadata"}
EDIT_SCOPES = {"affected", "unrelated"}
UNSIGNED = {
    "total_ops",
    "affected_ops",
    "fanout",
    "stages",
    "iteration",
    "wall_ns",
    "select_ns",
    "evaluate_ns",
    "verify_ns",
    "executed_stages",
    "reused_stages",
    "observed_ops",
    "observed_values",
    "changed_functions",
    "evaluated_ops",
    "plan_compiles",
    "plan_hits",
    "seed",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument(
        "--allow-partial",
        action="store_true",
        help="accept a subset of the five policies",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    with args.csv.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        rows = list(reader)
        fields = set(reader.fieldnames or [])
    required = {
        "system",
        "system_revision",
        "subject",
        "subject_hash",
        "edit_class",
        "edit_scope",
        "edit_site",
        "policy",
        "iteration",
        "output_digest",
        "correct",
    } | UNSIGNED
    missing = sorted(required - fields)
    if missing:
        raise SystemExit(f"missing columns: {', '.join(missing)}")
    if not rows:
        raise SystemExit("CSV contains no measurements")

    seen: set[tuple[str, ...]] = set()
    groups: dict[tuple[str, ...], list[dict[str, str]]] = defaultdict(list)
    for line, row in enumerate(rows, start=2):
        for name in UNSIGNED:
            try:
                value = int(row[name])
            except ValueError as error:
                raise SystemExit(f"line {line}: {name} is not an integer") from error
            if value < 0:
                raise SystemExit(f"line {line}: {name} is negative")
        if row["policy"] not in POLICIES:
            raise SystemExit(f"line {line}: unknown policy {row['policy']}")
        if row["edit_class"] not in EDIT_CLASSES:
            raise SystemExit(f"line {line}: unknown edit class {row['edit_class']}")
        if row["edit_scope"] not in EDIT_SCOPES:
            raise SystemExit(f"line {line}: unknown edit scope {row['edit_scope']}")
        if row["correct"] != "true":
            raise SystemExit(f"line {line}: correctness gate failed")
        identity = (
            row["system"],
            row["system_revision"],
            row["subject"],
            row["subject_hash"],
            row["edit_class"],
            row["edit_scope"],
            row["edit_site"],
            row["stages"],
            row["policy"],
            row["cache_state"],
            row["iteration"],
            row["seed"],
        )
        if identity in seen:
            raise SystemExit(f"line {line}: duplicate primary key")
        seen.add(identity)
        comparison = (
            row["system"],
            row["system_revision"],
            row["subject"],
            row["subject_hash"],
            row["edit_class"],
            row["edit_scope"],
            row["edit_site"],
            row["stages"],
            row["cache_state"],
            row["iteration"],
            row["seed"],
        )
        groups[comparison].append(row)

    for key, samples in groups.items():
        policies = {row["policy"] for row in samples}
        if not args.allow_partial and policies != POLICIES:
            absent = ", ".join(sorted(POLICIES - policies))
            raise SystemExit(f"group {key}: missing policies: {absent}")
        digests = {row["output_digest"] for row in samples}
        if len(digests) != 1:
            raise SystemExit(f"group {key}: policies produced different graphs")

    print(f"validated {len(rows)} rows in {len(groups)} comparison groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
