#!/usr/bin/env python3
"""Run the frozen matrix contract through an ONNX-MLIR shared library."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np

from PyRuntime import OMExecutionSession


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
    left = np.asarray(contract["left"], dtype=np.float32).reshape(
        contract["left_shape"]
    )
    right = np.asarray(contract["right"], dtype=np.float32).reshape(
        contract["right_shape"]
    )
    expected = np.asarray(contract["expected"], dtype=np.float32)

    session = OMExecutionSession(str(args.library.resolve()))
    outputs = session.run([left, right])
    if len(outputs) != 1:
        raise RuntimeError(f"expected one result, received {len(outputs)}")

    actual = np.asarray(outputs[0], dtype=np.float32).reshape(-1)
    tolerance = float(contract["absolute_tolerance"])
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
