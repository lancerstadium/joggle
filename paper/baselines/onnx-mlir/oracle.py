#!/usr/bin/env python3
"""Run the frozen matrix contract through an ONNX-MLIR shared library."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np

from PyRuntime import OMExecutionSession


def values(contract: dict) -> tuple[list[np.ndarray], list[np.ndarray], float]:
    if "scalar_cases" in contract:
        inputs: list[np.ndarray] = []
        expected: list[np.ndarray] = []
        for case in contract["scalar_cases"]:
            inputs.extend(
                np.asarray(case[name], dtype=np.int64) for name in ("left", "right")
            )
            expected.append(np.asarray([case["expected"]], dtype=np.int64))
        tensor = contract["tensor_case"]
        inputs.extend(
            np.asarray(tensor[name], dtype=np.int64) for name in ("left", "right")
        )
        expected.append(np.asarray(tensor["expected"], dtype=np.int64))
        return inputs, expected, 0.0
    if "fusion" in contract:
        case = contract["fusion"]
        inputs = [
            np.asarray(case["inputs"][name], dtype=np.float32)
            for name in ("a", "b", "c", "d")
        ]
    else:
        case = contract
        inputs = [
            np.asarray(case["left"], dtype=np.float32).reshape(
                case["left_shape"]
            ),
            np.asarray(case["right"], dtype=np.float32).reshape(
                case["right_shape"]
            ),
        ]
    return (
        inputs,
        [np.asarray(case["expected"], dtype=np.float32)],
        float(case["absolute_tolerance"]),
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("library", type=Path)
    parser.add_argument(
        "--contract",
        type=Path,
        default=Path("paper/tasks/implementation.json"),
    )
    args = parser.parse_args()

    contract = json.loads(args.contract.read_text(encoding="utf-8"))
    inputs, expected, tolerance = values(contract)

    session = OMExecutionSession(str(args.library.resolve()))
    outputs = session.run(inputs)
    if len(outputs) != len(expected):
        raise RuntimeError(f"expected {len(expected)} results, received {len(outputs)}")

    actual = [np.asarray(output).reshape(-1) for output in outputs]
    errors: list[float] = []
    for index, (observed, wanted) in enumerate(zip(actual, expected, strict=True)):
        wanted = wanted.reshape(-1)
        exact = np.issubdtype(wanted.dtype, np.integer)
        matches = np.array_equal(observed, wanted) if exact else np.allclose(
            observed, wanted, atol=tolerance, rtol=0.0
        )
        if observed.shape != wanted.shape or not matches:
            raise RuntimeError(
                f"oracle mismatch at result {index}: expected {wanted.tolist()}, "
                f"got {observed.tolist()}"
            )
        errors.append(float(np.max(np.abs(observed - wanted))))

    rendered = [output.tolist() for output in actual]

    print(
        json.dumps(
            {
                "schema": 1,
                "library": args.library.name,
                "result": rendered[0] if len(rendered) == 1 else rendered,
                "max_absolute_error": max(errors, default=0.0),
                "absolute_tolerance": tolerance,
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
