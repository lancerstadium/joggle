#!/usr/bin/env python3
"""Create deterministic f32 input and reference output for one ONNX model."""

import argparse
import json

import numpy as np
import onnxruntime as ort


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
    parser.add_argument("output")
    parser.add_argument("--shape", type=parse_shape)
    parser.add_argument("--seed", type=int, default=0)
    args = parser.parse_args()

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
    if len(inputs) != 1 or len(outputs) != 1:
        raise ValueError("reference generation requires one input and one output")
    if inputs[0].type != "tensor(float)" or outputs[0].type != "tensor(float)":
        raise ValueError("reference generation requires f32 input and output")

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

    rng = np.random.default_rng(args.seed)
    value = rng.standard_normal(shape).astype(np.float32)
    result = np.asarray(session.run(None, {inputs[0].name: value})[0])
    if result.dtype != np.float32:
        raise ValueError("model output is not f32")
    value.tofile(args.input)
    result.tofile(args.output)
    print(
        json.dumps(
            {
                "input": inputs[0].name,
                "input_shape": list(value.shape),
                "output": outputs[0].name,
                "output_shape": list(result.shape),
                "output_sum": float(result.sum(dtype=np.float64)),
                "seed": args.seed,
            },
            sort_keys=True,
        )
    )


if __name__ == "__main__":
    main()
