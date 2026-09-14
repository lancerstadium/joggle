#!/usr/bin/env python3
"""Compile one operator fixture and emit a matched-system study manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from typing import Any


FLAGS = (
    "-std=c11",
    "-O3",
    "-DNDEBUG",
    "-march=native",
    "-Wall",
    "-Wextra",
    "-Wstrict-prototypes",
    "-Werror",
)


def fail(message: str) -> None:
    raise SystemExit(f"prepare_operator_case: {message}")


def checked(command: list[str], *, binary: bool = False) -> bytes | str:
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        error = result.stderr.decode(errors="replace")
        fail(f"command failed ({result.returncode}): {' '.join(command)}\n{error}")
    return result.stdout if binary else result.stdout.decode()


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def relative(path: Path, repo: Path) -> str:
    try:
        return str(path.resolve().relative_to(repo))
    except ValueError:
        fail(f"path must be inside the repository: {path}")


def artifact(path: Path, repo: Path) -> dict[str, str | int]:
    if not path.is_file():
        fail(f"missing artifact: {path}")
    return {
        "path": relative(path, repo),
        "bytes": path.stat().st_size,
        "sha256": digest(path),
    }


def write(path: Path, contents: bytes | str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(contents, bytes):
        path.write_bytes(contents)
    else:
        path.write_text(contents, encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--modules", type=Path, required=True)
    parser.add_argument("--cc", type=Path, required=True)
    parser.add_argument("--ort-python", type=Path, required=True)
    parser.add_argument(
        "--pass", dest="passes", action="append", default=[],
        help="module pass to apply to the canonical function body; repeatable",
    )
    parser.add_argument(
        "--pass-arg", dest="pass_args", action="append", default=[],
        help=("typed Joggle argument passed to the requested pass sequence; "
              "repeatable"),
    )
    parser.add_argument(
        "--module-root", type=Path, action="append", default=[],
        help="additional module search root used by requested passes; repeatable",
    )
    parser.add_argument("--repo", type=Path, default=Path("."))
    parser.add_argument("--trials", type=int, default=40)
    parser.add_argument("--inner-repetitions", type=int, default=100)
    args = parser.parse_args()
    if args.trials < 4 or args.trials % 4:
        parser.error("--trials must be a positive multiple of four")
    if args.inner_repetitions < 1:
        parser.error("--inner-repetitions must be positive")

    repo = args.repo.resolve()
    fixture = args.fixture.resolve()
    output = args.output.resolve()
    app = args.app.resolve()
    tool = args.tool.resolve()
    modules = args.modules.resolve()
    cc = args.cc.resolve()
    ort_python = args.ort_python.resolve()
    module_roots = [path.resolve() for path in args.module_root]
    required = (
        fixture / "model.onnx",
        fixture / "test_data_set_0/input_0.pb",
        fixture / "test_data_set_0/output_0.pb",
        app,
        tool,
        cc,
        ort_python,
        modules / "onnx" / "module.jog",
    )
    if (any(not path.exists() for path in required) or
            any(not path.is_dir() for path in module_roots)):
        parser.error("fixture, tools, compiler, Python, and modules must exist")
    relative(output, repo)
    output.mkdir(parents=True, exist_ok=True)

    paths = {
        "source": output / "model.c",
        "header": output / "model.h",
        "input": output / "input.bin",
        "expected": output / "expected.bin",
        "program_ir": output / "model.jog",
        "canonical_ir": output / "canonical.jog",
        "image": output / "model.vm",
        "weights": output / "weights.bin",
        "api": output / "api.json",
        "harness": output / "harness.c",
        "program": output / "model",
    }
    search = [modules, *module_roots]
    search_args = [item for path in search for item in ("-M", str(path))]
    app_command = [
        str(app),
        str(required[0]),
        str(required[1]),
        str(required[2]),
        str(paths["source"]),
        str(paths["input"]),
        str(paths["expected"]),
        str(modules),
        str(paths["program_ir"]),
        str(paths["image"]),
        str(paths["header"]),
        "no-vm",
        str(paths["canonical_ir"]),
    ]
    checked(app_command)

    transform_commands: list[list[str]] = []
    if args.passes:
        stages = {
            "scheduled": output / "scheduled.jog",
            "clean": output / "clean.jog",
            "planned": output / "planned.jog",
            "noalias": output / "noalias.jog",
        }

        def transform(functions: list[str], source: Path, target: Path,
                      arguments: list[str] | None = None) -> None:
            command = [str(tool), "run", *functions, str(source)]
            for argument in arguments or []:
                command += ["--arg", argument]
            command += search_args
            write(target, checked(command))
            transform_commands.append(command)

        transform(
            args.passes, paths["canonical_ir"], stages["scheduled"],
            args.pass_args,
        )
        transform(
            ["bounds.fold", "opt.fold", "opt.basic", "tile.scalarize",
             "opt.basic"],
            stages["scheduled"], stages["clean"],
        )
        transform(["mem.plan"], stages["clean"], stages["planned"])
        transform(["c.noalias"], stages["planned"], stages["noalias"])
        transform(
            ["c.place"], stages["noalias"], paths["program_ir"],
            ['"static"'],
        )

    external = ['"weights"']
    write(
        paths["weights"],
        checked(
            [str(tool), "emit", "c.data", str(paths["program_ir"]),
             *search_args],
            binary=True,
        ),
    )
    for kind, path in (("c.source", paths["source"]),
                       ("c.header", paths["header"]),
                       ("c.api", paths["api"])):
        action = "query" if kind == "c.api" else "emit"
        write(
            path,
            checked(
                [str(tool), action, kind, str(paths["program_ir"]),
                 "--arg", *external, *search_args]
            ),
        )

    harness_command = [
        str(ort_python),
        str(repo / "examples/onnx/make_harness.py"),
        str(paths["api"]),
        str(paths["harness"]),
        "--header",
        paths["header"].name,
    ]
    checked(harness_command)
    compile_command = [
        str(cc),
        *FLAGS,
        "-I",
        str(output),
        str(paths["source"]),
        str(paths["harness"]),
        "-lm",
        "-o",
        str(paths["program"]),
    ]
    checked(compile_command)
    joggle_command = [
        relative(paths["program"], repo),
        relative(paths["input"], repo),
    ]
    if paths["weights"].stat().st_size:
        joggle_command.append(relative(paths["weights"], repo))
    joggle_command += [
        relative(paths["expected"], repo), "3", str(args.inner_repetitions)
    ]
    checked([str(repo / joggle_command[0]), *joggle_command[1:]])

    ort_command = [
        relative(ort_python, repo) if repo in ort_python.parents else str(ort_python),
        "paper/bench_onnxruntime.py",
        relative(required[0], repo),
        relative(paths["input"], repo),
        relative(paths["expected"], repo),
        "--warmup",
        "3",
        "--repetitions",
        str(args.inner_repetitions),
        "--protocol",
    ]
    checked(
        [str(ort_python), str(repo / "paper/bench_onnxruntime.py"),
         *ort_command[2:]]
    )

    manifest: dict[str, Any] = {
        "schema": 2,
        "study": fixture.name,
        "trials": args.trials,
        "subjects": [
            {
                "system": "joggle",
                "command": joggle_command,
                "artifacts": {
                    name: relative(path, repo)
                    for name, path in paths.items()
                    if (name in {"source", "program", "input", "expected"} or
                        (name == "weights" and path.stat().st_size))
                },
                "version_command": [relative(tool, repo), "--version"],
            },
            {
                "system": "onnxruntime",
                "command": ort_command,
                "artifacts": {
                    "model": relative(required[0], repo),
                    "input": relative(paths["input"], repo),
                    "expected": relative(paths["expected"], repo),
                },
                "version_command": [
                    ort_command[0],
                    "-c",
                    "import onnxruntime as ort; print(ort.__version__)",
                ],
            },
        ],
    }
    write(output / "systems.json", json.dumps(manifest, indent=2) + "\n")
    provenance = {
        "schema": 1,
        "fixture": artifact(required[0], repo),
        "artifacts": {
            name: artifact(path, repo)
            for name, path in paths.items()
            if path.is_file() and name != "image"
        },
        "commands": {
            "application": app_command,
            "passes": transform_commands,
            "harness": harness_command,
            "compile": compile_command,
        },
    }
    write(output / "prepare.json", json.dumps(provenance, indent=2) + "\n")


if __name__ == "__main__":
    main()
