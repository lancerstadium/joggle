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
            rows.extend(reader)
        provider = path.with_suffix(".json")
        payload = json.loads(provider.read_text(encoding="utf-8"))
        if (payload.get("schema") != "agent-provider/v1"
                or payload.get("dirty")
                or payload.get("output_sha256") != sha256(path)):
            raise SystemExit(f"{path}: invalid agent-provider record")
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
