#!/usr/bin/env python3
"""Check the current paper exports without running experiments or changing plots."""
from __future__ import annotations

import argparse
from collections import Counter
import csv
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "paper"), str(ROOT / "artifact/figures")]
from record_paths import record_path
from render_extensions import validate as validate_extensions
from render_update import audit as validate_updates
from figure_05_footprint import validate_packages
from figure_07_models import validate as validate_models
from validate_figure import performance

DATA = ROOT / "paper/data"


def rows(name):
    with (DATA / name).open(newline="") as stream:
        return list(csv.DictReader(stream))


def check_file(path, expected):
    actual = hashlib.sha256(path.read_bytes()).hexdigest()
    if actual != expected:
        raise ValueError(f"recorded content differs: {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw", action="store_true",
                        help="also check retained local source records referenced by the exports")
    args = parser.parse_args()
    refs = rows("extension-size.csv")
    validate_extensions(refs)
    validate_packages(DATA / "package-footprint.csv")
    validate_updates(rows("figure-06-update.csv"))
    validate_models(rows("figure-07-models.csv"))
    spec_path = ROOT / "artifact/manifests/benchmark-cases.json"
    spec = json.loads(spec_path.read_text())
    operators = rows("figure-07-operators.csv")
    variants = json.loads((DATA / "figure-07-operators.merge.json").read_text())["variants"]
    performance(operators, True, spec, hashlib.sha256(spec_path.read_bytes()).hexdigest(), variants)
    counts = Counter((row["subject"], row["variant"]) for row in operators)
    expected = {(case["id"], variant) for case in spec["operator_cases"] for variant in variants}
    if set(counts) != expected:
        raise ValueError("incomplete operator/variant population")
    for row in operators:
        n = spec["measurement"]["execution_iterations"] if row["supported"] == "true" else 1
        if counts[row["subject"], row["variant"]] != n:
            raise ValueError("incomplete operator repetitions")

    records = []
    for name in ("extension-size", "figure-06-update", "figure-07-models", "figure-07-operators"):
        suffix = ".merge.json" if name.startswith("figure-07") else ".json"
        record = json.loads((DATA / (name + suffix)).read_text())
        check_file(DATA / (name + ".csv"), record.get("csv_sha256") or record["output"]["sha256"])
        records.append(record)
    check_file(DATA / "figure-06-stages.csv", records[1]["stages_sha256"])
    if args.raw:
        for row in refs:
            report = json.loads(record_path(row["report"]).read_text())
            if not report.get("passed") or report["source_sha256"] != row["source_sha256"]:
                raise ValueError("reference admission differs from exported source")
        for record in records[2:]:
            for entry in record["inputs"]:
                check_file(record_path(entry["path"]), entry["sha256"])
                check_file(record_path(entry["record"]), entry["record_sha256"])
        for entry in records[1]["sources"]:
            for kind in ("csv", "samples", "record"):
                check_file(record_path(entry[kind + "_path"]), entry[kind + "_sha256"])
        packages = json.loads((DATA / "package-footprint.json").read_text())
        for entry in packages["cases"]:
            check_file(record_path(entry["record"]), entry["record_sha256"])
            if "parent_record" in entry:
                check_file(record_path(entry["parent_record"]), entry["parent_sha256"])
    print("Verified: 36 references, 24 packages, 540 update rows, "
          f"{len(operators)} operator rows, {len(rows('figure-07-models.csv'))} model rows.")
    if args.raw:
        print("Retained source records match the exported provenance.")
    print("Agent trajectories remain a stopped, incomplete collection; this check does not complete it.")


if __name__ == "__main__":
    main()
