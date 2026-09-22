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
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SUPPORTED_TASKS = {"ana-broadcast-shape", "ana-storage-cost"}


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def canonical(value: object) -> str:
    return json.dumps(value, sort_keys=True, allow_nan=False)


def invalid_constant(token: str) -> None:
    raise ValueError(f"non-finite JSON constant: {token}")


def native_attr(value: object) -> str:
    """Serialize the input only; expected outputs never enter native fixtures."""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return f"{value} : i64"
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


def execute(command: list[str | Path], timeout: float) -> dict:
    argv = list(map(str, command))
    try:
        result = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
        return {"command": argv, "exit_code": result.returncode,
                "stdout": result.stdout, "stderr": result.stderr, "timeout": False}
    except subprocess.TimeoutExpired:
        return {"command": argv, "exit_code": None, "stdout": "",
                "stderr": "task process exceeded timeout", "timeout": True}
    except OSError as failure:
        return {"command": argv, "exit_code": None, "stdout": "",
                "stderr": str(failure), "timeout": False}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--task", choices=sorted(SUPPORTED_TASKS), required=True)
    parser.add_argument("--system", choices=("Joggle", "MLIR", "xDSL"), required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--build-root", type=Path, required=True)
    parser.add_argument("--joggle", type=Path)
    parser.add_argument("--builtin-mods", type=Path)
    parser.add_argument("--mlir-dir", type=Path)
    parser.add_argument("--xdsl-python", type=Path)
    parser.add_argument("--timeout", type=float, default=60)
    args = parser.parse_args()
    required = {"Joggle": (args.joggle, args.builtin_mods),
                "MLIR": (args.mlir_dir,), "xDSL": (args.xdsl_python,)}[args.system]
    if any(value is None for value in required):
        parser.error(f"missing native tool paths for {args.system}")
    if args.timeout <= 0:
        parser.error("timeout must be positive")
    args.source = args.source.resolve(strict=True)
    if args.output.exists():
        parser.error("refusing to replace an existing oracle report")
    args.build_root.mkdir(parents=True, exist_ok=True)
    spec_path = ROOT / "manifests/extension-specs.json"
    spec = json.loads(spec_path.read_text())
    task = next(task for task in spec["tasks"] if task["id"] == args.task)
    source_hash = digest(args.source)
    harness_files = [Path(__file__).resolve(), ROOT / "extensions/CMakeLists.txt",
                     ROOT / "extensions/mlir-driver.cpp", ROOT / "extensions/xdsl-driver.py"]
    record = {"schema": "extension-task-oracle/v1", "task": args.task,
              "system": args.system, "source": str(args.source),
              "source_sha256": source_hash, "task_spec_sha256": digest(spec_path),
              "harness_sha256": {str(path.relative_to(ROOT)): digest(path)
                                 for path in harness_files},
              "setup": [], "cases": [], "passed": False}
    with tempfile.TemporaryDirectory(prefix="task-", dir=args.build_root) as directory:
        work = Path(directory)
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
            build = args.build_root.resolve() / "mlir" / build_key
            for argv in (["cmake", "-S", ROOT / "extensions", "-B", build,
                          f"-DMLIR_DIR={args.mlir_dir.resolve()}",
                          f"-DEXTENSION_SOURCE={args.source}", "-DCMAKE_BUILD_TYPE=Release"],
                         ["cmake", "--build", build, "--parallel", "1"]):
                step = execute(argv, args.timeout)
                record["setup"].append(step)
                if step["exit_code"] != 0:
                    break
            command, flags = [build / "extension-oracle"], []
            if command[0].is_file():
                record["executable_sha256"] = digest(command[0])
        setup_ok = all(step["exit_code"] == 0 for step in record["setup"])
        if setup_ok:
            for case in task["positive_cases"] + task["negative_cases"]:
                path = work / ("input.jog" if args.system == "Joggle" else "input.mlir")
                if args.system == "Joggle":
                    source = ("mod fixture\n[request: " + json.dumps(case["input"]) +
                              "]\nfn subject() -> int { return 0 }\n")
                else:
                    source = "module attributes {study.request = " + native_attr(case["input"]) + "} {}\n"
                path.write_text(source)
                step = execute([*command, path, *flags], args.timeout)
                actual = None
                error = ""
                if step["exit_code"] == 0:
                    try:
                        actual = json.loads(step["stdout"], parse_constant=invalid_constant)
                        canonical(actual)
                    except ValueError as failure:
                        actual = None
                        error = str(failure)
                # Compare canonical JSON to distinguish Boolean from integer
                # values as well as detect missing or additional result fields.
                passed = (step["exit_code"] == 0 and not error and
                          canonical(actual) == canonical(case["expect"]))
                record["cases"].append({"id": case["id"], "input": case["input"],
                                        "expected": case["expect"], "actual": actual,
                                        "passed": passed, "decode_error": error, **step})
        record["passed"] = (setup_ok and bool(record["cases"]) and
                            all(case["passed"] for case in record["cases"]) and
                            digest(args.source) == source_hash and
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
