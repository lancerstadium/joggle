#!/usr/bin/env python3
"""Export one pinned ONNX model through TVM's default C target.

Derives the input name and shape from the model instead of hard-coding them,
and upgrades the opset when TVM's Relax ONNX frontend rejects the pinned one,
recording the converted model's digest so the substitution stays auditable.

The result bounds TVM's default lowering. It is not TVM with MetaSchedule, a
tuned schedule, or an LLVM target.
"""
import argparse, hashlib, json, time, traceback
from pathlib import Path

import numpy as np
import onnx
from onnx import version_converter
import tvm
from tvm import relax
from tvm.relax.frontend.onnx import from_onnx
from tvm.support import cc as tvm_cc


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("model", type=Path)
    ap.add_argument("out", type=Path, help="model study directory")
    ap.add_argument("--opt", default="-O2")
    ap.add_argument("--target-opset", type=int, default=13)
    a = ap.parse_args()

    record = {"model": str(a.model), "model_sha256": digest(a.model),
              "tvm": tvm.__version__, "cc_options": [a.opt]}
    loaded = onnx.load(a.model)

    api = json.loads((a.out / "base" / "api.json").read_text())[0]
    graph_input = loaded.graph.input[0]
    shape = [d.dim_value if d.HasField("dim_value") and d.dim_value else 1
             for d in graph_input.type.tensor_type.shape.dim]
    expected = api["params"][0]["shape"]
    if list(shape) != list(expected):
        record["shape_note"] = f"ONNX {shape} vs artifact {expected}; using the artifact shape"
        shape = list(expected)
    shape_dict = {graph_input.name: shape}
    record["input"] = {"name": graph_input.name, "shape": shape}

    try:
        mod = from_onnx(loaded, shape_dict=shape_dict, keep_params_in_input=False)
        record["opset_converted"] = False
    except Exception as first:
        record["frontend_rejected_pinned_opset"] = str(first)[:400]
        converted = version_converter.convert_version(loaded, a.target_opset)
        path = a.out / f"tvm-opset{a.target_opset}.onnx"
        onnx.save(converted, str(path))
        record["opset_converted"] = True
        record["converted_model"] = str(path)
        record["converted_sha256"] = digest(path)
        mod = from_onnx(converted, shape_dict=shape_dict, keep_params_in_input=False)

    library = a.out / "tvm_c_O2.dylib"
    start = time.perf_counter()
    built = relax.build(mod, target=tvm.target.Target("c"))
    built.export_library(
        str(library),
        fcompile=lambda output, objects, **kw: tvm_cc.create_shared(
            output, objects, options=list(kw.get("options") or []) + [a.opt]))
    record["build_and_export_seconds"] = time.perf_counter() - start
    record["library"] = str(library)
    record["library_sha256"] = digest(library)
    (a.out / "tvm.json").write_text(json.dumps(record, indent=1) + "\n")
    print(json.dumps({k: record[k] for k in
                      ("opset_converted", "build_and_export_seconds", "library")}, indent=1))


if __name__ == "__main__":
    main()
