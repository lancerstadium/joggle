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
        "--task",
        type=Path,
        default=Path("paper/tasks/implementation.json"),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("paper/fixtures/implementation"),
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify checked-in bytes instead of writing them",
    )
    args = parser.parse_args()

    contract_bytes = args.task.read_bytes()
    contract = json.loads(contract_bytes)
    materialize(
        args.output,
        implementation(contract),
        args.check,
        args.task,
        contract_bytes,
    )


if __name__ == "__main__":
    main()
