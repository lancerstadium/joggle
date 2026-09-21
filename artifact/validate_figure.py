#!/usr/bin/env python3
"""Validate the non-update CSVs consumed by Figures 4, 5, and 7."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import re
from collections import Counter, defaultdict
from pathlib import Path

FAMILIES = {"definition", "analysis", "rewrite", "conversion", "emission", "vertical"}
SYSTEMS = {"Joggle", "MLIR", "xDSL"}
DEMOS = {0, 2}
SHA256 = re.compile(r"^[0-9a-f]{64}$")


def boolean(row: dict[str, str], field: str, line: int) -> bool:
    if row[field] not in {"true", "false"}:
        raise SystemExit(f"line {line}: {field} must be true or false")
    return row[field] == "true"


def unsigned(row: dict[str, str], field: str, line: int) -> int:
    try:
        value = int(row[field])
    except ValueError as error:
        raise SystemExit(f"line {line}: {field} is not an integer") from error
    if value < 0:
        raise SystemExit(f"line {line}: {field} is negative")
    return value


def real(row: dict[str, str], field: str, line: int) -> float:
    try:
        value = float(row[field])
    except ValueError as error:
        raise SystemExit(f"line {line}: {field} is not numeric") from error
    if not math.isfinite(value) or value < 0:
        raise SystemExit(f"line {line}: {field} must be finite and non-negative")
    return value


def digest(value: str, line: int, field: str) -> None:
    if not SHA256.fullmatch(value):
        raise SystemExit(f"line {line}: {field} must be a lowercase SHA-256")


def load(path: Path, template: Path) -> list[dict[str, str]]:
    with template.open(newline="", encoding="utf-8") as stream:
        expected = next(csv.reader(stream))
    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != expected:
            raise SystemExit(f"columns differ from {template.name}")
        rows = list(reader)
    if not rows:
        raise SystemExit("CSV contains no observations")
    return rows


def require_unique(rows: list[dict[str, str]], fields: tuple[str, ...]) -> None:
    seen = set()
    for line, row in enumerate(rows, start=2):
        key = tuple(row[field] for field in fields)
        if key in seen:
            raise SystemExit(f"line {line}: duplicate key {key}")
        seen.add(key)


def extension(rows: list[dict[str, str]], partial: bool, tasks: dict[str, str], spec_hash: str) -> None:
    require_unique(rows, ("model", "system", "task", "demo_count", "run", "seed"))
    groups: dict[tuple[str, str, str, int], list[dict[str, str]]] = defaultdict(list)
    condition_systems: dict[tuple[str, str, int], set[str]] = defaultdict(set)
    controls: dict[tuple[str, str, int, int], tuple[str, ...]] = {}
    for line, row in enumerate(rows, start=2):
        if row["system"] not in SYSTEMS or tasks.get(row["task"]) != row["family"]:
            raise SystemExit(f"line {line}: system or task differs from the frozen contract")
        if not row["model"] or not row["model_revision"] or not row["system_revision"]:
            raise SystemExit(f"line {line}: model or revision is empty")
        demos = unsigned(row, "demo_count", line)
        demo_ids = row["demo_ids"].split(";") if row["demo_ids"] else []
        if demos not in DEMOS or len(demo_ids) != demos or len(set(demo_ids)) != demos:
            raise SystemExit(f"line {line}: invalid demonstrations")
        run = unsigned(row, "run", line)
        if run >= 10:
            raise SystemExit(f"line {line}: run must be in [0, 9]")
        unsigned(row, "seed", line)
        for field in ("wall_ms", "prompt_tokens", "completion_tokens", "tool_calls",
                      "edit_attempts", "files_touched"):
            unsigned(row, field, line)
        if unsigned(row, "budget_actions", line) != 30:
            raise SystemExit(f"line {line}: action budget differs from the contract")
        if unsigned(row, "budget_tokens", line) != 32000:
            raise SystemExit(f"line {line}: token budget differs from the contract")
        if row["task_spec_sha256"] != spec_hash:
            raise SystemExit(f"line {line}: contract hash differs")
        for field in ("api_card_sha256", "trajectory_sha256", "patch_sha256"):
            digest(row[field], line, field)
        reference_tokens = unsigned(row, "reference_tokens", line)
        if reference_tokens == 0:
            raise SystemExit(f"line {line}: reference token count is zero")
        real(row, "reference_nll", line)
        phases = [boolean(row, field, line) for field in ("parsed", "typed", "built", "passed")]
        if phases != sorted(phases, reverse=True):
            raise SystemExit(f"line {line}: inconsistent oracle phases")
        allowed_stops = {"success", "parse", "type", "build", "semantic", "budget", "agent_error"}
        if row["stop_reason"] not in allowed_stops:
            raise SystemExit(f"line {line}: invalid stop reason")
        if phases[-1] != (row["stop_reason"] == "success"):
            raise SystemExit(f"line {line}: success and stop reason disagree")
        groups[(row["model"], row["system"], row["task"], demos)].append(row)
        condition = (row["model"], row["task"], demos)
        condition_systems[condition].add(row["system"])
        control_key = (*condition, run)
        control = (row["demo_ids"], row["seed"], row["budget_actions"], row["budget_tokens"])
        if control_key in controls and controls[control_key] != control:
            raise SystemExit(f"line {line}: paired systems use different sampling controls")
        controls[control_key] = control
    if partial:
        return
    if set(row["task"] for row in rows) != set(tasks):
        raise SystemExit("Figure 4 task population is incomplete")
    if len({row["model"] for row in rows}) != 2:
        raise SystemExit("Figure 4 requires two models")
    if len(condition_systems) != 2 * 24 * 2 or any(
        systems != SYSTEMS for systems in condition_systems.values()
    ):
        raise SystemExit("Figure 4 system pairing is incomplete")
    for key, group in groups.items():
        if len(group) != 10 or {int(row["run"]) for row in group} != set(range(10)):
            raise SystemExit(f"{key}: expected ten complete agent trajectories")
    if len(rows) != 2880:
        raise SystemExit(f"Figure 4 requires 2880 trajectories, found {len(rows)}")


def footprint(rows: list[dict[str, str]], partial: bool, tasks: dict[str, str]) -> None:
    require_unique(rows, ("system", "system_revision", "task"))
    observed: dict[str, set[str]] = defaultdict(set)
    fields = ("source_files", "source_added", "source_deleted", "test_files",
              "test_added", "test_deleted", "zones", "registrations", "fanout",
              "cross_zone_edges")
    for line, row in enumerate(rows, start=2):
        if row["system"] not in SYSTEMS or tasks.get(row["task"]) != row["family"]:
            raise SystemExit(f"line {line}: system or task differs from the frozen contract")
        for field in fields:
            unsigned(row, field, line)
        if not boolean(row, "oracle_passed", line) and not partial:
            raise SystemExit(f"line {line}: patch failed its oracle")
        observed[row["task"]].add(row["system"])
    if not partial and (set(observed) != set(tasks) or any(value != SYSTEMS for value in observed.values())):
        raise SystemExit("Figure 5 paired population is incomplete")


def performance(
    rows: list[dict[str, str]], partial: bool, spec: dict[str, object], spec_hash: str
) -> None:
    require_unique(rows, ("subject_kind", "subject", "variant", "iteration", "seed"))
    measurement = spec["measurement"]
    variants = {row["id"]: row for row in spec["variants"]}
    operators = {row["id"]: row for row in spec["operator_cases"]}
    models = {row["id"]: row for row in spec["model_cases"]}
    observed: dict[tuple[str, str], set[str]] = defaultdict(set)
    counts: Counter[tuple[str, str, str]] = Counter()
    supported_pairs: dict[tuple[str, str, str], bool] = {}
    pairing: dict[tuple[str, str, int], tuple[str, int]] = {}
    for line, row in enumerate(rows, start=2):
        kind, subject, variant = row["subject_kind"], row["subject"], row["variant"]
        subjects = operators if kind == "operator" else models if kind == "model" else {}
        if subject not in subjects or variant not in variants:
            raise SystemExit(f"line {line}: unknown subject or variant")
        case = subjects[subject]
        if row["system"] != variants[variant]["system"] or not row["system_revision"]:
            raise SystemExit(f"line {line}: system identity differs from manifest")
        if kind == "operator" and (
            row["family"] != case["family"] or row["subject_hash"] != spec_hash
        ):
            raise SystemExit(f"line {line}: operator family differs")
        if kind == "model" and (row["family"] or row["subject_hash"] != case["sha256"]):
            raise SystemExit(f"line {line}: model identity differs")
        observed[(kind, subject)].add(variant)
        supported = boolean(row, "supported", line)
        pair_id = (kind, subject, variant)
        if pair_id in supported_pairs and supported_pairs[pair_id] != supported:
            raise SystemExit(f"line {line}: support status changes within a pair")
        supported_pairs[pair_id] = supported
        counts[(kind, subject, variant)] += 1
        if not supported:
            if not row["reason"] or any(row[field] for field in (
                "iteration", "calls_per_sample", "latency_ns", "max_abs_error",
                "max_rel_error", "output_digest", "correct"
            )):
                raise SystemExit(f"line {line}: malformed unsupported row")
            continue
        iteration = unsigned(row, "iteration", line)
        if row["reason"] or unsigned(row, "latency_ns", line) == 0:
            raise SystemExit(f"line {line}: malformed timing row")
        if unsigned(row, "calls_per_sample", line) != measurement["execution_batches"][subject]:
            raise SystemExit(f"line {line}: batch size differs from manifest")
        real(row, "max_abs_error", line); real(row, "max_rel_error", line)
        digest(row["input_digest"], line, "input_digest")
        digest(row["output_digest"], line, "output_digest")
        if not boolean(row, "correct", line):
            raise SystemExit(f"line {line}: incorrect execution")
        pair = (row["input_digest"], unsigned(row, "seed", line))
        key = (kind, subject, iteration)
        if key in pairing and pairing[key] != pair:
            raise SystemExit(f"line {line}: variants use different input or seed")
        pairing[key] = pair
    if partial:
        return
    expected_variants = set(variants)
    expected_subjects = {*(('operator', key) for key in operators), *(('model', key) for key in models)}
    if set(observed) != expected_subjects:
        raise SystemExit("Figure 7 subject population is incomplete")
    for key, found in observed.items():
        if found != expected_variants:
            raise SystemExit(f"{key}: incomplete variants")
        for variant in expected_variants:
            pair_id = (*key, variant)
            expected_count = (measurement["execution_iterations"]
                              if supported_pairs[pair_id] else 1)
            if counts[pair_id] != expected_count:
                raise SystemExit(f"{key}/{variant}: incomplete measurements")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("figure", choices=("4", "5", "7"))
    parser.add_argument("csv", type=Path)
    parser.add_argument("--allow-partial", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    names = {"4": "figure-04-extension.csv", "5": "figure-05-footprint.csv",
             "7": "figure-07-performance.csv"}
    rows = load(args.csv, root / "templates" / names[args.figure])
    if args.figure in {"4", "5"}:
        manifest = list(csv.DictReader((root / "manifests/extension-tasks.csv").open(newline="", encoding="utf-8")))
        tasks = {row["task_id"]: row["family"] for row in manifest
                 if args.figure == "4" or row["footprint"] == "true"}
        if args.figure == "4":
            spec_path = root / "manifests/extension-specs.json"
            extension(rows, args.allow_partial, tasks, hashlib.sha256(spec_path.read_bytes()).hexdigest())
        else:
            footprint(rows, args.allow_partial, tasks)
    else:
        benchmark_path = root / "manifests/benchmark-cases.json"
        performance(rows, args.allow_partial, json.loads(benchmark_path.read_text()),
                    hashlib.sha256(benchmark_path.read_bytes()).hexdigest())
    print(f"validated {len(rows)} rows for Figure {args.figure}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
