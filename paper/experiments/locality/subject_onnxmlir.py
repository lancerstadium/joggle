#!/usr/bin/env python3
"""One fresh-process timing subject for an ONNX-MLIR compiled library.

Consumes the study's own input and references so the ONNX-MLIR column is
measured on identical data under the same one-thread protocol as the compiled
artifacts. Session construction is excluded from timing, as with the other
external columns.

Two properties of this column belong in every reading of it. The library is
built at ONNX-MLIR's `-O3`, because its command-line default is `-O0`; the
record states the flag, so this is that tool's optimized route rather than its
default one. And the pinned original model is consumed directly, because
ONNX-MLIR's frontend accepts it: unlike the TVM column, no opset conversion is
involved, so the two external columns are not fed byte-identical inputs.
"""
import argparse, hashlib, json, sys, time
from pathlib import Path

import numpy as np

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("root", type=Path, help="model study directory")
ap.add_argument("--inner", type=int, default=1)
ap.add_argument("--warmup", type=int, default=3)
ap.add_argument("--atol", type=float, default=1e-4)
a = ap.parse_args()

record = json.loads((a.root / "onnxmlir.json").read_text())
sys.path.insert(0, record["runtime_dir"])
from PyRuntime import OMExecutionSession

report = json.loads((a.root / "reference.json").read_text())
api = json.loads((a.root / "base" / "api.json").read_text())[0]

param = api["params"][0]
dtype = np.dtype("float32" if param["c"] == "float" else param["c"])
x = np.fromfile(a.root / "input.bin", dtype=dtype).reshape(report["input_shape"])

session = OMExecutionSession(str(Path(record["library"]).resolve()))

for _ in range(a.warmup):
    session.run([x])

rows, produced = [], None
for _ in range(a.inner):
    start = time.perf_counter()
    produced = session.run([x])
    rows.append(time.perf_counter() - start)

values = [np.asarray(v) for v in produced]

digest = hashlib.sha256()
worst, failed = 0.0, 0
for index, value in enumerate(values):
    value = np.ascontiguousarray(value)
    digest.update(value.tobytes())
    reference = a.root / f"reference_{index}.bin"
    if not reference.exists():
        continue
    ref = np.fromfile(reference, dtype=value.dtype)
    if ref.size != value.size:
        sys.exit(f"result {index}: ONNX-MLIR produced {value.size} elements, "
                 f"reference holds {ref.size}")
    diff = np.abs(value.ravel().astype(np.float64) - ref.astype(np.float64))
    worst = max(worst, float(diff.max()))
    failed += int((diff > a.atol).sum())
checksum = digest.hexdigest()[:16]

print("iteration,seconds,checksum")
for i, s in enumerate(rows):
    print(f"{i},{s:.9f},{checksum}")
verdict = "pass" if failed == 0 else "fail"
print(f"max_abs_error,{worst:.6e},failed,{failed},threads,1,{verdict}", file=sys.stderr)
if verdict != "pass":
    sys.exit(1)
