#!/usr/bin/env python3
"""TVM implementation-task baseline for the frozen extension study."""

import argparse
import json
import tempfile
from pathlib import Path

import numpy as np
import tvm
from tvm.script import tirx as T


@T.prim_func(s_tir=True)
def matmul(
    a: T.handle,
    b: T.handle,
    out: T.handle,
    rows: T.int64,
    columns: T.int64,
    inner: T.int64,
) -> None:
    left = T.match_buffer(a, (rows, inner), "float32")
    right = T.match_buffer(b, (inner, columns), "float32")
    result = T.match_buffer(out, (rows, columns), "float32")
    for i, j in T.grid(rows, columns):
        result[i, j] = T.float32(0)
    for i, k, j in T.grid(rows, inner, columns):
        result[i, j] = result[i, j] + left[i, k] * right[k, j]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("fixture", type=Path)
    args = parser.parse_args()
    fixture = json.loads(args.fixture.read_text(encoding="utf-8"))
    left = np.asarray(fixture["left"], dtype="float32").reshape(
        fixture["left_shape"]
    )
    right = np.asarray(fixture["right"], dtype="float32").reshape(
        fixture["right_shape"]
    )
    expected = np.asarray(fixture["expected"], dtype="float32").reshape(
        left.shape[0], right.shape[1]
    )
    built = tvm.compile(matmul, target="c")
    with tempfile.TemporaryDirectory() as directory:
        library = Path(directory) / "matmul.so"
        built.export_library(library)
        loaded = tvm.runtime.load_module(library)
        actual = np.zeros_like(expected)
        loaded(
            left,
            right,
            actual,
            left.shape[0],
            right.shape[1],
            left.shape[1],
        )
    np.testing.assert_allclose(
        actual,
        expected,
        rtol=0,
        atol=fixture["absolute_tolerance"],
    )
    print(json.dumps({"task": "implementation", "status": "pass"}))


if __name__ == "__main__":
    main()
