#!/usr/bin/env python3
"""Run the configured ONNX Zoo gates and collect their stage frontiers."""

import argparse
import csv
import hashlib
import json
import subprocess
import sys
from pathlib import Path


RECORD_FIELDS = [
    "schema",
    "model",
    "decode",
    "infer",
    "convert",
    "tensors",
    "nodes",
    "nested_graphs",
    "unknown_before",
    "unknown_after",
    "source_calls_after",
]
OUTPUT_FIELDS = [
    "schema",
    "revision",
    "model_sha256",
    *RECORD_FIELDS[1:],
]
PREFIX = "joggle-zoo "


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        check=False
    )


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def source_revision(build: Path) -> str:
    cache = build / "CMakeCache.txt"
    prefix = "Joggle_SOURCE_DIR:STATIC="
    try:
        source = next(
            Path(line[len(prefix):])
            for line in cache.read_text(encoding="utf-8").splitlines()
            if line.startswith(prefix)
        )
    except (OSError, StopIteration) as error:
        raise ValueError("build has no Joggle source directory") from error
    result = run(["git", "-C", str(source), "rev-parse", "HEAD"])
    revision = result.stdout.strip()
    if result.returncode != 0 or len(revision) != 40:
        raise ValueError("could not resolve the Joggle source revision")
    status = run([
        "git", "-C", str(source), "status", "--porcelain",
        "--untracked-files=no", "--", "CMakeLists.txt", "include", "src",
        "modules", "tool", "test",
    ])
    if status.returncode != 0:
        raise ValueError("could not inspect the Joggle source status")
    if status.stdout:
        raise ValueError(
            "compiler sources are dirty; commit them before collecting evidence"
        )
    return revision


def configured_models(ctest: str, build: Path) -> dict[str, Path]:
    result = run([
        ctest, "--test-dir", str(build), "--show-only=json-v1",
        "-L", "onnx-zoo-record",
    ])
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "could not list Zoo tests")
    document = json.loads(result.stdout)
    models: dict[str, Path] = {}
    for test in document.get("tests", []):
        command = test.get("command", [])
        if len(command) < 2 or Path(command[0]).stem != "joggle-onnx-zoo-test":
            raise ValueError(f"unexpected Zoo test command: {command}")
        path = Path(command[1]).resolve()
        model = path.stem
        if model in models:
            raise ValueError(f"duplicate configured Zoo model: {model}")
        if not path.is_file():
            raise ValueError(f"configured Zoo model is absent: {path}")
        models[model] = path
    if not models:
        raise ValueError("no onnx-zoo-record tests are configured")
    return models


def parse_record(line: str) -> dict[str, str] | None:
    position = line.find(PREFIX)
    if position == -1:
        return None
    fields = {}
    for token in line[position + len(PREFIX):].split():
        if "=" not in token:
            raise ValueError(f"malformed Zoo record token: {token}")
        key, value = token.split("=", 1)
        if key in fields:
            raise ValueError(f"duplicate Zoo record field: {key}")
        fields[key] = value
    if set(fields) != set(RECORD_FIELDS):
        raise ValueError(f"Zoo record fields differ: {sorted(fields)}")
    if fields["schema"] != "1" or fields["decode"] != "pass":
        raise ValueError("unsupported or unsuccessful Zoo record")
    if fields["infer"] not in {"pass", "partial", "not_run"}:
        raise ValueError(f"invalid inference status: {fields['infer']}")
    if fields["convert"] not in {"pass", "partial", "not_run"}:
        raise ValueError(f"invalid conversion status: {fields['convert']}")
    for key in ("tensors", "nodes", "nested_graphs", "unknown_before"):
        if not fields[key].isdigit():
            raise ValueError(f"non-numeric Zoo record field: {key}")
    for key in ("unknown_after", "source_calls_after"):
        if fields[key] != "na" and not fields[key].isdigit():
            raise ValueError(f"invalid optional Zoo record field: {key}")
    if fields["infer"] == "partial":
        if fields["unknown_after"] == "na" or int(fields["unknown_after"]) == 0:
            raise ValueError("partial inference requires a nonzero frontier")
        if fields["convert"] != "not_run":
            raise ValueError("conversion must not run after partial inference")
    if fields["infer"] == "pass" and fields["unknown_after"] != "0":
        raise ValueError("successful inference requires a zero frontier")
    if fields["infer"] == "not_run" and fields["unknown_after"] != "na":
        raise ValueError("unrun inference cannot report a frontier")
    if fields["convert"] == "pass" and fields["source_calls_after"] != "0":
        raise ValueError("successful conversion requires a zero frontier")
    if fields["convert"] == "partial":
        if (fields["source_calls_after"] == "na" or
                int(fields["source_calls_after"]) == 0):
            raise ValueError("partial conversion requires a nonzero frontier")
    if fields["convert"] == "not_run" and fields["source_calls_after"] != "na":
        raise ValueError("unrun conversion cannot report a frontier")
    return fields


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--ctest", default="ctest")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    build = args.build.resolve()
    models = configured_models(args.ctest, build)
    revision = source_revision(build)
    result = run([
        args.ctest, "--test-dir", str(build), "-V",
        "-L", "onnx-zoo-record",
    ])
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise SystemExit(result.returncode)

    records = {}
    for line in result.stdout.splitlines():
        record = parse_record(line)
        if record is None:
            continue
        model = record["model"]
        if model in records:
            raise ValueError(f"duplicate Zoo result: {model}")
        records[model] = record
    if set(records) != set(models):
        raise ValueError(
            f"configured and reported models differ: "
            f"configured={models}, reported={sorted(records)}"
        )

    output = (
        args.output.open("w", newline="", encoding="utf-8")
        if args.output else sys.stdout
    )
    try:
        writer = csv.DictWriter(
            output, fieldnames=OUTPUT_FIELDS, lineterminator="\n"
        )
        writer.writeheader()
        for model, path in models.items():
            record = records[model]
            writer.writerow({
                "schema": "2",
                "revision": revision,
                "model_sha256": digest(path),
                **{key: record[key] for key in RECORD_FIELDS[1:]},
            })
    finally:
        if args.output:
            output.close()


if __name__ == "__main__":
    main()
