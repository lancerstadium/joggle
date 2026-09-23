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
                   "emit-graph-manifest", "rew-add-zero", "rew-redundant-cast", "rew-transpose-pair",
                   "con-instruction-select"}

REWRITE_TASKS = {"rew-add-zero", "rew-redundant-cast", "rew-transpose-pair", "con-instruction-select"}


def matmul_graph(request: dict, select: bool) -> dict:
    element = request["type"]
    a, b = request["lhs"], request["rhs"]
    result = {"name": "y", "element": "f32", "shape": a[:-1] + [b[-1]]}
    target = {"op": "mma_m16n16k16", "inputs": ["a", "b"], "results": [result]}
    node = {"op": "mma_m16n16k16" if select else "matmul",
            "inputs": ["a", "b"], "results": [result], "attrs": {"tag": "selection"}}
    if select:
        node["attrs"]["tiles"] = [a[-2] // 16, b[-1] // 16, a[-1] // 16]
    return {"inputs": [{"name": "a", "element": element, "shape": a},
                       {"name": "b", "element": element, "shape": b}],
            "declarations": [target], "nodes": [node],
            "outputs": ["y", "y"] if request.get("repeated_output") else ["y"]}


def expected_rejection(step: dict, expected: dict) -> bool:
    diagnostic = expected.get("error")
    code = step.get("exit_code")
    return (isinstance(diagnostic, str) and bool(diagnostic) and
            type(code) is int and code > 0 and step.get("timeout") is False and
            not step.get("stdout", "").strip() and diagnostic in step.get("stderr", ""))


def case_completed(case: dict) -> bool:
    expected = case.get("expected", {})
    if isinstance(expected, dict) and "error" in expected:
        return expected_rejection(case, expected)
    return case.get("exit_code") == 0 and not case.get("decode_error")


def transpose_graph(request: dict, eliminate: bool) -> dict:
    shape, element = request["shape"], request.get("element", "f32")
    operand = {"name": "x", "element": element, "shape": shape}
    nodes, source = [], "x"
    for index, permutation in enumerate((request["p"], request["q"])):
        source_shape = shape
        # Invalid permutations remain native SSA fixtures for candidate-side
        # validation. They have no numerical interpretation.
        if sorted(permutation) == list(range(len(shape))):
            shape = [shape[i] for i in permutation]
        result = f"transpose{index}"
        tag = lambda dims: "x".join(map(str, dims)) or "scalar"
        nodes.append({"op": f"transpose_{element}_{tag(source_shape)}_to_{tag(shape)}", "inputs": [source],
                      "results": [{"name": result, "element": element, "shape": shape}],
                      "attrs": {"perm": permutation}})
        source = result
    shared = request.get("return_intermediate", False)
    return {"inputs": [operand],
            "nodes": (nodes[:1] if shared else []) if eliminate else nodes,
            "outputs": (["x"] if eliminate else [source]) + (["transpose0"] if shared else [])}


def cast_graph(request: dict, eliminate: bool) -> dict:
    element, shape = request["element"], request["shape"]
    operand = {"name": "x", "element": element, "shape": shape}
    nodes, source = [], "x"
    for index, result_element in enumerate(request["casts"]):
        result = f"cast{index}"
        nodes.append({"op": f"cast_{element}_{result_element}", "inputs": [source],
                      "results": [{"name": result, "element": result_element,
                                   "shape": request.get("result_shape", shape)}]})
        element, source = result_element, result
    shared = request.get("return_intermediate", False)
    return {"inputs": [operand],
            "nodes": (nodes[:1] if shared else []) if eliminate else nodes,
            "outputs": (["x"] if eliminate else [source]) + (["cast0"] if shared else [])}


def rewrite_graph(request: dict, eliminate: bool = False) -> dict:
    """Build the tensor-dialect fixture; the extension sees native SSA, not this map."""
    if "lhs" in request and "rhs" in request:
        return matmul_graph(request, eliminate)
    if "casts" in request:
        return cast_graph(request, eliminate)
    if "p" in request and "q" in request:
        return transpose_graph(request, eliminate)
    element, shape = request["element"], request["shape"]
    constant_shape = request.get("constant_shape", shape)
    rank = max(len(shape), len(constant_shape))
    a = [1] * (rank - len(shape)) + shape
    b = [1] * (rank - len(constant_shape)) + constant_shape
    if any(x != y and x != 1 and y != 1 for x, y in zip(a, b)):
        raise ValueError("incompatible fixture broadcast")
    result_shape = [y if x == 1 else x for x, y in zip(a, b)]
    operand = {"name": "x", "element": element, "shape": shape}
    constant = {"op": "splat", "inputs": [],
                "results": [{"name": "zero", "element": element, "shape": constant_shape}],
                "attrs": {"value": request["constant"]}}
    add = {"op": "add", "inputs": ["x", "zero"] if request["side"] == "rhs" else ["zero", "x"],
           "results": [{"name": "sum", "element": element, "shape": result_shape}],
           "attrs": {"no_signed_zeros": request.get("no_signed_zeros", False)}}
    shared = request.get("return_constant", False)
    nodes = ([constant] if shared else []) if eliminate else [constant, add]
    return {"inputs": [operand], "nodes": nodes,
            "outputs": (["x"] if eliminate else ["sum"]) + (["zero"] if shared else [])}


def graph_manifest(graph: dict) -> dict:
    ids = {}
    def define(value: dict) -> dict:
        key = f"v{len(ids)}"
        ids[value["name"]] = key
        return {"id": key, "type": {"element": value["element"], "shape": value["shape"]}}
    inputs = [define(value) for value in graph["inputs"]]
    nodes = []
    for node in graph["nodes"]:
        operands = [ids[name] for name in node["inputs"]]
        results = [define(value) for value in node["results"]]
        nodes.append({"id": f"n{len(nodes)}", "op": node["op"], "inputs": operands,
                      "results": results, "attrs": sorted(map(list, node.get("attrs", {}).items()))})
    return {"schema_version": 1, "inputs": inputs, "nodes": nodes,
            "outputs": [ids[name] for name in graph["outputs"]]}


def rewrite_numerics(actual: dict, original: dict, no_signed_zeros: bool) -> dict:
    """Interpret the independently observed post-IR, including strict zero bits."""
    import numpy as np
    dtypes = {"f16": np.float16, "f32": np.float32, "f64": np.float64, "i8": np.int8,
              "i16": np.int16, "i32": np.int32, "i64": np.int64}
    def evaluate(graph: dict, sample: list[float]) -> list:
        values = {}
        for value in graph["inputs"]:
            ty = value["type"]
            count = math.prod(ty["shape"])
            values[value["id"]] = np.resize(np.asarray(sample).astype(dtypes[ty["element"]]), count).reshape(ty["shape"])
        for node in graph["nodes"]:
            if len(node["results"]) != 1:
                raise ValueError("rewrite oracle requires one result per fixture operation")
            result = node["results"][0]
            ty = result["type"]
            attrs = dict(node["attrs"])
            if node["op"] == "splat" and not node["inputs"]:
                value = np.full(ty["shape"], attrs["value"], dtype=dtypes[ty["element"]])
            elif node["op"] == "add" and len(node["inputs"]) == 2:
                a, b = (values[key] for key in node["inputs"])
                value = np.add(a, b, dtype=dtypes[ty["element"]])
            elif node["op"].startswith("cast_") and len(node["inputs"]) == 1:
                converted = values[node["inputs"][0]].astype(dtypes[ty["element"]])
                value = np.broadcast_to(converted, ty["shape"])
            elif node["op"].startswith("transpose_") and len(node["inputs"]) == 1:
                value = np.transpose(values[node["inputs"][0]], attrs["perm"])
            elif node["op"] in {"matmul", "mma_m16n16k16"} and len(node["inputs"]) == 2:
                inputs = [values[key] for key in node["inputs"]]
                a, b = (value.astype(np.float32) for value in inputs)
                if node["op"] == "mma_m16n16k16":
                    if (any(value.dtype != np.float16 for value in inputs) or
                            a.ndim != 2 or b.ndim != 2 or a.shape[1] != b.shape[0] or
                            any(d <= 0 or d % 16 for d in (*a.shape, b.shape[1])) or
                            attrs.get("tiles") != [a.shape[0] // 16, b.shape[1] // 16, a.shape[1] // 16]):
                        raise ValueError("invalid matrix instruction selection")
                value = np.matmul(a, b)
            else:
                raise ValueError("unsupported operation in rewrite result")
            if list(value.shape) != ty["shape"]:
                raise ValueError("rewrite result has the wrong runtime shape")
            values[result["id"]] = value
        return [values[key] for key in graph["outputs"]]
    cases = []
    for sample in ([-0.0, 0.0, 1.0, -1.0], [-17, 23, 255, -1024],
                   [0, 0, 0, 0], [0.1, -0.3, 1.5, -2.75], [-128, 127, 256, -257]):
        before, after = evaluate(original, sample), evaluate(actual, sample)
        passed = len(before) == len(after)
        for a, b in zip(before, after):
            passed = passed and a.shape == b.shape and a.dtype == b.dtype
            passed = passed and (bool(np.array_equal(a, b)) if no_signed_zeros else a.tobytes() == b.tobytes())
        cases.append({"input": sample, "passed": passed,
                      "before_bits": [value.tobytes().hex() for value in before],
                      "after_bits": [value.tobytes().hex() for value in after]})
    return {"passed": all(case["passed"] for case in cases), "cases": cases}


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
    extra = request.get("declarations", [])
    for index, node in enumerate([*extra, *request["nodes"]]):
        operands = node["inputs"]
        inputs = [values[name] for name in operands]
        outputs = [tensor(value) for value in node["results"]]
        signature = (inputs, outputs)
        if node["op"] in declarations and declarations[node["op"]] != signature:
            raise ValueError("fixture symbols must have one function signature")
        declarations[node["op"]] = signature
        if index < len(extra):
            continue
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
                ROOT / "extensions/emit-graph-manifest/reference.py",
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
    rewriting = args.task in REWRITE_TASKS
    all_cases = task["positive_cases"] + task["negative_cases"]
    if not set(args.case).issubset({case["id"] for case in all_cases}):
        parser.error("--case names a fixture outside the selected task")
    source_hash = digest(args.source)
    harness_files = [Path(__file__).resolve(), ROOT / "extensions/CMakeLists.txt",
                     ROOT / "extensions/mlir-driver.cpp", ROOT / "extensions/xdsl-driver.py"]
    if rewriting:
        harness_files += [ROOT / "extensions/emit-graph-manifest/reference.jog",
                          ROOT / "extensions/emit-graph-manifest/reference.py"]
    record = {"schema": "extension-task-oracle/v1", "task": args.task,
              "system": args.system, "source": str(args.source),
              "source_sha256": source_hash, "task_spec_sha256": spec_hash,
              "harness_sha256": {str(path.relative_to(ROOT)): digest(path)
                                 for path in harness_files},
              "setup": [], "cases": [], "passed": False}
    record["complete_task"] = not args.case
    if rewriting:
        import numpy as np
        record["numerical_oracle"] = {"numpy_version": np.__version__,
                                      "comparison": "bitwise-except-explicit-nsz"}
    with tempfile.TemporaryDirectory(prefix="task-", dir=args.build_root) as directory:
        work = Path(directory).resolve()
        scratch = work / "tmp"
        scratch.mkdir()
        policy = sandbox_policy(args, work) if args.isolate else None
        record["execution_isolation"] = {"kind": "macos-seatbelt" if policy else "none",
                                         "policy": policy}
        if rewriting and args.system != "Joggle":
            observer_identity = execute([args.xdsl_python or sys.executable, "-c",
                "import importlib.metadata,json,sys; print(json.dumps({'python':sys.version,"
                "'xdsl':importlib.metadata.version('xdsl')}))"], args.timeout, policy, scratch)
            record["setup"].append(observer_identity)
            if observer_identity["exit_code"] == 0:
                record["observer_identity"] = json.loads(observer_identity["stdout"])
        if args.system == "Joggle":
            mod = work / "mods/extension"
            mod.mkdir(parents=True)
            shutil.copyfile(args.source, mod / "module.jog")
            command = [args.joggle, "run", "extension.transform"] if rewriting else [args.joggle, "query", "extension.analyze"]
            flags = ["-M", args.builtin_mods, "-M", work / "mods"]
            if rewriting:
                observer = work / "mods/observer"
                observer.mkdir()
                observer.joinpath("module.jog").write_text(
                    (ROOT / "extensions/emit-graph-manifest/reference.jog").read_text().replace(
                        "mod extension\n", "mod observer\n", 1).replace(
                        "for op in ir.ops(subject) {",
                        'for op in ir.ops(subject) {\n    assert(ir.kind(op) == "call" || '
                        'ir.kind(op) == "return", "rewrite result contains unsupported control or operations")'))
        elif args.system == "xDSL":
            command = [args.xdsl_python, ROOT / "extensions/xdsl-driver.py", args.source]
            flags = ["--rewrite"] if rewriting else []
        else:
            # A candidate must never inherit another candidate's executable.
            # Hash-separated builds also avoid timestamp-resolution races when
            # alternating sources or testing several patches in quick succession.
            build_key = hashlib.sha256(canonical({
                "source": str(args.source), "sha256": source_hash,
                "harness": record["harness_sha256"],
                "mlir_dir": str(args.mlir_dir.resolve()),
                "rewrite": rewriting,
            }).encode()).hexdigest()
            build = (work / "build" if args.isolate else
                     args.build_root.resolve() / "mlir" / build_key)
            for argv in (["cmake", "-S", ROOT / "extensions", "-B", build,
                          f"-DMLIR_DIR={args.mlir_dir.resolve()}",
                          f"-DEXTENSION_SOURCE={args.source}",
                          f"-DEXTENSION_REWRITE={'ON' if rewriting else 'OFF'}", "-DCMAKE_BUILD_TYPE=Release"],
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
                if rewriting:
                    source = graph_fixture(rewrite_graph(case["input"]), args.system)
                elif args.task == "ana-fusion-match":
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
                if rewriting and "error" in case["expect"]:
                    passed = expected_rejection(step, case["expect"])
                    record["cases"].append({"id": case["id"], "input": case["input"],
                                            "expected": case["expect"], "passed": passed,
                                            "actual": None, "decode_error": "", **step})
                    continue
                actual = None
                error = ""
                observation = None
                numerics = None
                checked_step = step
                if rewriting and step["exit_code"] == 0:
                    transformed = work / ("transformed.jog" if args.system == "Joggle" else "transformed.mlir")
                    transformed.write_text(step["stdout"])
                    if args.system == "Joggle":
                        inspect = [args.joggle, "query", "observer.analyze", transformed, *flags]
                    else:
                        inspect = [args.xdsl_python or sys.executable, ROOT / "extensions/xdsl-driver.py",
                                   "--inspect", transformed]
                    observation = execute(inspect, args.timeout, policy, scratch)
                    checked_step = observation
                    if observation["exit_code"] != 0:
                        error = "post-rewrite IR observation failed: " + observation["stderr"][-6000:]
                if checked_step["exit_code"] == 0:
                    try:
                        actual = json.loads(checked_step["stdout"], parse_constant=invalid_constant)
                        canonical(actual)
                    except ValueError as failure:
                        actual = None
                        error = str(failure)
                # Numeric tasks use the shared tolerance only for numbers;
                # object keys, sequence lengths, Booleans, and errors stay exact.
                expected = (graph_manifest(rewrite_graph(case["input"], case["expect"]["eliminate"]))
                            if rewriting else expected_result(args.task, case))
                passed = (step["exit_code"] == 0 and checked_step["exit_code"] == 0 and not error and
                          equivalent(actual, expected, task["oracle"]["comparison"] == "numerical",
                                     spec["comparison_policy"]))
                if rewriting and passed:
                    try:
                        numerics = rewrite_numerics(actual, graph_manifest(rewrite_graph(case["input"])),
                                                    case["input"].get("no_signed_zeros", False))
                        passed = numerics["passed"]
                    except (ValueError, KeyError, TypeError) as failure:
                        passed = False
                        error = str(failure)
                repeat = None
                if task["family"] == "emission" and step["exit_code"] == 0:
                    repeat = execute([*command, path, *flags], args.timeout, policy, scratch)
                    passed = passed and repeat["exit_code"] == 0 and repeat["stdout"] == step["stdout"]
                record["cases"].append({"id": case["id"], "input": case["input"],
                                        "expected": expected, "actual": actual,
                                        "expected_sha256": hashlib.sha256(canonical(expected).encode()).hexdigest(),
                                        "passed": passed, "repeat": repeat,
                                        "observation": observation, "numerics": numerics,
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
