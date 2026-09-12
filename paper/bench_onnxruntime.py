#!/usr/bin/env python3
"""Benchmark one static single-input ONNX model without timing file I/O."""

import argparse
import sys
import time

import numpy as np
import onnxruntime as ort


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("model")
    parser.add_argument("input")
    parser.add_argument("expected")
    parser.add_argument("--warmup", type=int, default=3)
    parser.add_argument("--repetitions", type=int, default=30)
    args = parser.parse_args()
    if args.warmup < 0 or args.repetitions <= 0:
        parser.error("warmup must be nonnegative and repetitions positive")

    options = ort.SessionOptions()
    options.intra_op_num_threads = 1
    options.inter_op_num_threads = 1
    options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
    options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
    session = ort.InferenceSession(
        args.model, sess_options=options, providers=["CPUExecutionProvider"]
    )
    inputs = session.get_inputs()
    outputs = session.get_outputs()
    if len(inputs) != 1 or len(outputs) != 1 or inputs[0].type != "tensor(float)":
        raise ValueError("benchmark requires one f32 input and one output")
    if any(not isinstance(extent, int) or extent <= 0 for extent in inputs[0].shape):
        raise ValueError("benchmark requires a positive static input shape")

    value = np.fromfile(args.input, dtype=np.float32).reshape(inputs[0].shape)
    expected = np.fromfile(args.expected, dtype=np.float32)
    feed = {inputs[0].name: value}
    for _ in range(args.warmup):
        session.run(None, feed)

    print("backend,iteration,seconds,checksum")
    result = None
    for iteration in range(args.repetitions):
        begin = time.perf_counter()
        result = session.run(None, feed)[0]
        elapsed = time.perf_counter() - begin
        flat = np.asarray(result, dtype=np.float32).reshape(-1)
        print(f"onnxruntime,{iteration},{elapsed:.9f},{flat.sum(dtype=np.float64):.17g}")

    assert result is not None
    flat = np.asarray(result, dtype=np.float32).reshape(-1)
    if flat.size != expected.size:
        raise ValueError(f"expected {expected.size} outputs, received {flat.size}")
    print(f"max_abs_error={np.max(np.abs(flat - expected)):.9g}", file=sys.stderr)


if __name__ == "__main__":
    main()
