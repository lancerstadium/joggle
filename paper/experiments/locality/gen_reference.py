#!/usr/bin/env python3
"""Generate a deterministic input and its ONNX Runtime reference for one model.

The input is drawn from a fixed seed so the fixture is reproducible without
shipping image data; this establishes agreement between compiled artifacts and
ONNX Runtime on one recorded input, not task accuracy.

References are written in the order of the Joggle artifact's own `c.api`
results, matched by name, so the timing subject can consume them positionally.
"""
import argparse, hashlib, json
from pathlib import Path

import numpy as np
import onnxruntime as ort

TYPE = {"tensor(float)": np.float32, "tensor(double)": np.float64,
        "tensor(int64)": np.int64, "tensor(int32)": np.int32}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("model", type=Path)
    ap.add_argument("out", type=Path, help="directory holding base/api.json")
    ap.add_argument("--seed", type=int, default=20260917)
    ap.add_argument("--batch", type=int, default=1)
    a = ap.parse_args()

    api = json.loads((a.out / "base" / "api.json").read_text())[0]
    # Execute the graph as written. With the default optimisation level ONNX
    # Runtime rewrites DequantizeLinear -> Conv -> QuantizeLinear into an integer
    # kernel, which is a different numerical path from the QDQ graph the compiler
    # is asked to implement; on EfficientNet-Lite4 that difference is 1.68e-3 and
    # would be charged to the compiler as an error. Disabling optimisation is the
    # general fix, not a per-model one, and it moves the float models' references
    # by at most 3.3e-6 against a 1e-4 tolerance.
    options = ort.SessionOptions()
    options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_DISABLE_ALL
    options.log_severity_level = 3
    session = ort.InferenceSession(str(a.model), options,
                                   providers=["CPUExecutionProvider"])
    inputs = session.get_inputs()
    if len(inputs) != 1:
        raise SystemExit(f"{a.model.name}: {len(inputs)} graph inputs, expected 1")
    spec = inputs[0]
    shape = [a.batch if not isinstance(d, int) else d for d in spec.shape]
    dtype = TYPE[spec.type]

    rng = np.random.default_rng(a.seed)
    x = rng.standard_normal(size=shape).astype(dtype) if dtype in (np.float32, np.float64) \
        else rng.integers(0, 2, size=shape).astype(dtype)

    descriptor_in = api["params"][0]
    if x.nbytes != descriptor_in["bytes"]:
        raise SystemExit(f"{a.model.name}: ORT input {x.nbytes} bytes vs "
                         f"artifact {descriptor_in['bytes']} bytes (shape {shape})")
    (a.out / "input.bin").write_bytes(x.tobytes())

    names = [o.name for o in session.get_outputs()]
    produced = dict(zip(names, session.run(names, {spec.name: x})))

    written, report = [], {"model": str(a.model), "model_sha256": digest(a.model),
                           "seed": a.seed, "input_shape": shape,
                           "input_sha256": digest(a.out / "input.bin"), "references": []}
    for i, result in enumerate(api["results"]):
        want = result["name"][:-4] if result["name"].endswith("_out") else result["name"]
        match = next((n for n in names if n == want), None)
        if match is None:
            match = names[i] if i < len(names) else None
        if match is None:
            raise SystemExit(f"no ORT output for artifact result {result['name']}")
        value = np.ascontiguousarray(produced[match])
        if value.nbytes != result["bytes"]:
            raise SystemExit(f"{result['name']}: ORT {value.nbytes} bytes vs "
                             f"artifact {result['bytes']} bytes")
        path = a.out / f"reference_{i}.bin"
        path.write_bytes(value.tobytes())
        written.append(str(path))
        report["references"].append({"artifact_result": result["name"], "ort_output": match,
                                     "bytes": value.nbytes, "sha256": digest(path)})
    (a.out / "reference.json").write_text(json.dumps(report, indent=1) + "\n")
    print(json.dumps({"input_shape": shape, "references": len(written),
                      "outputs": [r["ort_output"] for r in report["references"]]}, indent=1))


if __name__ == "__main__":
    main()
