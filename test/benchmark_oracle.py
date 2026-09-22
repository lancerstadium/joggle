"""Numerical-oracle regressions; run with NumPy, ONNX, and ONNX Runtime."""

import json
import os
import copy
import argparse
import hashlib
import importlib.util
import shutil
import subprocess
import sys
import socket
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path

import numpy as np
import onnx
from onnx import helper, numpy_helper

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "artifact"))
from run_baseline_benchmarks import compare_outputs, isolated_reference, ort_session, run_json
from run_joggle_benchmarks import compiler_identity, checkpoint_protocol, make_harness, Unsupported
from benchmark_backends import ONNXMLIRRunner, TVMRunner, onnx_mlir_identity, tvm_identity
from validate_figure import performance
from run_extension_task import execute, fusion_fixture, sandbox_policy
from run_extension_agent import public_case_ids, tool_feedback
from merge_benchmark_rows import audited_input


class BenchmarkOracleTests(unittest.TestCase):
    def test_benchmark_assembly_rejects_changed_compiler_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "rows.csv"
            path.write_text("header\n")
            identity = {"joggle_sha256": "a" * 64, "module_files": {}}
            record = {"release_eligible": True, "git_dirty": False,
                      "output_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                      "joggle_sha256": "a" * 64, "identity_stable": True,
                      "compiler_identity": identity,
                      "final_compiler_identity": copy.deepcopy(identity)}
            record_path = path.with_suffix(".run.json")
            record_path.write_text(json.dumps(record))
            audited_input(path)
            record["final_compiler_identity"]["joggle_sha256"] = "b" * 64
            record_path.write_text(json.dumps(record))
            with self.assertRaisesRegex(SystemExit, "compiler identity changed"):
                audited_input(path)

    def test_joggle_identity_tracks_native_mods_and_executable(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            executable = root / "joggle"
            executable.write_bytes(b"executable-v1")
            mods = root / "mods"
            native = mods / "native"
            native.mkdir(parents=True)
            (mods / "module.jog").write_text("mod example\n")
            plugin = native / "example.so"
            plugin.write_bytes(b"plugin-v1")
            args = argparse.Namespace(joggle=executable, builtin_mods=mods,
                                      extension_mods=root / "extensions")
            original = compiler_identity(args)
            self.assertEqual(original, compiler_identity(args))
            self.assertIn("native/example.so", original["module_files"]["builtin"])
            plugin.write_bytes(b"plugin-v2")
            self.assertNotEqual(original, compiler_identity(args))
            plugin.write_bytes(b"plugin-v1")
            executable.write_bytes(b"executable-v2")
            self.assertNotEqual(original, compiler_identity(args))
            executable.write_bytes(b"executable-v1")
            plugin.unlink()
            self.assertNotEqual(original, compiler_identity(args))

    def test_agent_assembler_rejects_integration_records(self):
        artifact = Path(__file__).resolve().parents[1] / "artifact"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "result.csv"
            source.write_bytes((artifact / "templates/figure-04-extension.csv").read_bytes())
            source.with_suffix(".json").write_text(json.dumps({
                "schema": "agent-provider/v1", "dirty": False, "release_eligible": False,
                "output_sha256": hashlib.sha256(source.read_bytes()).hexdigest()}))
            output = root / "release.csv"
            result = subprocess.run([sys.executable, str(artifact / "merge_agent_rows.py"),
                                     str(source), "--output", str(output)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("invalid agent-provider record", result.stderr)
            self.assertFalse(output.exists())

    def test_agent_feedback_exposes_only_public_cases(self):
        task = {"positive_cases": [{"id": "positive"}, {"id": "held-out-positive"}],
                "negative_cases": [{"id": "negative"}, {"id": "held-out-negative"}]}
        self.assertEqual(public_case_ids(task), ["positive", "negative"])
        report = {"complete_task": False, "setup": [], "cases": [{
            "id": "positive", "passed": False, "actual": {}, "expected": {"ok": True},
            "exit_code": 0, "decode_error": "", "stderr": "", "command": ["private-path"]}]}
        feedback = tool_feedback(report)
        self.assertNotIn("command", feedback["cases"][0])
        report["complete_task"] = True
        with self.assertRaisesRegex(ValueError, "public-fixture"):
            tool_feedback(report)

    def test_extension_process_timeout_is_recorded(self):
        report = execute([sys.executable, "-c", "import time; time.sleep(30)"], 0.1)
        self.assertTrue(report["timeout"])
        self.assertIsNone(report["exit_code"])

    @unittest.skipUnless(sys.platform == "darwin", "macOS isolation test")
    def test_candidate_sandbox_denies_private_files_network_and_inherited_secrets(self):
        with tempfile.TemporaryDirectory() as directory, socket.socket() as listener:
            root = Path(directory).resolve()
            work = root / "work"
            work.mkdir()
            scratch = work / "tmp"
            scratch.mkdir()
            private = root / "private.txt"
            private.write_text("not exposed to the candidate")
            args = argparse.Namespace(sandbox_read=[], source=work / "candidate.py",
                                      joggle=None, builtin_mods=None, xdsl_python=Path(sys.executable))
            listener.bind(("127.0.0.1", 0))
            listener.listen()
            program = (
                "import errno,json,os,socket\nfrom pathlib import Path\n"
                "denied=[]\n"
                f"p=Path({str(private)!r})\n"
                "for action in [lambda:p.read_text(),lambda:p.write_text('changed'),"
                f"lambda:socket.create_connection(('127.0.0.1',{listener.getsockname()[1]}),timeout=1)]:\n"
                " try: action();denied.append(False)\n"
                " except OSError as e: denied.append(e.errno in (errno.EPERM,errno.EACCES))\n"
                "Path('allowed.txt').write_text('ok')\n"
                "print(json.dumps({'denied':denied,'secret':os.getenv('JOGGLE_TEST_SECRET')}))\n"
            )
            with patch.dict(os.environ, {"JOGGLE_TEST_SECRET": "must-not-be-inherited"}):
                report = execute([sys.executable, "-c", program], 10,
                                 sandbox_policy(args, work), scratch)
            self.assertEqual(report["exit_code"], 0, report)
            self.assertEqual(json.loads(report["stdout"]), {"denied": [True, True, True], "secret": None})
            self.assertEqual(private.read_text(), "not exposed to the candidate")
            self.assertEqual((work / "allowed.txt").read_text(), "ok")

    @unittest.skipUnless(importlib.util.find_spec("xdsl"), "xDSL is unavailable")
    def test_fusion_fixture_uses_native_def_use_edges_and_tensor_types(self):
        from xdsl.context import Context
        from xdsl.dialects.builtin import Builtin
        from xdsl.dialects.func import CallOp, Func, FuncOp
        from xdsl.parser import Parser
        request = {"layout": "NHWC", "channels": 8, "bias": [8],
                   "ops": ["conv2d", "bias_add", "relu"], "uses": [2, 1],
                   "interleave": True}
        for system in ("Joggle", "MLIR", "xDSL"):
            text = fusion_fixture(request, system)
            self.assertNotIn("request", text)
            self.assertNotIn("matches", text)
        context = Context()
        context.load_dialect(Builtin)
        context.load_dialect(Func)
        module = Parser(context, fusion_fixture(request, "xDSL")).parse_module()
        module.verify()
        subject = next(op for op in module.ops
                       if isinstance(op, FuncOp) and op.sym_name.data == "subject")
        calls = [op for op in subject.body.block.ops if isinstance(op, CallOp)]
        self.assertEqual([op.callee.root_reference.data for op in calls],
                         ["conv2d", "side", "bias_add", "relu"])
        self.assertIs(calls[2].arguments[0], calls[0].res[0])
        self.assertIs(calls[3].arguments[0], calls[2].res[0])
        self.assertFalse(calls[0].res[0].has_one_use())
        self.assertTrue(calls[2].res[0].has_one_use())
        self.assertEqual(calls[0].arguments[0].type.get_shape(), (1, 5, 7, 3))
        self.assertEqual(calls[0].arguments[1].type.get_shape(), (8, 3, 3, 3))
        self.assertEqual(calls[0].res[0].type.get_shape(), (1, 5, 7, 8))
        self.assertEqual(calls[2].arguments[1].type.get_shape(), (8,))

    def test_semantic_oracle_uses_a_separate_process_and_named_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            values = np.array([1.5, -2.0], dtype=np.float32)
            data = values.tobytes()
            (root / "x.bin").write_bytes(data)
            (root / "index.json").write_text(json.dumps({"cases": [{
                "id": "isolation", "tensors": [{"name": "x", "path": "x.bin",
                    "sha256": hashlib.sha256(data).hexdigest()}]}]}))
            spec = {"measurement": {"threads": 1,
                "reference_execution_mode": "ORT_SEQUENTIAL",
                "reference_graph_optimization": "ORT_ENABLE_ALL",
                "reference_provider": "CPUExecutionProvider"},
                "operator_cases": [{"id": "isolation", "inputs": [
                    {"name": "x", "dtype": "float32", "shape": [2]}]}],
                "model_cases": []}
            spec_path = root / "spec.json"
            spec_path.write_text(json.dumps(spec))
            graph = helper.make_graph([
                helper.make_node("Identity", ["x"], ["float_out"]),
                helper.make_node("Cast", ["x"], ["integer_out"], to=onnx.TensorProto.INT64),
            ], "isolation", [helper.make_tensor_value_info("x", onnx.TensorProto.FLOAT, [2])], [
                helper.make_tensor_value_info("float_out", onnx.TensorProto.FLOAT, [2]),
                helper.make_tensor_value_info("integer_out", onnx.TensorProto.INT64, [2]),
            ])
            model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 13)])
            model.ir_version = 9
            model_path = root / "model.onnx"
            onnx.save(model, model_path)
            args = argparse.Namespace(spec=spec_path, inputs=root, case_id="isolation",
                                      model=model_path, case_timeout=30)
            with patch("run_baseline_benchmarks.ort_session",
                       side_effect=AssertionError("oracle must not run in candidate process")):
                actual = isolated_reference(args, ["integer_out", "float_out"])
                with self.assertRaises(ValueError):
                    isolated_reference(args, ["missing"])
            self.assertEqual(actual[0].dtype, np.dtype("int64"))
            self.assertEqual(actual[1].dtype, np.dtype("float32"))
            np.testing.assert_array_equal(actual[0], np.array([1, -2], dtype=np.int64))
            np.testing.assert_array_equal(actual[1], values)

    def test_external_worker_reports_errors_and_reaps_timeout(self):
        self.assertEqual(run_json([sys.executable, "-c", 'print("log"); print(\'{"ok": true}\')']),
                         {"ok": True})
        with self.assertRaises(subprocess.CalledProcessError) as failure:
            run_json([sys.executable, "-c", 'import sys; print("failed", file=sys.stderr); sys.exit(2)'])
        self.assertIn("failed", failure.exception.stderr)
        with self.assertRaises(subprocess.TimeoutExpired):
            run_json([sys.executable, "-c", "import time; time.sleep(30)"], timeout=0.1)

    def test_external_performance_population_and_sample_identity(self):
        # Synthetic validator fixtures, never used as experimental results.
        variants = {"joggle-unoptimized": "Joggle", "joggle-optimized": "Joggle",
                    "onnxruntime": "ONNX Runtime", "tvm-relax-llvm": "TVM Relax LLVM",
                    "onnx-mlir-llvm": "ONNX-MLIR LLVM"}
        spec = {"measurement": {"execution_iterations": 2, "execution_batches": {"op": 1}},
                "variants": [{"id": k, "system": v} for k, v in variants.items()
                             if k not in {"tvm-relax-llvm", "onnx-mlir-llvm"}],
                "operator_cases": [{"id": "op", "family": "elementwise"}], "model_cases": []}
        rows = [{"subject_kind": "operator", "subject": "op", "subject_hash": "a" * 64,
                 "family": "elementwise", "system": system, "system_revision": "revision",
                 "variant": variant, "supported": "true", "reason": "",
                 "iteration": str(i), "calls_per_sample": "1", "latency_ns": "100",
                 "max_abs_error": "0", "max_rel_error": "0", "input_digest": "b" * 64,
                 "output_digest": "c" * 64, "correct": "true", "seed": str(10 + i)}
                for variant, system in variants.items() for i in range(2)]
        performance(rows, False, spec, "a" * 64, list(variants))
        with self.assertRaises(SystemExit):
            performance(rows, False, spec, "a" * 64)  # External variants are opt-in.
        with self.assertRaises(SystemExit):
            performance(rows[:-1], False, spec, "a" * 64, list(variants))
        for field, value in (("iteration", "0"), ("iteration", "2"),
                             ("system_revision", "different"), ("input_digest", "d" * 64),
                             ("system", "other"), ("correct", "false")):
            invalid = copy.deepcopy(rows)
            invalid[-1][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(SystemExit):
                performance(invalid, False, spec, "a" * 64, list(variants))

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


@unittest.skipUnless(os.environ.get("ONNX_MLIR_BIN"), "ONNX_MLIR_BIN is not set")
class ONNXMLIRIntegrationTests(unittest.TestCase):
    def test_identity_and_single_thread_contract(self):
        compiler = Path(os.environ["ONNX_MLIR_BIN"])
        identity = onnx_mlir_identity(compiler)
        json.dumps(identity)
        self.assertEqual(identity["variant"], "onnx-mlir-llvm")
        self.assertEqual(len(identity["artifacts"]["onnx-mlir"]["sha256"]), 64)
        with self.assertRaises(ValueError):
            ONNXMLIRRunner(b"", {}, compiler, 2)

    def test_resident_inputs_multiple_outputs_and_native_lifetime(self):
        compiler = Path(os.environ["ONNX_MLIR_BIN"])
        # Identity can alias an input; transpose can expose non-contiguous storage.
        # Neither output release nor copying may damage resident input buffers.
        for dtype, tensor_type in ((np.float32, onnx.TensorProto.FLOAT),
                                   (np.int64, onnx.TensorProto.INT64)):
            shape = ["N", 3]
            graph = helper.make_graph([
                helper.make_node("Add", ["x.0", "offset"], ["sum"]),
                helper.make_node("Transpose", ["sum"], ["transposed"], perm=[1, 0]),
                helper.make_node("Identity", ["x.0"], ["identity"]),
            ], "native-lifetime", [
                helper.make_tensor_value_info("x.0", tensor_type, shape),
                helper.make_tensor_value_info("offset", tensor_type, [3]),
            ], [helper.make_tensor_value_info("transposed", tensor_type, [3, "N"]),
                helper.make_tensor_value_info("identity", tensor_type, shape)])
            model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 12)])
            model.ir_version = 10
            feeds = {"offset": np.array([1, -1, 2], dtype),
                     "x.0": np.arange(-3, 3, dtype=dtype).reshape(2, 3)}
            original = feeds["x.0"].copy()
            runner = ONNXMLIRRunner(model.SerializeToString(), feeds, compiler, 1)
            try:
                with self.assertRaises(RuntimeError):
                    runner.outputs()
                for _ in range(20):
                    runner.invoke()
                    actual = runner.outputs()
                    self.assertTrue(compare_outputs(actual, [(original + feeds["offset"]).T,
                                                              original], 0, 0)["correct"])
                self.assertEqual(runner.names, ["transposed", "identity"])
                self.assertEqual(set(runner.stages_ns), {"compile", "bind"})
            finally:
                runner.close()
            runner.close()  # Idempotent cleanup; previously copied outputs remain valid.
            np.testing.assert_array_equal(actual[1], original)
            np.testing.assert_array_equal(feeds["x.0"], original)
            with self.assertRaises(RuntimeError):
                runner.invoke()


if __name__ == "__main__":
    unittest.main()
