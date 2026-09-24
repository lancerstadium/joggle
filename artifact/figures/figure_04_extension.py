#!/usr/bin/env python3
"""Plot coding-agent extension results as dense grouped bars."""

from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

from common import COLORS, configure, number, read_rows, save, truth

FAMILIES = ("definition", "analysis", "rewrite", "conversion", "emission", "vertical")
FAMILY_LABELS = ("Def", "Ana", "Rew", "Conv", "Emit", "Vert")
SYSTEMS = ("Joggle", "MLIR", "xDSL")


def task_macro(rows: list[dict[str, str]], field: str, successful_only: bool) -> list[float]:
    by_task: dict[str, list[float]] = defaultdict(list)
    for row in rows:
        if successful_only and not truth(row["passed"]):
            continue
        value = float(truth(row[field])) if field == "passed" else number(row, field)
        by_task[row["task"]].append(value)
    return [float(np.mean(values)) for values in by_task.values()]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument("--output", type=Path, default=Path("figure-04-extension.pdf"))
    args = parser.parse_args()
    rows = read_rows(args.csv, {"model", "system", "task", "family", "run",
                                "passed", "completion_tokens", "tool_calls", "wall_ms"})
    models = sorted({row["model"] for row in rows})
    if len(models) != 2:
        raise ValueError(f"expected two frozen models, found {models}")

    configure()
    fig, axes = plt.subplots(2, 3, figsize=(3.35, 2.34), squeeze=False)
    x = np.arange(len(FAMILIES))
    width = 0.24
    metrics = (("passed", "Success", False),
               ("completion_tokens", "Tokens", True),
               ("tool_calls", "Tool calls", True))
    for row_index, model in enumerate(models):
        for column, (field, label, successful_only) in enumerate(metrics):
            axis = axes[row_index, column]
            for system_index, system in enumerate(SYSTEMS):
                values = []
                for family in FAMILIES:
                    selected = [row for row in rows if row["model"] == model
                                and row["system"] == system and row["family"] == family]
                    sample = task_macro(selected, field, successful_only)
                    values.append(float(np.mean(sample)) if sample else np.nan)
                axis.bar(x + (system_index - 1) * width, values, width=width,
                         color=COLORS[system], edgecolor="#26333D", linewidth=0.25,
                         label=system, zorder=2)
            axis.set_xticks(x, FAMILY_LABELS, rotation=45, ha="right")
            axis.tick_params(axis="both", labelsize=3.6, pad=0.7, length=1.8)
            axis.grid(axis="y", color="#DDE2E8", linewidth=0.35, zorder=0)
            axis.set_title(f"({chr(97 + row_index * 3 + column)}) {label}",
                           loc="left", fontsize=5.4, pad=1.0)
            if field == "passed":
                axis.set_ylim(0, 1.05)
            else:
                axis.set_yscale("log")
            if column == 0:
                axis.set_ylabel(model, fontsize=4.6, labelpad=1.2)
    handles, labels = axes[0, 0].get_legend_handles_labels()
    fig.legend(handles, labels, ncol=3, frameon=False, loc="outside upper center",
               fontsize=4.5, handlelength=1.0, columnspacing=0.75)
    save(fig, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
