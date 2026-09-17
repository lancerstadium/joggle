#!/usr/bin/env python3
"""Benchmark one static single-input TFLite model without timing file I/O."""

from __future__ import annotations

import argparse
import sys
import time

import numpy as np
from ai_edge_litert.interpreter import Interpreter


def fnv1a(value: np.ndarray) -> str:
    checksum = 1469598103934665603
    for byte in np.asarray(value, dtype=np.float32).reshape(-1).tobytes():
        checksum ^= byte
        checksum = (checksum * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"{checksum:016x}"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("model")
    parser.add_argument("input")
    parser.add_argument("expected")
    parser.add_argument("--warmup", type=int, default=3)
    parser.add_argument("--repetitions", type=int, default=30)
    parser.add_argument("--atol", type=float, default=1.0e-4)
    parser.add_argument("--rtol", type=float, default=1.0e-4)
    parser.add_argument("--protocol", action="store_true")
    args = parser.parse_args()
    if args.warmup < 0 or args.repetitions <= 0:
        parser.error("warmup must be nonnegative and repetitions positive")
    if (
        not np.isfinite(args.atol)
        or not np.isfinite(args.rtol)
        or args.atol < 0
        or args.rtol < 0
    ):
        parser.error("atol and rtol must be nonnegative and finite")

    interpreter = Interpreter(model_path=args.model, num_threads=1)
    interpreter.allocate_tensors()
    inputs = interpreter.get_input_details()
    outputs = interpreter.get_output_details()
    if len(inputs) != 1 or len(outputs) != 1:
        raise ValueError("benchmark requires one input and one output")
    if inputs[0]["dtype"] != np.float32 or outputs[0]["dtype"] != np.float32:
        raise ValueError("benchmark requires f32 input and output")
    shape = tuple(int(value) for value in inputs[0]["shape"])
    if not shape or any(value <= 0 for value in shape):
        raise ValueError("benchmark requires a static positive input shape")

    value = np.fromfile(args.input, dtype=np.float32).reshape(shape)
    expected = np.fromfile(args.expected, dtype=np.float32)
    interpreter.set_tensor(inputs[0]["index"], value)
    for _ in range(args.warmup):
        interpreter.invoke()

    print(
        "iteration,seconds,checksum" if args.protocol
        else "backend,iteration,seconds,checksum"
    )
    result = None
    for iteration in range(args.repetitions):
        started = time.perf_counter()
        interpreter.invoke()
        elapsed = time.perf_counter() - started
        result = np.asarray(
            interpreter.get_tensor(outputs[0]["index"]), dtype=np.float32
        ).reshape(-1)
        if args.protocol:
            print(f"{iteration},{elapsed:.9f},{fnv1a(result)}")
        else:
            checksum = result.sum(dtype=np.float64)
            print(f"litert,{iteration},{elapsed:.9f},{checksum:.17g}")

    assert result is not None
    if result.size != expected.size:
        raise ValueError(
            f"expected {expected.size} outputs, received {result.size}"
        )
    errors = np.abs(result - expected)
    if not np.all(np.isfinite(expected)):
        raise ValueError("reference output contains a nonfinite value")
    if not np.all(np.isfinite(result)) or np.any(
        errors > args.atol + args.rtol * np.abs(expected)
    ):
        raise ValueError("output exceeds the declared numerical tolerance")
    print(f"max_abs_error={np.max(errors):.9g}", file=sys.stderr)


if __name__ == "__main__":
    main()
