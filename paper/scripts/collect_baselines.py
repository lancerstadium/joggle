#!/usr/bin/env python3
"""Collect the matched-baseline extension table for an external system.

Both baseline systems preserve a record per frozen task, but the two tables were
produced by different means: the TVM table came from a runner that can only emit
a row for a task it can execute, and the ONNX-MLIR table had no generator at
all, so it could not be regenerated. This collector reads the preserved records
instead, so one mechanism produces both tables, every frozen task appears for
every system, and a task a system cannot complete is a row that says so rather
than a row that is missing.

Counts are recomputed from the declared source files with the same definition
the Joggle collector uses, and any count a record already states is checked
against the recomputation. Where a table already exists, every cell is compared
with it and differences are reported rather than written silently.

Usage:
    python3 paper/scripts/collect_baselines.py tvm
    python3 paper/scripts/collect_baselines.py onnx-mlir
"""

from __future__ import annotations

import argparse
import csv
import io
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from measure_extensions import source_digest, source_lines  # noqa: E402

FIELDS = [
    "schema", "system", "version", "revision", "task", "source_files",
    "source_sloc", "source_bytes", "core_files", "build_files",
    "native_registrations", "new_dependencies", "generated_artifacts",
    "validation", "source_sha256",
]

# Directory name under paper/baselines/ for each system this collector knows,
# and the name the tables publish for it.
SYSTEMS = {"tvm": "tvm", "onnx-mlir": "onnx-mlir"}
DISPLAY = {"tvm": "TVM", "onnx-mlir": "ONNX-MLIR"}

# Where each system's table belongs. The collector does not choose the path,
# so regeneration cannot quietly retarget a different record.
TABLES = {
    "tvm": "paper/data/extension-tvm-pilot.csv",
    "onnx-mlir": "paper/data/extension-onnx-mlir-pilot.csv",
}


def frozen_tasks(repo: Path) -> list[str]:
    """The task ids the contract freezes, in contract order."""
    contract = json.loads((repo / "paper/tasks/extension-tasks.json").read_text())
    tasks = contract["tasks"] if isinstance(contract, dict) else contract
    return [task["id"] for task in tasks]


def artifact_count(record: dict) -> int | str:
    """Artifacts a record observed.

    Every artifact the records store is named by its digest, so the digest keys
    are the artifact inventory. Counting the non-digest keys instead undercounts
    by one, because a lowered IR is named once and hashed once while a shared
    library appears only as a digest.
    """
    implementation = record.get("implementation") or {}
    outputs = implementation.get("outputs") or record.get("outputs")
    if isinstance(outputs, dict):
        return sum(1 for key in outputs if key.endswith("_sha256"))
    fusion = record.get("fusion")
    if isinstance(fusion, list):
        return sum(
            1
            for entry in fusion if isinstance(entry, dict)
            for key in entry if key.endswith("_sha256")
        )
    return ""


def from_implementation(record: dict) -> dict:
    """A row from a record that states its own footprint counts."""
    implementation = record["implementation"]
    status = record.get("status")
    if status is None:
        oracle = record.get("oracle") or {}
        status = oracle.get("status", "")
    return {
        "source_files": implementation.get("files", ""),
        "source_sloc": implementation.get("source_lines", ""),
        "source_bytes": implementation.get("source_bytes", ""),
        "core_files": implementation.get("framework_core_files_changed", ""),
        "build_files": implementation.get("build_files", ""),
        "native_registrations": implementation.get(
            "native_registration_entries", ""),
        "new_dependencies": implementation.get("new_external_dependencies", ""),
        "generated_artifacts": artifact_count(record),
        "validation": status,
        "source_sha256": implementation.get("source_sha256", ""),
    }


def from_sources(entry: dict, repo: Path) -> dict:
    """A row from a runnable task that declares the source files it measured.

    Counts are recomputed rather than trusted, and each count the record also
    states is checked against the recomputation.
    """
    paths = [repo / source for source in entry["sources"]]
    missing = [str(p) for p in paths if not p.exists()]
    if missing:
        raise SystemExit(f"{entry['id']}: declared source is absent: {missing}")

    counts = dict(zip(
        ("source_files", "source_sloc", "source_bytes"),
        (len(paths), sum(source_lines(p) for p in paths),
         sum(p.stat().st_size for p in paths)),
    ))
    for key, stated in (
        ("source_files", entry.get("source_files")),
        ("source_sloc", entry.get("source_sloc")),
        ("source_bytes", entry.get("source_bytes")),
    ):
        if stated is not None and stated != counts[key]:
            raise SystemExit(
                f"{entry['id']}: record states {key}={stated}, "
                f"recomputation gives {counts[key]}")

    def size(value: object) -> int | str:
        if isinstance(value, list):
            return len(value)
        if isinstance(value, int):
            return value
        return ""

    return {
        **counts,
        "core_files": size(entry.get("core_files")),
        "build_files": size(entry.get("build_files")),
        "native_registrations": size(entry.get("native_registrations")),
        "new_dependencies": size(entry.get("new_dependencies")),
        "generated_artifacts": size(entry.get("generated_artifacts")),
        "validation": "pass",
        "source_sha256": source_digest(paths, repo),
    }


def system_identity(record: dict) -> tuple[str, str]:
    """Version and revision a record states, in either schema.

    The per-task ONNX-MLIR records name the system as a string and carry only
    the revision, while that system's external-kernel record nests a dict that
    also holds the version. The version is taken from whichever record states
    it, because it describes the one pinned build they all measured.
    """
    version = record.get("version") or ""
    revision = record.get("revision") or ""
    nested = record.get("system")
    if isinstance(nested, dict):
        version = version or nested.get("version", "")
        revision = revision or nested.get("revision", "")
    return version, revision


def rows_for(system: str, repo: Path, tasks: list[str]) -> list[dict]:
    root = repo / "paper/baselines" / SYSTEMS[system]
    runnable: dict[str, dict] = {}
    identity = ("", "")
    record_path = root / "record.json"
    if record_path.exists():
        record = json.loads(record_path.read_text())
        identity = system_identity(record)
        runnable = {entry["id"]: entry for entry in record.get("tasks", [])}

    # Read every preserved per-task record first, so the system-level version
    # can be taken from whichever record states it.
    preserved = {}
    for task in tasks:
        path = root / task / "result.json"
        if path.exists():
            preserved[task] = json.loads(path.read_text())
    for record in preserved.values():
        version, revision = system_identity(record)
        if not identity[0] and version:
            identity = (version, identity[1])
        if not identity[1] and revision:
            identity = (identity[0], revision)
    if not identity[0] or not identity[1]:
        raise SystemExit(
            f"{system}: no preserved record states both a version and a "
            f"revision; a table without them is not traceable")

    rows = []
    for task in tasks:
        row = {"schema": 1, "system": DISPLAY[system], "task": task,
               "version": identity[0], "revision": identity[1]}
        if task in preserved:
            record = preserved[task]
            if "implementation" in record:
                row.update(from_implementation(record))
            else:
                # A task the system could not complete states why instead of
                # stating a footprint it never produced.
                row["validation"] = record.get("status", "unsupported")
        elif task in runnable:
            row.update(from_sources(runnable[task], repo))
        else:
            row["validation"] = "not attempted"
        rows.append({field: row.get(field, "") for field in FIELDS})
    return rows


def render(rows: list[dict]) -> str:
    buffer = io.StringIO()
    writer = csv.DictWriter(buffer, fieldnames=FIELDS, lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)
    return buffer.getvalue()


def differences(existing: str, produced: str) -> list[str]:
    """Cells where a regenerated table would change the recorded one."""
    old = list(csv.DictReader(io.StringIO(existing)))
    new = list(csv.DictReader(io.StringIO(produced)))
    by_task = {row["task"]: row for row in old}
    out = []
    for row in new:
        prior = by_task.get(row["task"])
        if prior is None:
            # Nested quotes are kept out of the f-string so the collector also
            # runs on the study venv's Python 3.11.
            state = row["validation"]
            out.append(f"{row['task']}: new row, validation={state}")
            continue
        for field in FIELDS:
            if field == "schema":
                continue
            if str(prior.get(field, "")) != str(row.get(field, "")):
                out.append(
                    f"{row['task']}.{field}: recorded {prior.get(field)!r}, "
                    f"recomputed {row.get(field)!r}")
    return out


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("system", choices=sorted(SYSTEMS))
    parser.add_argument("--repo", type=Path,
                        default=HERE.parent.parent)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--check", action="store_true",
                        help="compare with the recorded table and write nothing")
    args = parser.parse_args()

    repo = args.repo.resolve()
    rows = rows_for(args.system, repo, frozen_tasks(repo))
    produced = render(rows)

    target = args.output or repo / TABLES[args.system]
    drift: list[str] = []
    if target.exists():
        drift = differences(target.read_text(), produced)
        for line in drift:
            print(f"  {line}", file=sys.stderr)
    if args.check:
        # A difference means the recorded table and the preserved records
        # disagree, which is a defect either way: regenerate, or explain why the
        # record moved. Reporting it and passing would hide exactly that.
        if drift:
            raise SystemExit(
                f"{args.system}: the recorded table disagrees with the preserved "
                f"records; regenerate with "
                f"`python3 paper/scripts/collect_baselines.py {args.system}`")
        print(f"{args.system}: {len(rows)} task rows match the preserved records")
        return
    target.write_text(produced)
    print(f"{args.system}: wrote {len(rows)} task rows to {target.relative_to(repo)}")


if __name__ == "__main__":
    main()
