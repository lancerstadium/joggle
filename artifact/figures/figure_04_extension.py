#!/usr/bin/env python3
"""Plot Figure 4 from one extension-generation CSV."""

from __future__ import annotations

import argparse
import math
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import Patch

from common import COLORS, configure, read_rows, save, truth


FAMILY_ORDER = ("definition", "analysis", "rewrite", "conversion", "emission", "vertical")
FAMILY_LABELS = ("Def", "Analyze", "Rewrite", "Convert", "Emit", "Vertical")
SYSTEM_MARKERS = {"Joggle": "o", "MLIR": "s", "xDSL": "^"}
OUTCOME_COLORS = {
    "parse": "#D9DEE7",
    "type": "#F2C14E",
    "build": "#E07A2D",
    "semantic": "#C44E52",
    "pass": "#087E8B",
}


def pass_at_k(n: int, successes: int, k: int) -> float:
    if n < k:
        raise ValueError(f"pass@{k} requires at least {k} samples, found {n}")
    if n - successes < k:
        return 1.0
    return 1.0 - math.comb(n - successes, k) / math.comb(n, k)


def task_scores(rows: list[dict[str, str]], k: int):
    grouped: dict[tuple[str, str, str, int], list[bool]] = defaultdict(list)
    for row in rows:
        key = (row["model"], row["system"], row["task"], int(row["demo_count"]))
        grouped[key].append(truth(row["passed"]))
    return {key: pass_at_k(len(values), sum(values), k) for key, values in grouped.items()}


def stratified_interval(
    values: dict[str, list[float]], seed: int = 0,
) -> tuple[float, float, float]:
    strata = [np.asarray(values[family], dtype=float) for family in FAMILY_ORDER
              if values.get(family)]
    if not strata:
        raise ValueError("cannot summarize an empty task population")
    center = float(np.mean([np.mean(sample) for sample in strata]))
    if sum(len(sample) for sample in strata) == 1:
        return center, center, center
    random = np.random.default_rng(seed)
    means = np.mean([
        np.mean(
            random.choice(sample, (10_000, len(sample)), replace=True), axis=1
        )
        for sample in strata
    ], axis=0)
    low, high = np.quantile(means, (0.025, 0.975))
    return center, float(low), float(high)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument("--output", type=Path, default=Path("figure-04-extension.pdf"))
    args = parser.parse_args()
    rows = read_rows(args.csv, {"record_kind", "model", "system", "task", "family",
                                "demo_count", "sample_index", "nll", "target_tokens",
                                "parsed", "typed", "built", "passed"})
    samples = [row for row in rows if row["record_kind"] == "sample"]
    if not samples:
        raise SystemExit("CSV has no sample rows")
    models = sorted({row["model"] for row in samples})
    systems = sorted({row["system"] for row in samples})
    observed_families = {row["family"] for row in samples}
    families = [family for family in FAMILY_ORDER if family in observed_families]
    family_labels = [FAMILY_LABELS[FAMILY_ORDER.index(family)] for family in families]
    family_of = {row["task"]: row["family"] for row in samples}
    scores = {k: task_scores(samples, k) for k in (1, 5, 10)}

    configure()
    fig, axes = plt.subplots(len(models), 3, figsize=(7.0, 1.85 * len(models)),
                             squeeze=False)
    for row_index, model in enumerate(models):
        left, middle, right = axes[row_index]
        for system_index, system in enumerate(systems):
            centers, lower, upper = [], [], []
            for family_index, family in enumerate(families):
                task_values = [value for (m, s, task, demos), value in scores[1].items()
                               if m == model and s == system and demos == 4
                               and family_of[task] == family]
                center, low, high = stratified_interval(
                    {family: task_values}, seed=family_index
                )
                centers.append(center)
                lower.append(center - low)
                upper.append(high - center)
            x = np.arange(len(families)) + (system_index - (len(systems) - 1) / 2) * 0.16
            left.errorbar(x, centers, yerr=(lower, upper),
                          marker=SYSTEM_MARKERS.get(system, "o"), ms=3.2, lw=0,
                          elinewidth=0.8, capsize=1.5,
                          color=COLORS.get(system), label=system)
        left.set_xticks(range(len(families)), family_labels, rotation=28, ha="right")
        left.set_ylim(0, 1.02)
        left.set_ylabel("Task-macro pass@1")
        left.text(0.02, 0.96, model, transform=left.transAxes,
                  ha="left", va="top", fontweight="bold", fontsize=6.5)

        for system in systems:
            centers, lower, upper = [], [], []
            for demos in (0, 1, 2, 4):
                task_values: dict[str, list[float]] = defaultdict(list)
                for (m, s, task, count), value in scores[1].items():
                    if m == model and s == system and count == demos:
                        task_values[family_of[task]].append(value)
                center, low, high = stratified_interval(
                    task_values, seed=10 + demos
                )
                centers.append(center)
                lower.append(center - low)
                upper.append(high - center)
            middle.errorbar((0, 1, 2, 4), centers, yerr=(lower, upper),
                            marker=SYSTEM_MARKERS.get(system, "o"),
                            ms=3, lw=1, capsize=1.5, color=COLORS.get(system),
                            label=system)
        middle.set_xticks((0, 1, 2, 4))
        middle.set_ylim(0, 1.02)
        middle.set_xlabel("Demonstrations")
        middle.set_ylabel("Task-macro pass@1")
        stages = ["parse", "type", "build", "semantic", "pass"]
        bottoms = np.zeros(len(systems))
        for stage in stages:
            values = []
            for system in systems:
                subset = [entry for entry in samples if entry["model"] == model
                          and entry["system"] == system and int(entry["demo_count"]) == 4]
                counts = dict.fromkeys(stages, 0)
                for entry in subset:
                    if not truth(entry["parsed"]): counts["parse"] += 1
                    elif not truth(entry["typed"]): counts["type"] += 1
                    elif not truth(entry["built"]): counts["build"] += 1
                    elif not truth(entry["passed"]): counts["semantic"] += 1
                    else: counts["pass"] += 1
                values.append(counts[stage] / len(subset))
            right.bar(systems, values, bottom=bottoms, width=0.66,
                      color=OUTCOME_COLORS[stage], label=stage)
            bottoms += np.asarray(values)
        right.set_ylim(0, 1)
        right.set_ylabel("Sample fraction")

        if row_index == 0:
            left.set_title("(a) Completion by family")
            middle.set_title("(b) Demonstration response")
            right.set_title("(c) Outcome composition")

    for axis in axes.flat:
        axis.grid(axis="y", color="#E7E9EC", lw=0.5)
    system_handles, system_labels = axes[0, 0].get_legend_handles_labels()
    fig.legend(system_handles, system_labels, ncol=len(systems), loc="upper left",
               bbox_to_anchor=(0.15, 1.01), frameon=False)
    outcome_handles = [Patch(facecolor=OUTCOME_COLORS[stage], label=stage)
                       for stage in stages]
    fig.legend(outcome_handles, stages, ncol=len(stages), loc="upper right",
               bbox_to_anchor=(0.99, 1.01), frameon=False)
    fig.tight_layout(rect=(0.02, 0, 1, 0.91), w_pad=1.0, h_pad=0.8)
    save(fig, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
