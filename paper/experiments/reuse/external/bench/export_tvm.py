#!/usr/bin/env python3
"""Build the UltraFace Relax module with TVM's C target and export it with explicit cc options."""
import argparse, hashlib, json, sys, time
from pathlib import Path
import onnx, tvm
from tvm import relax
from tvm.relax.frontend.onnx import from_onnx
from tvm.support import cc as tvm_cc

ap = argparse.ArgumentParser()
ap.add_argument("--model", default="/Users/lancer/Documents/Item/joggle/build-study/tvm-control/ultraface-rfb-320-opset13.onnx")
ap.add_argument("--out", required=True)
ap.add_argument("--opt", default="-O2")
a = ap.parse_args()
mod = from_onnx(onnx.load(a.model), shape_dict={"input": [1, 3, 240, 320]}, keep_params_in_input=False)
t0 = time.perf_counter()
ex = relax.build(mod, target=tvm.target.Target("c"))
fcompile = lambda output, objects, **kw: tvm_cc.create_shared(output, objects, options=list(kw.get("options") or []) + [a.opt])
ex.export_library(a.out, fcompile=fcompile)
dt = time.perf_counter() - t0
rec = {"library": a.out, "sha256": hashlib.sha256(Path(a.out).read_bytes()).hexdigest(), "cc_options": [a.opt],
       "build_and_export_seconds": dt, "tvm": tvm.__version__, "model_sha256": hashlib.sha256(Path(a.model).read_bytes()).hexdigest()}
Path(a.out + ".json").write_text(json.dumps(rec, indent=1) + "\n")
print(json.dumps(rec))
