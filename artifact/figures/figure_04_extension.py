#!/usr/bin/env python3
"""Plot Figure 4 from complete coding-agent trajectories."""

from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

from common import COLORS, configure, number, read_rows, save, truth

FAMILY_ORDER = ("definition", "analysis", "rewrite", "conversion", "emission", "vertical")
FAMILY_LABELS = ("Def", "Analyze", "Rewrite", "Convert", "Emit", "Vertical")
SYSTEMS = ("Joggle", "MLIR", "xDSL")
MARKERS = {"Joggle": "o", "MLIR": "s", "xDSL": "^"}


def interval(values: list[float], seed: int) -> tuple[float, float, float]:
    sample = np.asarray(values, dtype=float)
    center = float(np.mean(sample))
    if len(sample) == 1:
        return center, center, center
    random = np.random.default_rng(seed)
    means = np.mean(random.choice(sample, (10_000, len(sample)), replace=True), axis=1)
    low, high = np.quantile(means, (0.025, 0.975))
    return center, float(low), float(high)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument("--output", type=Path, default=Path("figure-04-extension.pdf"))
    args = parser.parse_args()
    rows = read_rows(args.csv, {
        "model", "system", "task", "family", "demo_count", "run", "passed",
        "completion_tokens", "tool_calls", "wall_ms",
    })
    models = sorted({row["model"] for row in rows})
    family_of = {row["task"]: row["family"] for row in rows}
    tasks = sorted(family_of)

    configure()
    fig, axes = plt.subplots(len(models), 3, figsize=(7.0, 1.8 * len(models)), squeeze=False)
    for model_index, model in enumerate(models):
        family_axis, demo_axis, effort_axis = axes[model_index]
        for system_index, system in enumerate(SYSTEMS):
            centers, lows, highs = [], [], []
            for family_index, family in enumerate(FAMILY_ORDER):
                task_scores = []
                for task in tasks:
                    if family_of[task] != family:
                        continue
                    sample = [truth(row["passed"]) for row in rows
                              if row["model"] == model and row["system"] == system
                              and row["task"] == task and int(row["demo_count"]) == 2]
                    task_scores.append(float(np.mean(sample)))
                center, low, high = interval(task_scores, 100 + family_index)
                centers.append(center); lows.append(center - low); highs.append(high - center)
            x = np.arange(len(FAMILY_ORDER)) + (system_index - 1) * 0.17
            family_axis.errorbar(
                x, centers, yerr=(lows, highs), fmt=MARKERS[system], ms=3.2,
                capsize=1.4, lw=0.7, color=COLORS[system], label=system,
            )

            demo_centers, demo_lows, demo_highs = [], [], []
            for demos in (0, 2):
                task_scores = []
                for task in tasks:
                    sample = [truth(row["passed"]) for row in rows
                              if row["model"] == model and row["system"] == system
                              and row["task"] == task and int(row["demo_count"]) == demos]
                    task_scores.append(float(np.mean(sample)))
                center, low, high = interval(task_scores, 200 + demos)
                demo_centers.append(center); demo_lows.append(center - low)
                demo_highs.append(high - center)
            demo_axis.errorbar(
                (0, 2), demo_centers, yerr=(demo_lows, demo_highs),
                marker=MARKERS[system], ms=3.2, capsize=1.4, lw=0.8,
                color=COLORS[system], label=system,
            )

            by_task: dict[str, list[dict[str, str]]] = defaultdict(list)
            for row in rows:
                if (row["model"] == model and row["system"] == system
                        and int(row["demo_count"]) == 2 and truth(row["passed"])):
                    by_task[row["task"]].append(row)
            effort_axis.scatter(
                [np.median([number(row, "completion_tokens") for row in sample])
                 for sample in by_task.values()],
                [np.median([number(row, "tool_calls") for row in sample])
                 for sample in by_task.values()],
                s=13, alpha=0.76, marker=MARKERS[system], color=COLORS[system],
                label=system,
            )

        family_axis.set_xticks(range(len(FAMILY_ORDER)), FAMILY_LABELS, rotation=28, ha="right")
        family_axis.set_ylim(0, 1.02)
        family_axis.set_ylabel("Agent success")
        family_axis.text(0.02, 0.96, model, transform=family_axis.transAxes,
                         va="top", fontweight="bold", fontsize=6.5)
        demo_axis.set_xticks((0, 2))
        demo_axis.set_ylim(0, 1.02)
        demo_axis.set_xlabel("Demonstrations")
        demo_axis.set_ylabel("Task-macro success")
        effort_axis.set_xscale("log")
        effort_axis.set_xlabel("Completion tokens")
        effort_axis.set_ylabel("Tool calls")
        if model_index == 0:
            family_axis.set_title("(a) Success by extension family", loc="left")
            demo_axis.set_title("(b) Demonstration response", loc="left")
            effort_axis.set_title("(c) Successful-agent effort", loc="left")

    for axis in axes.flat:
        axis.grid(color="#E1E5EA", lw=0.5)
    handles, labels = axes[0, 0].get_legend_handles_labels()
    fig.legend(handles, labels, ncol=3, frameon=False, loc="upper center")
    fig.tight_layout(rect=(0.01, 0, 1, 0.92), w_pad=0.8, h_pad=0.7)
    save(fig, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
