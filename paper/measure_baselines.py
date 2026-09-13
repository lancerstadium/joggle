#!/usr/bin/env python3
"""Validate and measure one pinned matched-baseline record."""

import argparse
import csv
import json
import os
import subprocess
import sys
from pathlib import Path

from measure_extensions import source_digest, source_lines, string_list


def run(command: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def required_string(record: dict, field: str) -> str:
    value = record.get(field)
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"baseline record requires a nonempty {field}")
    return value


def validate_checkout(record: dict) -> None:
    environment = required_string(record, "checkout_environment")
    checkout_value = os.environ.get(environment)
    if not checkout_value:
        raise ValueError(f"{environment} must name the baseline checkout")
    checkout = Path(checkout_value).resolve()
    revision = run(["git", "rev-parse", "HEAD"], checkout)
    if revision.returncode != 0 or revision.stdout.strip() != record["revision"]:
        raise ValueError("baseline checkout does not match the recorded revision")
    status = run(["git", "status", "--porcelain"], checkout)
    if status.returncode != 0 or status.stdout.strip():
        raise ValueError("baseline checkout must be clean")


def validate_task(task: object, repo: Path) -> tuple[list[Path], list[str]]:
    fields = {
        "id",
        "sources",
        "command",
        "core_files",
        "build_files",
        "native_registrations",
        "new_dependencies",
        "generated_artifacts",
    }
    if not isinstance(task, dict) or set(task) != fields:
        raise ValueError("baseline task contains unexpected fields")
    required_string(task, "id")
    for field in ("sources", "command", "generated_artifacts"):
        if not string_list(task.get(field)):
            raise ValueError(f"baseline task requires a nonempty {field}")
    for field in ("core_files", "build_files", "new_dependencies"):
        value = task.get(field)
        if not isinstance(value, list) or not all(
            isinstance(item, str) and item.strip() for item in value
        ):
            raise ValueError(f"baseline task requires a string list for {field}")
    registrations = task.get("native_registrations")
    if not isinstance(registrations, int) or registrations < 0:
        raise ValueError("native_registrations must be a nonnegative integer")
    sources = [(repo / item).resolve() for item in task["sources"]]
    if any(repo not in path.parents or not path.is_file() for path in sources):
        raise ValueError(f"baseline task {task['id']} has an invalid source")
    return sources, task["command"]


def validate_result(task: dict, result: subprocess.CompletedProcess[str]) -> str:
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        return "fail"
    lines = [line for line in result.stdout.splitlines() if line.strip()]
    try:
        report = json.loads(lines[-1])
    except (IndexError, json.JSONDecodeError):
        return "fail"
    return (
        "pass"
        if report == {"task": task["id"], "status": "pass"}
        else "fail"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--record", type=Path, required=True)
    parser.add_argument("--contracts", type=Path, required=True)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    repo = args.repo.resolve()
    record = json.loads(args.record.read_text(encoding="utf-8"))
    contracts = json.loads(args.contracts.read_text(encoding="utf-8"))
    expected = {
        "schema",
        "system",
        "version",
        "revision",
        "checkout_environment",
        "tasks",
    }
    if not isinstance(record, dict) or set(record) != expected:
        raise ValueError("baseline record contains unexpected fields")
    if record["schema"] != 1:
        raise ValueError("unsupported baseline record schema")
    for field in ("system", "version", "revision"):
        required_string(record, field)
    tasks = record.get("tasks")
    if not isinstance(tasks, list) or not tasks:
        raise ValueError("baseline record requires tasks")
    contract_tasks = contracts.get("tasks")
    if contracts.get("schema") != 2 or not isinstance(contract_tasks, list):
        raise ValueError("extension contracts must use schema 2")
    contract_by_id = {task.get("id"): task for task in contract_tasks}
    if None in contract_by_id or len(contract_by_id) != len(contract_tasks):
        raise ValueError("extension contracts require unique task ids")
    validate_checkout(record)

    rows = []
    ids = set()
    for task in tasks:
        sources, command = validate_task(task, repo)
        if task["id"] in ids:
            raise ValueError(f"duplicate baseline task: {task['id']}")
        ids.add(task["id"])
        contract = contract_by_id.get(task["id"])
        if contract is None:
            raise ValueError(f"unknown extension contract: {task['id']}")
        inputs = set(contract["contract"]["inputs"])
        if not inputs.issubset(command):
            raise ValueError(f"baseline task {task['id']} omits contract inputs")
        if inputs.intersection(task["sources"]):
            raise ValueError(f"baseline task {task['id']} counts supplied inputs")
        result = run([sys.executable, *command], repo)
        rows.append(
            {
                "schema": record["schema"],
                "system": record["system"],
                "version": record["version"],
                "revision": record["revision"],
                "task": task["id"],
                "source_files": len(sources),
                "source_sloc": sum(source_lines(path) for path in sources),
                "source_bytes": sum(path.stat().st_size for path in sources),
                "core_files": len(task["core_files"]),
                "build_files": len(task["build_files"]),
                "native_registrations": task["native_registrations"],
                "new_dependencies": len(task["new_dependencies"]),
                "generated_artifacts": len(task["generated_artifacts"]),
                "validation": validate_result(task, result),
                "source_sha256": source_digest(sources, repo),
            }
        )

    output = (
        args.output.open("w", newline="", encoding="utf-8")
        if args.output
        else sys.stdout
    )
    try:
        writer = csv.DictWriter(output, fieldnames=list(rows[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    finally:
        if args.output:
            output.close()


if __name__ == "__main__":
    main()
