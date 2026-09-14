#!/usr/bin/env python3
"""Run the frozen matrix contract through an ONNX-MLIR shared library."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np

from PyRuntime import OMExecutionSession


def values(contract: dict) -> tuple[list[np.ndarray], np.ndarray, float]:
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
        np.asarray(case["expected"], dtype=np.float32),
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
    if len(outputs) != 1:
        raise RuntimeError(f"expected one result, received {len(outputs)}")

    actual = np.asarray(outputs[0], dtype=np.float32).reshape(-1)
    if actual.shape != expected.shape or not np.allclose(
        actual, expected, atol=tolerance, rtol=0.0
    ):
        raise RuntimeError(
            f"oracle mismatch: expected {expected.tolist()}, got {actual.tolist()}"
        )

    print(
        json.dumps(
            {
                "schema": 1,
                "library": args.library.name,
                "result": actual.tolist(),
                "max_absolute_error": float(np.max(np.abs(actual - expected))),
                "absolute_tolerance": tolerance,
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
