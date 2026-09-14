#!/usr/bin/env python3
"""Run a frozen, process-isolated comparison across independent systems."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import platform
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*\Z")
CHECKSUM = re.compile(r"[0-9a-f]{16}\Z")
THREAD_ENV = {
    "OMP_NUM_THREADS": "1",
    "OPENBLAS_NUM_THREADS": "1",
    "MKL_NUM_THREADS": "1",
    "NUMEXPR_NUM_THREADS": "1",
    "VECLIB_MAXIMUM_THREADS": "1",
}


def fail(message: str) -> None:
    raise SystemExit(f"measure_systems: {message}")


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def load_document(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        fail(f"cannot read manifest: {error}")
    if not isinstance(value, dict):
        fail("manifest must be an object")
    expected = {"schema", "study", "trials", "subjects"}
    if set(value) != expected:
        fail(f"manifest fields must be exactly {sorted(expected)}")
    if value["schema"] != 2:
        fail("manifest schema must be 2")
    if not isinstance(value["study"], str) or not NAME.fullmatch(value["study"]):
        fail("study must be a safe nonempty name")
    if not isinstance(value["trials"], int) or value["trials"] < 3:
        fail("trials must be an integer of at least three")
    if not isinstance(value["subjects"], list) or len(value["subjects"]) < 2:
        fail("subjects must contain at least two systems")
    cycle = 2 * len(value["subjects"])
    if value["trials"] % cycle != 0:
        fail(f"trials must be a multiple of {cycle} for balanced order")
    return value


def string_list(value: Any, field: str) -> list[str]:
    if (
        not isinstance(value, list)
        or not value
        or any(not isinstance(item, str) or not item for item in value)
    ):
        fail(f"{field} must be a nonempty string array")
    return value


def subjects(document: dict[str, Any], repo: Path) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    systems: set[str] = set()
    for index, raw in enumerate(document["subjects"]):
        if not isinstance(raw, dict):
            fail(f"subject {index} must be an object")
        required = {"system", "command", "artifacts", "version_command"}
        if set(raw) != required:
            fail(f"subject {index} fields must be exactly {sorted(required)}")
        system = raw["system"]
        if not isinstance(system, str) or not NAME.fullmatch(system):
            fail(f"subject {index} has an unsafe system identity")
        if system in systems:
            fail(f"system must appear exactly once: {system}")
        systems.add(system)
        command = string_list(raw["command"], f"{system}.command")
        version_command = string_list(
            raw["version_command"], f"{system}.version_command"
        )
        if not isinstance(raw["artifacts"], dict) or not raw["artifacts"]:
            fail(f"{system}.artifacts must be a nonempty object")
        artifacts: dict[str, dict[str, object]] = {}
        for label, source in raw["artifacts"].items():
            if not isinstance(label, str) or not NAME.fullmatch(label):
                fail(f"{system} has an unsafe artifact label")
            if not isinstance(source, str) or not source:
                fail(f"{system}.{label} must name an artifact")
            path = (repo / source).resolve()
            try:
                relative = path.relative_to(repo)
            except ValueError:
                fail(f"{system}.{label} escapes the repository: {path}")
            if not path.is_file():
                fail(f"{system}.{label} does not exist: {path}")
            artifacts[label] = {
                "path": str(relative),
                "bytes": path.stat().st_size,
                "sha256": digest(path),
            }
        out.append(
            {
                "system": system,
                "command": command,
                "version_command": version_command,
                "artifacts": artifacts,
            }
        )
    return out


def invoke(
    command: list[str],
    repo: Path,
    env: dict[str, str],
    timeout: float,
    **options: Any,
) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            command, cwd=repo, env=env, text=True, timeout=timeout, **options
        )
    except subprocess.TimeoutExpired:
        fail(f"command exceeded {timeout:g} seconds: {' '.join(command)}")


def version(
    command: list[str], repo: Path, env: dict[str, str], timeout: float
) -> str:
    result = invoke(
        command,
        repo,
        env,
        timeout,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if result.returncode != 0:
        fail(f"version command failed ({result.returncode}): {' '.join(command)}")
    lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    if not lines:
        fail(f"version command produced no text: {' '.join(command)}")
    return lines[0]


def affinity(cpu: int | None):
    if cpu is None:
        return None
    if not hasattr(os, "sched_setaffinity"):
        fail("--cpu is not supported on this host")
    if cpu not in os.sched_getaffinity(0):
        fail(f"CPU {cpu} is not available to this process")

    def pin() -> None:
        os.sched_setaffinity(0, {cpu})

    return pin


def run_subject(
    subject: dict[str, Any],
    repo: Path,
    env: dict[str, str],
    cpu: int | None,
    timeout: float,
) -> tuple[dict[str, str], str]:
    result = invoke(
        subject["command"],
        repo,
        env,
        timeout,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        preexec_fn=affinity(cpu),
    )
    if result.returncode != 0:
        fail(
            f"{subject['system']} failed ({result.returncode}): "
            f"{' '.join(subject['command'])}\n{result.stderr}"
        )
    rows = list(csv.DictReader(result.stdout.splitlines()))
    required = {"iteration", "seconds", "checksum"}
    if len(rows) != 1 or not required <= rows[0].keys():
        fail(f"{subject['system']} did not emit one protocol row")
    row = rows[0]
    try:
        seconds = float(row["seconds"])
    except ValueError:
        fail(f"{subject['system']} emitted a nonnumeric duration")
    if (
        row["iteration"] != "0"
        or not math.isfinite(seconds)
        or seconds <= 0
        or not CHECKSUM.fullmatch(row["checksum"])
    ):
        fail(f"{subject['system']} emitted an invalid protocol row")
    validation = " | ".join(
        line.strip() for line in result.stderr.splitlines() if line.strip()
    )
    return row, validation


def rotation(values: list[dict[str, Any]], trial: int) -> list[dict[str, Any]]:
    offset = trial % len(values)
    out = values[offset:] + values[:offset]
    if (trial // len(values)) % 2:
        out.reverse()
    return out


def git(repo: Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", *arguments],
        cwd=repo,
        check=True,
        stdout=subprocess.PIPE,
        text=True,
    )
    return result.stdout.strip()


def cpu_model() -> str:
    cpuinfo = Path("/proc/cpuinfo")
    if cpuinfo.is_file():
        for line in cpuinfo.read_text(errors="replace").splitlines():
            field, separator, value = line.partition(":")
            if separator and field.strip() in {"model name", "Hardware"}:
                return value.strip()
    return platform.processor()


def available_cpus() -> list[int] | None:
    if not hasattr(os, "sched_getaffinity"):
        return None
    return sorted(os.sched_getaffinity(0))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--repo", type=Path, default=Path("."))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--record", type=Path, required=True)
    parser.add_argument("--cpu", type=int)
    parser.add_argument("--max-load1", type=float)
    parser.add_argument("--timeout-seconds", type=float, default=900.0)
    parser.add_argument("--require-clean", action="store_true")
    args = parser.parse_args()
    if args.cpu is not None and args.cpu < 0:
        parser.error("--cpu must be nonnegative")
    if args.max_load1 is not None and (
        not math.isfinite(args.max_load1) or args.max_load1 <= 0
    ):
        parser.error("--max-load1 must be positive and finite")
    if not math.isfinite(args.timeout_seconds) or args.timeout_seconds <= 0:
        parser.error("--timeout-seconds must be positive and finite")

    repo = args.repo.resolve()
    manifest = args.manifest.resolve()
    output = args.output.resolve()
    record = args.record.resolve()
    if not (repo / ".git").exists() or not manifest.is_file():
        parser.error("repo and manifest must exist")
    try:
        manifest.relative_to(repo)
        output.relative_to(repo)
        record.relative_to(repo)
    except ValueError:
        parser.error("manifest, output, and record must be inside the repository")
    if len({manifest, output, record}) != 3:
        parser.error("manifest, output, and record must be distinct files")
    document = load_document(manifest)
    selected = subjects(document, repo)
    revision = git(repo, "rev-parse", "HEAD")
    dirty = bool(git(repo, "status", "--porcelain"))
    if args.require_clean and dirty:
        fail("repository is dirty")

    env = os.environ.copy()
    env.update(THREAD_ENV)
    versions = {
        subject["system"]: version(
            subject["version_command"], repo, env, args.timeout_seconds
        )
        for subject in selected
    }
    rows: list[dict[str, object]] = []
    started = datetime.now(timezone.utc)
    for trial in range(document["trials"]):
        for position, subject in enumerate(rotation(selected, trial)):
            load_before = os.getloadavg()[0]
            if args.max_load1 is not None and load_before > args.max_load1:
                fail(
                    f"load average {load_before:.3f} exceeds "
                    f"--max-load1 {args.max_load1:.3f}"
                )
            row, validation = run_subject(
                subject, repo, env, args.cpu, args.timeout_seconds
            )
            extra = {
                key: value
                for key, value in row.items()
                if key not in {"iteration", "seconds", "checksum"}
            }
            rows.append(
                {
                    "study": document["study"],
                    "trial": trial,
                    "position": position,
                    "system": subject["system"],
                    "seconds": row["seconds"],
                    "checksum": row["checksum"],
                    "metrics": json.dumps(extra, sort_keys=True, separators=(",", ":")),
                    "validation": validation,
                    "load1_before": f"{load_before:.6f}",
                    "load1_after": f"{os.getloadavg()[0]:.6f}",
                }
            )
    finished = datetime.now(timezone.utc)

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(
            stream, fieldnames=list(rows[0]), lineterminator="\n"
        )
        writer.writeheader()
        writer.writerows(rows)

    record.parent.mkdir(parents=True, exist_ok=True)
    record.write_text(
        json.dumps(
            {
                "schema": 2,
                "study": document["study"],
                "started_utc": started.isoformat(),
                "finished_utc": finished.isoformat(),
                "trials": document["trials"],
                "cpu": args.cpu,
                "max_load1": args.max_load1,
                "timeout_seconds": args.timeout_seconds,
                "repo_revision": revision,
                "repo_dirty": dirty,
                "manifest": {
                    "path": str(manifest.relative_to(repo)),
                    "sha256": digest(manifest),
                },
                "runner_sha256": digest(Path(__file__).resolve()),
                "host": {
                    "node": platform.node(),
                    "platform": platform.platform(),
                    "machine": platform.machine(),
                    "processor": cpu_model(),
                    "logical_cpus": os.cpu_count(),
                    "available_cpus": available_cpus(),
                    "python": sys.version.split()[0],
                },
                "thread_environment": THREAD_ENV,
                "subjects": [
                    {
                        "system": subject["system"],
                        "command": subject["command"],
                        "version_command": subject["version_command"],
                        "version": versions[subject["system"]],
                        "artifacts": subject["artifacts"],
                    }
                    for subject in selected
                ],
                "output": {
                    "path": str(output.relative_to(repo)),
                    "rows": len(rows),
                    "sha256": digest(output),
                },
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
