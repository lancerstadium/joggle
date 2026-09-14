#!/usr/bin/env python3
"""Materialize deterministic operator-study ONNX cases and their oracles."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper
from onnx.reference import ReferenceEvaluator


M_EXTENTS = (1, 16, 128)
K_EXTENTS = (128, 256, 512)
N_EXTENTS = (128, 256, 512)
ROW_EXTENTS = (1, 4, 16, 64, 256)
WIDTH_EXTENTS = (64, 128, 256, 512, 1024)
ROW_OPERATIONS = (
    "add",
    "multiply",
    "relu",
    "silu",
    "softmax",
    "reduce_mean",
    "rmsnorm",
    "layernorm",
)
CONTRACTION_OPERATIONS = (
    "matmul",
    "matmul_add",
    "matmul_relu",
    "softmax_matmul",
    "rmsnorm_matmul",
    "silu_matmul",
    "swiglu",
    "qkv_projection",
)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def encode(message: Any) -> bytes:
    return message.SerializeToString(deterministic=True)


def values(shape: tuple[int, ...], multiplier: int, modulus: int) -> np.ndarray:
    count = int(np.prod(shape, dtype=np.int64))
    sequence = np.arange(count, dtype=np.int64)
    centered = (sequence * multiplier) % modulus - modulus // 2
    return (centered.astype(np.float32) / np.float32(modulus)).reshape(shape)


def contraction_case(
    operation: str, m: int, k: int, n: int
) -> tuple[dict[str, bytes], dict[str, Any]]:
    case = f"{operation}-m{m}-k{k}-n{n}"
    data = values((m, k), 5, 29)
    weight = values((k, n), 7, 31)
    initializers = [numpy_helper.from_array(weight, "weight")]
    nodes = []
    source = "data"
    if operation == "softmax_matmul":
        nodes.append(helper.make_node("Softmax", [source], ["normalized"], axis=-1))
        source = "normalized"
    elif operation == "rmsnorm_matmul":
        epsilon = np.asarray(1.0e-5, dtype=np.float32)
        initializers.append(numpy_helper.from_array(epsilon, "epsilon"))
        nodes += [
            helper.make_node("Mul", [source, source], ["square"]),
            helper.make_node(
                "ReduceMean", ["square"], ["variance"], axes=[-1], keepdims=1
            ),
            helper.make_node("Add", ["variance", "epsilon"], ["stabilized"]),
            helper.make_node("Sqrt", ["stabilized"], ["scale"]),
            helper.make_node("Div", [source, "scale"], ["normalized"]),
        ]
        source = "normalized"
    elif operation == "silu_matmul":
        nodes += [
            helper.make_node("Sigmoid", [source], ["gate"]),
            helper.make_node("Mul", [source, "gate"], ["activated"]),
        ]
        source = "activated"

    output_shape = [m, n]
    weight_elements = k * n
    matmul_flops = 2 * m * k * n
    if operation == "swiglu":
        gate_weight = values((k, n), 11, 37)
        up_weight = values((k, n), 13, 41)
        initializers = [
            numpy_helper.from_array(gate_weight, "gate_weight"),
            numpy_helper.from_array(up_weight, "up_weight"),
        ]
        nodes += [
            helper.make_node("MatMul", ["data", "gate_weight"], ["gate_linear"]),
            helper.make_node("Sigmoid", ["gate_linear"], ["gate_sigmoid"]),
            helper.make_node("Mul", ["gate_linear", "gate_sigmoid"], ["gate"]),
            helper.make_node("MatMul", ["data", "up_weight"], ["up"]),
            helper.make_node("Mul", ["gate", "up"], ["result"]),
        ]
        weight_elements = 2 * k * n
        matmul_flops = 4 * m * k * n
    elif operation == "qkv_projection":
        weights = [
            values((k, n), 11, 37),
            values((k, n), 13, 41),
            values((k, n), 17, 43),
        ]
        initializers = [
            numpy_helper.from_array(value, name)
            for value, name in zip(weights, ("q_weight", "k_weight", "v_weight"))
        ]
        nodes += [
            helper.make_node("MatMul", ["data", "q_weight"], ["q"]),
            helper.make_node("MatMul", ["data", "k_weight"], ["k"]),
            helper.make_node("MatMul", ["data", "v_weight"], ["v"]),
            helper.make_node("Concat", ["q", "k", "v"], ["result"], axis=-1),
        ]
        output_shape = [m, 3 * n]
        weight_elements = 3 * k * n
        matmul_flops = 6 * m * k * n
    else:
        nodes.append(helper.make_node("MatMul", [source, "weight"], ["linear"]))
        result = "linear"
        if operation == "matmul_add":
            bias = values((n,), 11, 37)
            initializers.append(numpy_helper.from_array(bias, "bias"))
            nodes.append(helper.make_node("Add", [result, "bias"], ["result"]))
        elif operation == "matmul_relu":
            nodes.append(helper.make_node("Relu", [result], ["result"]))
        else:
            nodes.append(helper.make_node("Identity", [result], ["result"]))
    graph = helper.make_graph(
        nodes,
        case,
        [helper.make_tensor_value_info("data", TensorProto.FLOAT, [m, k])],
        [helper.make_tensor_value_info("result", TensorProto.FLOAT, output_shape)],
        initializers,
    )
    model = helper.make_model(
        graph,
        producer_name="joggle-operator-study",
        opset_imports=[helper.make_opsetid("", 13)],
        ir_version=8,
    )
    onnx.checker.check_model(model)
    result = np.asarray(
        ReferenceEvaluator(model).run(None, {"data": data})[0],
        dtype=np.float32,
    )
    if tuple(result.shape) != tuple(output_shape) or not np.isfinite(result).all():
        raise RuntimeError(f"invalid reference result for {case}")
    files = {
        "model.onnx": encode(model),
        "test_data_set_0/input_0.pb": encode(
            numpy_helper.from_array(data, "data")
        ),
        "test_data_set_0/output_0.pb": encode(
            numpy_helper.from_array(result, "result")
        ),
    }
    record = {
        "case": case,
        "operation": operation,
        "shape": {"M": m, "K": k, "N": n},
        "input_elements": m * k,
        "weight_elements": weight_elements,
        "output_elements": int(result.size),
        "matmul_flops": matmul_flops,
    }
    return files, record


def row_case(operation: str, rows: int, width: int) -> tuple[dict[str, bytes], dict[str, Any]]:
    case = f"{operation}-m{rows}-n{width}"
    data = values((rows, width), 5, 29)
    initializers = []
    if operation in {"add", "multiply"}:
        parameter = values((width,), 7, 31)
        if operation == "multiply":
            parameter = parameter + np.float32(1.0)
        initializers.append(numpy_helper.from_array(parameter, "parameter"))
        nodes = [
            helper.make_node(
                "Add" if operation == "add" else "Mul",
                ["data", "parameter"], ["result"],
            )
        ]
    elif operation == "relu":
        nodes = [helper.make_node("Relu", ["data"], ["result"])]
    elif operation == "silu":
        nodes = [
            helper.make_node("Sigmoid", ["data"], ["gate"]),
            helper.make_node("Mul", ["data", "gate"], ["result"]),
        ]
    elif operation == "softmax":
        nodes = [helper.make_node("Softmax", ["data"], ["result"], axis=-1)]
    elif operation == "reduce_mean":
        nodes = [
            helper.make_node(
                "ReduceMean", ["data"], ["result"], axes=[-1], keepdims=1
            )
        ]
    elif operation in {"rmsnorm", "layernorm"}:
        epsilon = np.asarray(1.0e-5, dtype=np.float32)
        initializers.append(numpy_helper.from_array(epsilon, "epsilon"))
        nodes = []
        normalized = "data"
        if operation == "layernorm":
            nodes += [
                helper.make_node(
                    "ReduceMean", ["data"], ["mean"], axes=[-1], keepdims=1
                ),
                helper.make_node("Sub", ["data", "mean"], ["centered"]),
            ]
            normalized = "centered"
        nodes += [
            helper.make_node("Mul", [normalized, normalized], ["square"]),
            helper.make_node(
                "ReduceMean", ["square"], ["variance"],
                axes=[-1], keepdims=1,
            ),
            helper.make_node("Add", ["variance", "epsilon"], ["stabilized"]),
            helper.make_node("Sqrt", ["stabilized"], ["scale"]),
            helper.make_node("Div", [normalized, "scale"], ["result"]),
        ]
    else:
        raise ValueError(f"unknown row operation: {operation}")

    output_shape = [rows, 1] if operation == "reduce_mean" else [rows, width]
    graph = helper.make_graph(
        nodes,
        case,
        [helper.make_tensor_value_info("data", TensorProto.FLOAT, [rows, width])],
        [helper.make_tensor_value_info("result", TensorProto.FLOAT, output_shape)],
        initializers,
    )
    model = helper.make_model(
        graph,
        producer_name="joggle-operator-study",
        opset_imports=[helper.make_opsetid("", 13)],
        ir_version=8,
    )
    onnx.checker.check_model(model)
    result = np.asarray(
        ReferenceEvaluator(model).run(None, {"data": data})[0],
        dtype=np.float32,
    )
    if tuple(result.shape) != tuple(output_shape) or not np.isfinite(result).all():
        raise RuntimeError(f"invalid reference result for {case}")
    files = {
        "model.onnx": encode(model),
        "test_data_set_0/input_0.pb": encode(
            numpy_helper.from_array(data, "data")
        ),
        "test_data_set_0/output_0.pb": encode(
            numpy_helper.from_array(result, "result")
        ),
    }
    record = {
        "case": case,
        "operation": operation,
        "shape": {"M": rows, "N": width},
        "input_elements": rows * width,
        "output_elements": int(result.size),
    }
    return files, record


def all_cases() -> list[tuple[str, tuple[int, ...]]]:
    return [
        (operation, (m, k, n))
        for operation in CONTRACTION_OPERATIONS
        for m in M_EXTENTS
        for k in K_EXTENTS
        for n in N_EXTENTS
    ] + [
        (operation, (rows, width))
        for operation in ROW_OPERATIONS
        for rows in ROW_EXTENTS
        for width in WIDTH_EXTENTS
    ]


def case_name(spec: tuple[str, tuple[int, ...]]) -> str:
    operation, shape = spec
    if operation in CONTRACTION_OPERATIONS:
        return f"matmul-m{shape[0]}-k{shape[1]}-n{shape[2]}"
    return f"{operation}-m{shape[0]}-n{shape[1]}"


def requested_cases(names: set[str]) -> list[tuple[str, tuple[int, ...]]]:
    cases = all_cases()
    known = {case_name(case) for case in cases}
    unknown = names - known
    if unknown:
        raise SystemExit(f"unknown case(s): {', '.join(sorted(unknown))}")
    return [case for case in cases if not names or case_name(case) in names]


def materialize(root: Path, selected: set[str], check: bool) -> None:
    records = []
    for operation, shape in requested_cases(selected):
        if operation in CONTRACTION_OPERATIONS:
            files, record = contraction_case(operation, *shape)
        else:
            files, record = row_case(operation, *shape)
        record["files"] = {
            name: digest(data) for name, data in sorted(files.items())
        }
        records.append(record)
        for name, data in files.items():
            path = root / record["case"] / name
            if check:
                if not path.is_file() or path.read_bytes() != data:
                    raise SystemExit(f"operator fixture differs: {path}")
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)

    manifest = {
        "schema": 1,
        "suite": "operator-study",
        "generator": "paper/operator_suite.py",
        "onnx": onnx.__version__,
        "numpy": np.__version__,
        "cases": records,
    }
    manifest_bytes = (
        json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    ).encode()
    manifest_path = root / "manifest.json"
    if check:
        if not manifest_path.is_file() or manifest_path.read_bytes() != manifest_bytes:
            raise SystemExit(f"operator manifest differs: {manifest_path}")
    else:
        root.mkdir(parents=True, exist_ok=True)
        manifest_path.write_bytes(manifest_bytes)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output", type=Path, default=Path("build/operator-study/fixtures")
    )
    parser.add_argument(
        "--case", action="append", default=[], help="materialize one named case"
    )
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    materialize(args.output, set(args.case), args.check)


if __name__ == "__main__":
    main()
