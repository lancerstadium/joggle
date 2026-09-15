#!/usr/bin/env python3
"""Generate deterministic ONNX fixtures from the frozen study contracts."""

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


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def encode(message: Any) -> bytes:
    return message.SerializeToString(deterministic=True)


def implementation(contract: dict[str, Any]) -> dict[str, bytes]:
    left_shape = tuple(contract["left_shape"])
    right_shape = tuple(contract["right_shape"])
    if len(left_shape) != 2 or len(right_shape) != 2:
        raise ValueError("implementation fixture requires two rank-two inputs")
    if left_shape[1] != right_shape[0]:
        raise ValueError("implementation fixture has incompatible matrix shapes")

    left = np.asarray(contract["left"], dtype=np.float32).reshape(left_shape)
    right = np.asarray(contract["right"], dtype=np.float32).reshape(right_shape)
    expected = np.asarray(contract["expected"], dtype=np.float32).reshape(
        left_shape[0], right_shape[1]
    )
    tolerance = float(contract["absolute_tolerance"])
    if not np.allclose(left @ right, expected, rtol=0.0, atol=tolerance):
        raise ValueError("implementation fixture disagrees with its frozen oracle")

    graph = helper.make_graph(
        [helper.make_node("MatMul", ["a", "b"], ["c"])],
        "joggle_extension_implementation",
        [
            helper.make_tensor_value_info("a", TensorProto.FLOAT, left_shape),
            helper.make_tensor_value_info("b", TensorProto.FLOAT, right_shape),
        ],
        [helper.make_tensor_value_info("c", TensorProto.FLOAT, expected.shape)],
    )
    model = helper.make_model(
        graph,
        producer_name="joggle-extension-study",
        producer_version="1",
        opset_imports=[helper.make_opsetid("", 13)],
        ir_version=8,
    )
    onnx.checker.check_model(model)
    reference = ReferenceEvaluator(model).run(None, {"a": left, "b": right})[0]
    if not np.allclose(reference, expected, rtol=0.0, atol=tolerance):
        raise ValueError("ONNX reference evaluator disagrees with the frozen oracle")
    return {
        "model.onnx": encode(model),
        "test_data_set_0/input_0.pb": encode(numpy_helper.from_array(left, "a")),
        "test_data_set_0/input_1.pb": encode(numpy_helper.from_array(right, "b")),
        "test_data_set_0/output_0.pb": encode(
            numpy_helper.from_array(expected, "c")
        ),
    }


def policy(contract: dict[str, Any]) -> dict[str, bytes]:
    fusion = contract["fusion"]
    extent = int(fusion["extent"])
    names = ("a", "b", "c", "d")
    inputs = {
        name: np.asarray(fusion["inputs"][name], dtype=np.float32)
        for name in names
    }
    if extent <= 0 or any(value.shape != (extent,) for value in inputs.values()):
        raise ValueError("policy fixture inputs must match the positive static extent")
    expected = np.asarray(fusion["expected"], dtype=np.float32)
    if expected.shape != (extent,):
        raise ValueError("policy fixture output must match the static extent")
    actual = inputs["a"] + inputs["b"] + inputs["c"] + inputs["d"]
    tolerance = float(fusion["absolute_tolerance"])
    if not np.allclose(actual, expected, rtol=0.0, atol=tolerance):
        raise ValueError("policy fixture disagrees with its frozen oracle")

    graph = helper.make_graph(
        [
            helper.make_node("Add", ["a", "b"], ["ab"]),
            helper.make_node("Add", ["ab", "c"], ["abc"]),
            helper.make_node("Add", ["abc", "d"], ["result"]),
        ],
        "joggle_extension_policy",
        [
            helper.make_tensor_value_info(name, TensorProto.FLOAT, [extent])
            for name in names
        ],
        [helper.make_tensor_value_info("result", TensorProto.FLOAT, [extent])],
    )
    model = helper.make_model(
        graph,
        producer_name="joggle-extension-study",
        producer_version="1",
        opset_imports=[helper.make_opsetid("", 13)],
        ir_version=8,
    )
    onnx.checker.check_model(model)
    reference = ReferenceEvaluator(model).run(None, inputs)[0]
    if not np.allclose(reference, expected, rtol=0.0, atol=tolerance):
        raise ValueError("ONNX reference evaluator disagrees with the policy oracle")
    files = {
        "model.onnx": encode(model),
        "test_data_set_0/output_0.pb": encode(
            numpy_helper.from_array(expected, "result")
        ),
    }
    files.update(
        {
            f"test_data_set_0/input_{index}.pb": encode(
                numpy_helper.from_array(inputs[name], name)
            )
            for index, name in enumerate(names)
        }
    )
    return files


def numeric_format(contract: dict[str, Any]) -> dict[str, bytes]:
    minimum = int(contract["valid_widths"]["minimum"])
    maximum = int(contract["valid_widths"]["maximum"])
    cases: list[tuple[str, int, np.ndarray, np.ndarray, np.ndarray]] = []

    for index, case in enumerate(contract["scalar_cases"]):
        width = int(case["width"])
        cases.append(
            (
                f"scalar_{index}",
                width,
                np.asarray(case["left"], dtype=np.int64),
                np.asarray(case["right"], dtype=np.int64),
                np.asarray(case["expected"], dtype=np.int64),
            )
        )
    tensor = contract["tensor_case"]
    cases.append(
        (
            "tensor",
            int(tensor["width"]),
            np.asarray(tensor["left"], dtype=np.int64),
            np.asarray(tensor["right"], dtype=np.int64),
            np.asarray(tensor["expected"], dtype=np.int64),
        )
    )

    nodes = []
    inputs = []
    outputs = []
    feeds: dict[str, np.ndarray] = {}
    expected_values: dict[str, np.ndarray] = {}
    formats = []
    for name, width, left, right, expected in cases:
        if width < minimum or width > maximum:
            raise ValueError(f"numeric-format width is outside the contract: {width}")
        if left.shape != right.shape or left.shape != expected.shape:
            raise ValueError(f"numeric-format case has inconsistent shapes: {name}")
        low = -(1 << (width - 1))
        high = (1 << (width - 1)) - 1
        saturated = np.asarray(
            [min(high, max(low, int(a) + int(b)))
             for a, b in zip(left.reshape(-1), right.reshape(-1))],
            dtype=np.int64,
        ).reshape(left.shape)
        if not np.array_equal(saturated, expected):
            raise ValueError(f"numeric-format oracle is inconsistent: {name}")

        left_name = f"{name}_left"
        right_name = f"{name}_right"
        result_name = f"{name}_result"
        node_name = f"{name}_add"
        nodes.append(
            helper.make_node(
                "Add", [left_name, right_name], [result_name], name=node_name
            )
        )
        shape = list(left.shape)
        inputs.extend(
            [
                helper.make_tensor_value_info(left_name, TensorProto.INT64, shape),
                helper.make_tensor_value_info(right_name, TensorProto.INT64, shape),
            ]
        )
        outputs.append(
            helper.make_tensor_value_info(result_name, TensorProto.INT64, shape)
        )
        feeds[left_name] = left
        feeds[right_name] = right
        expected_values[result_name] = expected
        formats.append({"node": node_name, "width": width})

    graph = helper.make_graph(
        nodes,
        "joggle_extension_numeric_format",
        inputs,
        outputs,
    )
    model = helper.make_model(
        graph,
        producer_name="joggle-extension-study",
        producer_version="1",
        opset_imports=[helper.make_opsetid("", 13)],
        ir_version=8,
    )
    onnx.checker.check_model(model)
    ordinary = ReferenceEvaluator(model).run(None, feeds)
    if all(
        np.array_equal(value, expected_values[output.name])
        for value, output in zip(ordinary, model.graph.output)
    ):
        raise ValueError("numeric-format fixture does not exercise saturation")

    format_map = {
        "schema": 1,
        "carrier": "int64",
        "type": "signed saturating integer",
        "operations": formats,
    }
    files = {
        "model.onnx": encode(model),
        "format-map.json": (
            json.dumps(format_map, indent=2, sort_keys=True) + "\n"
        ).encode(),
    }
    input_index = 0
    for value_info in model.graph.input:
        value = feeds[value_info.name]
        files[f"test_data_set_0/input_{input_index}.pb"] = encode(
            numpy_helper.from_array(value, value_info.name)
        )
        input_index += 1
    for output_index, value_info in enumerate(model.graph.output):
        files[f"test_data_set_0/output_{output_index}.pb"] = encode(
            numpy_helper.from_array(
                expected_values[value_info.name], value_info.name
            )
        )
    return files


def evolution(contract: dict[str, Any]) -> dict[str, bytes]:
    source = contract["source"]
    if source["format"] != "ONNX" or source["graph"] != "Conv -> Add -> Relu":
        raise ValueError(
            "evolution fixture requires the frozen ONNX Conv -> Add -> Relu graph"
        )
    if source["layout"] != "NCHW" or source["weight_layout"] != "OIHW":
        raise ValueError("evolution fixture requires NCHW activations and OIHW weights")

    stride = [int(value) for value in source["stride"]]
    pads = [int(value) for value in source["pads"]]
    if len(stride) != 2 or len(pads) != 4:
        raise ValueError("evolution fixture has invalid convolution attributes")

    rng = np.random.default_rng(int(source["seed"]))
    files: dict[str, bytes] = {}
    for case in source["cases"]:
        name = str(case["id"])
        input_shape = tuple(int(value) for value in case["input"])
        weight_shape = tuple(int(value) for value in case["weight"])
        bias_shape = tuple(int(value) for value in case["bias"])
        output_shape = tuple(int(value) for value in case["output"])
        if len(input_shape) != 4 or len(weight_shape) != 4:
            raise ValueError(f"evolution case must be rank four: {name}")
        if bias_shape != (1, weight_shape[0], 1, 1):
            raise ValueError(f"evolution case has inconsistent bias shape: {name}")
        if input_shape[1] != weight_shape[1]:
            raise ValueError(f"evolution case has inconsistent channels: {name}")

        x = (rng.standard_normal(input_shape) * 0.25).astype(np.float32)
        weight = (rng.standard_normal(weight_shape) * 0.25).astype(np.float32)
        bias = (rng.standard_normal(bias_shape) * 0.10).astype(np.float32)
        weight_proto = numpy_helper.from_array(weight, "weight")
        bias_proto = numpy_helper.from_array(bias, "bias")
        graph = helper.make_graph(
            [
                helper.make_node(
                    "Conv",
                    ["x", "weight"],
                    ["convolved"],
                    name="projection_conv",
                    strides=stride,
                    pads=pads,
                ),
                helper.make_node(
                    "Add", ["convolved", "bias"], ["biased"], name="projection_bias"
                ),
                helper.make_node(
                    "Relu", ["biased"], ["y"], name="projection_relu"
                ),
            ],
            f"joggle_vertical_evolution_{name}",
            [helper.make_tensor_value_info("x", TensorProto.FLOAT, input_shape)],
            [helper.make_tensor_value_info("y", TensorProto.FLOAT, output_shape)],
            initializer=[weight_proto, bias_proto],
        )
        model = helper.make_model(
            graph,
            producer_name="joggle-evolution-study",
            producer_version="1",
            opset_imports=[helper.make_opsetid("", int(source["opset"]))],
            ir_version=8,
        )
        onnx.checker.check_model(model)
        expected = ReferenceEvaluator(model).run(None, {"x": x})[0]
        if tuple(expected.shape) != output_shape:
            raise ValueError(f"evolution case has inconsistent output shape: {name}")
        if not np.isfinite(expected).all():
            raise ValueError(f"evolution case produced a non-finite oracle: {name}")

        prefix = f"{name}/"
        files[prefix + "model.onnx"] = encode(model)
        files[prefix + "weight.pb"] = encode(weight_proto)
        files[prefix + "bias.pb"] = encode(bias_proto)
        files[prefix + "test_data_set_0/input_0.pb"] = encode(
            numpy_helper.from_array(x, "x")
        )
        files[prefix + "test_data_set_0/output_0.pb"] = encode(
            numpy_helper.from_array(expected.astype(np.float32), "y")
        )
        files[prefix + "input.bin"] = x.tobytes(order="C")
        files[prefix + "expected.bin"] = expected.astype(np.float32).tobytes(
            order="C"
        )
    return files


def materialize(
    root: Path,
    files: dict[str, bytes],
    check: bool,
    contract: Path,
    contract_bytes: bytes,
) -> None:
    manifest = {
        "schema": 1,
        "generator": "paper/fixtures/generate.py",
        "contract": str(contract),
        "contract_sha256": digest(contract_bytes),
        "onnx": onnx.__version__,
        "numpy": np.__version__,
        "files": {name: digest(data) for name, data in sorted(files.items())},
    }
    files = dict(files)
    files["manifest.json"] = (
        json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    ).encode()
    for name, data in files.items():
        path = root / name
        if check:
            if not path.is_file() or path.read_bytes() != data:
                raise SystemExit(f"fixture differs from generator output: {path}")
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--fixture",
        choices=("implementation", "policy", "numeric-format", "evolution"),
        default="implementation",
    )
    parser.add_argument(
        "--task",
        type=Path,
    )
    parser.add_argument(
        "--output",
        type=Path,
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify checked-in bytes instead of writing them",
    )
    args = parser.parse_args()

    task = args.task or Path(f"paper/tasks/{args.fixture}.json")
    output = args.output or Path(f"paper/fixtures/{args.fixture}")

    contract_bytes = task.read_bytes()
    contract = json.loads(contract_bytes)
    generators = {
        "implementation": implementation,
        "policy": policy,
        "numeric-format": numeric_format,
        "evolution": evolution,
    }
    materialize(
        output,
        generators[args.fixture](contract),
        args.check,
        task,
        contract_bytes,
    )


if __name__ == "__main__":
    main()
