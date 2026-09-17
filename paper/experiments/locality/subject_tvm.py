#!/usr/bin/env python3
"""One fresh-process timing subject for a TVM C-target library.

Consumes the study's own input and references so the TVM column is measured on
identical data under the same one-thread protocol as the compiled artifacts.
Module load and VM construction are excluded from timing.

The library comes from TVM's default lowering: it bounds untuned compiler
output, not TVM with MetaSchedule, a tuned schedule, or an LLVM target.
"""
import argparse, hashlib, json, sys, time
from pathlib import Path

import numpy as np
import tvm
from tvm import relax

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("root", type=Path, help="model study directory")
ap.add_argument("--inner", type=int, default=1)
ap.add_argument("--warmup", type=int, default=3)
ap.add_argument("--atol", type=float, default=1e-4)
a = ap.parse_args()

record = json.loads((a.root / "tvm.json").read_text())
report = json.loads((a.root / "reference.json").read_text())
api = json.loads((a.root / "base" / "api.json").read_text())[0]

dtype = np.dtype(api["params"][0]["c"].replace("float", "float32")
                 if api["params"][0]["c"] == "float" else api["params"][0]["c"])
x = np.fromfile(a.root / "input.bin", dtype=dtype).reshape(report["input_shape"])

module = tvm.runtime.load_module(record["library"])
machine = relax.VirtualMachine(module, tvm.cpu())
try:
    device_x = tvm.runtime.tensor(x, tvm.cpu())
except AttributeError:
    device_x = tvm.nd.array(x, tvm.cpu())
entry = machine["main"]

for _ in range(a.warmup):
    entry(device_x)

rows, produced = [], None
for _ in range(a.inner):
    start = time.perf_counter()
    produced = entry(device_x)
    rows.append(time.perf_counter() - start)

# A multi-result entry point returns a container, and the container type has
# moved between TVM builds: it has been a Python list and is now
# tvm_ffi.container.Array. Treat anything indexable that is not itself a tensor
# as the result sequence, so the subject does not depend on which container the
# installed build returns.
if hasattr(produced, "numpy"):
    values = [produced]
elif hasattr(produced, "__len__"):
    values = [produced[i] for i in range(len(produced))]
else:
    values = [produced]
values = [v.numpy() if hasattr(v, "numpy") else np.asarray(v) for v in values]

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
        sys.exit(f"result {index}: TVM produced {value.size} elements, "
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
