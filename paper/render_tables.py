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
        "**Table 1. Independent-system Linux diagnostics (median milliseconds).**\n\n"
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
        "**Table 2. MobileNetV2 module-policy diagnostics (median milliseconds).**\n\n"
        + table(
            ["Run", "Plain C", "Canonical C", "Canon / plain",
             "Adjacent ORT", "Canon / ORT"],
            body,
            {1, 2, 3, 4, 5},
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
        ("systems", systems()),
        ("policy", policy()),
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
