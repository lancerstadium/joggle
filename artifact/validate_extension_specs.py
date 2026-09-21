#!/usr/bin/env python3
"""Validate the system-neutral task contract shared by Figures 4 and 5."""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path
from typing import Any


FAMILIES = {"definition", "analysis", "rewrite", "conversion", "emission", "vertical"}
ROLES = {"definition", "analysis", "rewrite", "conversion", "emission"}
PHASE_ORDER = {"parse": 0, "type": 1, "build": 2, "semantic": 3}
COMPARISONS = {"exact-json", "canonical-graph", "numerical", "compile-and-run"}
TASK_FIELDS = {
    "id", "family", "title", "footprint", "required_roles", "contract",
    "positive_cases", "negative_cases", "oracle",
}
CASE_FIELDS = {"id", "input", "expect"}


def fail(where: str, message: str) -> None:
    raise SystemExit(f"{where}: {message}")


def exact_fields(value: dict[str, Any], fields: set[str], where: str) -> None:
    missing = fields - value.keys()
    extra = value.keys() - fields
    if missing or extra:
        fail(where, f"field mismatch; missing={sorted(missing)} extra={sorted(extra)}")


def nonempty_text(value: Any, where: str, minimum: int = 1) -> None:
    if not isinstance(value, str) or len(value.strip()) < minimum:
        fail(where, f"expected at least {minimum} non-space characters")


def validate_case(case: Any, where: str, phases: list[str], negative: bool) -> None:
    if not isinstance(case, dict):
        fail(where, "case must be an object")
    exact_fields(case, CASE_FIELDS, where)
    nonempty_text(case["id"], f"{where}.id")
    if not isinstance(case["input"], dict) or not case["input"]:
        fail(f"{where}.input", "must be a non-empty object")
    if not isinstance(case["expect"], dict) or not case["expect"]:
        fail(f"{where}.expect", "must be a non-empty object")
    expected = case["expect"]
    if negative and "error" in expected:
        if expected.get("phase") not in phases:
            fail(f"{where}.expect.phase", "must name a phase executed by this oracle")
        nonempty_text(expected["error"], f"{where}.expect.error")


def validate_spec(spec: Any, manifest: list[dict[str, str]]) -> None:
    if not isinstance(spec, dict):
        fail("root", "must be an object")
    exact_fields(spec, {"schema_version", "comparison_policy", "tasks"}, "root")
    if spec["schema_version"] != 1:
        fail("schema_version", "must equal 1")

    policy = spec["comparison_policy"]
    if not isinstance(policy, dict):
        fail("comparison_policy", "must be an object")
    exact_fields(policy, {"float_rtol", "float_atol", "nan_equal", "structural_form"},
                 "comparison_policy")
    for field in ("float_rtol", "float_atol"):
        value = policy[field]
        if isinstance(value, bool) or not isinstance(value, (int, float)) or value < 0:
            fail(f"comparison_policy.{field}", "must be a non-negative number")
    if not isinstance(policy["nan_equal"], bool):
        fail("comparison_policy.nan_equal", "must be boolean")
    nonempty_text(policy["structural_form"], "comparison_policy.structural_form")

    tasks = spec["tasks"]
    if not isinstance(tasks, list) or len(tasks) != 24:
        fail("tasks", "must contain exactly 24 tasks")
    frozen = {row["task_id"]: row for row in manifest}
    if len(frozen) != 24:
        fail("extension-tasks.csv", "must contain 24 unique task IDs")

    observed: dict[str, dict[str, Any]] = {}
    family_counts: Counter[str] = Counter()
    footprint_counts: Counter[str] = Counter()
    for index, task in enumerate(tasks):
        where = f"tasks[{index}]"
        if not isinstance(task, dict):
            fail(where, "task must be an object")
        exact_fields(task, TASK_FIELDS, where)
        task_id = task["id"]
        nonempty_text(task_id, f"{where}.id")
        if task_id in observed:
            fail(f"{where}.id", f"duplicate task {task_id}")
        if task_id not in frozen:
            fail(f"{where}.id", "not present in extension-tasks.csv")
        observed[task_id] = task
        row = frozen[task_id]

        if task["family"] not in FAMILIES or task["family"] != row["family"]:
            fail(f"{where}.family", "differs from frozen manifest")
        family_counts[task["family"]] += 1
        if task["title"] != row["title"]:
            fail(f"{where}.title", "differs from frozen manifest")
        if not isinstance(task["footprint"], bool):
            fail(f"{where}.footprint", "must be boolean")
        expected_footprint = row["footprint"] == "true"
        if task["footprint"] != expected_footprint:
            fail(f"{where}.footprint", "differs from frozen manifest")
        if task["footprint"]:
            footprint_counts[task["family"]] += 1

        roles = task["required_roles"]
        if (not isinstance(roles, list) or not roles or len(set(roles)) != len(roles)
                or not set(roles) <= ROLES):
            fail(f"{where}.required_roles", "must contain unique known roles")
        if roles != row["required_roles"].split("+"):
            fail(f"{where}.required_roles", "differs from frozen manifest order")
        nonempty_text(task["contract"], f"{where}.contract", 20)

        oracle = task["oracle"]
        if not isinstance(oracle, dict):
            fail(f"{where}.oracle", "must be an object")
        exact_fields(oracle, {"phases", "comparison"}, f"{where}.oracle")
        phases = oracle["phases"]
        if (not isinstance(phases, list) or not phases
                or any(phase not in PHASE_ORDER for phase in phases)
                or phases != sorted(set(phases), key=PHASE_ORDER.get)):
            fail(f"{where}.oracle.phases", "must be unique and ordered parse/type/build/semantic")
        if oracle["comparison"] not in COMPARISONS:
            fail(f"{where}.oracle.comparison", "unknown comparison")

        positive = task["positive_cases"]
        negative = task["negative_cases"]
        if not isinstance(positive, list) or len(positive) < 2:
            fail(f"{where}.positive_cases", "requires at least two cases")
        if not isinstance(negative, list):
            fail(f"{where}.negative_cases", "must be an array")
        case_ids: set[str] = set()
        for kind, cases in (("positive_cases", positive), ("negative_cases", negative)):
            for case_index, case in enumerate(cases):
                case_where = f"{where}.{kind}[{case_index}]"
                validate_case(case, case_where, phases, kind == "negative_cases")
                if case["id"] in case_ids:
                    fail(f"{case_where}.id", "duplicate within task")
                case_ids.add(case["id"])

    if set(observed) != set(frozen):
        fail("tasks", "task IDs differ from extension-tasks.csv")
    if family_counts != Counter({family: 4 for family in FAMILIES}):
        fail("tasks", f"expected four tasks per family; found {dict(family_counts)}")
    if footprint_counts != Counter({family: 2 for family in FAMILIES}):
        fail("tasks", f"expected two footprint tasks per family; found {dict(footprint_counts)}")


def main() -> int:
    parser = argparse.ArgumentParser()
    root = Path(__file__).resolve().parent
    parser.add_argument("spec", type=Path, nargs="?",
                        default=root / "manifests" / "extension-specs.json")
    parser.add_argument("--manifest", type=Path,
                        default=root / "manifests" / "extension-tasks.csv")
    args = parser.parse_args()
    with args.manifest.open(newline="", encoding="utf-8") as stream:
        manifest = list(csv.DictReader(stream))
    with args.spec.open(encoding="utf-8") as stream:
        spec = json.load(stream)
    validate_spec(spec, manifest)
    print(f"validated {len(spec['tasks'])} extension task specifications")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
