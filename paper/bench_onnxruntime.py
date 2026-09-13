#!/usr/bin/env python3
"""Benchmark one static single-input ONNX model without timing file I/O."""

import argparse
import sys
import time

import numpy as np
import onnxruntime as ort


def fnv1a(value: np.ndarray) -> str:
    checksum = 1469598103934665603
    for byte in np.asarray(value, dtype=np.float32).reshape(-1).tobytes():
        checksum ^= byte
        checksum = (checksum * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"{checksum:016x}"


def parse_shape(text: str) -> tuple[int, ...]:
    try:
        shape = tuple(int(item) for item in text.split(","))
    except ValueError as error:
        raise argparse.ArgumentTypeError("shape must contain integers") from error
    if not shape or any(extent <= 0 for extent in shape):
        raise argparse.ArgumentTypeError("shape extents must be positive")
    return shape


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("model")
    parser.add_argument("input")
    parser.add_argument("expected")
    parser.add_argument("--shape", type=parse_shape)
    parser.add_argument("--warmup", type=int, default=3)
    parser.add_argument("--repetitions", type=int, default=30)
    parser.add_argument("--atol", type=float, default=1.0e-4)
    parser.add_argument("--rtol", type=float, default=1.0e-4)
    parser.add_argument(
        "--protocol",
        action="store_true",
        help="emit the backend-neutral iteration,seconds,checksum protocol",
    )
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

    options = ort.SessionOptions()
    options.intra_op_num_threads = 1
    options.inter_op_num_threads = 1
    options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
    options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
    options.log_severity_level = 3
    session = ort.InferenceSession(
        args.model, sess_options=options, providers=["CPUExecutionProvider"]
    )
    inputs = session.get_inputs()
    outputs = session.get_outputs()
    if len(inputs) != 1 or len(outputs) != 1 or inputs[0].type != "tensor(float)":
        raise ValueError("benchmark requires one f32 input and one output")
    shape = args.shape
    if shape is None:
        if any(
            not isinstance(extent, int) or extent <= 0
            for extent in inputs[0].shape
        ):
            raise ValueError("a positive --shape is required for a dynamic input")
        shape = tuple(inputs[0].shape)
    if len(shape) != len(inputs[0].shape):
        raise ValueError("--shape rank does not match the model input")
    for declared, extent in zip(inputs[0].shape, shape):
        if isinstance(declared, int) and declared > 0 and declared != extent:
            raise ValueError("--shape conflicts with a static model extent")

    value = np.fromfile(args.input, dtype=np.float32).reshape(shape)
    expected = np.fromfile(args.expected, dtype=np.float32)
    feed = {inputs[0].name: value}
    for _ in range(args.warmup):
        session.run(None, feed)

    if args.protocol:
        print("iteration,seconds,checksum")
    else:
        print("backend,iteration,seconds,checksum")
    result = None
    for iteration in range(args.repetitions):
        begin = time.perf_counter()
        result = session.run(None, feed)[0]
        elapsed = time.perf_counter() - begin
        flat = np.asarray(result, dtype=np.float32).reshape(-1)
        if args.protocol:
            print(f"{iteration},{elapsed:.9f},{fnv1a(flat)}")
        else:
            checksum = flat.sum(dtype=np.float64)
            print(f"onnxruntime,{iteration},{elapsed:.9f},{checksum:.17g}")

    assert result is not None
    flat = np.asarray(result, dtype=np.float32).reshape(-1)
    if flat.size != expected.size:
        raise ValueError(f"expected {expected.size} outputs, received {flat.size}")
    if not np.all(np.isfinite(expected)):
        raise ValueError("reference output contains a nonfinite value")
    errors = np.abs(flat - expected)
    if not np.all(np.isfinite(flat)) or np.any(
        errors > args.atol + args.rtol * np.abs(expected)
    ):
        raise ValueError("output exceeds the declared numerical tolerance")
    print(f"max_abs_error={np.max(errors):.9g}", file=sys.stderr)


if __name__ == "__main__":
    main()
