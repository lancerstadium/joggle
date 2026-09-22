#!/usr/bin/env python3
"""Validate the matched updates and production rebuilds consumed by Figure 6."""

from __future__ import annotations

import argparse
import csv
import re
from collections import defaultdict
from pathlib import Path

SHA256 = re.compile(r"^[0-9a-f]{64}$")
OUTPUT_DIGEST = re.compile(r"^(?:[0-9a-f]{16}|[0-9a-f]{64})$")
POLICIES = {"full", "update"}
SYSTEMS = {"Joggle", "MLIR", "xDSL"}
EDIT_CLASSES = {"operation_metadata", "value_type"}
EDIT_SCOPES = {"affected", "unrelated"}
EDIT_SITES = {"early", "middle", "late"}
ITERATIONS = set(range(100))
PRODUCTION_STAGES = (
    "decode", "infer_convert", "c_prepare", "scalar_lowering",
    "storage_plan", "storage_place", "c_emit",
)
PRODUCTION_ITERATIONS = set(range(10))


def unsigned(row: dict[str, str], field: str, line: int) -> int:
    try:
        value = int(row[field])
    except ValueError as error:
        raise SystemExit(f"line {line}: {field} is not an integer") from error
    if value < 0:
        raise SystemExit(f"line {line}: {field} is negative")
    return value


def common(row: dict[str, str], line: int) -> None:
    if not row["system"] or not row["system_revision"] or not row["subject"]:
        raise SystemExit(f"line {line}: missing system or subject identity")
    if not SHA256.fullmatch(row["subject_hash"]):
        raise SystemExit(f"line {line}: invalid subject hash")
    for field in (
        "total_ops", "affected_ops", "iteration", "wall_ns", "visited_ops",
        "executed_stages", "total_stages", "artifact_bytes", "seed",
    ):
        unsigned(row, field, line)
    if unsigned(row, "wall_ns", line) == 0 or unsigned(row, "total_ops", line) == 0:
        raise SystemExit(f"line {line}: time and total_ops must be positive")
    if unsigned(row, "affected_ops", line) > unsigned(row, "total_ops", line):
        raise SystemExit(f"line {line}: affected_ops exceeds total_ops")
    if row["correct"] != "true" or not OUTPUT_DIGEST.fullmatch(row["output_digest"]):
        raise SystemExit(f"line {line}: correctness or digest gate failed")


def validate_matched(rows: list[tuple[int, dict[str, str]]], partial: bool) -> None:
    groups: dict[tuple[str, ...], list[dict[str, str]]] = defaultdict(list)
    seen: set[tuple[str, ...]] = set()
    for line, row in rows:
        common(row, line)
        if len(row["output_digest"]) != 16:
            raise SystemExit(f"line {line}: matched output digest must be FNV-1a-64")
        if row["stage"] != "all" or unsigned(row, "artifact_bytes", line) != 0:
            raise SystemExit(f"line {line}: malformed matched-stage row")
        if row["policy"] not in POLICIES:
            raise SystemExit(f"line {line}: policy must be full or update")
        if row["edit_class"] not in EDIT_CLASSES:
            raise SystemExit(f"line {line}: edit_class is outside the release matrix")
        if row["edit_scope"] not in EDIT_SCOPES:
            raise SystemExit(f"line {line}: edit_scope is outside the release matrix")
        if row["edit_site"] not in EDIT_SITES:
            raise SystemExit(f"line {line}: edit_site is outside the release matrix")
        identity = (
            row["system"], row["system_revision"], row["subject"],
            row["edit_class"], row["edit_scope"], row["edit_site"],
            row["policy"], row["iteration"], row["seed"],
        )
        if identity in seen:
            raise SystemExit(f"line {line}: duplicate matched primary key")
        seen.add(identity)
        groups[identity[:6] + identity[7:]].append(row)

    for key, group in groups.items():
        if {row["policy"] for row in group} != POLICIES:
            raise SystemExit(f"group {key}: expected full and update")
        if len({row["output_digest"] for row in group}) != 1:
            raise SystemExit(f"group {key}: policies produced different outputs")

    semantic_outputs: dict[tuple[str, ...], set[str]] = defaultdict(set)
    systems_by_subject: dict[str, set[str]] = defaultdict(set)
    revisions_by_system: dict[str, set[str]] = defaultdict(set)
    hashes_by_subject: dict[str, set[str]] = defaultdict(set)
    seeds: set[int] = set()
    plain_rows = [row for _, row in rows]
    for row in plain_rows:
        key = (
            row["subject"], row["edit_class"], row["edit_scope"],
            row["edit_site"], row["iteration"], row["seed"],
        )
        semantic_outputs[key].add(row["output_digest"])
        systems_by_subject[row["subject"]].add(row["system"])
        revisions_by_system[row["system"]].add(row["system_revision"])
        hashes_by_subject[row["subject"]].add(row["subject_hash"])
        seeds.add(int(row["seed"]))
    if any(len(digests) != 1 for digests in semantic_outputs.values()):
        raise SystemExit("systems produced different canonical outputs")
    if partial:
        return
    if set(revisions_by_system) != SYSTEMS or any(
        len(revisions) != 1 for revisions in revisions_by_system.values()
    ):
        raise SystemExit("Figure 6 requires one pinned revision per system")
    if any(len(hashes) != 1 for hashes in hashes_by_subject.values()):
        raise SystemExit("Figure 6 subject hashes differ across systems")
    if len(seeds) != 1:
        raise SystemExit("Figure 6 requires one paired seed")
    if len(systems_by_subject) != 15 or any(
        systems != SYSTEMS for systems in systems_by_subject.values()
    ):
        raise SystemExit("Figure 6 requires 15 subjects paired across three systems")
    expected = {
        (system, subject, edit_class, scope, site, policy, iteration)
        for system in SYSTEMS for subject in systems_by_subject
        for edit_class in EDIT_CLASSES for scope in EDIT_SCOPES
        for site in EDIT_SITES for policy in POLICIES for iteration in ITERATIONS
    }
    observed = {
        (row["system"], row["subject"], row["edit_class"], row["edit_scope"],
         row["edit_site"], row["policy"], int(row["iteration"]))
        for row in plain_rows
    }
    if observed != expected or len(plain_rows) != 108_000:
        raise SystemExit(
            "Figure 6 matched matrix is incomplete: "
            f"{len(expected - observed)} missing, {len(observed - expected)} unexpected cells"
        )


def validate_production(rows: list[tuple[int, dict[str, str]]], partial: bool) -> None:
    seen: set[tuple[str, str, str]] = set()
    cells: dict[tuple[str, int], set[str]] = defaultdict(set)
    subjects: dict[str, set[str]] = defaultdict(set)
    for line, row in rows:
        common(row, line)
        if len(row["output_digest"]) != 64:
            raise SystemExit(f"line {line}: production output digest must be SHA-256")
        if row["system"] != "Joggle" or row["policy"] != "full":
            raise SystemExit(f"line {line}: production rows must be Joggle full rebuilds")
        if any(row[field] for field in ("edit_class", "edit_scope", "edit_site")):
            raise SystemExit(f"line {line}: production rows cannot carry edit coordinates")
        if row["stage"] not in PRODUCTION_STAGES:
            raise SystemExit(f"line {line}: unknown production stage")
        if unsigned(row, "affected_ops", line) != 0 or unsigned(row, "visited_ops", line) != 0:
            raise SystemExit(f"line {line}: production rows use stage time, not update counters")
        if unsigned(row, "executed_stages", line) != 1 or unsigned(
            row, "total_stages", line
        ) != len(PRODUCTION_STAGES):
            raise SystemExit(f"line {line}: production stage counters differ from the contract")
        key = (row["subject"], row["iteration"], row["stage"])
        if key in seen:
            raise SystemExit(f"line {line}: duplicate production primary key")
        seen.add(key)
        cells[(row["subject"], int(row["iteration"]))].add(row["stage"])
        subjects[row["subject"]].add(row["subject_hash"])
    if any(stages != set(PRODUCTION_STAGES) for stages in cells.values()):
        raise SystemExit("production rebuild has an incomplete stage sequence")
    if partial:
        return
    if len(subjects) != 15 or any(len(hashes) != 1 for hashes in subjects.values()):
        raise SystemExit("Figure 6 production panel requires 15 pinned subjects")
    expected = {(subject, iteration) for subject in subjects for iteration in PRODUCTION_ITERATIONS}
    if set(cells) != expected or len(rows) != 15 * 10 * len(PRODUCTION_STAGES):
        raise SystemExit("Figure 6 production rebuild matrix is incomplete")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument("--allow-partial", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    with (root / "templates/figure-06-update.csv").open(newline="", encoding="utf-8") as stream:
        expected = next(csv.reader(stream))
    with args.csv.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != expected:
            raise SystemExit("columns differ from figure-06-update.csv")
        rows = list(reader)
    if not rows:
        raise SystemExit("CSV contains no measurements")
    indexed = list(enumerate(rows, start=2))
    matched = [(line, row) for line, row in indexed if row["path"] == "matched"]
    production = [(line, row) for line, row in indexed if row["path"] == "production"]
    unknown = [
        (line, row["path"]) for line, row in indexed
        if row["path"] not in {"matched", "production"}
    ]
    if unknown:
        raise SystemExit(f"line {unknown[0][0]}: unknown Figure 6 path {unknown[0][1]!r}")
    if not args.allow_partial and (not matched or not production):
        raise SystemExit("Figure 6 requires matched and production paths")
    if matched:
        validate_matched(matched, args.allow_partial)
    if production:
        validate_production(production, args.allow_partial)
    if not matched and not production:
        raise SystemExit("Figure 6 contains no recognized rows")
    print(f"validated {len(matched)} matched rows and {len(production)} production rows")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
