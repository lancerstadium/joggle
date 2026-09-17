#!/usr/bin/env python3
"""One fresh-process timing subject for any emitted Joggle C artifact.

The ABI comes from the artifact's own `c.api` descriptor, so no model-specific
signature is written here. Emits protocol rows (iteration,seconds,checksum) on
stdout and a validation line on stderr, mirroring the same-host study.

Setup, buffer allocation, and validation are excluded from timing.
"""
import argparse, ctypes, hashlib, json, platform, sys, time
from pathlib import Path

import numpy as np

KIND = {"float": (ctypes.c_float, np.float32),
        "double": (ctypes.c_double, np.float64),
        "int64_t": (ctypes.c_int64, np.int64),
        "int32_t": (ctypes.c_int32, np.int32),
        "unsigned char": (ctypes.c_ubyte, np.uint8)}
SO = "dylib" if platform.system() == "Darwin" else "so"

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("directory", type=Path, help="variant directory with model.<so> and api.json")
ap.add_argument("--input", type=Path, required=True)
ap.add_argument("--reference", type=Path, action="append", required=True,
                help="expected output, once per result in descriptor order")
ap.add_argument("--inner", type=int, default=1)
ap.add_argument("--warmup", type=int, default=3)
ap.add_argument("--atol", type=float, default=1e-4)
a = ap.parse_args()

api = json.loads((a.directory / "api.json").read_text())[0]
lib = ctypes.CDLL(str(a.directory / f"model.{SO}"))
entry = getattr(lib, api["name"])

inputs = [p for p in api["params"] if p["name"] != api.get("data")]
assert len(inputs) == 1, f"expected one tensor input, got {[p['name'] for p in inputs]}"
spec = inputs[0]
ctype, dtype = KIND[spec["c"]]

x = np.fromfile(a.input, dtype=dtype)
assert x.nbytes == spec["bytes"], f"input is {x.nbytes} bytes, descriptor wants {spec['bytes']}"
weights = np.fromfile(a.directory / "weights.bin", dtype=np.uint8)

results, refs = api["results"], []
assert len(a.reference) == len(results), \
    f"{len(results)} results but {len(a.reference)} references"
outs = []
for r, path in zip(results, a.reference):
    rct, rdt = KIND[r["c"]]
    buf = np.zeros(r["elements"], rdt)
    buf[:] = np.nan if rdt in (np.float32, np.float64) else 0
    outs.append((r, rct, buf))
    ref = np.fromfile(path, dtype=rdt)
    assert ref.nbytes == r["bytes"], f"{path}: {ref.nbytes} bytes, descriptor wants {r['bytes']}"
    refs.append(ref)

entry.argtypes = ([ctypes.POINTER(ctype), ctypes.POINTER(ctypes.c_ubyte)] +
                  [ctypes.POINTER(rct) for _, rct, _ in outs])
entry.restype = None
px = x.ctypes.data_as(ctypes.POINTER(ctype))
pw = weights.ctypes.data_as(ctypes.POINTER(ctypes.c_ubyte))
pouts = [buf.ctypes.data_as(ctypes.POINTER(rct)) for _, rct, buf in outs]
args = [px, pw, *pouts]

for _ in range(a.warmup):
    entry(*args)

rows = []
for i in range(a.inner):
    start = time.perf_counter()
    entry(*args)
    rows.append(time.perf_counter() - start)

digest = hashlib.sha256()
worst, failed = 0.0, 0
for (r, _, buf), ref in zip(outs, refs):
    if not np.isfinite(buf).all():
        sys.exit(f"{r['name']}: non-finite output")
    digest.update(buf.tobytes())
    diff = np.abs(buf.astype(np.float64) - ref.astype(np.float64))
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
