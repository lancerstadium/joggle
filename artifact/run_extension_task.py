#!/usr/bin/env python3
"""Execute a native extension against the shared semantic task cases.

This is the task oracle, not an agent or a Figure 4 measurement collector.
Candidate programs are executable code; run untrusted candidates in the agent's
isolated execution environment, not directly on the host.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import signal
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SUPPORTED_TASKS = {"ana-broadcast-shape", "ana-storage-cost", "ana-numeric-range",
                   "ana-fusion-match", "emit-storage-plan", "emit-target-capability",
                   "emit-graph-manifest"}


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def canonical(value: object) -> str:
    return json.dumps(value, sort_keys=True, allow_nan=False)


def invalid_constant(token: str) -> None:
    raise ValueError(f"non-finite JSON constant: {token}")


def equivalent(actual: object, expected: object, numerical: bool, policy: dict) -> bool:
    if isinstance(expected, dict):
        return (isinstance(actual, dict) and actual.keys() == expected.keys() and
                all(equivalent(actual[key], value, numerical, policy)
                    for key, value in expected.items()))
    if isinstance(expected, list):
        return (isinstance(actual, list) and len(actual) == len(expected) and
                all(equivalent(a, b, numerical, policy) for a, b in zip(actual, expected)))
    if numerical and type(expected) in (int, float) and type(actual) in (int, float):
        return math.isfinite(actual) and math.isclose(
            actual, expected, rel_tol=policy["float_rtol"], abs_tol=policy["float_atol"])
    return canonical(actual) == canonical(expected)


def expected_result(task: str, case: dict) -> dict:
    if task == "emit-target-capability":
        request = case["input"]
        return {"target": request["target"], "pointer_bits": request["pointer_bits"],
                "little_endian": request["little_endian"],
                "features": sorted(request["features"]),
                "intrinsics": sorted(request["intrinsics"])}
    return case["expect"]


def native_attr(value: object) -> str:
    """Serialize the input only; expected outputs never enter native fixtures."""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return f"{value} : i64"
    if isinstance(value, float) and math.isfinite(value):
        return f"{value:.17e} : f64"
    if isinstance(value, list):
        return "[" + ", ".join(map(native_attr, value)) + "]"
    if isinstance(value, dict):
        return "{" + ", ".join(
            f"{json.dumps(key)} = {native_attr(item)}"
            for key, item in sorted(value.items())
        ) + "}"
    if isinstance(value, str):
        return json.dumps(value)
    raise ValueError(f"no native attribute serialization for {value!r}")


def fusion_fixture(request: dict, system: str) -> str:
    """Materialize types and SSA uses; never pass a graph description to the candidate."""
    layout = request.get("layout", "NCHW")
    channels = request["channels"]
    shape = [1, channels, 5, 7] if layout == "NCHW" else [1, 5, 7, channels]
    input_shape = [1, 3, 5, 7] if layout == "NCHW" else [1, 5, 7, 3]
    weight_shape = [channels, 3, 3, 3]
    bias_shape = request["bias"]
    conv, bias, activation = request["ops"]
    returned = ["activated"]
    for name, uses in zip(("convolved", "biased"), request["uses"], strict=True):
        returned.extend([name] * (uses - 1))
    if system == "Joggle":
        tensor = lambda dims: "tensor<f32, [" + ", ".join(map(str, dims)) + "]>"
        ty, bt, xt, wt = map(tensor, (shape, bias_shape, input_shape, weight_shape))
        returns = ty if len(returned) == 1 else "(" + ", ".join([ty] * len(returned)) + ")"
        declaration = f"fn side(x: {xt}) -> {xt};\n" if request.get("interleave") else ""
        side = "  let independent = side(x)\n" if request.get("interleave") else ""
        return (f"mod fixture\nuse tensor\n"
                f"{declaration}"
                f"fn {conv}(x: {xt}, w: {wt}) -> {ty};\n"
                f"fn {bias}(x: {ty}, b: {bt}) -> {ty};\n"
                f"fn {activation}(x: {ty}) -> {ty};\n"
                f"[layout: {json.dumps(layout)}]\n"
                f"fn subject(x: {xt}, w: {wt}, b: {bt}) -> {returns} {{\n"
                f"  let convolved = {conv}(x, w)\n"
                f"{side}"
                f"  let biased = {bias}(convolved, b)\n"
                f"  let activated = {activation}(biased)\n"
                f"  return {', '.join(returned)}\n}}\n")
    tensor = lambda dims: "tensor<" + "x".join(map(str, dims)) + "xf32>"
    ty, bt, xt, wt = map(tensor, (shape, bias_shape, input_shape, weight_shape))
    returns = ty if len(returned) == 1 else "(" + ", ".join([ty] * len(returned)) + ")"
    declaration = f"  func.func private @side({xt}) -> {xt}\n" if request.get("interleave") else ""
    side = f"    %independent = func.call @side(%x) : ({xt}) -> {xt}\n" if request.get("interleave") else ""
    return (f"module {{\n"
            f"{declaration}"
            f"  func.func private @{conv}({xt}, {wt}) -> {ty}\n"
            f"  func.func private @{bias}({ty}, {bt}) -> {ty}\n"
            f"  func.func private @{activation}({ty}) -> {ty}\n"
            f"  func.func @subject(%x: {xt}, %w: {wt}, %b: {bt}) -> {returns} "
            f"attributes {{layout = {json.dumps(layout)}}} {{\n"
            f"    %convolved = func.call @{conv}(%x, %w) : ({xt}, {wt}) -> {ty}\n"
            f"{side}"
            f"    %biased = func.call @{bias}(%convolved, %b) : ({ty}, {bt}) -> {ty}\n"
            f"    %activated = func.call @{activation}(%biased) : ({ty}) -> {ty}\n"
            f"    func.return {', '.join('%' + n for n in returned)} : "
            f"{', '.join([ty] * len(returned))}\n  }}\n}}\n")


def graph_fixture(request: dict, system: str) -> str:
    """Create typed SSA calls with native attributes, not a serialized request."""
    def tensor(value: dict) -> str:
        dims, element = value["shape"], value["element"]
        if system == "Joggle":
            return f"tensor<{element}, [" + ", ".join("_" if d < 0 else str(d) for d in dims) + "]>"
        return "tensor<" + "".join(("?" if d < 0 else str(d)) + "x" for d in dims) + element + ">"

    def result_type(types: list[str]) -> str:
        return types[0] if len(types) == 1 else "(" + ", ".join(types) + ")"

    values = {value["name"]: tensor(value) for value in request["inputs"]}
    declarations, body = {}, []
    for node in request["nodes"]:
        operands = node["inputs"]
        inputs = [values[name] for name in operands]
        outputs = [tensor(value) for value in node["results"]]
        signature = (inputs, outputs)
        if node["op"] in declarations and declarations[node["op"]] != signature:
            raise ValueError("fixture symbols must have one function signature")
        declarations[node["op"]] = signature
        for result, ty in zip(node["results"], outputs, strict=True):
            if result["name"] in values:
                raise ValueError("fixture result redefines an SSA name")
            values[result["name"]] = ty
        names = [value["name"] for value in node["results"]]
        attrs = node.get("attrs", {})
        if system == "Joggle":
            if attrs:
                body.append("  [" + ", ".join(f"{key}: {json.dumps(value)}" for key, value in attrs.items()) + "]")
            body.append(f"  let {', '.join(names)} = {node['op']}({', '.join(operands)})")
        else:
            metadata = (" {" + ", ".join(f"{key} = {native_attr(value)}" for key, value in attrs.items()) + "}") if attrs else ""
            body.append(f"    {', '.join('%' + name for name in names)} = func.call @{node['op']}"
                        f"({', '.join('%' + name for name in operands)}){metadata} : "
                        f"({', '.join(inputs)}) -> {result_type(outputs)}")
    returns = [values[name] for name in request["outputs"]]
    if system == "Joggle":
        declarations_text = [f"fn {name}(" + ", ".join(f"a{i}: {ty}" for i, ty in enumerate(inputs)) +
                             f") -> {result_type(outputs)};" for name, (inputs, outputs) in declarations.items()]
        params = ", ".join(f"{value['name']}: {tensor(value)}" for value in request["inputs"])
        return "\n".join(["mod fixture", "use tensor", *declarations_text,
                          f"fn subject({params}) -> {result_type(returns)} {{", *body,
                          "  return " + ", ".join(request["outputs"]), "}", ""])
    declarations_text = [f"  func.func private @{name}({', '.join(inputs)}) -> {result_type(outputs)}"
                         for name, (inputs, outputs) in declarations.items()]
    params = ", ".join(f"%{value['name']}: {tensor(value)}" for value in request["inputs"])
    return "\n".join(["module {", *declarations_text,
                      f"  func.func @subject({params}) -> {result_type(returns)} {{", *body,
                      "    func.return " + ", ".join("%" + name for name in request["outputs"]) +
                      " : " + ", ".join(returns), "  }", "}", ""])


def sandbox_policy(args: argparse.Namespace, work: Path) -> str:
    """Grant native toolchains read access, with writes confined to this trial."""
    if sys.platform != "darwin" or not Path("/usr/bin/sandbox-exec").is_file():
        raise ValueError("--isolate requires macOS sandbox-exec")
    roots = [Path(p) for p in ("/System", "/usr", "/bin", "/sbin", "/opt/homebrew",
                              "/Library/Developer", "/Library/Apple")]
    roots += [work.resolve(), Path(sys.prefix).resolve(), *args.sandbox_read]
    if args.joggle:
        roots.append(args.joggle.resolve().parent)
    if args.builtin_mods:
        roots.append(args.builtin_mods.resolve())
    if args.xdsl_python:
        roots.append(args.xdsl_python.absolute().parent.parent.resolve())
    literals = [args.source.resolve(), ROOT / "extensions/CMakeLists.txt",
                ROOT / "extensions/mlir-driver.cpp", ROOT / "extensions/xdsl-driver.py",
                Path("/"), Path("/dev/null"), Path("/dev/random"), Path("/dev/urandom")]
    read_rules = [f"(subpath {json.dumps(str(path.resolve()))})" for path in roots]
    read_rules += [f"(literal {json.dumps(str(path))})" for path in literals]
    return ("(version 1)\n(deny default)\n"
            "(allow process-exec process-fork sysctl-read file-read-metadata)\n"
            "(allow mach-lookup (global-name \"com.apple.system.logger\"))\n"
            "(allow file-read* " + " ".join(read_rules) + ")\n"
            "(allow file-write* (subpath " + json.dumps(str(work.resolve())) + ") "
            "(literal \"/dev/null\"))\n")


def execute(command: list[str | Path], timeout: float, policy: str | None = None,
            scratch: Path | None = None) -> dict:
    argv = list(map(str, command))
    environment = None
    if policy:
        argv = ["/usr/bin/sandbox-exec", "-p", policy, *argv]
        environment = {"PATH": "/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin",
                       "LANG": "C", "TMPDIR": str(scratch),
                       "PYTHONDONTWRITEBYTECODE": "1", "PYTHONNOUSERSITE": "1"}
        # Preserve the existing home path for tool discovery, without granting
        # access to its contents or inheriting credentials from the environment.
        if "HOME" in os.environ:
            environment["HOME"] = os.environ["HOME"]
    try:
        with subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              text=True, env=environment,
                              cwd=scratch.parent if policy else None,
                              start_new_session=True) as process:
            try:
                stdout, stderr = process.communicate(timeout=timeout)
                return {"command": list(map(str, command)), "exit_code": process.returncode,
                        "stdout": stdout, "stderr": stderr, "timeout": False}
            finally:
                # Also reap descendants after a successful parent exits.
                # No candidate process may survive into the next fixture.
                if os.name == "posix":
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                elif process.poll() is None:
                    process.kill()
                process.communicate()
    except subprocess.TimeoutExpired:
        return {"command": argv, "exit_code": None, "stdout": "",
                "stderr": "task process exceeded timeout", "timeout": True}
    except OSError as failure:
        return {"command": argv, "exit_code": None, "stdout": "",
                "stderr": str(failure), "timeout": False}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--task", choices=sorted(SUPPORTED_TASKS), required=True)
    parser.add_argument("--case", action="append", default=[],
                        help="run selected public fixtures; omitted for complete task scoring")
    parser.add_argument("--system", choices=("Joggle", "MLIR", "xDSL"), required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--build-root", type=Path, required=True)
    parser.add_argument("--joggle", type=Path)
    parser.add_argument("--builtin-mods", type=Path)
    parser.add_argument("--mlir-dir", type=Path)
    parser.add_argument("--xdsl-python", type=Path)
    parser.add_argument("--timeout", type=float, default=60)
    parser.add_argument("--isolate", action="store_true",
                        help="restrict native candidate processes to the trial workspace")
    parser.add_argument("--sandbox-read", type=Path, action="append", default=[],
                        help="additional read-only native toolchain directory")
    args = parser.parse_args()
    required = {"Joggle": (args.joggle, args.builtin_mods),
                "MLIR": (args.mlir_dir,), "xDSL": (args.xdsl_python,)}[args.system]
    if any(value is None for value in required):
        parser.error(f"missing native tool paths for {args.system}")
    if args.timeout <= 0:
        parser.error("timeout must be positive")
    if args.isolate and sys.platform != "darwin":
        parser.error("--isolate requires macOS sandbox-exec")
    for path in args.sandbox_read:
        if not path.is_dir() or path.resolve() in {Path("/"), Path.home(), ROOT.parent}:
            parser.error("--sandbox-read requires a specific toolchain directory")
    args.source = args.source.resolve(strict=True)
    for key in ("joggle", "builtin_mods", "xdsl_python"):
        path = getattr(args, key)
        if path is not None:
            setattr(args, key, path.absolute())
    if args.output.exists():
        parser.error("refusing to replace an existing oracle report")
    args.build_root.mkdir(parents=True, exist_ok=True)
    spec_path = ROOT / "manifests/extension-specs.json"
    spec = json.loads(spec_path.read_text())
    spec_hash = digest(spec_path)
    task = next(task for task in spec["tasks"] if task["id"] == args.task)
    all_cases = task["positive_cases"] + task["negative_cases"]
    if not set(args.case).issubset({case["id"] for case in all_cases}):
        parser.error("--case names a fixture outside the selected task")
    source_hash = digest(args.source)
    harness_files = [Path(__file__).resolve(), ROOT / "extensions/CMakeLists.txt",
                     ROOT / "extensions/mlir-driver.cpp", ROOT / "extensions/xdsl-driver.py"]
    record = {"schema": "extension-task-oracle/v1", "task": args.task,
              "system": args.system, "source": str(args.source),
              "source_sha256": source_hash, "task_spec_sha256": spec_hash,
              "harness_sha256": {str(path.relative_to(ROOT)): digest(path)
                                 for path in harness_files},
              "setup": [], "cases": [], "passed": False}
    record["complete_task"] = not args.case
    with tempfile.TemporaryDirectory(prefix="task-", dir=args.build_root) as directory:
        work = Path(directory).resolve()
        scratch = work / "tmp"
        scratch.mkdir()
        policy = sandbox_policy(args, work) if args.isolate else None
        record["execution_isolation"] = {"kind": "macos-seatbelt" if policy else "none",
                                         "policy": policy}
        if args.system == "Joggle":
            mod = work / "mods/extension"
            mod.mkdir(parents=True)
            shutil.copyfile(args.source, mod / "module.jog")
            command = [args.joggle, "query", "extension.analyze"]
            flags = ["-M", args.builtin_mods, "-M", work / "mods"]
        elif args.system == "xDSL":
            command = [args.xdsl_python, ROOT / "extensions/xdsl-driver.py", args.source]
            flags = []
        else:
            # A candidate must never inherit another candidate's executable.
            # Hash-separated builds also avoid timestamp-resolution races when
            # alternating sources or testing several patches in quick succession.
            build_key = hashlib.sha256(canonical({
                "source": str(args.source), "sha256": source_hash,
                "harness": record["harness_sha256"],
                "mlir_dir": str(args.mlir_dir.resolve()),
            }).encode()).hexdigest()
            build = (work / "build" if args.isolate else
                     args.build_root.resolve() / "mlir" / build_key)
            for argv in (["cmake", "-S", ROOT / "extensions", "-B", build,
                          f"-DMLIR_DIR={args.mlir_dir.resolve()}",
                          f"-DEXTENSION_SOURCE={args.source}", "-DCMAKE_BUILD_TYPE=Release"],
                         ["cmake", "--build", build, "--parallel", "1"]):
                step = execute(argv, args.timeout, policy, scratch)
                record["setup"].append(step)
                if step["exit_code"] != 0:
                    break
            command, flags = [build / "extension-oracle"], []
            if command[0].is_file():
                record["executable_sha256"] = digest(command[0])
        setup_ok = all(step["exit_code"] == 0 for step in record["setup"])
        if setup_ok:
            for case in all_cases:
                if args.case and case["id"] not in args.case:
                    continue
                path = work / ("input.jog" if args.system == "Joggle" else "input.mlir")
                if args.task == "ana-fusion-match":
                    source = fusion_fixture(case["input"], args.system)
                elif args.task == "emit-graph-manifest":
                    source = graph_fixture(case["input"], args.system)
                elif args.system == "Joggle":
                    source = ("mod fixture\n[request: " + json.dumps(case["input"]) +
                              "]\nfn subject() -> int { return 0 }\n")
                else:
                    source = "module attributes {study.request = " + native_attr(case["input"]) + "} {}\n"
                path.write_text(source)
                step = execute([*command, path, *flags], args.timeout, policy, scratch)
                actual = None
                error = ""
                if step["exit_code"] == 0:
                    try:
                        actual = json.loads(step["stdout"], parse_constant=invalid_constant)
                        canonical(actual)
                    except ValueError as failure:
                        actual = None
                        error = str(failure)
                # Numeric tasks use the shared tolerance only for numbers;
                # object keys, sequence lengths, Booleans, and errors stay exact.
                expected = expected_result(args.task, case)
                passed = (step["exit_code"] == 0 and not error and
                          equivalent(actual, expected, task["oracle"]["comparison"] == "numerical",
                                     spec["comparison_policy"]))
                repeat = None
                if task["family"] == "emission" and step["exit_code"] == 0:
                    repeat = execute([*command, path, *flags], args.timeout, policy, scratch)
                    passed = passed and repeat["exit_code"] == 0 and repeat["stdout"] == step["stdout"]
                record["cases"].append({"id": case["id"], "input": case["input"],
                                        "expected": expected, "actual": actual,
                                        "expected_sha256": hashlib.sha256(canonical(expected).encode()).hexdigest(),
                                        "passed": passed, "repeat": repeat,
                                        "decode_error": error, **step})
        record["passed"] = (setup_ok and bool(record["cases"]) and
                            all(case["passed"] for case in record["cases"]) and
                            digest(args.source) == source_hash and
                            digest(spec_path) == spec_hash and
                            all(digest(ROOT / path) == value
                                for path, value in record["harness_sha256"].items()))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x") as stream:
        json.dump(record, stream, indent=2, sort_keys=True, allow_nan=False)
        stream.write("\n")
    passed = sum(case["passed"] for case in record["cases"])
    print(f"{args.system}/{args.task}: {passed}/{len(record['cases'])} cases; {args.output}")
    return 0 if record["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
