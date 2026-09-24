#!/usr/bin/env python3
"""Assemble agent-provider CSVs into the Figure 4 release matrix."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def validate_trajectory(path: Path, provider: dict, rows: list[dict[str, str]]) -> None:
    """Bind a primary measurement to its isolated final oracle and tool record."""
    if len(rows) != 1:
        raise SystemExit(f"{path}: expected one trajectory per provider file")
    row = rows[0]
    trajectory = Path(provider["trajectory"])
    if sha256(trajectory) != provider["trajectory_sha256"] or sha256(trajectory) != row["trajectory_sha256"]:
        raise SystemExit(f"{path}: trajectory digest differs")
    data = json.loads(trajectory.read_text())
    if (data.get("schema") != "extension-agent-trajectory/v1" or data.get("dirty")
            or data.get("identity_stable") is not True or data.get("infrastructure_error")
            or data.get("options") != {"temperature": 0.0, "num_ctx": 32768}
            or data.get("context_check", {}).get("verified") is not True
            or data.get("demonstrations") or data.get("think") is not False):
        raise SystemExit(f"{path}: trajectory violates the frozen execution protocol")
    overflow = data["context_check"].get("overflow", {})
    if (overflow.get("type") != "exceed_context_size_error" or overflow.get("n_ctx") != 32768
            or type(overflow.get("n_prompt_tokens")) is not int or overflow["n_prompt_tokens"] <= 32768):
        raise SystemExit(f"{path}: missing native context-budget check")
    for field in ("task", "system", "task_spec_sha256", "api_card_sha256"):
        if data[field] != row[field]:
            raise SystemExit(f"{path}: {field} differs from trajectory")
    if data["model"]["digest"] != row["model_revision"] or data["model"]["name"] != row["model"]:
        raise SystemExit(f"{path}: model identity differs")
    if sha256(trajectory.parent / "candidate.patch") != row["patch_sha256"]:
        raise SystemExit(f"{path}: patch digest differs")
    oracle_path = trajectory.parent / "final-oracle.json"
    if sha256(oracle_path) != data["final_oracle_sha256"]:
        raise SystemExit(f"{path}: final oracle digest differs")
    oracle = json.loads(oracle_path.read_text())
    if (oracle.get("complete_task") is not True
            or oracle.get("execution_isolation", {}).get("kind") != "macos-seatbelt"
            or oracle["task"] != row["task"] or oracle["system"] != row["system"]
            or oracle["task_spec_sha256"] != row["task_spec_sha256"]
            or (row["passed"] == "true") != oracle["passed"]):
        raise SystemExit(f"{path}: final oracle does not support the measured result")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", type=Path, nargs="+")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    record_path = args.output.with_suffix(".json")
    if args.output.exists() or record_path.exists():
        raise SystemExit("refusing to replace an existing output or record")

    root = Path(__file__).resolve().parent
    with (root / "templates/figure-04-extension.csv").open(
        newline="", encoding="utf-8"
    ) as stream:
        header = next(csv.reader(stream))
    rows: list[dict[str, str]] = []
    records = []
    for path in args.inputs:
        with path.open(newline="", encoding="utf-8") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames != header:
                raise SystemExit(f"{path}: columns differ from Figure 4 schema")
            source_rows = list(reader)
            rows.extend(source_rows)
        provider = path.with_suffix(".json")
        payload = json.loads(provider.read_text(encoding="utf-8"))
        if (payload.get("schema") != "agent-provider/v1"
                or payload.get("dirty")
                or payload.get("release_eligible") is not True
                or payload.get("output_sha256") != sha256(path)):
            raise SystemExit(f"{path}: invalid agent-provider record")
        validate_trajectory(path, payload, source_rows)
        records.append({
            "path": str(path.resolve()), "sha256": sha256(path),
            "record": str(provider.resolve()), "record_sha256": sha256(provider),
        })

    rows.sort(key=lambda row: (
        row["model"], row["task"], int(row["demo_count"]), row["system"],
        int(row["run"]), int(row["seed"]),
    ))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    handle, temporary = tempfile.mkstemp(
        prefix=f".{args.output.name}.", suffix=".tmp", dir=args.output.parent
    )
    os.close(handle)
    temporary_path = Path(temporary)
    try:
        with temporary_path.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=header, lineterminator="\n")
            writer.writeheader()
            writer.writerows(rows)
        subprocess.run(
            [sys.executable, str(root / "validate_figure.py"), "4", str(temporary_path)],
            check=True,
        )
        temporary_path.replace(args.output)
    finally:
        temporary_path.unlink(missing_ok=True)

    record = {
        "schema": "extension-agent-assembly/v1",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "inputs": records,
        "output": {"path": str(args.output.resolve()), "sha256": sha256(args.output)},
        "rows": len(rows),
    }
    temporary_record = record_path.with_name(f".{record_path.name}.tmp")
    with temporary_record.open("w", encoding="utf-8") as stream:
        stream.write(json.dumps(record, indent=2, sort_keys=True) + "\n")
        stream.flush()
        os.fsync(stream.fileno())
    temporary_record.replace(record_path)
    print(f"assembled {len(rows)} Figure 4 agent trajectories in {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
