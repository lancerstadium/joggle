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
from run_joggle_benchmarks import compiler_identity, checkpoint_protocol, make_harness, Unsupported
from benchmark_backends import ONNXMLIRRunner, TVMRunner, onnx_mlir_identity, tvm_identity
from validate_figure import performance
from run_extension_task import (execute, fusion_fixture, graph_fixture, sandbox_policy,
                                equivalent, rewrite_graph, graph_manifest, rewrite_numerics)
from run_extension_agent import public_case_ids, tool_feedback, response_usage, final_checks
import run_extension_agent
from merge_benchmark_rows import audited_input


class BenchmarkOracleTests(unittest.TestCase):
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
                raise TimeoutError("test provider timeout")
            def git(command, **kwargs):
                return "a" * 40 if "rev-parse" in command else b""
            with patch.object(sys, "argv", argv), patch.object(sys, "platform", "darwin"), \
                    patch.object(run_extension_agent, "local_api", side_effect=api), \
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
