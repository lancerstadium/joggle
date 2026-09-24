#!/usr/bin/env python3
"""Derive Figure 5 change-footprint rows from pinned Git patches."""

from __future__ import annotations

import argparse
import csv
import difflib
import fnmatch
import hashlib
import json
import os
import re
import subprocess
import tempfile
from collections import deque
from datetime import datetime, timezone
from pathlib import Path


CASE_COLUMNS = [
    "system",
    "system_revision",
    "task",
    "family",
    "repo",
    "base",
    "head",
    "policy",
    "oracle_log",
    "oracle_log_sha256",
    "minimization_log",
    "minimization_log_sha256",
    "oracle_passed",
]
OUTPUT_COLUMNS = [
    "system",
    "system_revision",
    "task",
    "family",
    "patch_hash",
    "source_files",
    "source_added",
    "source_deleted",
    "test_files",
    "test_added",
    "test_deleted",
    "zones",
    "registrations",
    "fanout",
    "cross_zone_edges",
    "oracle_passed",
]


def command(repo: Path, args: list[str], *, binary: bool = False) -> str | bytes:
    result = subprocess.run(
        ["git", "-C", str(repo), *args],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=not binary,
    )
    return result.stdout


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def matches(path: str, patterns: list[str]) -> bool:
    return any(fnmatch.fnmatchcase(path, pattern) for pattern in patterns)


def load_policy(path: Path) -> dict[str, object]:
    policy = json.loads(path.read_text(encoding="utf-8"))
    required = {
        "version",
        "source_paths",
        "test_paths",
        "exclude_paths",
        "registration_paths",
        "registration_patterns",
        "zones",
        "dependents",
    }
    if set(policy) != required or policy["version"] != 1:
        raise SystemExit(f"{path}: invalid footprint policy keys or version")
    list_fields = (
        "source_paths",
        "test_paths",
        "exclude_paths",
        "registration_paths",
        "registration_patterns",
        "zones",
    )
    if any(not isinstance(policy[field], list) for field in list_fields):
        raise SystemExit(f"{path}: policy list field has the wrong type")
    zones = policy["zones"]
    names = []
    for zone in zones:
        if set(zone) != {"name", "paths"} or not isinstance(zone["paths"], list):
            raise SystemExit(f"{path}: invalid zone declaration")
        names.append(zone["name"])
    if len(names) != len(set(names)):
        raise SystemExit(f"{path}: duplicate zone name")
    dependents = policy["dependents"]
    if not isinstance(dependents, dict) or set(dependents) != set(names):
        raise SystemExit(f"{path}: dependents must name every zone exactly once")
    for name, values in dependents.items():
        if not isinstance(values, list) or any(value not in names for value in values):
            raise SystemExit(f"{path}: invalid dependents for {name}")
    for pattern in policy["registration_patterns"]:
        re.compile(pattern)
    return policy


def classify(path: str, policy: dict[str, object]) -> str:
    if matches(path, policy["exclude_paths"]):
        return "exclude"
    if matches(path, policy["test_paths"]):
        return "test"
    if matches(path, policy["source_paths"]):
        return "source"
    raise SystemExit(f"unclassified changed path: {path}")


def zone_for(path: str, policy: dict[str, object]) -> str:
    found = [zone["name"] for zone in policy["zones"]
             if matches(path, zone["paths"])]
    if len(found) != 1:
        raise SystemExit(f"source path {path} maps to {len(found)} zones: {found}")
    return found[0]


def changed_lines(repo: Path, base: str, head: str) -> list[tuple[str, int, int]]:
    text = command(repo, ["diff", "--no-ext-diff", "--no-renames", "--numstat",
                          base, head, "--"])
    rows = []
    for line in text.splitlines():
        added, deleted, path = line.split("\t", 2)
        if added == "-" or deleted == "-":
            raise SystemExit(f"binary patch is outside the footprint metric: {path}")
        rows.append((path, int(added), int(deleted)))
    return rows


def registration_edits(
    repo: Path, base: str, head: str, policy: dict[str, object]
) -> int:
    text = command(repo, ["diff", "--no-ext-diff", "--no-renames", "--unified=0",
                          base, head, "--"])
    patterns = [re.compile(value) for value in policy["registration_patterns"]]
    current = ""
    count = 0
    for line in text.splitlines():
        if line.startswith("+++ b/") or line.startswith("--- a/"):
            current = line[6:]
            continue
        if line.startswith(("+++ ", "--- ", "@@", "diff ", "index ")):
            continue
        if not line.startswith(("+", "-")) or classify(current, policy) != "source":
            continue
        content = line[1:]
        if matches(current, policy["registration_paths"]) or any(
            pattern.search(content) for pattern in patterns
        ):
            count += 1
    return count


def graph_metrics(touched: set[str], policy: dict[str, object]) -> tuple[int, int]:
    dependents = policy["dependents"]
    reached = set(touched)
    pending = deque(touched)
    while pending:
        current = pending.popleft()
        for dependent in dependents[current]:
            if dependent not in reached:
                reached.add(dependent)
                pending.append(dependent)
    edges = sum(
        target in touched
        for source in touched
        for target in dependents[source]
    )
    return len(reached - touched), edges


def collect(case: dict[str, str], root: Path) -> tuple[dict[str, object], dict[str, object]]:
    repo = (root / case["repo"]).resolve()
    policy_path = (root / case["policy"]).resolve()
    oracle_log = (root / case["oracle_log"]).resolve()
    minimization_log = (root / case["minimization_log"]).resolve()
    if not repo.is_dir():
        raise SystemExit(f"not a Git repository: {repo}")
    inside = command(repo, ["rev-parse", "--is-inside-work-tree"]).strip()
    if inside != "true":
        raise SystemExit(f"not a Git worktree: {repo}")
    policy = load_policy(policy_path)
    evidence = (
        (oracle_log, case["oracle_log_sha256"], "oracle"),
        (minimization_log, case["minimization_log_sha256"], "minimization"),
    )
    for path, expected, label in evidence:
        if not path.is_file() or len(expected) != 64 or sha256_file(path) != expected:
            raise SystemExit(
                f"{case['system']}/{case['task']}: invalid {label} evidence"
            )
    base = command(repo, ["rev-parse", f"{case['base']}^{{commit}}"] ).strip()
    head = command(repo, ["rev-parse", f"{case['head']}^{{commit}}"] ).strip()
    if case["system_revision"] != base:
        raise SystemExit(
            f"{case['system']}/{case['task']}: system_revision is not base commit"
        )
    patch = command(
        repo,
        ["diff", "--binary", "--no-ext-diff", "--no-renames", base, head, "--"],
        binary=True,
    )
    if not patch:
        raise SystemExit(f"{case['system']}/{case['task']}: empty patch")
    patch_digest = sha256_bytes(patch)
    minimization = json.loads(minimization_log.read_text(encoding="utf-8"))
    trials = minimization.get("trials", [])
    if (
        minimization.get("schema") != "hunk-minimization/v1"
        or minimization.get("base") != base
        or minimization.get("final_patch_sha256") != patch_digest
        or not minimization.get("retained_hunks")
        or not trials
        or trials[-1].get("phase") != "final"
        or trials[-1].get("returncode") != 0
        or trials[-1].get("output_sha256") != sha256_file(oracle_log)
    ):
        raise SystemExit(
            f"{case['system']}/{case['task']}: minimization evidence does not match patch"
        )
    source_files = source_added = source_deleted = 0
    test_files = test_added = test_deleted = 0
    touched_zones: set[str] = set()
    included_paths = []
    for path, added, deleted in changed_lines(repo, base, head):
        kind = classify(path, policy)
        if kind == "exclude":
            continue
        included_paths.append(path)
        if kind == "test":
            test_files += 1
            test_added += added
            test_deleted += deleted
        else:
            source_files += 1
            source_added += added
            source_deleted += deleted
            touched_zones.add(zone_for(path, policy))
    if not touched_zones:
        raise SystemExit(f"{case['system']}/{case['task']}: patch has no source zone")
    fanout, crossed = graph_metrics(touched_zones, policy)
    passed = case["oracle_passed"].lower()
    if passed not in {"true", "false"}:
        raise SystemExit("oracle_passed must be true or false")
    result = {
        "system": case["system"],
        "system_revision": base,
        "task": case["task"],
        "family": case["family"],
        "patch_hash": patch_digest,
        "source_files": source_files,
        "source_added": source_added,
        "source_deleted": source_deleted,
        "test_files": test_files,
        "test_added": test_added,
        "test_deleted": test_deleted,
        "zones": len(touched_zones),
        "registrations": registration_edits(repo, base, head, policy),
        "fanout": fanout,
        "cross_zone_edges": crossed,
        "oracle_passed": passed,
    }
    audit = {
        "system": case["system"],
        "task": case["task"],
        "repo": str(repo),
        "base": base,
        "head": head,
        "policy": str(policy_path),
        "policy_sha256": sha256_file(policy_path),
        "oracle_log": str(oracle_log),
        "oracle_log_sha256": case["oracle_log_sha256"],
        "minimization_log": str(minimization_log),
        "minimization_log_sha256": case["minimization_log_sha256"],
        "included_paths": included_paths,
        "touched_zones": sorted(touched_zones),
    }
    return result, audit


def collect_packages(root: Path, output: Path) -> None:
    """Collect the complete native-package matrix, including parent controls.

    These are observed implementation footprints, not minimal-patch estimates.
    Publication scaffolding is included at integration and only when changed
    during maintenance. No system receives an artificial extra ownership zone.
    """
    repo = Path(__file__).resolve().parent.parent
    manifest = repo / "artifact/manifests/package-changes.json"
    matrix = json.loads(manifest.read_text())
    if output.exists() or output.with_suffix(".json").exists():
        raise ValueError("refusing to replace an existing package result")
    rows, audits = [], []
    for contract in matrix["cases"]:
        for system in matrix["systems"]:
            label = contract["id"] + "-" + system
            path = root / label / "result.json"
            report = json.loads(path.read_text())
            if (report.get("schema") != "native-package-task/v1" or report.get("error")
                    or not report["passed"] or report["parent"] or report["system"] != system
                    or report["contract"] != contract):
                raise ValueError(f"unvalidated candidate: {label}")
            for name, expected in report["hashes"].items():
                if sha256_file(repo / name) != expected:
                    raise ValueError(f"changed experiment input: {name}")
            for name, key in (("fixtures.json", "fixtures_sha256"), ("change.patch", "patch_sha256")):
                if sha256_file(path.parent / name) != report[key]:
                    raise ValueError(f"changed run input: {label}/{name}")
            fixtures = json.loads((path.parent / "fixtures.json").read_text())
            if {c["id"] for c in fixtures} != {c["case"] for c in report["cases"]} or not all(c["passed"] for c in report["cases"]):
                raise ValueError(f"incomplete cases: {label}")
            parent_failures = 0
            audit = {"case": contract["id"], "system": system, "record": str(path.relative_to(repo)),
                     "record_sha256": sha256_file(path)}
            if contract["kind"] == "maintenance":
                parent_path = root / ("parent-"+label) / "result.json"
                parent = json.loads(parent_path.read_text())
                if (parent.get("error") or parent["passed"] or not parent["parent"]
                        or parent["hashes"] != report["hashes"] or parent["system"] != system
                        or parent["contract"] != contract
                        or parent["fixtures_sha256"] != report["fixtures_sha256"]
                        or sha256_file(parent_path.parent / "fixtures.json") != report["fixtures_sha256"]
                        or {c["case"] for c in parent["cases"]} != {c["id"] for c in fixtures}
                        or any(s["exit_code"] != 0 for s in parent["setup"])):
                    raise ValueError(f"invalid paired parent: {label}")
                positives = {c["id"] for c in fixtures if "error" not in c["expect"] and c["input"].get("conv_uses", 1) == 1}
                parent_failures = sum(c["case"] in positives and not c["passed"] for c in parent["cases"])
                if not parent_failures:
                    raise ValueError(f"parent satisfies the changed contract: {label}")
                for source in parent["sources"]:
                    if source["before_sha256"] != source["after_sha256"] or sha256_file(parent_path.parent / "source" / source["deployed_path"]) != source["before_sha256"]:
                        raise ValueError(f"modified parent source: {label}")
                audit.update(parent_record=str(parent_path.relative_to(repo)), parent_sha256=sha256_file(parent_path))
            files, added, deleted, publication = 0, 0, 0, 0
            patches = []
            for source in report["sources"]:
                original = repo / source["source"]
                target = path.parent / "source" / source["deployed_path"]
                if sha256_file(original) != source["before_sha256"] or sha256_file(target) != source["after_sha256"]:
                    raise ValueError(f"source hash mismatch: {label}")
                before = "" if contract["kind"] == "integration" else original.read_text()
                diff = list(difflib.unified_diff(before.splitlines(True), target.read_text().splitlines(True),
                    fromfile="a/"+source["deployed_path"], tofile="b/"+source["deployed_path"]))
                patches.extend(diff)
                a = sum(line.startswith("+") and not line.startswith("+++") for line in diff)
                d = sum(line.startswith("-") and not line.startswith("---") for line in diff)
                if (a, d) != (source["added"], source["deleted"]):
                    raise ValueError(f"line count mismatch: {label}")
                files += bool(a+d)
                added += a
                deleted += d
                if source["role"] != "implementation":
                    publication += a+d
            if "".join(patches) != (path.parent / "change.patch").read_text():
                raise ValueError(f"patch mismatch: {label}")
            rows.append(dict(task=contract["id"], kind=contract["kind"], system=system,
                source_files=files, source_added=added, source_deleted=deleted,
                publication_lines=publication, zones=int(files > 0),
                semantic_cases=len(report["cases"]), runtime_probes=report["runtime_probes"],
                parent_positive_failures=parent_failures, passed=True))
            audits.append(audit)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    output.with_suffix(".json").write_text(json.dumps({
        "schema": "native-package-footprint/v1", "matrix_complete": True,
        "hunk_minimized": False, "count_policy": "Observed source lines including publication scaffolding; excludes fixtures and shared measurement code; one feature package is one zone.",
        "manifest_sha256": sha256_file(manifest), "output_sha256": sha256_file(output),
        "cases": audits}, indent=2)+"\n")
    print(f"collected {len(rows)} validated package rows")


def main() -> int:
    parser = argparse.ArgumentParser()
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--cases", type=Path)
    inputs.add_argument("--package-runs", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--task-manifest",
        type=Path,
        default=Path(__file__).resolve().parent / "manifests" / "extension-tasks.csv",
    )
    args = parser.parse_args()
    if args.package_runs:
        collect_packages(args.package_runs.resolve(), args.output.resolve())
        return 0
    with args.cases.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != CASE_COLUMNS:
            raise SystemExit(f"case columns must be {','.join(CASE_COLUMNS)}")
        cases = list(reader)
    if not cases:
        raise SystemExit("case manifest contains no rows")
    with args.task_manifest.open(newline="", encoding="utf-8") as stream:
        tasks = {
            row["task_id"]: row
            for row in csv.DictReader(stream)
            if row["footprint"] == "true"
        }
    for line, case in enumerate(cases, start=2):
        task = tasks.get(case["task"])
        if not task or task["family"] != case["family"]:
            raise SystemExit(f"case line {line}: task is not in the footprint manifest")
    root = args.cases.resolve().parent
    results, audits = [], []
    for case in cases:
        result, audit = collect(case, root)
        results.append(result)
        audits.append(audit)
    results.sort(key=lambda row: (row["task"], row["system"]))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    record_path = args.output.with_suffix(".json")
    if args.output.exists() or record_path.exists():
        raise SystemExit("refusing to replace an existing footprint result")
    handle, temporary = tempfile.mkstemp(
        prefix=f".{args.output.name}.", suffix=".tmp", dir=args.output.parent
    )
    os.close(handle)
    temporary_path = Path(temporary)
    with temporary_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=OUTPUT_COLUMNS, lineterminator="\n")
        writer.writeheader()
        writer.writerows(results)
        stream.flush()
        os.fsync(stream.fileno())
    temporary_path.replace(args.output)
    metadata = {
        "schema": "figure-05-footprint/v1",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "case_manifest": str(args.cases.resolve()),
        "case_manifest_sha256": sha256_file(args.cases.resolve()),
        "task_manifest": str(args.task_manifest.resolve()),
        "task_manifest_sha256": sha256_file(args.task_manifest.resolve()),
        "output_sha256": sha256_file(args.output),
        "cases": audits,
    }
    record_temporary = record_path.with_name(f".{record_path.name}.tmp")
    with record_temporary.open("w", encoding="utf-8") as stream:
        stream.write(json.dumps(metadata, indent=2, sort_keys=True) + "\n")
        stream.flush()
        os.fsync(stream.fileno())
    record_temporary.replace(record_path)
    print(f"collected {len(results)} footprint rows")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
