#!/usr/bin/env python3
"""Run the retained-graph study after standard ONNX shape inference."""
import argparse
import csv
import json
from pathlib import Path
import subprocess
import sys

import onnx


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    work = root / ".cache/artifact/reactive-shaped-20260925"
    inputs = work / "inputs"
    inputs.mkdir(parents=True, exist_ok=True)
    records, paths = [], []
    with (root / "artifact/manifests/reactive-models.csv").open() as stream:
        models = list(csv.DictReader(stream))
    for entry in models:
        source = root / ".cache/onnx-zoo" / (entry["model"] + ".onnx")
        target = inputs / source.name
        model = onnx.load(source)
        before = len(model.graph.value_info)
        # Only value/type annotations are added; operators and tensors stay fixed.
        inferred = onnx.shape_inference.infer_shapes(model, strict_mode=False)
        # Copy only top-level annotations: inference may also annotate nested
        # subgraphs, which this study deliberately leaves byte-for-byte intact.
        del model.graph.value_info[:]
        model.graph.value_info.extend(inferred.graph.value_info)
        onnx.save(model, target)
        records.append({"model": entry["model"], "source": str(source),
                        "source_sha256": entry["sha256"], "value_info_before": before,
                        "value_info_after": len(inferred.graph.value_info)})
        paths.append(str(target))
        print(entry["model"], before, "->", len(inferred.graph.value_info), flush=True)
    (work / "input-preparation.json").write_text(json.dumps({
        "onnx_version": onnx.__version__, "method": "onnx.shape_inference.infer_shapes",
        "strict_mode": False, "models": records}, indent=2) + "\n")
    if args.prepare_only:
        return
    subprocess.run([sys.executable, str(root / "artifact/run_reactive.py"),
                    "--models", *paths, "--output", str(root / "paper/data/reactive-scheduler.csv"),
                    "--build-root", str(work / "measurement"), "--sites", "early", "middle", "late",
                    "--edit-classes", "operation_metadata", "--scopes", "affected", "unrelated",
                    "--stages", "5", "--warmups", "3", "--iterations", "10", "--seed", "1701"],
                   cwd=root, check=True)


if __name__ == "__main__":
    main()
