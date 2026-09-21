#!/usr/bin/env python3
"""Assemble provider outputs into the CSV consumed by Figure 6."""

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
    with (root / "templates/figure-06-update.csv").open(newline="", encoding="utf-8") as stream:
        header = next(csv.reader(stream))
    rows = []
    records = []
    for path in args.inputs:
        with path.open(newline="", encoding="utf-8") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames != header:
                raise SystemExit(f"{path}: columns differ from Figure 6 schema")
            rows.extend(reader)
        provider_record = path.with_suffix(".json")
        record = json.loads(provider_record.read_text(encoding="utf-8"))
        if (record.get("schema") != "update-provider/v1" or record.get("dirty")
                or record.get("output_sha256") != sha256(path)):
            raise SystemExit(f"{path}: invalid provider record")
        records.append({"path": str(path.resolve()), "sha256": sha256(path),
                        "record": str(provider_record.resolve()),
                        "record_sha256": sha256(provider_record)})
    rows.sort(key=lambda row: (
        row["subject"], row["system"], row["edit_class"],
        row["edit_scope"], row["edit_site"], row["policy"],
        int(row["iteration"]), int(row["seed"]),
    ))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    handle, temporary = tempfile.mkstemp(prefix=f".{args.output.name}.", dir=args.output.parent)
    os.close(handle); temporary_path = Path(temporary)
    try:
        with temporary_path.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=header)
            writer.writeheader(); writer.writerows(rows)
        subprocess.run([sys.executable, str(root / "validate_reactive.py"),
                        str(temporary_path)], check=True)
        temporary_path.replace(args.output)
    finally:
        temporary_path.unlink(missing_ok=True)
    payload = {"schema": "update-assembly/v1",
               "created_utc": datetime.now(timezone.utc).isoformat(),
               "inputs": records, "output_sha256": sha256(args.output),
               "rows": len(rows)}
    with record_path.open("w", encoding="utf-8") as stream:
        stream.write(json.dumps(payload, indent=2, sort_keys=True) + "\n")
        stream.flush(); os.fsync(stream.fileno())
    print(f"assembled {len(rows)} Figure 6 rows in {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
