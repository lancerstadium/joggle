#!/usr/bin/env python3
"""Collect descriptive, mechanically reproducible extension footprints."""

import argparse
import csv
import hashlib
import json
import subprocess
import sys
from pathlib import Path


def source_lines(path: Path) -> int:
    count = 0
    block_comment = False
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if block_comment:
            if "*/" in line:
                block_comment = False
                line = line.split("*/", 1)[1].strip()
            else:
                continue
        if line.startswith("/*"):
            if "*/" not in line[2:]:
                block_comment = True
            continue
        if not line or line.startswith("//"):
            continue
        count += 1
    return count


def run(command: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command, cwd=cwd, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, check=False
    )


def module_info(
    tool: Path, module: str, module_paths: list[Path], cwd: Path
) -> tuple[set[str], int]:
    paths: list[str] = []
    for path in module_paths:
        paths.extend(["-M", str(path)])
    checked = run([str(tool), "module", "check", module, *paths], cwd)
    if checked.returncode != 0:
        raise RuntimeError(
            f"module check failed for {module}: {checked.stderr.strip()}"
        )
    command = [str(tool), "module", "info", module, *paths]
    result = run(command, cwd)
    if result.returncode != 0:
        raise RuntimeError(
            f"module info failed for {module}: {result.stderr.strip()}"
        )
    dependencies = {
        line.removeprefix("use ")
        for line in result.stdout.splitlines()
        if line.startswith("use ")
    }
    public_functions = sum(
        line.startswith("fn ") for line in result.stdout.splitlines()
    )
    return dependencies, public_functions


def validate(build: Path, tests: list[str], cwd: Path) -> str:
    expression = "^(" + "|".join(tests) + ")$"
    result = run(
        ["ctest", "--test-dir", str(build), "-R", expression,
         "--output-on-failure"],
        cwd,
    )
    executed = sum("Test #" in line for line in result.stdout.splitlines())
    if result.returncode != 0 or executed != len(tests):
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        return "fail"
    return "pass"


def source_digest(paths: list[Path], repo: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(paths):
        digest.update(path.relative_to(repo).as_posix().encode("utf-8"))
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--module-path", type=Path, action="append", default=[])
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    repo = args.repo.resolve()
    manifest_path = args.manifest.resolve()
    tool = args.tool.resolve()
    build = args.build.resolve()
    module_paths = [path.resolve() for path in args.module_path]
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    tasks = manifest.get("tasks")
    if manifest.get("schema") != 1 or not isinstance(tasks, list) or not tasks:
        raise ValueError("extension manifest must use schema 1 and contain tasks")

    rows = []
    task_ids: set[str] = set()
    for task in tasks:
        required = {"id", "scope", "modules", "sources", "tests"}
        if not isinstance(task, dict) or not required.issubset(task):
            raise ValueError("each extension task must contain the required fields")
        if task["id"] in task_ids:
            raise ValueError(f"duplicate extension task: {task['id']}")
        task_ids.add(task["id"])
        if not task["modules"] or not task["sources"] or not task["tests"]:
            raise ValueError(f"extension task {task['id']} has an empty field")
        paths = [(repo / source).resolve() for source in task["sources"]]
        for path in paths:
            if repo not in path.parents or not path.is_file():
                raise ValueError(f"invalid extension source: {path}")
        dependencies: set[str] = set()
        functions = 0
        for module in task["modules"]:
            uses, count = module_info(tool, module, module_paths, repo)
            dependencies.update(uses)
            functions += count
        rows.append({
            "schema": manifest["schema"],
            "task": task["id"],
            "scope": task["scope"],
            "modules": len(task["modules"]),
            "source_files": len(paths),
            "source_sloc": sum(source_lines(path) for path in paths),
            "source_bytes": sum(path.stat().st_size for path in paths),
            "declared_dependencies": len(dependencies),
            "public_functions": functions,
            "validation": validate(build, task["tests"], repo),
            "source_sha256": source_digest(paths, repo),
        })

    fields = list(rows[0])
    output = (
        args.output.open("w", newline="", encoding="utf-8")
        if args.output
        else sys.stdout
    )
    try:
        writer = csv.DictWriter(output, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    finally:
        if args.output:
            output.close()


if __name__ == "__main__":
    main()
