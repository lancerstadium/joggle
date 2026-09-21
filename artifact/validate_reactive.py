#!/usr/bin/env python3
"""Validate the cross-system update measurements consumed by Figure 6."""

from __future__ import annotations

import argparse
import csv
import re
from collections import defaultdict
from pathlib import Path

SHA256 = re.compile(r"^[0-9a-f]{64}$")
EXTERNAL = {"full", "update"}
ABLATION = {"full", "reactive", "whole-mod", "no-plan-cache"}


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
        if row["track"] not in {"external", "ablation"}:
            raise SystemExit(f"line {line}: unknown track")
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
        if row["correct"] != "true" or not SHA256.fullmatch(row["output_digest"]):
            raise SystemExit(f"line {line}: correctness or digest gate failed")
        identity = (row["track"], row["system"], row["system_revision"], row["subject"],
                    row["edit_class"], row["edit_scope"], row["edit_site"],
                    row["policy"], row["iteration"], row["seed"])
        if identity in seen:
            raise SystemExit(f"line {line}: duplicate primary key")
        seen.add(identity)
        comparison = identity[:4] + identity[4:7] + identity[8:]
        groups[comparison].append(row)

    for key, group in groups.items():
        expected_policies = EXTERNAL if key[0] == "external" else ABLATION
        policies = {row["policy"] for row in group}
        if not args.allow_partial and policies != expected_policies:
            raise SystemExit(f"group {key}: expected {sorted(expected_policies)}, found {sorted(policies)}")
        if len({row["output_digest"] for row in group}) != 1:
            raise SystemExit(f"group {key}: policies produced different outputs")
    semantic_outputs: dict[tuple[str, ...], set[str]] = defaultdict(set)
    for row in rows:
        if row["track"] == "external":
            key = (row["subject"], row["edit_class"], row["edit_scope"],
                   row["edit_site"], row["iteration"], row["seed"])
            semantic_outputs[key].add(row["output_digest"])
    if any(len(digests) != 1 for digests in semantic_outputs.values()):
        raise SystemExit("external systems produced different canonical outputs")
    if not args.allow_partial:
        tracks = {row["track"] for row in rows}
        if tracks != {"external", "ablation"}:
            raise SystemExit("Figure 6 requires external and ablation tracks")
        external = [row for row in rows if row["track"] == "external"]
        if {row["system"] for row in external} != {"Joggle", "MLIR", "xDSL"}:
            raise SystemExit("external track requires Joggle, MLIR, and xDSL")
        systems_by_subject: dict[str, set[str]] = defaultdict(set)
        for row in external:
            systems_by_subject[row["subject"]].add(row["system"])
        if len(systems_by_subject) != 15 or any(
            systems != {"Joggle", "MLIR", "xDSL"}
            for systems in systems_by_subject.values()
        ):
            raise SystemExit("external track requires 15 subjects paired across systems")
        ablation_subjects = {row["subject"] for row in rows if row["track"] == "ablation"}
        if len(ablation_subjects) != 3 or any(
            row["system"] != "Joggle" for row in rows if row["track"] == "ablation"
        ):
            raise SystemExit("ablation track requires three Joggle subjects")
    print(f"validated {len(rows)} Figure 6 rows in {len(groups)} paired groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
