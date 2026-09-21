#!/usr/bin/env python3
"""Validate the cross-system update measurements consumed by Figure 6."""

from __future__ import annotations

import argparse
import csv
import re
from collections import defaultdict
from pathlib import Path

SHA256 = re.compile(r"^[0-9a-f]{64}$")
POLICIES = {"full", "update"}
SYSTEMS = {"Joggle", "MLIR", "xDSL"}
EDIT_CLASSES = {"operation_metadata", "value_type"}
EDIT_SCOPES = {"affected", "unrelated"}
EDIT_SITES = {"early", "middle", "late"}
ITERATIONS = set(range(100))


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

    groups: dict[tuple[str, ...], list[dict[str, str]]] = defaultdict(list)
    seen: set[tuple[str, ...]] = set()
    for line, row in enumerate(rows, start=2):
        if not row["system"] or not row["system_revision"] or not row["subject"]:
            raise SystemExit(f"line {line}: missing system or subject identity")
        if not SHA256.fullmatch(row["subject_hash"]):
            raise SystemExit(f"line {line}: invalid subject hash")
        for field in ("total_ops", "affected_ops", "iteration", "wall_ns",
                      "visited_ops", "executed_stages", "total_stages", "seed"):
            try:
                value = int(row[field])
            except ValueError as error:
                raise SystemExit(f"line {line}: {field} is not an integer") from error
            if value < 0:
                raise SystemExit(f"line {line}: {field} is negative")
        if int(row["wall_ns"]) == 0 or int(row["total_ops"]) == 0:
            raise SystemExit(f"line {line}: time and total_ops must be positive")
        if int(row["affected_ops"]) > int(row["total_ops"]):
            raise SystemExit(f"line {line}: affected_ops exceeds total_ops")
        if row["policy"] not in POLICIES:
            raise SystemExit(f"line {line}: policy must be full or update")
        if row["edit_class"] not in EDIT_CLASSES:
            raise SystemExit(f"line {line}: edit_class is outside the release matrix")
        if row["edit_scope"] not in EDIT_SCOPES:
            raise SystemExit(f"line {line}: edit_scope is outside the release matrix")
        if row["edit_site"] not in EDIT_SITES:
            raise SystemExit(f"line {line}: edit_site is outside the release matrix")
        if row["correct"] != "true" or not SHA256.fullmatch(row["output_digest"]):
            raise SystemExit(f"line {line}: correctness or digest gate failed")
        identity = (row["system"], row["system_revision"], row["subject"],
                    row["edit_class"], row["edit_scope"], row["edit_site"],
                    row["policy"], row["iteration"], row["seed"])
        if identity in seen:
            raise SystemExit(f"line {line}: duplicate primary key")
        seen.add(identity)
        comparison = identity[:6] + identity[7:]
        groups[comparison].append(row)

    for key, group in groups.items():
        policies = {row["policy"] for row in group}
        if policies != POLICIES:
            raise SystemExit(f"group {key}: expected full and update")
        if len({row["output_digest"] for row in group}) != 1:
            raise SystemExit(f"group {key}: policies produced different outputs")

    semantic_outputs: dict[tuple[str, ...], set[str]] = defaultdict(set)
    systems_by_subject: dict[str, set[str]] = defaultdict(set)
    revisions_by_system: dict[str, set[str]] = defaultdict(set)
    hashes_by_subject: dict[str, set[str]] = defaultdict(set)
    seeds: set[int] = set()
    for row in rows:
        key = (row["subject"], row["edit_class"], row["edit_scope"],
               row["edit_site"], row["iteration"], row["seed"])
        semantic_outputs[key].add(row["output_digest"])
        systems_by_subject[row["subject"]].add(row["system"])
        revisions_by_system[row["system"]].add(row["system_revision"])
        hashes_by_subject[row["subject"]].add(row["subject_hash"])
        seeds.add(int(row["seed"]))
    if any(len(digests) != 1 for digests in semantic_outputs.values()):
        raise SystemExit("systems produced different canonical outputs")
    if not args.allow_partial:
        if set(revisions_by_system) != SYSTEMS or any(
            len(revisions) != 1 for revisions in revisions_by_system.values()
        ):
            raise SystemExit("Figure 6 requires one pinned revision per system")
        if any(len(hashes) != 1 for hashes in hashes_by_subject.values()):
            raise SystemExit("Figure 6 subject hashes differ across systems")
        if len(seeds) != 1:
            raise SystemExit("Figure 6 requires one paired seed")
        if (
            len(systems_by_subject) != 15
            or any(systems != SYSTEMS for systems in systems_by_subject.values())
        ):
            raise SystemExit("Figure 6 requires 15 subjects paired across three systems")
        expected_conditions = {
            (system, subject, edit_class, edit_scope, edit_site, policy, iteration)
            for system in SYSTEMS
            for subject in systems_by_subject
            for edit_class in EDIT_CLASSES
            for edit_scope in EDIT_SCOPES
            for edit_site in EDIT_SITES
            for policy in POLICIES
            for iteration in ITERATIONS
        }
        observed_conditions = {
            (row["system"], row["subject"], row["edit_class"], row["edit_scope"],
             row["edit_site"], row["policy"], int(row["iteration"]))
            for row in rows
        }
        if observed_conditions != expected_conditions:
            missing = len(expected_conditions - observed_conditions)
            extra = len(observed_conditions - expected_conditions)
            raise SystemExit(
                f"Figure 6 matrix is incomplete: {missing} missing, {extra} unexpected cells"
            )
        if len(rows) != 108_000:
            raise SystemExit(f"Figure 6 requires 108000 rows, found {len(rows)}")

    print(f"validated {len(rows)} Figure 6 rows in {len(groups)} paired groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
