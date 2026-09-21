#!/usr/bin/env python3
"""Generate and verify the frozen ONNX operator fixtures."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

import numpy as np

try:
    import onnx
    from onnx import checker, helper, numpy_helper
except ImportError as error:
    raise SystemExit("onnx is required: python3 -m pip install onnx") from error


NUMPY_DTYPES = {
    "float32": np.dtype("<f4"),
    "uint8": np.dtype("u1"),
    "int8": np.dtype("i1"),
    "int32": np.dtype("<i4"),
}
ONNX_DTYPES = {
    "float32": onnx.TensorProto.FLOAT,
    "uint8": onnx.TensorProto.UINT8,
    "int8": onnx.TensorProto.INT8,
    "int32": onnx.TensorProto.INT32,
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write_once(path: Path, data: bytes) -> None:
    if path.exists():
        if path.read_bytes() != data:
            raise SystemExit(f"refusing to replace non-matching {path}")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def tensor_array(path: Path, tensor: dict[str, Any]) -> np.ndarray:
    try:
        dtype = NUMPY_DTYPES[tensor["dtype"]]
    except KeyError as error:
        raise SystemExit(f"unsupported dtype {tensor['dtype']}") from error
    data = path.read_bytes()
    expected = int(np.prod(tensor["shape"], dtype=np.int64)) * dtype.itemsize
    if len(data) != expected:
        raise SystemExit(f"{path}: expected {expected} bytes, found {len(data)}")
    return np.frombuffer(data, dtype=dtype).reshape(tensor["shape"]).copy()


def case_record(index: dict[str, Any], case_id: str) -> dict[str, Any]:
    matches = [record for record in index["cases"]
               if record["group"] == "operators" and record["id"] == case_id]
    if len(matches) != 1:
        raise SystemExit(f"{case_id}: expected one operator input record")
    return matches[0]


def make_model(
    case: dict[str, Any], record: dict[str, Any], input_root: Path, opset: int
) -> tuple[onnx.ModelProto, dict[str, np.ndarray]]:
    tensors = {item["name"]: item for item in record["tensors"]}
    feeds: dict[str, np.ndarray] = {}
    graph_inputs = []
    initializers = []
    for spec in case["inputs"]:
        item = tensors[spec["name"]]
        value = tensor_array(input_root / item["path"], spec)
        feeds[spec["name"]] = value
        graph_inputs.append(helper.make_tensor_value_info(
            spec["name"], ONNX_DTYPES[spec["dtype"]], spec["shape"]
        ))
    for name, spec in case["initializers"].items():
        item = tensors[name]
        value = tensor_array(input_root / item["path"], spec)
        initializers.append(numpy_helper.from_array(value, name=name))
    nodes = [
        helper.make_node(
            node["op"], node["inputs"], node["outputs"],
            name=f"{case['id']}-{position:02d}-{node['op'].lower()}",
            **node["attributes"],
        )
        for position, node in enumerate(case["nodes"])
    ]
    graph_outputs = [
        helper.make_tensor_value_info(
            output["name"], ONNX_DTYPES[output["dtype"]], output["shape"]
        )
        for output in case["outputs"]
    ]
    graph = helper.make_graph(
        nodes, case["id"], graph_inputs, graph_outputs, initializer=initializers
    )
    model = helper.make_model(
        graph,
        producer_name="joggle-artifact",
        producer_version="1",
        opset_imports=[helper.make_opsetid("", opset)],
    )
    # IR version 9 is accepted by all runtimes in the frozen comparison matrix.
    model.ir_version = 9
    checker.check_model(model, full_check=True)
    return model, feeds


def verify_runtime(
    model_bytes: bytes, case: dict[str, Any], feeds: dict[str, np.ndarray]
) -> list[dict[str, Any]]:
    try:
        import onnxruntime as ort
    except ImportError as error:
        raise SystemExit("onnxruntime is required for --verify-runtime") from error
    options = ort.SessionOptions()
    options.intra_op_num_threads = 1
    options.inter_op_num_threads = 1
    options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_DISABLE_ALL
    options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
    options.log_severity_level = 3
    session = ort.InferenceSession(
        model_bytes, sess_options=options, providers=["CPUExecutionProvider"]
    )
    values = session.run([output["name"] for output in case["outputs"]], feeds)
    records = []
    for declared, value in zip(case["outputs"], values, strict=True):
        expected_dtype = NUMPY_DTYPES[declared["dtype"]]
        if value.dtype != expected_dtype:
            raise SystemExit(
                f"{case['id']}:{declared['name']}: dtype {value.dtype} != {expected_dtype}"
            )
        if list(value.shape) != declared["shape"]:
            raise SystemExit(
                f"{case['id']}:{declared['name']}: shape {list(value.shape)} "
                f"!= {declared['shape']}"
            )
        data = value.tobytes(order="C")
        records.append({
            "name": declared["name"],
            "dtype": declared["dtype"],
            "shape": declared["shape"],
            "bytes": len(data),
            "sha256": sha256(data),
        })
    return records


def main() -> int:
    parser = argparse.ArgumentParser()
    root = Path(__file__).resolve().parent
    parser.add_argument("--spec", type=Path,
                        default=root / "manifests" / "benchmark-cases.json")
    parser.add_argument("--inputs", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--verify-runtime", action="store_true")
    args = parser.parse_args()

    spec_bytes = args.spec.read_bytes()
    spec_hash = sha256(spec_bytes)
    spec = json.loads(spec_bytes)
    input_index = json.loads((args.inputs / "index.json").read_text())
    if input_index.get("spec_sha256") != spec_hash:
        raise SystemExit("input index was generated from a different benchmark specification")

    records = []
    for case in spec["operator_cases"]:
        source = case_record(input_index, case["id"])
        model, feeds = make_model(
            case, source, args.inputs, spec["measurement"]["operator_opset"]
        )
        model_bytes = model.SerializeToString(deterministic=True)
        relative = Path(f"{case['id']}.onnx")
        write_once(args.output / relative, model_bytes)
        outputs = verify_runtime(model_bytes, case, feeds) if args.verify_runtime else []
        records.append({
            "id": case["id"],
            "family": case["family"],
            "path": relative.as_posix(),
            "bytes": len(model_bytes),
            "sha256": sha256(model_bytes),
            "outputs": outputs,
        })

    output_index = {
        "schema_version": 1,
        "spec_sha256": spec_hash,
        "opset": spec["measurement"]["operator_opset"],
        "runtime_verified": args.verify_runtime,
        "cases": records,
    }
    encoded = (json.dumps(output_index, indent=2, sort_keys=True) + "\n").encode()
    write_once(args.output / "index.json", encoded)
    print(
        f"generated {len(records)} operator models in {args.output}; "
        f"runtime_verified={args.verify_runtime}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
