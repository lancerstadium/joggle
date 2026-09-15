"""Run an ONNX-MLIR S0 artifact against one frozen raw-tensor oracle."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np

from PyRuntime import OMExecutionSession


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("case", choices=("eligible", "fallback"))
    parser.add_argument("library", type=Path)
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[4]
    contract = json.loads((repo / "paper/tasks/evolution.json").read_text())
    spec = next(item for item in contract["source"]["cases"] if item["id"] == args.case)
    fixture = repo / "paper/fixtures/evolution" / args.case
    x = np.fromfile(fixture / "input.bin", dtype=np.float32).reshape(spec["input"])
    expected = np.fromfile(fixture / "expected.bin", dtype=np.float32).reshape(
        spec["output"]
    )

    session = OMExecutionSession(str(args.library.resolve()))
    outputs = session.run([x])
    if len(outputs) != 1 or tuple(outputs[0].shape) != tuple(spec["output"]):
        raise SystemExit("artifact output shape or count differs from contract")
    actual = outputs[0]
    max_abs_error = float(np.max(np.abs(actual - expected)))
    exact = bool(np.array_equal(actual, expected))
    print(
        json.dumps(
            {
                "case": args.case,
                "output_elements": int(actual.size),
                "max_abs_error": max_abs_error,
                "bitwise_equal": exact,
            },
            sort_keys=True,
        )
    )
    if not np.allclose(actual, expected, atol=1e-6, rtol=0):
        raise SystemExit("artifact differs from frozen numerical oracle")


if __name__ == "__main__":
    main()
