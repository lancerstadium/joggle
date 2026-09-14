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


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def encode(message: Any) -> bytes:
    return message.SerializeToString(deterministic=True)


def values(shape: tuple[int, ...], multiplier: int, modulus: int) -> np.ndarray:
    count = int(np.prod(shape, dtype=np.int64))
    sequence = np.arange(count, dtype=np.int64)
    centered = (sequence * multiplier) % modulus - modulus // 2
    return (centered.astype(np.float32) / np.float32(modulus)).reshape(shape)


def matmul_case(m: int, k: int, n: int) -> tuple[dict[str, bytes], dict[str, Any]]:
    case = f"matmul-m{m}-k{k}-n{n}"
    data = values((m, k), 5, 29)
    weight = values((k, n), 7, 31)
    graph = helper.make_graph(
        [helper.make_node("MatMul", ["data", "weight"], ["result"])],
        case,
        [helper.make_tensor_value_info("data", TensorProto.FLOAT, [m, k])],
        [helper.make_tensor_value_info("result", TensorProto.FLOAT, [m, n])],
        [numpy_helper.from_array(weight, "weight")],
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
    direct = np.asarray(data @ weight, dtype=np.float32)
    if not np.allclose(result, direct, rtol=1.0e-5, atol=1.0e-5):
        raise RuntimeError(f"ONNX reference and NumPy disagree for {case}")
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
        "operation": "MatMul",
        "shape": {"M": m, "K": k, "N": n},
        "input_elements": m * k,
        "weight_elements": k * n,
        "output_elements": m * n,
        "floating_point_operations": 2 * m * k * n,
    }
    return files, record


def requested_cases(names: set[str]) -> list[tuple[int, int, int]]:
    cases = [
        (m, k, n)
        for m in M_EXTENTS
        for k in K_EXTENTS
        for n in N_EXTENTS
    ]
    known = {f"matmul-m{m}-k{k}-n{n}" for m, k, n in cases}
    unknown = names - known
    if unknown:
        raise SystemExit(f"unknown case(s): {', '.join(sorted(unknown))}")
    return [shape for shape in cases if not names or (
        f"matmul-m{shape[0]}-k{shape[1]}-n{shape[2]}" in names
    )]


def materialize(root: Path, selected: set[str], check: bool) -> None:
    records = []
    for m, k, n in requested_cases(selected):
        files, record = matmul_case(m, k, n)
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
