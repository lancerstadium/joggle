#!/usr/bin/env python3
"""Render the modification-scope matrix from the three recorded pilots.

Every cell comes from paper/data/extension-*.csv; nothing is transcribed by
hand, and an unsupported cell stays unsupported rather than being estimated.
"""

import csv
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "data"
TASKS = ["implementation", "policy", "external-kernel", "numeric-format"]
LABEL = {"implementation": "Implementation", "policy": "Selection policy",
         "external-kernel": "External kernel", "numeric-format": "Numeric format"}


def read(name):
    with (DATA / name).open(newline="") as stream:
        return {r["task"]: r for r in csv.DictReader(stream)}


def cell(row, *keys):
    if row is None or row.get("validation") == "unsupported":
        return "---"
    values = [row.get(k, "") for k in keys]
    if not any(values):
        return "---"
    return " / ".join(v if v != "" else "0" for v in values)


def main():
    joggle = read("extension-footprint-pilot.csv")
    tvm = read("extension-tvm-pilot.csv")
    onnxmlir = read("extension-onnx-mlir-pilot.csv")
    rows = []
    for task in TASKS:
        rows.append((
            LABEL[task],
            cell(joggle.get(task), "source_files", "modules", "declared_dependencies"),
            cell(tvm.get(task), "source_files", "build_files", "native_registrations"),
            cell(onnxmlir.get(task), "source_files", "build_files", "native_registrations"),
            joggle.get(task, {}).get("validation", "---"),
            tvm.get(task, {}).get("validation", "---"),
            onnxmlir.get(task, {}).get("validation", "---"),
        ))
    width = [max(len(str(r[i])) for r in rows + [("Task", "J files/mod/dep",
              "T files/build/reg", "O files/build/reg", "J", "T", "O")])
              for i in range(7)]
    head = ("Task", "Joggle file/mod/dep", "TVM file/build/reg",
            "ONNX-MLIR file/build/reg", "J", "T", "O")
    print("  ".join(h.ljust(width[i]) for i, h in enumerate(head)))
    for r in rows:
        print("  ".join(str(v).ljust(width[i]) for i, v in enumerate(r)))


if __name__ == "__main__":
    main()
