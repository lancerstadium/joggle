#!/usr/bin/env python3

import argparse
from pathlib import Path

import numpy as np
import onnxruntime as ort


def deterministic_input(shape: list[int]) -> np.ndarray:
    count = int(np.prod(shape, dtype=np.int64))
    indices = np.arange(count, dtype=np.int64)
    values = ((indices * 17 + 13) % 256 - 128).astype(np.float32)
    return (values / np.float32(128.0)).reshape(shape)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate the external ONNX Runtime oracle for Anchor"
    )
    parser.add_argument("model", type=Path)
    parser.add_argument("output", type=Path)
    arguments = parser.parse_args()

    options = ort.SessionOptions()
    options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_DISABLE_ALL
    options.intra_op_num_threads = 1
    options.inter_op_num_threads = 1
    session = ort.InferenceSession(
        str(arguments.model), options, providers=["CPUExecutionProvider"]
    )
    inputs = session.get_inputs()
    outputs = session.get_outputs()
    if len(inputs) != 1 or len(outputs) != 1:
        raise RuntimeError("the Anchor oracle requires one model input and output")
    if inputs[0].type != "tensor(float)":
        raise RuntimeError("the Anchor oracle requires an f32 model input")
    if not all(
        isinstance(dimension, int) and dimension > 0
        for dimension in inputs[0].shape
    ):
        raise RuntimeError("the Anchor oracle requires a static input shape")

    values = deterministic_input(inputs[0].shape)
    result = session.run([outputs[0].name], {inputs[0].name: values})[0]
    if result.dtype != np.float32:
        raise RuntimeError("the Anchor oracle requires an f32 model output")
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    result.astype("<f4", copy=False).tofile(arguments.output)


if __name__ == "__main__":
    main()
