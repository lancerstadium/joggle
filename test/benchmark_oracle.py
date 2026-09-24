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
import csv
from unittest.mock import patch
from pathlib import Path

import numpy as np
import onnx
from onnx import helper, numpy_helper

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "artifact"))
from run_baseline_benchmarks import compare_outputs, isolated_reference, ort_session, run_json
from run_baseline_benchmarks import checked_native_build, apply_model_edit, production_update
from run_baseline_benchmarks import production_sample_row, correctness_oracle_record, command as baseline_command
from run_baseline_benchmarks import production_worker_timeout
from run_joggle_benchmarks import compiler_identity, checkpoint_protocol, make_harness, Unsupported
from benchmark_backends import ONNXMLIRRunner, TVMRunner, JoggleRunner, JoggleCompiler, onnx_mlir_identity, tvm_identity
from validate_figure import performance, extension
from run_extension_task import (execute, fusion_fixture, graph_fixture, sandbox_policy,
                                equivalent, rewrite_graph, graph_manifest, rewrite_numerics, gelu_structure,
                                quant_structure, convolution_nhwc, layout_structure, wrapper_execution,
                                definition_fixture, definition_result)
from run_extension_agent import public_case_ids, tool_feedback, response_usage, final_checks
import run_extension_agent
from merge_benchmark_rows import audited_input
from merge_update_rows import production_rows, sha256 as file_sha256
import minimize_patch


class BenchmarkOracleTests(unittest.TestCase):
    def test_patch_minimization_uses_private_snapshot_and_preserves_checkout(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            repo = root / "repo"
            repo.mkdir()
            def git(*args):
                return subprocess.check_output(["git", "-C", str(repo), *args],
                                               text=True, stderr=subprocess.PIPE).strip()
            git("init", "-q")
            git("config", "user.name", "Oracle Test")
            git("config", "user.email", "oracle@example.invalid")
            (repo / "feature.txt").write_text("old\n")
            (repo / "obsolete.txt").write_text("remove me\n")
            git("add", ".")
            git("commit", "-qm", "base")
            base = git("rev-parse", "HEAD")
            (repo / "feature.txt").write_text("new\n")
            (repo / "obsolete.txt").unlink()
            (repo / "required.txt").write_text("required\n")
            (repo / "unneeded.txt").write_text("optional\n")
            git("add", "-A")
            git("commit", "-qm", "candidate")
            head = git("rev-parse", "HEAD")
            # A user's staged and unstaged edits must survive the measurement.
            (repo / "feature.txt").write_text("staged author edit\n")
            git("add", "feature.txt")
            (repo / "feature.txt").write_text("unstaged author edit\n")
            index_hash = file_sha256(repo / ".git/index")
            worktrees = git("worktree", "list", "--porcelain")
            command = [sys.executable, "-c", "from pathlib import Path; import subprocess; "
                "assert subprocess.run(['git','rev-parse','--show-toplevel'],capture_output=True).returncode != 0; "
                "assert Path('feature.txt').read_text() == 'new\\n'; "
                "assert Path('required.txt').read_text() == 'required\\n'; "
                "assert not Path('obsolete.txt').exists()"]
            args = ["minimize_patch.py", "--repo", str(repo), "--base", base, "--head", head,
                    "--output-patch", str(root / "result.patch"),
                    "--oracle-log", str(root / "oracle.log"), "--log", str(root / "trace.json"),
                    "--scratch-root", str(repo / "scratch"), "--", *command]
            with patch.object(sys, "argv", args):
                self.assertEqual(minimize_patch.main(), 0)
            result = (root / "result.patch").read_text()
            self.assertIn("required.txt", result)
            self.assertIn("obsolete.txt", result)
            self.assertNotIn("unneeded.txt", result)
            self.assertEqual((repo / "feature.txt").read_text(), "unstaged author edit\n")
            self.assertEqual(file_sha256(repo / ".git/index"), index_hash)
            self.assertEqual(git("rev-parse", "HEAD"), head)
            self.assertEqual(git("worktree", "list", "--porcelain"), worktrees)
            self.assertEqual(list((repo / "scratch").iterdir()), [])
            record = json.loads((root / "trace.json").read_text())
            self.assertEqual(record["workspace"], "isolated-archive/private-index")
            self.assertEqual(record["trials"][0]["phase"], "baseline")
            self.assertNotEqual(record["trials"][0]["returncode"], 0)
            self.assertEqual(len(record["retained_hunks"]), 3)
            # A non-discriminating oracle must not produce a publishable patch.
            args[args.index("--") + 1:] = [sys.executable, "-c", "pass"]
            with patch.object(sys, "argv", args), self.assertRaisesRegex(SystemExit, "baseline"):
                minimize_patch.main()

    def test_extension_primary_matrix_requires_all_72_conditions(self):
        families = ("definition", "analysis", "rewrite", "conversion", "emission", "vertical")
        tasks = {f"{family}-{i}": family for family in families for i in range(2)}
        rows = []
        for model in ("model-a", "model-b"):
            for task, family in tasks.items():
                for system in ("Joggle", "MLIR", "xDSL"):
                    rows.append(dict(model=model, model_revision="revision", system=system,
                        system_revision="native", task=task, family=family, demo_count="0",
                        demo_ids="", run="0", seed="1701", wall_ms="1", prompt_tokens="5",
                        completion_tokens="3", tool_calls="1", edit_attempts="1", files_touched="1",
                        budget_actions="30", budget_tokens="32000", task_spec_sha256="a"*64,
                        api_card_sha256="b"*64, trajectory_sha256="c"*64, patch_sha256="d"*64,
                        reference_tokens="", reference_nll="", parsed="true", typed="true",
                        built="true", passed="true", stop_reason="success"))
        extension(rows, False, tasks, "a"*64)
        for field, value in (("run", "1"), ("demo_count", "2"),
                             ("parsed", ""), ("reference_nll", "1.0"),
                             ("seed", "1702"), ("stop_reason", "agent_error")):
            changed = copy.deepcopy(rows)
            changed[0][field] = value
            with self.subTest(field=field), self.assertRaises(SystemExit):
                extension(changed, False, tasks, "a"*64)
        with self.assertRaises(SystemExit):
            extension(rows[:-1], False, tasks, "a"*64)
        failed = copy.deepcopy(rows)
        failed[0].update(parsed="", typed="", built="", passed="false", stop_reason="build")
        extension(failed, False, tasks, "a"*64)

    def test_agent_context_preflight_requires_real_overflow_rejection(self):
        detail = {"code": 400, "type": "exceed_context_size_error", "message": "too long",
                  "n_prompt_tokens": 80000, "n_ctx": 32768}
        with patch.object(run_extension_agent, "local_api",
                          side_effect=run_extension_agent.ContextLimitError(detail)) as api:
            result = run_extension_agent.check_context_policy("test-model")
            self.assertTrue(result["verified"])
            self.assertEqual(len(result["request_sha256"]), 64)
            request = api.call_args.args[1]
            self.assertEqual(request["options"]["temperature"], 0.0)
            self.assertFalse(request["shift"])
            self.assertFalse(request["truncate"])
        with patch.object(run_extension_agent, "local_api", return_value={}), self.assertRaises(ValueError):
            run_extension_agent.check_context_policy("test-model")
        with patch.object(run_extension_agent, "local_api", side_effect=
                          run_extension_agent.ContextLimitError(detail | {"n_ctx": 2048})), \
                self.assertRaises(ValueError):
            run_extension_agent.check_context_policy("test-model")

    def test_resident_scalarization_preserves_mutable_index_declarations(self):
        repo = Path(__file__).resolve().parents[1]
        compiler = Path(os.environ.get("JOGGLE_RESIDENT_COMPILER", repo / "build/artifact/joggle-artifact-reactive"))
        if not compiler.is_file() or not shutil.which("cc"):
            self.skipTest("resident compiler and C compiler are required")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            variants = [(False, False), (False, True), (True, False), (True, True)]
            sources = []
            for i, (left, right) in enumerate(variants):
                lhs = "[3, 2]" if left else "[2, 3]"
                rhs = "[4, 3]" if right else "[3, 4]"
                source = root / f"{i}.jog"
                source.write_text(f"mod transpose\nuse tensor\n"
                    f"fn main(a: tensor<f32, {lhs}>, b: tensor<f32, {rhs}>) -> tensor<f32, [2, 4]> {{\n"
                    f"return tensor.matmul<f32, {lhs}, {rhs}, [2, 4]>(a,b,{str(left).lower()},{str(right).lower()},1.0)\n}}\n")
                sources.append(source)
            output = root / "compiled"
            lowered = subprocess.run([str(compiler), "--compile-sequence", str(repo / "modules"),
                                      str(output), *map(str, sources)], capture_output=True, text=True, timeout=30)
            self.assertEqual(lowered.returncode, 0, lowered.stderr)
            for i, (left, right) in enumerate(variants):
                with self.subTest(transpose_a=left, transpose_b=right):
                    harness = root / f"main{i}.c"
                    harness.write_text("void transpose_main(const float*,const float*,float*);\n"
                        "int main(void){float a[6]={1,-2,3,4,-5,6},b[12]={7,1,-3,2,5,-4,8,6,-2,9,3,4},out[8];"
                        "for(int repeat=0;repeat<2;++repeat){transpose_main(a,b,out);"
                        "for(int row=0;row<2;++row)for(int col=0;col<4;++col){float expected=0;"
                        "for(int k=0;k<3;++k)expected+=a[" + ("k*2+row" if left else "row*3+k") +
                        "]*b[" + ("col*3+k" if right else "k*4+col") +
                        "];if(out[row*4+col]!=expected)return 1;}a[0]+=2;}return 0;}\n")
                    compiled = subprocess.run([shutil.which("cc"), "-std=c11", "-Werror", "-O2",
                        str(output / f"{i}.c"), str(harness), "-o", str(root / "run")],
                        capture_output=True, text=True, timeout=30)
                    self.assertEqual(compiled.returncode, 0, compiled.stderr)
                    subprocess.run([str(root / "run")], check=True, timeout=10)

    @unittest.skipUnless(importlib.util.find_spec("xdsl"), "xDSL is required")
    def test_quantized_operation_native_construction_and_verifier(self):
        from io import StringIO
        from xdsl.context import Context
        from xdsl.dialects.builtin import Builtin, FloatAttr, IntegerAttr, TensorType, f64, i8, i16, i32, i64
        from xdsl.dialects.func import Func
        from xdsl.ir import Block
        from xdsl.parser import Parser
        from xdsl.printer import Printer
        from xdsl.utils.exceptions import VerifyException
        path = Path(__file__).resolve().parents[1] / "artifact/extensions/def-quantized-op/reference.py"
        namespace = {"__name__": "quantized_operation_reference"}
        exec(compile(path.read_bytes(), str(path), "exec"), namespace)
        operation = namespace["QAdd"]
        attrs = {name: FloatAttr(value, f64) for name, value in
                 (("lhs_scale", 0.5), ("rhs_scale", 0.25), ("output_scale", 0.125))}
        attrs.update({name: IntegerAttr(value, i32) for name, value in
                      (("lhs_zero", -3), ("rhs_zero", 2), ("output_zero", 1))})
        context = Context(); context.load_dialect(Builtin); context.load_dialect(Func); namespace["register"](context)
        for shape in ([4], [2, 3], [], [0, 3]):
            type = TensorType(i8, shape)
            block = Block(arg_types=[type, type])
            op = operation.construct(*block.args, attrs)
            op.verify()
            self.assertEqual(op.result.type, type)
        text = ('builtin.module { func.func @subject(%a: tensor<4xi8>, %b: tensor<4xi8>) -> tensor<4xi8> { '
                '%r = "extension.qadd"(%a, %b) {lhs_scale = 5.0e-1 : f64, '
                'rhs_scale = 2.5e-1 : f64, output_scale = 1.25e-1 : f64, '
                'lhs_zero = -3 : i32, rhs_zero = 2 : i32, output_zero = 1 : i32} '
                ': (tensor<4xi8>, tensor<4xi8>) -> tensor<4xi8> func.return %r : tensor<4xi8> } }')
        module = Parser(context, text).parse_module(); module.verify()
        stream = StringIO(); Printer(stream=stream).print_op(module)
        reparsed = Parser(context, stream.getvalue()).parse_module(); reparsed.verify()
        qadd = list(next(iter(reparsed.ops)).body.block.ops)[0]
        self.assertIsInstance(qadd, operation)
        self.assertEqual(qadd.attributes, attrs)
        for field in ("lhs_scale", "rhs_scale", "output_scale"):
            for value in (0.0, -0.25, float("inf"), float("nan")):
                block = Block(arg_types=[TensorType(i8, [4])] * 2)
                bad = attrs | {field: FloatAttr(value, f64)}
                with self.subTest(field=field, value=value), self.assertRaisesRegex(VerifyException, "invalid-scale"):
                    operation.construct(*block.args, bad).verify()
        for lhs, rhs, result, error in (
                (TensorType(i8, [2, 3]), TensorType(i8, [3, 2]), None, "shape-mismatch"),
                (TensorType(i16, [4]), TensorType(i16, [4]), None, "operand-type-mismatch"),
                (TensorType(i8, [4]), TensorType(i8, [4]), TensorType(i8, [5]), "shape-mismatch")):
            block = Block(arg_types=[lhs, rhs])
            op = operation.construct(*block.args, attrs)
            if result is not None:
                op = operation.create(operands=block.args, result_types=[result], attributes=attrs)
            with self.assertRaisesRegex(VerifyException, error): op.verify()
        for field in ("lhs_zero", "rhs_zero", "output_zero"):
            block = Block(arg_types=[TensorType(i8, [4])] * 2)
            with self.assertRaisesRegex(VerifyException, "invalid-zero-point"):
                operation.construct(*block.args, attrs | {field: IntegerAttr(0, i64)}).verify()

    def test_production_worker_timeout_accounts_for_original_build(self):
        self.assertEqual(production_worker_timeout("rebuild", 180), 180)
        self.assertEqual(production_worker_timeout("update", 180), 360)
        for policy in ("update", "rebuild"):
            self.assertEqual(production_worker_timeout(policy, 180, 900), 900)
            for bad in (0, -1, float("nan"), float("inf")):
                with self.subTest(policy=policy, value=bad):
                    with self.assertRaises(ValueError):
                        production_worker_timeout(policy, bad)
                    with self.assertRaises(ValueError):
                        production_worker_timeout(policy, 180, bad)
        with self.assertRaises(ValueError):
            production_worker_timeout("execute", 180)

    def test_production_assembly_reconciles_raw_samples_and_pairs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            spec, index, population = [root / name for name in ("spec.json", "index.json", "population.json")]
            for path in (spec, index, population):
                path.write_text("{}")
            case = {"case_id": "model", "edit_id": "edit", "edit": {"model_sha256": "a" * 64},
                    "replacement_sha256": "b" * 64}
            identity = {"backend": "tvm", "collector_dirty": False, "collector_revision": "revision"}
            sample = {"schema": "production-update-sample/v1", "backend": "tvm", "policy": "update",
                      "case_id": "model", "edit_sha256": hashlib.sha256(json.dumps(case["edit"], sort_keys=True).encode()).hexdigest(),
                      "benchmark_spec_sha256": file_sha256(spec), "input_index_sha256": file_sha256(index),
                      "identity_stable": True, "compiler_identity": identity, "final_compiler_identity": identity,
                      "oracle": correctness_oracle_record(), "retained_state": "native caches",
                      "initial": {"correct": True, "model_sha256": "a" * 64}, "input_digest": "c" * 64,
                      "replacement": {"correct": True, "model_sha256": "b" * 64, "wall_ns": 10,
                          "ready_ns": 8, "edit_ns": 1, "validation_ns": 2, "output_digest": "d" * 64,
                          "stages_ns": {"compile": 6}}}
            rows, raw = [], []
            for order, policy in enumerate(("update", "rebuild")):
                current = copy.deepcopy(sample)
                current["policy"] = policy
                if policy == "rebuild":
                    current.update(initial=None, retained_state="fresh-worker")
                key = dict(backend="tvm", case_id="model", edit_id="edit", iteration=0,
                           policy=policy, order=order, seed=42)
                rows.append(key | {"edit_sha256": current["edit_sha256"], "error": ""} |
                            production_sample_row(current, "tvm", policy, case, file_sha256(spec), file_sha256(index)))
                raw.append({"key": key, "sample": current})
            output = root / "provider.csv"

            def write_data(selected_rows, selected_raw):
                with output.open("w", newline="") as stream:
                    writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
                    writer.writeheader(); writer.writerows(selected_rows)
                output.with_suffix(".samples.jsonl").write_text("".join(json.dumps(r) + "\n" for r in selected_raw))
                record = {"schema": "production-update-collection/v1", "dirty": False, "stable": True, "revision": "revision",
                          "population_sha256": file_sha256(population), "output_sha256": file_sha256(output),
                          "raw_sha256": file_sha256(output.with_suffix(".samples.jsonl")),
                          "sources": {str(p.resolve()): file_sha256(p) for p in (spec, index, population)},
                          "rows": len(selected_rows), "iterations": 1, "seed": 42,
                          "failures": sum(r["correct"] == "false" for r in selected_rows)}
                output.with_suffix(".json").write_text(json.dumps(record))

            def check(selected_rows, selected_raw):
                write_data(selected_rows, selected_raw)
                return production_rows([output], [case], spec, index, population)[0]

            self.assertEqual(len(check(rows, raw)), 2)
            self.assertEqual(json.loads(check(rows, raw)[0]["stages_ns"]), {"compile": 6})
            with self.assertRaisesRegex(ValueError, "incomplete"):
                check(rows[:1], raw[:1])
            wrong_rows = copy.deepcopy(rows); wrong_rows[0]["ready_ns"] = 9
            with self.assertRaisesRegex(ValueError, "differs from worker"):
                check(wrong_rows, raw)
            wrong_raw = copy.deepcopy(raw); wrong_raw[0]["sample"]["replacement"]["stages_ns"]["compile"] = 9
            with self.assertRaisesRegex(ValueError, "overlapping"):
                check(rows, wrong_raw)
            wrong_rows = copy.deepcopy(rows); wrong_raw = copy.deepcopy(raw)
            wrong_rows[1]["input_digest"] = "e" * 64; wrong_raw[1]["sample"]["input_digest"] = "e" * 64
            with self.assertRaisesRegex(ValueError, "different inputs"):
                check(wrong_rows, wrong_raw)
            wrong_rows = copy.deepcopy(rows); wrong_rows[0]["correct"] = "false"
            with self.assertRaisesRegex(ValueError, "failure row"):
                check(wrong_rows, raw)
            failed = copy.deepcopy(rows)
            for name in ("wall_ns", "ready_ns", "edit_ns", "validation_ns", "model_sha256", "input_digest",
                         "output_digest", "compiler_identity_sha256"):
                failed[0][name] = ""
            failed[0].update(correct="false", error="compiler failed")
            failed_raw = copy.deepcopy(raw); failed_raw[0]["sample"] = {"failure": "compiler failed"}
            self.assertEqual(sum(r["correct"] == "false" for r in check(failed, failed_raw)), 1)

    def test_joggle_host_compile_failure_keeps_compiler_diagnostics(self):
        x = helper.make_tensor_value_info("x", onnx.TensorProto.FLOAT, [1])
        model = helper.make_model(helper.make_graph([], "identity", [x], [x])).SerializeToString()
        compiler = type("Compiler", (), {"compile": lambda self, source, output: None})()
        result = subprocess.CompletedProcess(["cc"], 1, "", "error: undeclared storage binding")
        with patch("run_joggle_benchmarks.run_to_file"), patch("benchmark_backends.subprocess.run", return_value=result):
            with self.assertRaisesRegex(RuntimeError, "undeclared storage binding"):
                JoggleRunner(model, {"x": np.zeros(1, dtype=np.float32)}, compiler,
                             Path("tool"), Path("mods"), "cc", 10)

    @unittest.skipUnless((Path(__file__).resolve().parents[1] /
                          "build/artifact/joggle-artifact-reactive").is_file() and shutil.which("cc"),
                         "resident compiler and C compiler are required")
    def test_resident_index_updates_keep_the_planned_storage_binding(self):
        repo = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "input.jog"
            source.write_text("""mod resident_storage
use tensor
fn main(x: tensor<f32, [4]>) -> tensor<f32, [4]> {
  var shape = tensor<index, [4]>(index(0))
  shape[0] = index(1)
  shape[1] = index(2)
  shape[2] = index(3)
  shape[3] = index(4)
  var result = tensor<f32, [4]>(f32(0))
  for i in 0..4 { result[i] = x[i] + f32(shape[i]) }
  return result
}
""")
            output = root / "compiled"
            subprocess.run([str(repo / "build/artifact/joggle-artifact-reactive"), "--compile-sequence",
                            str(repo / "build/modules"), str(output), str(source)],
                           check=True, capture_output=True, text=True, timeout=30)
            harness = root / "main.c"
            harness.write_text("void resident_storage_main(const float*,float*);\n"
                               "int main(void){float x[4]={-1,-2,-3,-4},y[4];"
                               "resident_storage_main(x,y);for(int i=0;i<4;++i)if(y[i]!=0)return 1;"
                               "for(int i=0;i<4;++i)x[i]=0;resident_storage_main(x,y);"
                               "for(int i=0;i<4;++i)if(y[i]!=i+1)return 2;return 0;}\n")
            compiled = subprocess.run([shutil.which("cc"), "-std=c11", "-O3", str(output / "0.c"),
                            str(harness), "-o", str(root / "run")],
                           capture_output=True, text=True, timeout=30)
            self.assertEqual(compiled.returncode, 0, compiled.stderr + "\n" + (output / "0.c").read_text())
            subprocess.run([str(root / "run")], check=True, timeout=10)

    @unittest.skipUnless((Path(__file__).resolve().parents[1] /
                          "build/artifact/joggle-artifact-reactive").is_file() and shutil.which("cc"),
                         "resident compiler and C compiler are required")
    def test_resident_external_constants_keep_owned_abi_storage(self):
        repo = Path(__file__).resolve().parents[1]
        compiler = JoggleCompiler(repo / "build/artifact/joggle-artifact-reactive",
                                  repo / "build/modules", 60)
        try:
            for dtype in (np.float32, np.float64, np.int32):
                with self.subTest(dtype=dtype):
                    x = np.array([1, -2, 3, -4], dtype=dtype)
                    proto_type = helper.np_dtype_to_tensor_dtype(x.dtype)

                    def model(weights):
                        nodes = [helper.make_node("Identity", ["x"], ["y"])]
                        initializers = []
                        if weights is not None:
                            nodes = [helper.make_node("Add", ["x", "w"], ["y"])]
                            initializers = [numpy_helper.from_array(weights, "w")]
                        graph = helper.make_graph(nodes, "external_data",
                            [helper.make_tensor_value_info("x", proto_type, [4])],
                            [helper.make_tensor_value_info("y", proto_type, [4])], initializers)
                        result = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 13)])
                        result.ir_version = 9
                        return result.SerializeToString()

                    runners = []
                    expected = []
                    try:
                        for weights in (np.array([2, 3, -1, 5], dtype=dtype),
                                        np.array([-4, 2, 7, 1], dtype=dtype), None):
                            runner = JoggleRunner(model(weights), {"x": x}, compiler,
                                repo / "build/joggle", repo / "build/modules", shutil.which("cc"), 60)
                            runners.append(runner)
                            expected.append(x if weights is None else x + weights)
                            output = Path(runner._temporary.name) / "compiled"
                            entry = json.loads((output / "0.api.json").read_text())[0]
                            if weights is None:
                                self.assertEqual(entry["data"], "")
                            else:
                                self.assertTrue(entry["data"])
                                self.assertIn(weights.tobytes(), (output / "0.bin").read_bytes())
                                self.assertIn("const unsigned char*", (output / "0.c").read_text())
                        # A changed weight artifact and a no-data executable must
                        # neither alter nor outlive storage owned by another runner.
                        for _ in range(3):
                            for runner, values in zip(runners, expected, strict=True):
                                runner.invoke()
                                np.testing.assert_array_equal(runner.outputs()[0], values)
                    finally:
                        for runner in runners:
                            runner.close()
        finally:
            compiler.close()

    @unittest.skipUnless((Path(__file__).resolve().parents[1] /
                          "build/artifact/joggle-artifact-reactive").is_file() and shutil.which("cc"),
                         "resident compiler and C compiler are required")
    def test_external_constant_initializes_planned_writable_slot(self):
        repo = Path(__file__).resolve().parents[1]
        compiler = JoggleCompiler(repo / "build/artifact/joggle-artifact-reactive",
                                  repo / "build/modules", 60)
        try:
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                source = root / "input.jog"
                source.write_text('''mod planned_data
use tensor
fn main(x: tensor<i32, [4]>) -> tensor<i32, [4]> {
  var scratch: tensor<i32, [4]> = hex"01000000020000000300000004000000"
  scratch[0] = x[0]
  var out = tensor<i32, [4]>(i32(0))
  for i in 0..4 { out[i] = scratch[i] }
  return out
}
''')
                output = root / "compiled"
                compiler.compile(source, output)
                emitted = (output / "0.c").read_text()
                self.assertIn("memcpy(slot_", emitted)
                self.assertIn("#include <string.h>", emitted)
                harness = root / "main.c"
                harness.write_text('''#include "compiled/0.h"
#include <stdint.h>
int main(void) {
  const int32_t weights[4] = {1, 2, 3, 4};
  int32_t x[4] = {9, 0, 0, 0}, out[4] = {0};
  planned_data_main(x, (const unsigned char*)weights, out);
  if(out[0]!=9 || out[1]!=2 || out[2]!=3 || out[3]!=4) return 1;
  x[0] = -7;
  planned_data_main(x, (const unsigned char*)weights, out);
  if(out[0]!=-7 || out[1]!=2 || out[2]!=3 || out[3]!=4) return 2;
  if(weights[0]!=1) return 3;
  return 0;
}
''')
                subprocess.run([shutil.which("cc"), "-std=c11", "-O3", "-Wall", "-Werror",
                                str(output / "0.c"), str(harness), "-o", str(root / "run")],
                               check=True, capture_output=True, text=True, timeout=30)
                subprocess.run([str(root / "run")], check=True, timeout=10)
        finally:
            compiler.close()

    def test_production_sample_requires_matching_edit_protocol_and_boundaries(self):
        case = {"case_id": "model", "edit": {"model_sha256": "a" * 64},
                "replacement_sha256": "b" * 64}
        identity = {"backend": "test"}
        sample = {"schema": "production-update-sample/v1", "backend": "tvm", "policy": "update",
                  "case_id": "model", "edit_sha256": hashlib.sha256(json.dumps(case["edit"], sort_keys=True).encode()).hexdigest(),
                  "benchmark_spec_sha256": "s", "input_index_sha256": "i", "identity_stable": True,
                  "compiler_identity": identity, "final_compiler_identity": identity,
                  "oracle": correctness_oracle_record(), "retained_state": "native caches",
                  "initial": {"correct": True, "model_sha256": "a" * 64}, "input_digest": "c" * 64,
                  "replacement": {"correct": True, "model_sha256": "b" * 64,
                                  "wall_ns": 10, "ready_ns": 8, "edit_ns": 1, "validation_ns": 2,
                                  "output_digest": "d" * 64}}
        self.assertEqual(production_sample_row(sample, "tvm", "update", case, "s", "i")["wall_ns"], 10)
        for key, value in (("edit_sha256", "wrong"), ("benchmark_spec_sha256", "wrong"),
                           ("initial", None), ("retained_state", "fresh-worker"),
                           ("final_compiler_identity", {}), ("input_digest", "")):
            with self.subTest(key=key), self.assertRaises(ValueError):
                production_sample_row(sample | {key: value}, "tvm", "update", case, "s", "i")
        for key, value in (("wall_ns", 11), ("ready_ns", 0), ("edit_ns", 9),
                           ("correct", False), ("model_sha256", "wrong"), ("output_digest", "")):
            wrong = sample | {"replacement": sample["replacement"] | {key: value}}
            with self.subTest(key=key), self.assertRaises(ValueError):
                production_sample_row(wrong, "tvm", "update", case, "s", "i")
        fresh = sample | {"policy": "rebuild", "initial": None, "retained_state": "fresh-worker"}
        self.assertEqual(production_sample_row(fresh, "tvm", "rebuild", case, "s", "i")["correct"], "true")

    def test_production_worker_command_preserves_explicit_tool_configuration(self):
        args = argparse.Namespace(spec=Path("spec"), inputs=Path("inputs"), backend="joggle",
                                  target_json='{"kind":"llvm"}', onnx_mlir=None,
                                  edit_json=Path("edit.json"), joggle=Path("custom/joggle"),
                                  joggle_server=Path("custom/server"), builtin_mods=Path("custom/mods"),
                                  cc="custom/clang", case_timeout=17)
        argv = baseline_command(args, "update", "model", Path("model.onnx"))
        for key in ("edit_json", "joggle", "joggle_server", "builtin_mods", "cc", "case_timeout"):
            flag = "--" + key.replace("_", "-")
            self.assertEqual(argv[argv.index(flag) + 1], str(getattr(args, key)))

    def test_native_definition_observation_requires_one_parametric_type(self):
        for text in ("fx<8,3>", "!extension.fx<8, 3>"):
            self.assertEqual(definition_result({"types": [text]}),
                             {"canonical": "fx<8,3>", "storage_bits": 8})
        for actual in ({}, {"types": True}, {"types": []}, {"types": [3]},
                       {"types": ["i8"]}, {"types": ["fx<8,3>", "fx<16,7>"]}):
            with self.subTest(actual=actual), self.assertRaises(ValueError):
                definition_result(actual)

    @unittest.skipUnless(importlib.util.find_spec("xdsl"), "xDSL is required")
    def test_registered_fixed_point_type_roundtrip_and_bounds(self):
        from io import StringIO
        from xdsl.context import Context
        from xdsl.dialects.builtin import Builtin
        from xdsl.dialects.func import Func
        from xdsl.parser import Parser
        from xdsl.printer import Printer
        from xdsl.utils.exceptions import VerifyException, ParseError
        root = Path(__file__).resolve().parents[1] / "artifact"
        path = root / "extensions/def-parametric-type/reference.py"
        spec = importlib.util.spec_from_file_location("fixed_point_reference", path)
        reference = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(reference)
        task = next(t for t in json.loads((root / "manifests/extension-specs.json").read_text())["tasks"]
                    if t["id"] == "def-parametric-type")
        for case in task["positive_cases"] + task["negative_cases"]:
            context = Context()
            context.load_dialect(Builtin)
            context.load_dialect(Func)
            reference.register(context)
            def parse():
                module = Parser(context, definition_fixture(case["input"], "xDSL")).parse_module()
                module.verify()
                return module
            if "error" in case["expect"]:
                with self.assertRaisesRegex((VerifyException, ParseError), "invalid-type-parameter"):
                    parse()
                continue
            module = parse()
            output = StringIO()
            Printer(stream=output).print_op(module)
            reparsed = Parser(context, output.getvalue()).parse_module()
            reparsed.verify()
            argument = next(iter(reparsed.ops)).body.block.args[0]
            self.assertEqual(definition_result({"types": [str(argument.type)]}), case["expect"])

    @unittest.skipUnless(shutil.which("clang"), "Clang is required")
    def test_emitted_wrapper_is_compiled_and_runtime_mutants_are_rejected(self):
        compiler = Path(shutil.which("clang")).resolve()
        case = {"id": "wrapper", "input": {"values": [-2.0, -0.0, 1.5, 4.0]},
                "expect": {"values": [0.0, 0.0, 1.5, 4.0]}}
        valid = "for(size_t i=0;i<count;++i) output[i]=input[i]>0?input[i]:0.0f;"
        variants = [(valid, True), ("", False),
                    (valid + "output[count]=1;", False),
                    (valid + "((float*)input)[0]=1;", False),
                    ("for(size_t i=0;i<count;++i) output[i]=input[i]>=0?input[i]:0.0f;", False),
                    ("not valid C", False)]
        for body, passed in variants:
            with self.subTest(body=body), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                actual = {"symbol": "task_kernel", "source": "#include <stddef.h>\n"
                          "void task_kernel(const float* input,float* output,size_t count){" + body + "}"}
                result = wrapper_execution(actual, case, root, compiler, 20, None, root)
                self.assertEqual(result["passed"], passed)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            empty = {"id": "empty", "input": {"values": []}, "expect": {"values": []}}
            actual = {"symbol": "task_kernel", "source": "#include <stddef.h>\n"
                      "void task_kernel(const float* input,float* output,size_t count){" + valid + "}"}
            self.assertTrue(wrapper_execution(actual, empty, root, compiler, 20, None, root)["passed"])

    @unittest.skipUnless(importlib.util.find_spec("matplotlib"), "Matplotlib is required")
    def test_performance_plot_external_variants_and_missing_reference(self):
        sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "artifact/figures"))
        from figure_07_performance import summarize, plot_variants, HATCHES
        from common import COLORS
        # Synthetic plotting fixtures, never exported as experiment data.
        rows = [{"subject_kind": "operator", "subject": "op", "family": "elementwise",
                 "variant": variant, "system_revision": "revision", "subject_hash": "a" * 64,
                 "input_digest": "b" * 64, "supported": "true", "correct": "true",
                 "reason": "", "iteration": str(i), "latency_ns": str(value)}
                for variant in ("joggle-optimized", "tvm-relax-llvm", "onnx-mlir-llvm", "onnxruntime")
                for i, value in enumerate((10, 20))]
        summary = summarize(rows)
        candidates = plot_variants(summary)
        self.assertEqual(candidates, ("joggle-optimized", "tvm-relax-llvm", "onnx-mlir-llvm"))
        self.assertTrue(all(v in COLORS and v in HATCHES for v in candidates))
        self.assertTrue(all(row["latency_over_ort"] == 1 for row in summary))
        # A correct candidate with an invalid ORT reference has no ratio,
        # rather than a zero latency or a failed candidate status.
        for row in rows:
            if row["variant"] == "onnxruntime":
                row["correct"] = "false"
                row["reason"] = "numerical"
        summary = summarize(rows)
        self.assertTrue(all(row["latency_over_ort"] == "" for row in summary))
        self.assertEqual(sum(row["correct"] for row in summary), 3)
        for field in ("family", "reason", "input_digest"):
            invalid = copy.deepcopy(rows)
            invalid[0][field] = "different"
            with self.subTest(field=field), self.assertRaises(ValueError):
                summarize(invalid)
        with self.assertRaises(ValueError):
            plot_variants([dict(summary[0], variant="unknown"), summary[-1]])

    @unittest.skipUnless((Path(__file__).resolve().parents[1] /
                          "build/artifact/joggle-artifact-reactive").is_file() and shutil.which("cc"),
                         "resident compiler and C compiler are required")
    def test_resident_lowering_keeps_fresh_sources_and_matches_full_rebuild(self):
        repo = Path(__file__).resolve().parents[1]
        tool = repo / "build/artifact/joggle-artifact-reactive"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            before, after = root / "before.jog", root / "after.jog"
            for path, value in ((before, 1), (after, 2)):
                path.write_text(f"mod resident\nfn main(x: f32) -> f32 {{ return x + f32({value}) }}\n")
            original_sources = (before.read_bytes(), after.read_bytes())
            command = [str(tool), "--compile-sequence", str(repo / "build/modules")]
            warm, full = root / "warm", root / "full"
            subprocess.run(command + [str(warm), str(before), str(after), str(before)],
                           check=True, capture_output=True, text=True, timeout=60)
            subprocess.run(command + [str(full), str(after)], check=True,
                           capture_output=True, text=True, timeout=60)
            self.assertEqual(original_sources, (before.read_bytes(), after.read_bytes()))
            for suffix in (".c", ".h", ".api.json"):
                self.assertEqual((warm / ("1" + suffix)).read_bytes(),
                                 (full / ("0" + suffix)).read_bytes())
                self.assertEqual((warm / ("0" + suffix)).read_bytes(),
                                 (warm / ("2" + suffix)).read_bytes())
            harness = root / "main.c"
            harness.write_text("float resident_main(float);\n"
                               "int main(void) { return resident_main(3.0f) != EXPECTED; }\n")
            for index, expected in enumerate((4, 5, 4)):
                timing = json.loads((warm / f"{index}.timing.json").read_text())
                self.assertEqual(timing["wall_ns"], sum(timing[key] for key in
                                 ("parse_ns", "lower_ns", "emit_ns")))
                binary = root / f"run-{index}"
                subprocess.run([shutil.which("cc"), "-std=c11", "-O3", f"-DEXPECTED={expected}",
                                str(warm / f"{index}.c"), str(harness), "-o", str(binary)],
                               check=True, capture_output=True, timeout=60)
                subprocess.run([str(binary)], check=True, timeout=10)
            rejected = subprocess.run(command + [str(warm), str(after)],
                                      capture_output=True, text=True, timeout=10)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn("refusing to replace", rejected.stderr)

    def test_checked_build_rejects_stale_outputs_and_releases_runner(self):
        class Runner:
            names = ["y"]
            stages_ns = {"compile": 1}
            closed = False

            def invoke(self):
                pass

            def outputs(self):
                return [np.array([1], dtype=np.float32)]

            def close(self):
                self.closed = True

        for names in (["y"], ["wrong"]):
            runner = Runner()
            with self.assertRaises(ValueError):
                checked_native_build(lambda model, feeds: runner, b"edited", {}, names,
                                     [np.array([2], dtype=np.float32)], 0, 0)
            self.assertTrue(runner.closed)

    def test_conv_fusion_preserves_layout_attributes_users_and_arithmetic(self):
        root = Path(__file__).resolve().parents[1] / "artifact"
        task = next(t for t in json.loads((root / "manifests/extension-specs.json").read_text())["tasks"]
                    if t["id"] == "rew-conv-bias-relu")
        for case in task["positive_cases"] + task["negative_cases"]:
            request = case["input"]
            original = graph_manifest(rewrite_graph(request))
            expected = graph_manifest(rewrite_graph(request, case["expect"]["eliminate"]))
            self.assertTrue(rewrite_numerics(expected, original, False)["passed"], case["id"])
            if case["expect"]["eliminate"]:
                self.assertEqual(len(expected["nodes"]), 1)
                self.assertEqual(expected["nodes"][0]["attrs"], original["nodes"][0]["attrs"])
                # Incorrect operand wiring must not be accepted as a valid fusion.
                wrong = rewrite_graph(request, True)
                wrong["nodes"][0]["inputs"][-1] = "x"
                with self.assertRaises(ValueError):
                    rewrite_numerics(graph_manifest(wrong), original, False)
            else:
                self.assertEqual(expected, original)
        request = task["negative_cases"][0]["input"]
        wrong = graph_manifest(rewrite_graph(request, True))
        original = graph_manifest(rewrite_graph(request))
        self.assertFalse(rewrite_numerics(wrong, original, False)["passed"])
        request = {"op": "conv-bias-activation", "layout": "NHWC",
                   "input": [1, 1, 4, 1], "weight": [1, 1, 1, 1]}
        report = rewrite_numerics(graph_manifest(rewrite_graph(request, True)),
                                  graph_manifest(rewrite_graph(request)), False,
                                  extra_feeds={"v0": [-2, 0, 1, 3], "v1": [2], "v2": [-1]})
        self.assertTrue(report["passed"])
        observed = np.frombuffer(bytes.fromhex(report["cases"][-1]["after_bits"][0]), dtype=np.float32)
        np.testing.assert_array_equal(observed, [0, 0, 1, 5])

    def test_layout_conversion_rejects_wrong_padding_and_permutation(self):
        source = rewrite_graph({"input": [1, 4, 4, 2], "weight": [3, 3, 2, 3],
                                "pad": [1, 0, 0, 1], "shared_output": True})
        original = graph_manifest(source)
        names = ("tx", "tw", "c", "y")
        operands = (["x"], ["w"], ["tx", "tw"], ["c"])
        attrs = ({"perm": [0, 3, 1, 2]}, {"perm": [3, 2, 0, 1]},
                 source["nodes"][0]["attrs"], {"perm": [0, 2, 3, 1]})
        graph = copy.deepcopy(source)
        graph["nodes"] = [{"op": declaration["op"], "inputs": inputs,
                           "attrs": copy.deepcopy(meta),
                           "results": [dict(declaration["results"][0], name=name)]}
                          for declaration, name, inputs, meta in
                          zip(source["declarations"], names, operands, attrs)]
        observed = graph_manifest(graph)
        self.assertTrue(layout_structure(observed, original))
        self.assertTrue(rewrite_numerics(observed, original, False)["passed"])
        self.assertFalse(layout_structure(original, original))
        # Both mutations preserve tensor shapes, so numerical checks must reject them.
        for index, key, value in ((2, "pad", [0, 1, 1, 0]),
                                  (1, "perm", [3, 2, 1, 0])):
            wrong = copy.deepcopy(graph)
            wrong["nodes"][index]["attrs"][key] = value
            self.assertFalse(rewrite_numerics(graph_manifest(wrong), original, False)["passed"])

    def test_layout_convolution_matches_independent_onnx_execution(self):
        root = Path(__file__).resolve().parents[1] / "artifact"
        task = next(t for t in json.loads((root / "manifests/extension-specs.json").read_text())["tasks"]
                    if t["id"] == "con-layout-legalize")
        import onnxruntime as ort
        rng = np.random.default_rng(731)
        for case in task["positive_cases"]:
            request = case["input"]
            graph = rewrite_graph(request)
            shape = graph["nodes"][0]["results"][0]["shape"]
            self.assertEqual(shape, case["expect"]["output"])
            attrs = graph["nodes"][0]["attrs"]
            x = rng.normal(size=request["input"]).astype(np.float32)
            w = rng.normal(size=request["weight"]).astype(np.float32)
            nx, nw = x.transpose(0, 3, 1, 2), w.transpose(3, 2, 0, 1)
            ny = [shape[i] for i in (0, 3, 1, 2)]
            model = helper.make_model(helper.make_graph([
                helper.make_node("Conv", ["x", "w"], ["y"], strides=attrs["stride"],
                                 pads=attrs["pad"], dilations=attrs["dilation"])], "layout",
                [helper.make_tensor_value_info("x", onnx.TensorProto.FLOAT, nx.shape),
                 helper.make_tensor_value_info("w", onnx.TensorProto.FLOAT, nw.shape)],
                [helper.make_tensor_value_info("y", onnx.TensorProto.FLOAT, ny)]),
                opset_imports=[helper.make_opsetid("", 18)], ir_version=10)
            session = ort.InferenceSession(model.SerializeToString(), providers=["CPUExecutionProvider"])
            expected = session.run(None, {"x": nx, "w": nw})[0].transpose(0, 2, 3, 1)
            observed = convolution_nhwc(x, w, attrs)
            self.assertEqual(list(observed.shape), shape, case["id"])
            np.testing.assert_allclose(observed, expected, rtol=1e-4, atol=1e-5,
                                       err_msg=case["id"])
        x = np.arange(1, 7, dtype=np.float32).reshape(1, 2, 3, 1)
        w = np.array([1, 2], dtype=np.float32).reshape(1, 2, 1, 1)
        np.testing.assert_array_equal(convolution_nhwc(x, w,
            {"stride": [1, 1], "pad": [0, 0, 0, 0], "dilation": [1, 1]}),
            np.array([5, 8, 14, 17], dtype=np.float32).reshape(1, 2, 2, 1))

    def test_quant_conversion_checks_rounding_zero_points_and_saturation(self):
        root = Path(__file__).resolve().parents[1] / "artifact"
        task = next(t for t in json.loads((root / "manifests/extension-specs.json").read_text())["tasks"]
                    if t["id"] == "con-quant-expand")
        for case in task["positive_cases"] + task["negative_cases"]:
            request = case["input"]
            source = rewrite_graph(request)
            original = graph_manifest(source)
            if "error" in case["expect"]:
                with self.assertRaises(ValueError):
                    rewrite_numerics(original, original, False)
                continue
            shape = source["inputs"][0]["shape"]
            def node(name, op, inputs, element, attrs):
                return {"op": op, "inputs": inputs, "attrs": attrs,
                        "results": [{"name": name, "element": element, "shape": shape}]}
            zeros = request["zeros"]
            nodes = [node("l", "dequantize", ["a"], "f32", {"scale": request["lhs_scale"], "zero": zeros[0]}),
                     node("r", "dequantize", ["b"], "f32", {"scale": request["rhs_scale"], "zero": zeros[1]}),
                     node("s", "add", ["l", "r"], "f32", {}),
                     node("y", "quantize", ["s"], "i8", {"scale": request["output_scale"], "zero": zeros[2]})]
            graph = source | {"nodes": nodes}
            expanded = graph_manifest(graph)
            feeds = {"v0": request["lhs"], "v1": request["rhs"]}
            self.assertTrue(quant_structure(expanded, original))
            self.assertFalse(quant_structure(original, original))
            report = rewrite_numerics(expanded, original, False, extra_feeds=feeds)
            self.assertTrue(report["passed"], case["id"])
            observed = np.frombuffer(bytes.fromhex(report["cases"][-1]["before_bits"][0]), dtype=np.int8)
            self.assertEqual(observed.tolist(), case["expect"]["values"])
            graph["nodes"][-1]["attrs"]["zero"] += 1
            self.assertFalse(rewrite_numerics(graph_manifest(graph), original, False,
                                            extra_feeds=feeds)["passed"], case["id"])

    def test_gelu_conversion_checks_semantics_without_fixed_node_order(self):
        for element, tolerance in (("f32", (1e-5, 1e-6)), ("f64", (1e-12, 1e-12))):
            source = rewrite_graph({"op": "gelu", "element": element, "shape": [5]})
            original = graph_manifest(source)
            nodes = []
            def node(name, op, inputs, attrs=None):
                nodes.append({"op": op, "inputs": inputs,
                              "results": [{"name": name, "element": element, "shape": [5]}],
                              "attrs": attrs or {}})
            node("root", "splat", [], {"value": 2 ** 0.5})
            node("scale", "div", ["x", "root"])
            node("error", "erf", ["scale"])
            node("one", "splat", [], {"value": 1.0})
            node("sum", "add", ["error", "one"])
            node("product", "mul", ["x", "sum"])
            node("half", "splat", [], {"value": 0.5})
            node("y", "mul", ["product", "half"])
            graph = {"inputs": source["inputs"], "nodes": nodes, "outputs": ["y"]}
            expanded = graph_manifest(graph)
            self.assertTrue(gelu_structure(expanded, original))
            self.assertTrue(rewrite_numerics(expanded, original, False, tolerance)["passed"])
            self.assertFalse(gelu_structure(original, original))
            graph["nodes"][-2]["attrs"]["value"] = 0.0
            wrong = graph_manifest(graph)
            self.assertTrue(gelu_structure(wrong, original))
            self.assertFalse(rewrite_numerics(wrong, original, False, tolerance)["passed"])

    def test_instruction_selection_checks_target_constraints_and_tiles(self):
        root = Path(__file__).resolve().parents[1] / "artifact"
        task = next(t for t in json.loads((root / "manifests/extension-specs.json").read_text())["tasks"]
                    if t["id"] == "con-instruction-select")
        for case in task["positive_cases"] + task["negative_cases"]:
            with self.subTest(case=case["id"]):
                original = graph_manifest(rewrite_graph(case["input"]))
                selected = graph_manifest(rewrite_graph(case["input"], True))
                self.assertTrue(rewrite_numerics(original, original, False)["passed"])
                if case["expect"]["eliminate"]:
                    self.assertTrue(rewrite_numerics(selected, original, False)["passed"])
                    self.assertNotEqual(original, selected)
                    broken = copy.deepcopy(selected)
                    broken["nodes"][0]["attrs"] = [["tiles", [0, 0, 0]]]
                    with self.assertRaises(ValueError):
                        rewrite_numerics(broken, original, False)
                else:
                    with self.assertRaises(ValueError):
                        rewrite_numerics(selected, original, False)
                for system in ("Joggle", "MLIR", "xDSL"):
                    fixture = graph_fixture(rewrite_graph(case["input"]), system)
                    self.assertIn("mma_m16n16k16", fixture)
                    self.assertNotIn("tiles", fixture)
                    self.assertNotIn("request", fixture)

    def test_transpose_rewrite_oracle_checks_permutations_not_shapes(self):
        root = Path(__file__).resolve().parents[1] / "artifact"
        task = next(t for t in json.loads((root / "manifests/extension-specs.json").read_text())["tasks"]
                    if t["id"] == "rew-transpose-pair")
        for case in task["positive_cases"] + task["negative_cases"]:
            with self.subTest(case=case["id"]):
                original = graph_manifest(rewrite_graph(case["input"]))
                simplified = graph_manifest(rewrite_graph(case["input"], True))
                if "error" in case["expect"]:
                    with self.assertRaises(ValueError):
                        rewrite_numerics(original, original, False)
                    continue
                self.assertEqual(rewrite_numerics(simplified, original, False)["passed"],
                                 case["expect"]["eliminate"])
                self.assertTrue(rewrite_numerics(original, original, False)["passed"])
                if case["input"].get("return_intermediate"):
                    self.assertEqual(len(simplified["nodes"]), 1)
                    self.assertEqual(len(simplified["outputs"]), 2)
                for system in ("Joggle", "MLIR", "xDSL"):
                    self.assertNotIn("request", graph_fixture(rewrite_graph(case["input"]), system))

    def test_cast_rewrite_oracle_rejects_lossy_shortcuts(self):
        root = Path(__file__).resolve().parents[1] / "artifact"
        task = next(t for t in json.loads((root / "manifests/extension-specs.json").read_text())["tasks"]
                    if t["id"] == "rew-redundant-cast")
        for case in task["positive_cases"] + task["negative_cases"]:
            with self.subTest(case=case["id"]):
                original = graph_manifest(rewrite_graph(case["input"]))
                simplified = graph_manifest(rewrite_graph(case["input"], True))
                self.assertEqual(rewrite_numerics(simplified, original, False)["passed"],
                                 case["expect"]["eliminate"])
                if case["input"].get("return_intermediate"):
                    self.assertEqual(len(simplified["nodes"]), 1)
                    self.assertEqual(len(simplified["outputs"]), 2)
                self.assertNotIn("request", graph_fixture(rewrite_graph(case["input"]), "Joggle"))

    def test_rewrite_oracle_uses_post_ir_and_preserves_signed_zero(self):
        root = Path(__file__).resolve().parents[1] / "artifact"
        task = next(t for t in json.loads((root / "manifests/extension-specs.json").read_text())["tasks"]
                    if t["id"] == "rew-add-zero")
        for case in task["positive_cases"] + task["negative_cases"]:
            with self.subTest(case=case["id"]):
                before = graph_manifest(rewrite_graph(case["input"]))
                after = graph_manifest(rewrite_graph(case["input"], case["expect"]["eliminate"]))
                self.assertTrue(rewrite_numerics(after, before,
                    case["input"].get("no_signed_zeros", False))["passed"])
                if case["expect"]["eliminate"]:
                    self.assertFalse(equivalent(before, after, False, {}))
                for system in ("Joggle", "MLIR", "xDSL"):
                    fixture = graph_fixture(rewrite_graph(case["input"]), system)
                    self.assertNotIn("request", fixture)
                    self.assertNotIn("eliminate", fixture)
        strict = next(c for c in task["negative_cases"] if c["id"] == "strict-positive-zero")
        original = graph_manifest(rewrite_graph(strict["input"]))
        illegal = graph_manifest(rewrite_graph(strict["input"], True))
        self.assertFalse(rewrite_numerics(illegal, original, False)["passed"])
        self.assertTrue(rewrite_numerics(illegal, original, True)["passed"])

    @unittest.skipUnless(importlib.util.find_spec("xdsl"), "xDSL is unavailable")
    def test_manifest_fixtures_use_verified_native_ssa(self):
        from xdsl.context import Context
        from xdsl.dialects.builtin import Builtin
        from xdsl.dialects.func import CallOp, Func, FuncOp, ReturnOp
        from xdsl.parser import Parser
        root = Path(__file__).resolve().parents[1] / "artifact"
        task = next(t for t in json.loads((root / "manifests/extension-specs.json").read_text())["tasks"]
                    if t["id"] == "emit-graph-manifest")
        self.assertEqual(len(task["positive_cases"]), 8)
        for case in task["positive_cases"]:
            with self.subTest(case=case["id"]):
                for system in ("Joggle", "MLIR", "xDSL"):
                    source = graph_fixture(case["input"], system)
                    self.assertNotIn("request", source)
                    self.assertNotIn("schema_version", source)
                context = Context()
                context.load_dialect(Builtin)
                context.load_dialect(Func)
                module = Parser(context, graph_fixture(case["input"], "xDSL")).parse_module()
                module.verify()
                subject = next(op for op in module.ops
                               if isinstance(op, FuncOp) and op.sym_name.data == "subject")
                calls = [op for op in subject.body.block.ops if isinstance(op, CallOp)]
                self.assertEqual(len(calls), len(case["input"]["nodes"]))
                self.assertEqual(len(subject.body.block.args), len(case["input"]["inputs"]))
                ret = subject.body.block.last_op
                self.assertIsInstance(ret, ReturnOp)
                self.assertEqual(len(ret.arguments), len(case["input"]["outputs"]))
                expected = case["expect"]
                self.assertFalse(equivalent({}, expected, False, {}))
                self.assertFalse(equivalent({"schema_version": 1}, expected, False, {}))
                self.assertFalse(equivalent(expected | {"outputs": []}, expected, False, {}))

    def test_benchmark_assembly_rejects_invalidated_measurements(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "rows.csv"
            path.write_text("header\n")
            path.with_suffix(".invalid.json").write_text(json.dumps({
                "reason": "concurrent native build during measurement"}))
            with self.assertRaisesRegex(SystemExit, "measurement was invalidated"):
                audited_input(path)

    def test_agent_provider_and_final_failures_still_export_records(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "trajectory"
            argv = ["agent", "--model", "test-model", "--system", "Joggle",
                    "--task", "ana-storage-cost", "--seed", "1", "--output", str(output),
                    "--joggle", sys.executable, "--builtin-mods", directory]
            def api(path, payload=None):
                if path == "tags":
                    return {"models": [{"name": "test-model", "digest": "f" * 64}]}
                if path == "version":
                    return {"version": "test"}
                self.assertIs(payload["truncate"], False)
                self.assertIs(payload["shift"], False)
                raise TimeoutError("test provider timeout")
            def git(command, **kwargs):
                return "a" * 40 if "rev-parse" in command else b""
            with patch.object(sys, "argv", argv), patch.object(sys, "platform", "darwin"), \
                    patch.object(run_extension_agent, "local_api", side_effect=api), \
                    patch.object(run_extension_agent, "check_context_policy", return_value={"verified": True}), \
                    patch.object(run_extension_agent, "native_identity", return_value={}), \
                    patch.object(subprocess, "check_output", side_effect=git), \
                    patch.object(subprocess, "run", side_effect=OSError("test oracle unavailable")):
                self.assertEqual(run_extension_agent.main(), 0)
            record = json.loads((output / "trajectory.json").read_text())
            self.assertIn("provider", record["infrastructure_error"])
            self.assertIn("final-oracle", record["infrastructure_error"])
            self.assertIsNone(record["final_oracle_sha256"])
            self.assertTrue(record["identity_stable"])
            with (output / "result.csv").open() as stream:
                row = next(csv.DictReader(stream))
            self.assertEqual(row["stop_reason"], "agent_error")
            self.assertEqual(row["passed"], "false")
            self.assertEqual(row["parsed"], "")
            self.assertFalse(json.loads((output / "result.json").read_text())["release_eligible"])

    def test_agent_context_limit_requires_structured_native_error(self):
        detail = {"code": 400, "type": "exceed_context_size_error", "message": "too long",
                  "n_prompt_tokens": 16404, "n_ctx": 2048}
        for value in (detail, {"error": detail}, {"error": json.dumps({"error": detail})}):
            self.assertEqual(run_extension_agent.context_limit(json.dumps(value).encode()), detail)
        for change in ({"type": "other"}, {"code": 500}, {"n_ctx": 0},
                       {"n_prompt_tokens": 10}, {"n_prompt_tokens": True}):
            self.assertIsNone(run_extension_agent.context_limit(json.dumps(detail | change).encode()))
        self.assertIsNone(run_extension_agent.context_limit(b"exceed_context_size_error"))

    def test_agent_context_limit_stops_without_model_action(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "trajectory"
            argv = ["agent", "--model", "test-model", "--system", "Joggle",
                    "--task", "ana-storage-cost", "--seed", "1", "--output", str(output),
                    "--joggle", sys.executable, "--builtin-mods", directory]
            detail = {"code": 400, "type": "exceed_context_size_error", "message": "too long",
                      "n_prompt_tokens": 40000, "n_ctx": 32768}
            def api(path, payload=None):
                if path == "tags":
                    return {"models": [{"name": "test-model", "digest": "f" * 64}]}
                if path == "version":
                    return {"version": "test"}
                self.assertIs(payload["truncate"], False)
                self.assertIs(payload["shift"], False)
                raise run_extension_agent.ContextLimitError(detail)
            def git(command, **kwargs):
                return "a" * 40 if "rev-parse" in command else b""
            final = {"passed": False, "setup": [], "cases": []}
            with patch.object(sys, "argv", argv), patch.object(sys, "platform", "darwin"), \
                    patch.object(run_extension_agent, "local_api", side_effect=api), \
                    patch.object(run_extension_agent, "check_context_policy", return_value={"verified": True}), \
                    patch.object(run_extension_agent, "native_identity", return_value={}), \
                    patch.object(subprocess, "check_output", side_effect=git), \
                    patch.object(run_extension_agent, "final_checks", return_value=(final, True, [])):
                self.assertEqual(run_extension_agent.main(), 0)
            record = json.loads((output / "trajectory.json").read_text())
            self.assertIsNone(record["infrastructure_error"])
            self.assertEqual(record["events"][0]["context_limit"], detail)
            self.assertEqual(len(record["events"][0]["request_sha256"]), 64)
            with (output / "result.csv").open() as stream:
                row = next(csv.DictReader(stream))
            self.assertEqual(row["stop_reason"], "context_budget")

    def test_agent_usage_rejects_missing_or_out_of_budget_counts(self):
        response = {"done": True, "model": "test-model", "message": {"content": "{}"},
                    "prompt_eval_count": 100, "eval_count": 20}
        self.assertEqual(response_usage(response, "test-model", 128, 32), (100, 20))
        for field, value in (("prompt_eval_count", None), ("eval_count", True),
                             ("eval_count", "20"), ("eval_count", 0),
                             ("eval_count", 33), ("prompt_eval_count", 109),
                             ("done", False), ("model", "other")):
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                response_usage(response | {field: value}, "test-model", 128, 32)

    def test_agent_final_oracle_failure_retains_identity_check(self):
        def fail(*args, **kwargs):
            raise RuntimeError("oracle executable missing")
        with patch("run_extension_agent.native_identity") as identity:
            identity.return_value = True
            final, stable, errors = final_checks(fail, identity)
            self.assertIsNone(final)
            self.assertTrue(stable)
            identity.assert_called_once_with()
            self.assertIn("final-oracle", errors[0])

    def test_agent_final_identity_failure_retains_oracle_result(self):
        report = {"passed": False, "setup": [], "cases": []}
        def unavailable():
            raise TimeoutError("provider unavailable")
        final, stable, errors = final_checks(lambda *a, **k: report, unavailable)
        self.assertIs(final, report)
        self.assertFalse(stable)
        self.assertIn("identity", errors[0])
        final, stable, errors = final_checks(lambda *a, **k: report, lambda: False)
        self.assertIs(final, report)
        self.assertFalse(stable)
        self.assertIn("identity changed", errors[0])

    def test_agent_final_checks_reject_inconsistent_success(self):
        valid = {"passed": True, "setup": [], "cases": [
            {"exit_code": 0, "decode_error": "", "passed": True}]}
        final, stable, errors = final_checks(lambda *a, **k: valid, lambda: True)
        self.assertIs(final, valid)
        self.assertTrue(stable)
        self.assertEqual(errors, [])
        invalid = [None, {}, valid | {"cases": []}, valid | {"setup": [{}]},
                   valid | {"cases": [{"exit_code": 0, "decode_error": "", "passed": False}]}]
        for report in invalid:
            with self.subTest(report=report):
                final, stable, errors = final_checks(lambda *a, **k: report, lambda: True)
                self.assertIsNone(final)
                self.assertTrue(stable)
                self.assertTrue(errors)

    def test_agent_final_checks_accept_only_expected_rejections(self):
        case = {"exit_code": 5, "decode_error": "", "passed": True,
                "expected": {"error": "invalid-permutation", "phase": "build"},
                "stdout": "", "stderr": "invalid-permutation\n", "timeout": False}
        report = {"passed": True, "setup": [], "cases": [case]}
        final, stable, errors = final_checks(lambda *a, **k: report, lambda: True)
        self.assertIs(final, report)
        self.assertTrue(stable)
        self.assertEqual(errors, [])
        for change in ({"exit_code": 0}, {"exit_code": -9}, {"timeout": True},
                       {"stderr": "unrelated error"}, {"stdout": "module {}"},
                       {"expected": {"error": ""}}, {"passed": False}):
            invalid = report | {"cases": [case | change]}
            with self.subTest(change=change):
                final, _, errors = final_checks(lambda *a, **k: invalid, lambda: True)
                self.assertIsNone(final)
                self.assertTrue(errors)

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
        report["cases"][0]["numerics"] = {"build": {
            "exit_code": 1, "stdout": "", "stderr": "invalid generated C", "timeout": False,
            "command": ["private-compiler-path"]}}
        feedback = tool_feedback(report)
        build = feedback["cases"][0]["artifact"]["build"]
        self.assertEqual(build["stderr"], "invalid generated C")
        self.assertNotIn("command", build)
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
                "operator_cases": [{"id": "isolation", "rtol": 0, "atol": 0, "inputs": [
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

            # Exercise the production worker entry point using the same pinned
            # inputs and isolated oracle, not a second measurement harness.
            edit = {"schema": "onnx-node-edit/v1", "node_index": 0, "domain": "",
                    "before": "Identity", "after": "Neg",
                    "model_sha256": hashlib.sha256(model_path.read_bytes()).hexdigest()}
            edit_path = root / "edit.json"
            edit_path.write_text(json.dumps(edit))
            for changes in ({"before": "Add"}, {"node_index": -1}, {"node_index": True},
                            {"model_sha256": "0" * 64}, {"after": "Identity"},
                            {"after": "MissingOperator"}):
                with self.assertRaises((ValueError, onnx.checker.ValidationError)):
                    apply_model_edit(model_path.read_bytes(), {**edit, **changes})
            backends = []
            if (Path(__file__).resolve().parents[1] / "build/artifact/joggle-artifact-reactive").is_file():
                backends.append(["--backend", "joggle"])
            if importlib.util.find_spec("tvm"):
                backends.append(["--backend", "tvm"])
            if os.environ.get("ONNX_MLIR_BIN"):
                backends.append(["--backend", "onnx-mlir", "--onnx-mlir",
                                 os.environ["ONNX_MLIR_BIN"]])
            for backend in backends:
                with self.subTest(backend=backend):
                    common = [sys.executable, str(Path(__file__).resolve().parents[1] /
                              "artifact/run_baseline_benchmarks.py"), "--spec", str(spec_path),
                              "--inputs", str(root), "--case-id", "isolation", *backend,
                              "--model", str(model_path), "--edit-json", str(edit_path)]
                    try:
                        updated = run_json(common + ["--worker", "update"])
                        rebuilt = run_json(common + ["--worker", "rebuild"])
                    except subprocess.CalledProcessError as error:
                        self.fail(error.stderr)
                    self.assertTrue(updated["initial"]["correct"])
                    self.assertTrue(updated["replacement"]["correct"])
                    self.assertIsNone(rebuilt["initial"])
                    self.assertGreater(updated["replacement"]["edit_ns"], 0)
                    self.assertEqual(updated["edit_sha256"], rebuilt["edit_sha256"])
                    self.assertTrue(updated["identity_stable"] and rebuilt["identity_stable"])
                    self.assertEqual(updated["compiler_identity"], updated["final_compiler_identity"])
                    self.assertEqual(updated["compiler_identity"], rebuilt["compiler_identity"])
                    self.assertEqual(updated["benchmark_spec_sha256"],
                                     hashlib.sha256(spec_path.read_bytes()).hexdigest())
                    self.assertEqual(updated["input_index_sha256"],
                                     hashlib.sha256((root / "index.json").read_bytes()).hexdigest())
                    self.assertNotEqual(updated["initial"]["output_digest"],
                                        updated["replacement"]["output_digest"])
                    for field in ("model_sha256", "output_digest"):
                        self.assertEqual(updated["replacement"][field], rebuilt["replacement"][field])

            repo = Path(__file__).resolve().parents[1]
            if (repo / "build/artifact/joggle-artifact-reactive").is_file():
                worker_args = argparse.Namespace(**vars(args), backend="joggle", worker="rebuild",
                    joggle=repo / "build/joggle", builtin_mods=repo / "build/modules",
                    joggle_server=repo / "build/artifact/joggle-artifact-reactive",
                    cc="cc", edit_json=edit_path)
                with patch("run_baseline_benchmarks.production_identity",
                           side_effect=[{"compiler": "before"}, {"compiler": "after"}]), \
                        self.assertRaisesRegex(ValueError, "compiler changed"):
                    production_update(worker_args)

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
        # A live original executable must not cause the edited graph to reuse
        # stale code through a native process cache.
        model.graph.node[0].op_type = "Sub"
        edited = feeds["x.0"] - feeds["offset"]
        replacement, record = checked_native_build(
            lambda blob, inputs: TVMRunner(blob, inputs, {"kind": "llvm", "num-cores": 1}, 1),
            model.SerializeToString(), feeds, ["positive", "sum"],
            [np.maximum(edited, 0), edited], 0, 0)
        self.assertTrue(record["correct"])
        self.assertEqual(record["wall_ns"], record["ready_ns"] + record["validation_ns"])
        runner.invoke()
        self.assertTrue(compare_outputs(runner.outputs(), [np.maximum(expected, 0), expected], 0, 0)["correct"])
        self.assertFalse(compare_outputs(replacement.outputs(), runner.outputs(), 0, 0)["correct"])


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
                model.graph.node[0].op_type = "Sub"
                replacement, record = checked_native_build(
                    lambda blob, inputs: ONNXMLIRRunner(blob, inputs, compiler, 1),
                    model.SerializeToString(), feeds, ["transposed", "identity"],
                    [(original - feeds["offset"]).T, original], 0, 0)
                try:
                    self.assertTrue(record["correct"])
                    self.assertEqual(record["wall_ns"], record["ready_ns"] + record["validation_ns"])
                    runner.invoke()
                    self.assertTrue(compare_outputs(runner.outputs(),
                        [(original + feeds["offset"]).T, original], 0, 0)["correct"])
                    self.assertFalse(compare_outputs(replacement.outputs(), runner.outputs(), 0, 0)["correct"])
                finally:
                    replacement.close()
            finally:
                runner.close()
            runner.close()  # Idempotent cleanup; previously copied outputs remain valid.
            np.testing.assert_array_equal(actual[1], original)
            np.testing.assert_array_equal(feeds["x.0"], original)
            with self.assertRaises(RuntimeError):
                runner.invoke()


if __name__ == "__main__":
    unittest.main()
