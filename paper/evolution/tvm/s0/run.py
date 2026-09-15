#!/usr/bin/env python3
"""Reload an exported TVM S0 artifact in a fresh process and check its oracle."""

import argparse
import json
from pathlib import Path

import numpy as np
import onnx
import tvm
from tvm import relax


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("fixture", type=Path)
    parser.add_argument("library", type=Path)
    args = parser.parse_args()
    model = onnx.load(args.fixture / "model.onnx")
    shape = tuple(int(dim.dim_value) for dim in
                  model.graph.input[0].type.tensor_type.shape.dim)
    x = np.fromfile(args.fixture / "input.bin", dtype=np.float32)
    expected = np.fromfile(args.fixture / "expected.bin", dtype=np.float32)
    if x.size != np.prod(shape) or expected.size != 100:
        raise AssertionError((x.size, expected.size))
    module = tvm.runtime.load_module(str(args.library.resolve()))
    vm = relax.VirtualMachine(module, tvm.cpu())
    actual = vm["main"](
        tvm.runtime.tensor(x.reshape(shape), tvm.cpu())
    ).numpy().ravel()
    max_error = float(np.max(np.abs(actual - expected)))
    if not np.array_equal(actual, expected):
        raise AssertionError(f"not bitwise equal; max abs error={max_error}")
    print(json.dumps({"case": args.fixture.name, "output_elements": 100,
                      "max_abs_error": max_error, "bitwise_equal": True},
                     sort_keys=True))


if __name__ == "__main__":
    main()
