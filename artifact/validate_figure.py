#!/usr/bin/env python3
"""Validate non-reactive figure CSVs before statistical analysis."""

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
DEMOS = {0, 1, 2, 4}
SHA256 = re.compile(r"^[0-9a-f]{64}$")


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
    if not math.isfinite(value) or value < 0:
        raise SystemExit(f"line {line}: {field} must be finite and non-negative")
    return value


def digest(row: dict[str, str], field: str, line: int) -> str:
    value = row[field]
    if not SHA256.fullmatch(value):
        raise SystemExit(f"line {line}: {field} must be a lowercase SHA-256")
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


def extension(
    rows: list[dict[str, str]], partial: bool, expected_tasks: dict[str, str],
    expected_spec_hash: str,
) -> None:
    unique(rows, ("record_kind", "model", "system", "task", "demo_count", "seed", "sample_index"))
    samples: dict[tuple[str, str, str, int], list[int]] = defaultdict(list)
    references: Counter[tuple[str, str, str, int]] = Counter()
    task_family: dict[str, str] = {}
    audit: dict[tuple[str, str, int, str, int], tuple[str, str, str, str]] = {}
    condition_systems: dict[tuple[str, str, int], set[str]] = defaultdict(set)
    for line, row in enumerate(rows, start=2):
        if row["record_kind"] not in {"sample", "reference"}:
            raise SystemExit(f"line {line}: unknown record_kind")
        for field in ("model", "model_revision", "system", "system_revision", "task"):
            if not row[field].strip():
                raise SystemExit(f"line {line}: {field} is empty")
        if row["family"] not in FAMILIES:
            raise SystemExit(f"line {line}: unknown family {row['family']}")
        if expected_tasks.get(row["task"]) != row["family"]:
            raise SystemExit(f"line {line}: task is not in the frozen manifest")
        if row["task"] in task_family and task_family[row["task"]] != row["family"]:
            raise SystemExit(f"line {line}: task changes family")
        task_family[row["task"]] = row["family"]
        demos = unsigned(row, "demo_count", line)
        if demos not in DEMOS:
            raise SystemExit(f"line {line}: demo_count must be 0, 1, 2, or 4")
        demo_ids = row["demo_ids"].split(";") if row["demo_ids"] else []
        if len(demo_ids) != demos or len(set(demo_ids)) != len(demo_ids):
            raise SystemExit(f"line {line}: demo_ids must contain {demos} unique IDs")
        seed = unsigned(row, "seed", line)
        temperature = real(row, "temperature", line)
        top_p = real(row, "top_p", line)
        if temperature > 2:
            raise SystemExit(f"line {line}: temperature exceeds 2")
        if not 0 < top_p <= 1:
            raise SystemExit(f"line {line}: top_p must lie in (0, 1]")
        max_new_tokens = unsigned(row, "max_new_tokens", line)
        if max_new_tokens == 0:
            raise SystemExit(f"line {line}: max_new_tokens must be positive")
        target_tokens = unsigned(row, "target_tokens", line)
        if target_tokens == 0:
            raise SystemExit(f"line {line}: target_tokens must be positive")
        unsigned(row, "context_tokens", line)
        api_card_tokens = unsigned(row, "api_card_tokens", line)
        api_card_budget = unsigned(row, "api_card_budget_tokens", line)
        if api_card_budget == 0 or api_card_tokens > api_card_budget:
            raise SystemExit(f"line {line}: API card exceeds its positive token budget")
        if digest(row, "task_spec_sha256", line) != expected_spec_hash:
            raise SystemExit(f"line {line}: task_spec_sha256 differs from frozen contract")
        for field in ("api_card_sha256", "prompt_sha256", "output_sha256"):
            digest(row, field, line)
        key = (row["model"], row["system"], row["task"], demos)
        condition = (row["model"], row["task"], demos)
        condition_systems[condition].add(row["system"])
        if row["record_kind"] == "sample":
            if row["nll"]:
                raise SystemExit(f"line {line}: sample row must not contain reference NLL")
            index = unsigned(row, "sample_index", line)
            outcomes = [boolean(row, field, line)
                        for field in ("parsed", "typed", "built", "passed")]
            if outcomes != sorted(outcomes, reverse=True):
                raise SystemExit(f"line {line}: completion phases are inconsistent")
            samples[key].append(index)
            audit_key = (*condition, "sample", index)
        else:
            if row["sample_index"]:
                raise SystemExit(f"line {line}: reference row has sample_index")
            real(row, "nll", line)
            if any(row[field] for field in ("parsed", "typed", "built", "passed")):
                raise SystemExit(f"line {line}: reference row has completion outcomes")
            references[key] += 1
            audit_key = (*condition, "reference", 0)
        audit_value = (row["demo_ids"], str(seed), row["temperature"],
                       f"{row['top_p']}:{max_new_tokens}:{api_card_budget}")
        if audit_key in audit and audit[audit_key] != audit_value:
            raise SystemExit(
                f"line {line}: demonstrations or sampling controls differ across systems"
            )
        audit[audit_key] = audit_value
    if partial:
        return
    families = Counter(task_family.values())
    if set(task_family) != set(expected_tasks) or families != Counter(
        {family: 4 for family in FAMILIES}
    ):
        raise SystemExit(f"expected 24 tasks, four per family; found {dict(families)}")
    models = {row["model"] for row in rows}
    systems = {row["system"] for row in rows}
    if len(models) != 2:
        raise SystemExit(f"expected two models; found {sorted(models)}")
    expected_systems = {"Joggle", "MLIR", "xDSL"}
    if systems != expected_systems:
        raise SystemExit(f"expected {sorted(expected_systems)}; found {sorted(systems)}")
    for condition, observed in condition_systems.items():
        if observed != systems:
            raise SystemExit(f"condition {condition}: incomplete systems {sorted(observed)}")
    if set(samples) != set(references):
        missing = sorted(set(samples) - set(references))
        extra = sorted(set(references) - set(samples))
        raise SystemExit(
            "sample/reference conditions differ; "
            f"missing={missing[:2]} extra={extra[:2]}"
        )
    for key, indexes in samples.items():
        if sorted(indexes) != list(range(50)):
            raise SystemExit(f"condition {key}: expected sample indexes 0..49")
        if references[key] != 1:
            raise SystemExit(f"condition {key}: expected one reference row")


def footprint(
    rows: list[dict[str, str]], partial: bool, expected_tasks: dict[str, str]
) -> None:
    unique(rows, ("system", "system_revision", "task"))
    counts = ("source_files", "source_added", "source_deleted", "test_files",
              "test_added", "test_deleted", "zones", "registrations", "fanout",
              "cross_zone_edges")
    task_family: dict[str, str] = {}
    task_systems: dict[str, set[str]] = defaultdict(set)
    for line, row in enumerate(rows, start=2):
        if row["family"] not in FAMILIES:
            raise SystemExit(f"line {line}: unknown family {row['family']}")
        if expected_tasks.get(row["task"]) != row["family"]:
            raise SystemExit(f"line {line}: task is not in the footprint manifest")
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
    if set(task_family) != set(expected_tasks) or families != Counter(
        {family: 2 for family in FAMILIES}
    ):
        raise SystemExit(f"expected 12 tasks, two per family; found {dict(families)}")
    systems = {row["system"] for row in rows}
    for task, observed in task_systems.items():
        if observed != systems:
            raise SystemExit(f"task {task}: incomplete systems {sorted(observed)}")


def blank(row: dict[str, str], fields: tuple[str, ...], line: int) -> None:
    present = [field for field in fields if row[field]]
    if present:
        raise SystemExit(f"line {line}: fields must be empty: {', '.join(present)}")


def benchmark_rows(
    rows: list[dict[str, str]], partial: bool, subjects: dict[str, dict[str, str]],
    variants: dict[str, dict[str, object]], expected_spec_hash: str,
    *, subject_field: str, include_family: bool,
) -> None:
    unique(rows, (subject_field, "variant", "record_kind", "iteration", "seed"))
    preparations: dict[tuple[str, str], list[int]] = defaultdict(list)
    executions: dict[tuple[str, str], list[int]] = defaultdict(list)
    memories: dict[tuple[str, str], list[int]] = defaultdict(list)
    coverage: Counter[tuple[str, str]] = Counter()
    observed: dict[str, set[str]] = defaultdict(set)
    input_digests: dict[str, str] = {}
    audit: dict[tuple[str, str, int], tuple[int, str]] = {}
    revisions: dict[str, tuple[str, str]] = {}
    preparation_fields = (
        "latency_ns", "peak_bytes", "max_abs_error", "max_rel_error",
        "output_digest", "correct",
    )
    execution_fields = ("prepare_ns", "peak_bytes", "artifact_bytes")
    memory_fields = ("prepare_ns", "latency_ns", "artifact_bytes")
    unsupported_fields = tuple(sorted(set(preparation_fields + execution_fields)))

    for line, row in enumerate(rows, start=2):
        subject = row[subject_field]
        if subject not in subjects:
            raise SystemExit(f"line {line}: unknown {subject_field} {subject}")
        expected_subject = subjects[subject]
        if include_family and row["family"] != expected_subject["family"]:
            raise SystemExit(f"line {line}: family differs from benchmark manifest")
        if subject_field == "model" and row["model_hash"] != expected_subject["sha256"]:
            raise SystemExit(f"line {line}: model_hash differs from benchmark manifest")
        if digest(row, "case_spec_sha256", line) != expected_spec_hash:
            raise SystemExit(f"line {line}: case_spec_sha256 differs from frozen manifest")
        input_digest = digest(row, "input_digest", line)
        if subject in input_digests and input_digests[subject] != input_digest:
            raise SystemExit(f"line {line}: input_digest changes within subject")
        input_digests[subject] = input_digest

        variant = row["variant"]
        if variant not in variants:
            raise SystemExit(f"line {line}: unknown variant {variant}")
        expected_variant = variants[variant]
        if row["system"] != expected_variant["system"]:
            raise SystemExit(f"line {line}: system differs from benchmark manifest")
        if not row["system_revision"]:
            raise SystemExit(f"line {line}: system_revision is empty")
        revision = (row["system"], row["system_revision"])
        if variant in revisions and revisions[variant] != revision:
            raise SystemExit(f"line {line}: system revision changes within variant")
        revisions[variant] = revision
        observed[subject].add(variant)
        supported = boolean(row, "supported", line)
        seed = unsigned(row, "seed", line)
        kind = row["record_kind"]
        if kind not in {"coverage", "prepare", "execute", "memory"}:
            raise SystemExit(f"line {line}: unknown record_kind {kind}")

        if kind == "coverage":
            if supported or not row["reason"] or row["iteration"]:
                raise SystemExit(
                    f"line {line}: coverage row must be unsupported with reason and no iteration"
                )
            blank(row, unsupported_fields, line)
            coverage[(subject, variant)] += 1
            continue

        if not supported or row["reason"]:
            raise SystemExit(f"line {line}: measured row must be supported without reason")
        iteration = unsigned(row, "iteration", line)
        audit_key = (subject, kind, iteration)
        audit_value = (seed, input_digest)
        if audit_key in audit and audit[audit_key] != audit_value:
            raise SystemExit(f"line {line}: paired seed or input differs across variants")
        audit[audit_key] = audit_value

        if kind == "prepare":
            prepare_ns = unsigned(row, "prepare_ns", line)
            if prepare_ns == 0:
                raise SystemExit(f"line {line}: prepare_ns must be positive")
            if bool(expected_variant["artifact_size"]):
                artifact_bytes = unsigned(row, "artifact_bytes", line)
                if artifact_bytes == 0:
                    raise SystemExit(f"line {line}: artifact_bytes must be positive")
            elif row["artifact_bytes"]:
                raise SystemExit(f"line {line}: reference artifact size is not comparable")
            blank(row, preparation_fields, line)
            preparations[(subject, variant)].append(iteration)
        elif kind == "execute":
            latency_ns = unsigned(row, "latency_ns", line)
            if latency_ns == 0:
                raise SystemExit(f"line {line}: latency_ns must be positive")
            real(row, "max_abs_error", line)
            real(row, "max_rel_error", line)
            digest(row, "output_digest", line)
            passed = boolean(row, "correct", line)
            if not passed and not partial:
                raise SystemExit(f"line {line}: incorrect execution enters release data")
            blank(row, execution_fields, line)
            executions[(subject, variant)].append(iteration)
        else:
            peak_bytes = unsigned(row, "peak_bytes", line)
            if peak_bytes == 0:
                raise SystemExit(f"line {line}: peak_bytes must be positive")
            real(row, "max_abs_error", line)
            real(row, "max_rel_error", line)
            digest(row, "output_digest", line)
            passed = boolean(row, "correct", line)
            if not passed and not partial:
                raise SystemExit(f"line {line}: incorrect memory run enters release data")
            blank(row, memory_fields, line)
            memories[(subject, variant)].append(iteration)

    if partial:
        return
    if set(observed) != set(subjects):
        raise SystemExit(f"benchmark subjects are incomplete")
    expected_variants = set(variants)
    prepare_count = int(next(iter(variants.values()))["preparation_iterations"])
    execute_count = int(next(iter(variants.values()))["execution_iterations"])
    memory_count = int(next(iter(variants.values()))["memory_iterations"])
    for subject, found in observed.items():
        if found != expected_variants:
            raise SystemExit(f"{subject}: incomplete variants {sorted(found)}")
        for variant in expected_variants:
            key = (subject, variant)
            unsupported = coverage[key]
            prepared = preparations[key]
            executed = executions[key]
            measured_memory = memories[key]
            if unsupported:
                if unsupported != 1 or prepared or executed or measured_memory:
                    raise SystemExit(f"{subject}/{variant}: invalid unsupported record set")
                continue
            if sorted(prepared) != list(range(prepare_count)):
                raise SystemExit(
                    f"{subject}/{variant}: expected preparation iterations 0..{prepare_count - 1}"
                )
            if sorted(executed) != list(range(execute_count)):
                raise SystemExit(
                    f"{subject}/{variant}: expected execution iterations 0..{execute_count - 1}"
                )
            if sorted(measured_memory) != list(range(memory_count)):
                raise SystemExit(
                    f"{subject}/{variant}: expected memory iterations 0..{memory_count - 1}"
                )


def operators(
    rows: list[dict[str, str]], partial: bool, subjects: dict[str, dict[str, str]],
    variants: dict[str, dict[str, object]], expected_spec_hash: str,
) -> None:
    benchmark_rows(rows, partial, subjects, variants, expected_spec_hash,
                   subject_field="case_id", include_family=True)


def models(
    rows: list[dict[str, str]], partial: bool, subjects: dict[str, dict[str, str]],
    variants: dict[str, dict[str, object]], expected_spec_hash: str,
) -> None:
    benchmark_rows(rows, partial, subjects, variants, expected_spec_hash,
                   subject_field="model", include_family=False)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("figure", choices=("4", "5", "8", "9"))
    parser.add_argument("csv", type=Path)
    parser.add_argument("--allow-partial", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    with (root / "manifests" / "extension-tasks.csv").open(
        newline="", encoding="utf-8"
    ) as stream:
        manifest = list(csv.DictReader(stream))
    all_tasks = {row["task_id"]: row["family"] for row in manifest}
    spec_path = root / "manifests" / "extension-specs.json"
    expected_spec_hash = hashlib.sha256(spec_path.read_bytes()).hexdigest()
    benchmark_path = root / "manifests" / "benchmark-cases.json"
    with benchmark_path.open(encoding="utf-8") as stream:
        benchmark = json.load(stream)
    expected_benchmark_hash = hashlib.sha256(benchmark_path.read_bytes()).hexdigest()
    measurement = benchmark["measurement"]
    benchmark_variants = {
        row["id"]: {
            **row,
            "preparation_iterations": measurement["preparation_iterations"],
            "execution_iterations": measurement["execution_iterations"],
            "memory_iterations": measurement["memory_iterations"],
        }
        for row in benchmark["variants"]
    }
    operator_subjects = {
        row["id"]: {"family": row["family"]}
        for row in benchmark["operator_cases"]
    }
    model_subjects = {
        row["id"]: {"sha256": row["sha256"]}
        for row in benchmark["model_cases"]
    }
    footprint_tasks = {
        row["task_id"]: row["family"]
        for row in manifest
        if row["footprint"] == "true"
    }
    names = {"4": "figure-04-extension.csv", "5": "figure-05-footprint.csv",
             "8": "figure-08-operators.csv", "9": "figure-09-models.csv"}
    rows = load(args.csv, root / "templates" / names[args.figure])
    if args.figure == "4":
        extension(rows, args.allow_partial, all_tasks, expected_spec_hash)
    elif args.figure == "5":
        footprint(rows, args.allow_partial, footprint_tasks)
    elif args.figure == "8":
        operators(rows, args.allow_partial, operator_subjects,
                  benchmark_variants, expected_benchmark_hash)
    else:
        models(rows, args.allow_partial, model_subjects,
               benchmark_variants, expected_benchmark_hash)
    print(f"validated {len(rows)} rows for Figure {args.figure}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
