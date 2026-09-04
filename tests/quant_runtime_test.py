import argparse
import subprocess

import numpy as np
import onnxruntime as ort


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--generator", required=True)
    parser.add_argument("--quant", required=True)
    parser.add_argument("--native", required=True)
    parser.add_argument("--model", required=True)
    parser.add_argument("--quantized", required=True)
    parser.add_argument("--dequantized", required=True)
    return parser.parse_args()


def main():
    args = parse_args()
    subprocess.run(
        [
            args.generator,
            args.quant,
            args.native,
            args.model,
            args.quantized,
            args.dequantized,
        ],
        check=True,
    )
    options = ort.SessionOptions()
    options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_DISABLE_ALL
    options.intra_op_num_threads = 1
    options.inter_op_num_threads = 1
    session = ort.InferenceSession(
        args.model, sess_options=options, providers=["CPUExecutionProvider"]
    )
    input_value = np.array(
        [[-2.5, -3.0, -0.25], [2.5, 3.0, 0.25]], dtype=np.float32
    )
    first = session.run(None, {"input": input_value})
    second = session.run(None, {"input": input_value})
    expected_quantized = np.fromfile(args.quantized, dtype=np.int8).reshape(2, 3)
    expected_dequantized = np.fromfile(args.dequantized, dtype="<f4").reshape(2, 3)
    quantized_exact = np.array_equal(first[0], expected_quantized)
    dequantized_exact = np.array_equal(
        first[1].view(np.uint32), expected_dequantized.view(np.uint32)
    )
    deterministic = all(
        np.array_equal(lhs.view(np.uint8), rhs.view(np.uint8))
        for lhs, rhs in zip(first, second)
    )
    if not (quantized_exact and dequantized_exact and deterministic):
        raise AssertionError(
            "Joggle affine reference differs from ONNX Runtime: "
            f"quantized_exact={quantized_exact}, "
            f"dequantized_exact={dequantized_exact}, "
            f"deterministic={deterministic}"
        )
    print(
        "quant_runtime: quantized_exact=True, "
        "dequantized_bits_exact=True, deterministic=True"
    )


if __name__ == "__main__":
    main()
