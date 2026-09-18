#!/usr/bin/env python3
"""Check the manuscript against the records it cites.

Every assertion here is a place where the paper and paper/data have drifted apart
before, or where a claim could quietly outlive the measurement behind it. Run it
after any edit to either side.
"""
import csv
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
TEX = (ROOT / "paper/submission/main.tex").read_text()


def load(name):
    return json.loads((ROOT / "paper/data" / name).read_text())


rows = list(csv.DictReader((ROOT / "paper/data/model-coverage-pilot.csv").open()))
systems = load("revalidation-all-systems.json")

CHECKS = []


def check(name, ok, detail=""):
    CHECKS.append((name, bool(ok), detail))


def has(text):
    return text in TEX


check("sixteen pinned models", len(rows) == 16, f"{len(rows)} rows")
check("twelve executed and passing",
      sum(1 for r in rows if r["execute"] == "pass") == 12,
      str(sum(1 for r in rows if r["execute"] == "pass")))
check("three never executed",
      sum(1 for r in rows if r["execute"] == "not_run") == 3,
      str(sum(1 for r in rows if r["execute"] == "not_run")))
check("manuscript says twelve validated", has("twelve produce validated artifacts"))
check("manuscript says three never", has("three never"))
check("manuscript keeps the campaign at ten", has("ten models carry the campaign"))
check("re-validation agrees with its record",
      str(systems.get("summary", {}).get("passed", "")) == "52",
      str(systems.get("summary", {}).get("passed")))

check("INT8 error is the corrected value", has("4.34\\times10^{-3}"))
check("INT8 element count is the corrected one", has("over 123 elements"))
check("QDQ speedup is stated", has("3.25\\times"))
check("abstract range uses the paired estimator at both ends",
      has("1.44\\times$ to") and has("$3.22\\times$"))
check("preparation law is the measured one", has("two milliseconds per"))
check("SSD operation count is the real one", has("19{,}432"))
check("per-expansion cost is stated", has("435\\,ms"))

check("no quadratic claim survives", not has("quadratic"))
check("no semantic-line proxy survives", not has("8{,}118"))
check("no pre-QDQ count survives", not has("of which eleven"))
check("no agent experiment is claimed", not has("or an agent"))

failed = [(n, d) for n, ok, d in CHECKS if not ok]
for name, ok, detail in CHECKS:
    print(f"  {'OK ' if ok else 'BAD'}  {name}" + (f"  ({detail})" if detail else ""))
print(f"\n{len(CHECKS) - len(failed)}/{len(CHECKS)} checks pass")
sys.exit(1 if failed else 0)
