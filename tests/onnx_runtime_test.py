#!/usr/bin/env python3

import argparse
import subprocess

import numpy as np
import onnxruntime as ort


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--emitter", required=True)
    parser.add_argument("--tensor", required=True)
    parser.add_argument("--onnx", required=True)
    parser.add_argument("--schema", required=True)
    parser.add_argument("--native", required=True)
    parser.add_argument("--model", required=True)
    parser.add_argument("--output", required=True)
    return parser.parse_args()


def session(path):
    options = ort.SessionOptions()
    options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_DISABLE_ALL
    return ort.InferenceSession(
        path, sess_options=options, providers=["CPUExecutionProvider"]
    )


def main():
    args = parse_args()
    command = [args.emitter, args.tensor]
    command.extend([args.onnx, args.schema, args.native, args.model, args.output])
    subprocess.run(command, check=True)

    original = session(args.model)
    roundtrip = session(args.output)
    shape = tuple(original.get_inputs()[0].shape)
    count = int(np.prod(shape))
    values = np.arange(count, dtype=np.float32)
    values = ((values % np.float32(257.0)) - np.float32(128.0)) / np.float32(
        128.0
    )
    values = values.reshape(shape)

    expected = original.run(None, {original.get_inputs()[0].name: values})[0]
    actual = roundtrip.run(None, {roundtrip.get_inputs()[0].name: values})[0]
    repeated = roundtrip.run(None, {roundtrip.get_inputs()[0].name: values})[0]

    if not np.array_equal(actual, repeated):
        raise AssertionError("round-tripped graph is not deterministic")
    np.testing.assert_allclose(actual, expected, rtol=1.0e-5, atol=1.0e-5)
    difference = np.abs(actual - expected)
    print(
        "ONNX Runtime differential validation:",
        f"shape={actual.shape}",
        f"max_abs={float(difference.max()):.9g}",
        f"mean_abs={float(difference.mean()):.9g}",
    )


if __name__ == "__main__":
    main()
