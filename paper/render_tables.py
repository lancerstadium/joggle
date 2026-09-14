#!/usr/bin/env python3
"""Render manuscript tables from preserved evaluation records."""

from __future__ import annotations

import argparse
import csv
import json
import re
import statistics
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANUSCRIPT = ROOT / "paper" / "manuscript.md"


def rows(path: str) -> list[dict[str, str]]:
    with (ROOT / path).open(newline="", encoding="utf-8") as stream:
        result = list(csv.DictReader(stream))
    if not result:
        raise ValueError(f"empty record: {path}")
    return result


def document(path: str) -> dict[str, object]:
    value = json.loads((ROOT / path).read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"document must be an object: {path}")
    return value


def table(headers: list[str], body: list[list[str]], right: set[int]) -> str:
    if not body or any(len(row) != len(headers) for row in body):
        raise ValueError("table rows must be nonempty and rectangular")
    align = ["---:" if index in right else "---" for index in range(len(headers))]
    out = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join(align) + " |",
    ]
    out.extend("| " + " | ".join(row) + " |" for row in body)
    return "\n".join(out)


def model_frontier() -> str:
    body: list[list[str]] = []
    for row in rows("paper/data/model-frontier-pilot.csv"):
        infer = row["infer"]
        if infer != "pass":
            infer += f" ({row['unknown_after']} unknown)"
        convert = row["convert"]
        if convert == "partial":
            convert += f" ({row['source_calls_after']} calls)"
        body.append([row["model"], row["nodes"], infer, convert])
    return (
        "**Table 1. Pinned ONNX Model Zoo structural frontier.**\n\n"
        + table(
            ["Model", "Nodes", "Type inference", "Semantic conversion"], body, {1}
        )
    )


def median(path: str, system: str) -> float:
    records = rows(path)
    samples = [float(row["seconds"]) for row in records if row["system"] == system]
    trials = {row["trial"] for row in records}
    if len(samples) != len(trials) or len(samples) < 3:
        raise ValueError(
            f"expected one {system} sample per trial in {path}, found "
            f"{len(samples)} samples for {len(trials)} trials"
        )
    return statistics.median(samples)


def cpu(path: str) -> str:
    host = document(path).get("host")
    if not isinstance(host, dict) or not isinstance(host.get("processor"), str):
        raise ValueError(f"missing processor in {path}")
    name = re.sub(r"\s+\d+-Core Processor$", "", host["processor"])
    match = re.fullmatch(r"Intel\(R\) Xeon\(R\) Platinum ([^ ]+) CPU.*", name)
    return f"Intel Xeon {match.group(1)}" if match else name


def systems() -> str:
    executions = [
        (
            "paper/data/mobilenetv2-systems-pilot.json",
            "paper/data/mobilenetv2-systems-pilot.csv",
            "paper/data/mnist-systems-pilot.csv",
        ),
        (
            "paper/data/linux-replication/mobilenetv2.json",
            "paper/data/linux-replication/mobilenetv2.csv",
            "paper/data/linux-replication/mnist.csv",
        ),
    ]
    body: list[list[str]] = []
    for record, mobilenet, mnist in executions:
        for label, path in (("MobileNetV2", mobilenet), ("MNIST", mnist)):
            joggle = median(path, "joggle")
            runtime = median(path, "onnxruntime")
            body.append(
                [cpu(record), label, f"{joggle * 1000:.3f}",
                 f"{runtime * 1000:.3f}", f"{joggle / runtime:.2f}x"]
            )
    return (
        "**Table 2. Independent-system Linux diagnostics (median milliseconds).**\n\n"
        + table(
            ["CPU class", "Model", "Joggle C", "ONNX Runtime", "Joggle / ORT"],
            body,
            {2, 3, 4},
        )
    )


def policy() -> str:
    executions = [
        ("A", "paper/data/mobilenetv2-policy/plain.csv",
         "paper/data/mobilenetv2-policy/canon.csv"),
        ("B", "paper/data/mobilenetv2-policy/replication/plain.csv",
         "paper/data/mobilenetv2-policy/replication/canon.csv"),
    ]
    body: list[list[str]] = []
    for label, plain_path, canon_path in executions:
        plain = median(plain_path, "joggle")
        canon = median(canon_path, "joggle")
        runtime = median(canon_path, "onnxruntime")
        body.append(
            [label, f"{plain * 1000:.3f}", f"{canon * 1000:.3f}",
             f"{canon / plain:.4f}", f"{runtime * 1000:.3f}",
             f"{canon / runtime:.2f}x"]
        )
    return (
        "**Table 3. MobileNetV2 module-policy diagnostics (median milliseconds).**\n\n"
        + table(
            ["Run", "Plain C", "Canonical C", "Canon / plain",
             "Adjacent ORT", "Canon / ORT"],
            body,
            {1, 2, 3, 4, 5},
        )
    )


def indexed(path: str) -> dict[str, dict[str, str]]:
    return {row["task"]: row for row in rows(path)}


def extension_surface() -> str:
    tasks = document("paper/extension-tasks.json").get("tasks")
    if not isinstance(tasks, list):
        raise ValueError("extension manifest has no task list")
    joggle = indexed("paper/data/extension-footprint-pilot.csv")
    tvm = indexed("paper/data/extension-tvm-pilot.csv")
    onnx = indexed("paper/data/extension-onnx-mlir-pilot.csv")
    external = document("paper/baselines/onnx-mlir/external-kernel/result.json")
    if external.get("task") != "external-kernel" or external.get("status") != "unsupported":
        raise ValueError("unexpected ONNX-MLIR external-kernel result")
    policy_result = document("paper/baselines/onnx-mlir/policy/result.json")
    if policy_result.get("task") != "policy" or policy_result.get("status") != "pass":
        raise ValueError("unexpected ONNX-MLIR policy result")

    def observed(records: dict[str, dict[str, str]], task: str) -> str:
        record = records.get(task)
        if record is None:
            return "incomplete"
        if record["validation"] != "pass":
            raise ValueError(f"unexpected validation for {task}")
        suffix = f", {record['source_sloc']} lines"
        if task == "implementation" and record.get("source_files") == "6":
            suffix += " in six files"
        return "pass" + suffix

    body: list[list[str]] = []
    for item in tasks:
        if not isinstance(item, dict) or not isinstance(item.get("id"), str):
            raise ValueError("extension task must have an id")
        task = item["id"]
        onnx_value = observed(onnx, task)
        if task == "external-kernel":
            onnx_value = "unsupported at first required MatMul case"
        body.append([task, observed(joggle, task), observed(tvm, task), onnx_value])
    return (
        "**Table 4. Frozen extension tasks and observed authored source surface.**\n\n"
        + table(
            ["Task", "Joggle", "TVM control", "ONNX-MLIR system path"], body, set()
        )
    )


def replace(text: str, name: str, rendered: str) -> str:
    begin = f"<!-- BEGIN GENERATED: {name} -->"
    end = f"<!-- END GENERATED: {name} -->"
    pattern = re.compile(re.escape(begin) + r".*?" + re.escape(end), re.DOTALL)
    updated, count = pattern.subn(f"{begin}\n{rendered}\n{end}", text)
    if count != 1:
        raise ValueError(f"expected exactly one generated block for {name}, found {count}")
    return updated


def render(text: str) -> str:
    for name, value in (
        ("model-frontier", model_frontier()),
        ("systems", systems()),
        ("policy", policy()),
        ("extension-surface", extension_surface()),
    ):
        text = replace(text, name, value)
    return text


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="fail if tables are stale")
    mode.add_argument("--update", action="store_true", help="update the manuscript in place")
    args = parser.parse_args()

    current = MANUSCRIPT.read_text(encoding="utf-8")
    expected = render(current)
    if args.check:
        if current != expected:
            raise SystemExit("render_tables: manuscript tables are stale; run with --update")
        print("render_tables: manuscript tables are current")
        return
    MANUSCRIPT.write_text(expected, encoding="utf-8")
    print("render_tables: updated manuscript tables")


if __name__ == "__main__":
    main()
