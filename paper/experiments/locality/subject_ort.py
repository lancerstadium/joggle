#!/usr/bin/env python3
"""One fresh-process ONNX Runtime timing subject on the study's own fixture.

Consumes the same `input.bin` and `reference_*.bin` the compiled artifacts use,
so the runtime column is measured on identical data under the same one-thread
protocol. Session construction is excluded from timing.

ONNX Runtime is a tuned runtime with hand-written kernels, not a compiler
output; it bounds the gap rather than serving as a like-for-like comparison.
"""
import argparse, hashlib, json, sys, time
from pathlib import Path

import numpy as np
import onnxruntime as ort

TYPE = {"tensor(float)": np.float32, "tensor(double)": np.float64,
        "tensor(int64)": np.int64, "tensor(int32)": np.int32}

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("model", type=Path, help="pinned .onnx file")
ap.add_argument("--root", type=Path, required=True, help="model study directory")
ap.add_argument("--inner", type=int, default=1)
ap.add_argument("--warmup", type=int, default=3)
ap.add_argument("--atol", type=float, default=1e-4)
a = ap.parse_args()

options = ort.SessionOptions()
# Execute the graph as written, matching both the stored reference and the
# compiler under test. With the default level ONNX Runtime fuses
# DequantizeLinear -> Conv -> QuantizeLinear into an integer kernel, which is a
# different numerical path and fails the reference check on quantised models.
options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_DISABLE_ALL
options.intra_op_num_threads = 1
options.inter_op_num_threads = 1
session = ort.InferenceSession(str(a.model), options, providers=["CPUExecutionProvider"])

spec = session.get_inputs()[0]
report = json.loads((a.root / "reference.json").read_text())
x = np.fromfile(a.root / "input.bin", dtype=TYPE[spec.type]).reshape(report["input_shape"])
names = [r["ort_output"] for r in report["references"]]
feed = {spec.name: x}

for _ in range(a.warmup):
    session.run(names, feed)

rows, produced = [], None
for _ in range(a.inner):
    start = time.perf_counter()
    produced = session.run(names, feed)
    rows.append(time.perf_counter() - start)

digest = hashlib.sha256()
worst, failed = 0.0, 0
for index, value in enumerate(produced):
    value = np.ascontiguousarray(value)
    digest.update(value.tobytes())
    ref = np.fromfile(a.root / f"reference_{index}.bin", dtype=value.dtype)
    diff = np.abs(value.ravel().astype(np.float64) - ref.astype(np.float64))
    worst = max(worst, float(diff.max()))
    failed += int((diff > a.atol).sum())
checksum = digest.hexdigest()[:16]
# The references were produced by this same runtime, so this check confirms the
# fixture is intact rather than validating the runtime against an oracle.

print("iteration,seconds,checksum")
for i, s in enumerate(rows):
    print(f"{i},{s:.9f},{checksum}")
verdict = "pass" if failed == 0 else "fail"
print(f"max_abs_error,{worst:.6e},failed,{failed},threads,1,{verdict}", file=sys.stderr)
if verdict != "pass":
    sys.exit(1)
