#!/usr/bin/env python3
"""Render one matched operator matrix as a native LaTeX speedup table."""

from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
from pathlib import Path
from typing import Any


def fail(message: str) -> None:
    raise SystemExit(f"render_operator_table: {message}")


def document(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        fail(f"cannot read {path}: {error}")
    if not isinstance(value, dict):
        fail(f"expected an object in {path}")
    return value


def median_times(path: Path) -> dict[str, float]:
    try:
        with path.open(newline="", encoding="utf-8") as stream:
            rows = list(csv.DictReader(stream))
    except OSError as error:
        fail(f"cannot read {path}: {error}")
    groups: dict[str, list[float]] = {}
    for row in rows:
        try:
            value = float(row["seconds"])
            system = row["system"]
        except (KeyError, ValueError):
            fail(f"invalid timing row in {path}")
        if not math.isfinite(value) or value <= 0:
            fail(f"invalid latency in {path}")
        groups.setdefault(system, []).append(value)
    return {name: statistics.median(values) for name, values in groups.items()}


def shade(value: float) -> str:
    if value < 0.5:
        return "speedslow"
    if value < 0.9:
        return "speedbehind"
    if value <= 1.1:
        return "speedparity"
    return "speedahead"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--runs-root", type=Path, required=True)
    parser.add_argument("--baseline", default="onnxruntime")
    parser.add_argument("--candidate", default="joggle")
    parser.add_argument("--baseline-label", default="ONNX Runtime")
    parser.add_argument(
        "--matrix", choices=("contraction", "row"), default="contraction"
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--summary", type=Path, required=True)
    args = parser.parse_args()

    fixture_doc = document(args.fixtures)
    cases = fixture_doc.get("cases")
    if not isinstance(cases, list) or not cases:
        fail("fixture manifest has no cases")

    required_shape = ("M", "K", "N") if args.matrix == "contraction" else ("M", "N")
    selected = []
    for case in cases:
        shape = case.get("shape") if isinstance(case, dict) else None
        if not isinstance(shape, dict):
            continue
        keys = tuple(key for key in ("M", "K", "N") if key in shape)
        if keys == required_shape:
            selected.append(case)
    if not selected:
        fail(f"fixture manifest has no {args.matrix} cases")

    values: list[dict[str, Any]] = []
    revisions: set[str] = set()
    hosts: set[tuple[str, str]] = set()
    trials: set[int] = set()
    for case in selected:
        if not isinstance(case, dict) or not isinstance(case.get("case"), str):
            fail("fixture manifest contains an invalid case")
        name = case["case"]
        shape = case.get("shape")
        if not isinstance(shape, dict):
            fail(f"{name} has no shape")
        directory = args.runs_root / name
        record = document(directory / "record.json")
        if record.get("repo_dirty") is not False:
            fail(f"{name} was measured from a dirty repository")
        revision = record.get("repo_revision")
        host = record.get("host")
        if not isinstance(revision, str) or not isinstance(host, dict):
            fail(f"{name} has incomplete provenance")
        revisions.add(revision)
        hosts.add((str(host.get("node")), str(host.get("platform"))))
        if not isinstance(record.get("trials"), int):
            fail(f"{name} has no outer-trial count")
        trials.add(record["trials"])
        medians = median_times(directory / "runs.csv")
        if args.baseline not in medians or args.candidate not in medians:
            fail(f"{name} does not contain both requested systems")
        speedup = medians[args.baseline] / medians[args.candidate]
        values.append({
            "case": name,
            "operation": str(case.get("operation", name)),
            "M": int(shape["M"]),
            "K": int(shape["K"]) if "K" in shape else "",
            "N": int(shape["N"]),
            "baseline_seconds": medians[args.baseline],
            "candidate_seconds": medians[args.candidate],
            "speedup": speedup,
        })
    if len(revisions) != 1 or len(hosts) != 1 or len(trials) != 1:
        fail("all cells must share one clean revision, host, and trial count")

    groups: dict[str, list[dict[str, Any]]] = {}
    for row in values:
        groups.setdefault(row["operation"], []).append(row)
    coordinates = None
    for rows in groups.values():
        rows.sort(key=lambda row: (row["M"], row["K"] or -1, row["N"]))
        current = [(row["M"], row["K"], row["N"]) for row in rows]
        if len(current) != len(set(current)):
            fail("operator matrix contains duplicate coordinates")
        if coordinates is None:
            coordinates = current
        elif current != coordinates:
            fail("every operator row must contain the same shape coordinates")
    assert coordinates is not None
    ordered_groups = sorted(groups.items(), key=lambda item: item[0].lower())
    column_spec = "l" + "r" * (len(coordinates) + 1)
    headers = " & ".join(
        (rf"\rotatebox{{90}}{{${m}\!\times\!{k}\!\times\!{n}$}}" if k != ""
         else rf"\rotatebox{{90}}{{${m}\!\times\!{n}$}}")
        for m, k, n in coordinates
    )
    body = []
    for operation, rows in ordered_groups:
        cells = " & ".join(
            rf"\cellcolor{{{shade(row['speedup'])}}}{row['speedup']:.2f}"
            for row in rows
        )
        geomean = math.exp(
            statistics.fmean(math.log(row["speedup"]) for row in rows)
        )
        label = operation.replace("_", r"\_")
        body.append(
            rf"    {label} & {cells} & "
            rf"\cellcolor{{{shade(geomean)}}}{geomean:.2f} \\"
        )
    revision = next(iter(revisions))[:12]
    trial_count = next(iter(trials))
    matrix_label = "contraction" if args.matrix == "contraction" else "row-wise"
    latex = rf"""% Generated by paper/render_operator_table.py; do not edit.
\definecolor{{speedslow}}{{HTML}}{{F4D7D7}}
\definecolor{{speedbehind}}{{HTML}}{{F7E8C6}}
\definecolor{{speedparity}}{{HTML}}{{E2E8F0}}
\definecolor{{speedahead}}{{HTML}}{{C8E6D2}}
\begin{{table*}}[t]
  \centering
  \setlength{{\tabcolsep}}{{1.7pt}}
  \renewcommand{{\arraystretch}}{{1.04}}
  \resizebox{{\textwidth}}{{!}}{{%
  \begin{{tabular}}{{{column_spec}}}
    \toprule
    Benchmark & {headers} & Geomean \\
    \midrule
    \multicolumn{{{len(coordinates) + 2}}}{{l}}{{\textit{{vs {args.baseline_label}}}}} \\
{chr(10).join(body)}
    \bottomrule
  \end{{tabular}}}}
  \caption{{Matched single-thread speedup of {matrix_label} functions generated
  by Joggle over {args.baseline_label} (baseline median/Joggle median; higher is
  better). Every cell uses the same metric and validated output. Diagnostic host,
  {trial_count} outer trials per system with batched inner timing, revision
  \texttt{{{revision}}}. These development-host data are not submission results.}}
  \label{{tab:{args.matrix}-operator-sweep-diagnostic}}
\end{{table*}}
"""
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(latex, encoding="utf-8")
    args.summary.parent.mkdir(parents=True, exist_ok=True)
    with args.summary.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(values[0]))
        writer.writeheader()
        writer.writerows(values)


if __name__ == "__main__":
    main()
