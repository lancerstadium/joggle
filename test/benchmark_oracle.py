"""Numerical-oracle regressions; run with NumPy, ONNX, and ONNX Runtime."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np
import onnx
from onnx import helper, numpy_helper

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "artifact"))
from run_onnxruntime_benchmarks import compare_outputs, ort_session
from run_joggle_benchmarks import checkpoint_protocol


class BenchmarkOracleTests(unittest.TestCase):
    def test_checkpoint_rejects_missing_or_changed_oracle(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "rows.csv"
            current = {"correctness_oracle": {"graph_optimization": "ORT_DISABLE_ALL"}}
            checkpoint_protocol(path, current)
            self.assertEqual(json.loads(path.with_suffix(".checkpoint.json").read_text()), current)
            path.write_text("existing checkpoint\n")
            checkpoint_protocol(path, current)
            with self.assertRaises(SystemExit):
                checkpoint_protocol(path, {"correctness_oracle": {"graph_optimization": "ORT_ENABLE_ALL"}})
            self.assertEqual(path.read_text(), "existing checkpoint\n")
            legacy = Path(directory) / "legacy.csv"
            legacy.write_text("legacy checkpoint\n")
            with self.assertRaises(SystemExit):
                checkpoint_protocol(legacy, current)
            self.assertFalse(legacy.with_suffix(".checkpoint.json").exists())
            self.assertEqual(legacy.read_text(), "legacy checkpoint\n")

    def test_exact_integer_and_shape_checks(self):
        value = np.array([1, 2], dtype=np.uint8)
        self.assertTrue(compare_outputs([value], [value.copy()], 1, 1)["correct"])
        self.assertFalse(compare_outputs([value + 1], [value], 1, 1)["correct"])
        self.assertFalse(compare_outputs([value.reshape(1, 2)], [value], 1, 1)["correct"])
        self.assertFalse(compare_outputs([value.astype(np.int8)], [value], 1, 1)["correct"])
        self.assertFalse(compare_outputs([], [value], 1, 1)["correct"])

    def test_float_tolerance_and_nonfinite(self):
        value = np.array([1], dtype=np.float32)
        self.assertTrue(compare_outputs([value + 1e-6], [value], 1e-4, 1e-6)["correct"])
        self.assertFalse(compare_outputs([value + .01], [value], 1e-4, 1e-6)["correct"])
        for number in [np.nan, np.inf, -np.inf]:
            invalid = np.array([number], dtype=np.float32)
            self.assertFalse(compare_outputs([invalid], [invalid], 1e-4, 1e-6)["correct"])

    def test_qdq_preserves_float_bias_semantics(self):
        params = {
            "xs": np.array(1, np.float32), "xz": np.array(0, np.uint8),
            "w": np.array([[[[-1]]]], np.int8),
            "ws": np.array(1e-9, np.float32), "wz": np.array(0, np.int8),
            "b": np.array([-2147483648], np.int32),
            "bs": np.array(1e-9, np.float32), "bz": np.array(0, np.int32),
            "ys": np.array(1, np.float32), "yz": np.array(0, np.uint8),
        }
        nodes = [
            helper.make_node("DequantizeLinear", ["x", "xs", "xz"], ["xf"]),
            helper.make_node("DequantizeLinear", ["w", "ws", "wz"], ["wf"]),
            helper.make_node("DequantizeLinear", ["b", "bs", "bz"], ["bf"]),
            helper.make_node("Conv", ["xf", "wf", "bf"], ["cf"]),
            helper.make_node("QuantizeLinear", ["cf", "ys", "yz"], ["y"]),
        ]
        shape = [1, 1, 1, 1]
        graph = helper.make_graph(
            nodes, "qdq-overflow-boundary",
            [helper.make_tensor_value_info("x", onnx.TensorProto.UINT8, shape)],
            [helper.make_tensor_value_info("y", onnx.TensorProto.UINT8, shape)],
            [numpy_helper.from_array(value, name) for name, value in params.items()],
        )
        model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 13)])
        model.ir_version = 10
        measurement = {"threads": 1, "reference_execution_mode": "ORT_SEQUENTIAL",
                       "reference_graph_optimization": "ORT_ENABLE_ALL",
                       "reference_provider": "CPUExecutionProvider"}
        feeds = {"x": np.ones(shape, np.uint8)}
        semantic = ort_session(model.SerializeToString(), measurement, semantic=True)
        result = semantic.run(None, feeds)
        self.assertTrue(np.array_equal(result[0], np.zeros(shape, np.uint8)))
        # The optimized result may change as ORT fixes its fusion. What must not
        # change is that the timing configuration is independent of the oracle.
        optimized = ort_session(model.SerializeToString(), measurement)
        actual = optimized.run(None, feeds)
        checked = compare_outputs(actual, result, 0, 0)
        self.assertEqual(checked["correct"], np.array_equal(actual[0], result[0]))
        self.assertEqual(measurement["reference_graph_optimization"], "ORT_ENABLE_ALL")


if __name__ == "__main__":
    unittest.main()
