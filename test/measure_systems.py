#!/usr/bin/env python3
"""Regression-test the process-isolated paper measurement protocol."""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
import time
from pathlib import Path


def subject(name: str) -> None:
    if name == "slow":
        time.sleep(0.2)
        name = "right"
    duration = {"left": "0.01", "right": "0.02", "bad": "0.03"}[name]
    checksum = {
        "left": "0123456789abcdef",
        "right": "fedcba9876543210",
        "bad": "not-a-checksum",
    }[name]
    print("iteration,seconds,checksum")
    print(f"0,{duration},{checksum}")


def run(runner: Path, repo: Path, root: Path) -> None:
    root.mkdir(parents=True, exist_ok=True)
    artifact = root / "artifact.txt"
    artifact.write_text("measurement protocol fixture\n", encoding="utf-8")
    manifest = root / "manifest.json"
    command = [sys.executable, str(Path(__file__).resolve()), "--subject"]
    version = [sys.executable, str(Path(__file__).resolve()), "--version-subject"]
    manifest.write_text(
        json.dumps(
            {
                "schema": 2,
                "study": "protocol-test",
                "trials": 4,
                "subjects": [
                    {
                        "system": "left",
                        "command": command + ["left"],
                        "artifacts": {"fixture": str(artifact.relative_to(repo))},
                        "version_command": version,
                    },
                    {
                        "system": "right",
                        "command": command + ["right"],
                        "artifacts": {"fixture": str(artifact.relative_to(repo))},
                        "version_command": version,
                    },
                ],
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    output = root / "rows.csv"
    record = root / "record.json"
    subprocess.run(
        [
            sys.executable,
            str(runner),
            "--manifest",
            str(manifest),
            "--repo",
            str(repo),
            "--output",
            str(output),
            "--record",
            str(record),
            "--timeout-seconds",
            "2",
        ],
        check=True,
    )
    with output.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) != 8:
        raise AssertionError(f"expected eight rows, received {len(rows)}")
    orders: dict[int, list[str]] = {}
    for row in rows:
        orders.setdefault(int(row["trial"]), []).append(row["system"])
    expected = [
        ["left", "right"],
        ["right", "left"],
        ["right", "left"],
        ["left", "right"],
    ]
    if list(orders.values()) != expected:
        raise AssertionError(f"unbalanced subject order: {orders}")
    provenance = json.loads(record.read_text(encoding="utf-8"))
    if provenance["output"]["rows"] != 8 or provenance["timeout_seconds"] != 2:
        raise AssertionError("measurement provenance is incomplete")
    if [item["system"] for item in provenance["subjects"]] != [
        "left",
        "right",
    ]:
        raise AssertionError("system identities are absent from provenance")
    if any(
        item["version"] != "protocol-subject 1"
        for item in provenance["subjects"]
    ):
        raise AssertionError("version output was displaced by stderr diagnostics")

    document = json.loads(manifest.read_text(encoding="utf-8"))
    for mode, timeout, expected_error in (
        ("bad", "2", "invalid protocol row"),
        ("slow", "0.05", "command exceeded"),
    ):
        document["subjects"][1]["command"][-1] = mode
        rejected_manifest = root / f"{mode}-manifest.json"
        rejected_manifest.write_text(
            json.dumps(document, indent=2) + "\n", encoding="utf-8"
        )
        rejected = subprocess.run(
            [
                sys.executable,
                str(runner),
                "--manifest",
                str(rejected_manifest),
                "--repo",
                str(repo),
                "--output",
                str(root / f"{mode}-rows.csv"),
                "--record",
                str(root / f"{mode}-record.json"),
                "--timeout-seconds",
                timeout,
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if rejected.returncode == 0 or expected_error not in rejected.stderr:
            raise AssertionError(f"{mode} subject was not rejected: {rejected.stderr}")

    document["subjects"][1]["command"][-1] = "right"
    document["subjects"][1]["system"] = "left"
    duplicate_manifest = root / "duplicate-system-manifest.json"
    duplicate_manifest.write_text(
        json.dumps(document, indent=2) + "\n", encoding="utf-8"
    )
    rejected = subprocess.run(
        [
            sys.executable,
            str(runner),
            "--manifest",
            str(duplicate_manifest),
            "--repo",
            str(repo),
            "--output",
            str(root / "duplicate-system-rows.csv"),
            "--record",
            str(root / "duplicate-system-record.json"),
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if (
        rejected.returncode == 0
        or "system must appear exactly once" not in rejected.stderr
    ):
        raise AssertionError(
            f"same-system comparison was not rejected: {rejected.stderr}"
        )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--repo", type=Path)
    parser.add_argument("--root", type=Path)
    parser.add_argument("--subject", choices=("left", "right", "bad", "slow"))
    parser.add_argument("--version-subject", action="store_true")
    args = parser.parse_args()
    if args.subject:
        subject(args.subject)
        return
    if args.version_subject:
        print("irrelevant discovery warning", file=sys.stderr)
        print("protocol-subject 1")
        return
    if args.runner is None or args.repo is None or args.root is None:
        parser.error("runner, repo, and root are required")
    run(args.runner.resolve(), args.repo.resolve(), args.root.resolve())


if __name__ == "__main__":
    main()
