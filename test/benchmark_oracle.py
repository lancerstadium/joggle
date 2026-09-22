"""Numerical-oracle regressions; run with NumPy, ONNX, and ONNX Runtime."""

import json
import importlib.util
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np
import onnx
from onnx import helper, numpy_helper

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "artifact"))
from run_baseline_benchmarks import compare_outputs, ort_session
from run_joggle_benchmarks import checkpoint_protocol, make_harness, Unsupported
from benchmark_backends import TVMRunner, tvm_identity


class BenchmarkOracleTests(unittest.TestCase):
    def test_dynamic_output_harness_uses_capacity_and_checks_extents(self):
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("C compiler is unavailable")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            harness = root / "harness.c"
            outputs = [("y", np.array([2, 4], dtype=np.float32))]
            api = [{"name": "model_main", "params": [], "results": [
                {"c": "float", "pointer": True, "shape": ["_"], "bytes": 16}
            ]}]
            make_harness(harness, [], {}, outputs, root, 1, api)
            self.assertIn("calloc(1, 16)", harness.read_text())
            (root / "model.h").write_text(
                "#include <stdint.h>\n"
                "static void model_main(float *out, int64_t *length) {\n"
                " out[0] = 2; out[1] = 4; out[3] = 8; *length = LENGTH;\n}\n"
            )
            for length in (2, 1):
                binary = root / f"run-{length}"
                subprocess.run([compiler, "-std=c11", f"-DLENGTH={length}",
                                str(harness), "-o", str(binary)], check=True,
                               capture_output=True, text=True)
                output = root / f"output-{length}"
                output.mkdir()
                run = subprocess.run([str(binary), "execute", "0", "1", str(output)],
                                     capture_output=True, text=True)
                if length == 2:
                    self.assertEqual(run.returncode, 0, run.stderr)
                    self.assertEqual((output / "output-0.bin").read_bytes(),
                                     outputs[0][1].tobytes())
                else:
                    self.assertEqual(run.returncode, 6)
                    self.assertIn("expected 2, got 1", run.stderr)
                    self.assertFalse((output / "output-0.bin").exists())
            api[0]["results"][0]["bytes"] = 4
            with self.assertRaises(Unsupported):
                make_harness(harness, [], {}, outputs, root, 1, api)

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


@unittest.skipUnless(importlib.util.find_spec("tvm"), "TVM is not installed")
class TVMIntegrationTests(unittest.TestCase):
    def test_identity_is_serializable_and_rejects_non_cpu_targets(self):
        identity = tvm_identity({"kind": "llvm", "num-cores": 1})
        json.dumps(identity)
        self.assertEqual(identity["variant"], "tvm-relax-llvm")
        self.assertTrue(identity["libraries"])
        with self.assertRaises(ValueError):
            tvm_identity({"kind": "cuda"})

    def test_named_dynamic_inputs_and_multiple_outputs(self):
        shape = ["N", 3]
        graph = helper.make_graph([
            helper.make_node("Add", ["x.0", "offset"], ["sum"]),
            helper.make_node("Relu", ["sum"], ["positive"]),
        ], "multi-output", [
            helper.make_tensor_value_info("x.0", onnx.TensorProto.FLOAT, shape),
            helper.make_tensor_value_info("offset", onnx.TensorProto.FLOAT, [3]),
        ], [
            helper.make_tensor_value_info("positive", onnx.TensorProto.FLOAT, shape),
            helper.make_tensor_value_info("sum", onnx.TensorProto.FLOAT, shape),
        ])
        model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 12)])
        model.ir_version = 10
        feeds = {"offset": np.array([1, -1, 2], np.float32),
                 "x.0": np.arange(-3, 3, dtype=np.float32).reshape(2, 3)}
        runner = TVMRunner(model.SerializeToString(), feeds, {"kind": "llvm", "num-cores": 1}, 1)
        expected = feeds["x.0"] + feeds["offset"]
        for _ in range(2):
            runner.invoke()
            actual = runner.outputs()
            self.assertTrue(compare_outputs(actual, [np.maximum(expected, 0), expected], 0, 0)["correct"])
        self.assertEqual(runner.names, ["positive", "sum"])
        self.assertEqual(set(runner.stages_ns), {"import", "compile", "bind"})


if __name__ == "__main__":
    unittest.main()
