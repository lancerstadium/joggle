#!/usr/bin/env python3
"""Plot Figure 4 from one extension-generation CSV."""

from __future__ import annotations

import argparse
import math
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

from common import COLORS, configure, number, read_rows, save, truth


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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument("--output", type=Path, default=Path("figure-04-extension.pdf"))
    args = parser.parse_args()
    rows = read_rows(args.csv, {"record_kind", "model", "system", "task", "family",
                                "demo_count", "sample_index", "nll", "target_tokens",
                                "parsed", "typed", "built", "passed"})
    samples = [row for row in rows if row["record_kind"] == "sample"]
    references = [row for row in rows if row["record_kind"] == "reference"]
    if not samples:
        raise SystemExit("CSV has no sample rows")
    models = sorted({row["model"] for row in samples})
    systems = sorted({row["system"] for row in samples})
    families = sorted({row["family"] for row in samples})
    family_of = {row["task"]: row["family"] for row in samples}
    scores = {k: task_scores(samples, k) for k in (1, 5, 10)}

    configure()
    fig, axes = plt.subplots(len(models), 3, figsize=(7.0, 2.25 * len(models)),
                             squeeze=False)
    for row_index, model in enumerate(models):
        left, middle, right = axes[row_index]
        for system in systems:
            values = []
            for family in families:
                task_values = [value for (m, s, task, demos), value in scores[1].items()
                               if m == model and s == system and demos == 4
                               and family_of[task] == family]
                values.append(float(np.mean(task_values)) if task_values else np.nan)
            left.plot(range(len(families)), values, marker="o", ms=3, lw=1,
                      color=COLORS.get(system), label=system)
        left.set_xticks(range(len(families)), families, rotation=35, ha="right")
        left.set_ylim(0, 1.02)
        left.set_ylabel("Task-macro pass@1")
        left.set_title(f"{model}: four demonstrations")

        for system in systems:
            values = []
            for k in (1, 5, 10):
                task_values = [value for (m, s, _task, demos), value in scores[k].items()
                               if m == model and s == system and demos == 4]
                values.append(float(np.mean(task_values)))
            middle.plot((1, 5, 10), values, marker="o", ms=3, lw=1,
                        color=COLORS.get(system), label=system)
        middle.set_xticks((1, 5, 10))
        middle.set_ylim(0, 1.02)
        middle.set_xlabel("k")
        middle.set_ylabel("Task-macro pass@k")
        middle.set_title("Sampling budget")

        stages = ["parse", "type", "build", "oracle", "pass"]
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
                    elif not truth(entry["passed"]): counts["oracle"] += 1
                    else: counts["pass"] += 1
                values.append(counts[stage] / len(subset))
            right.bar(systems, values, bottom=bottoms, width=0.68, label=stage)
            bottoms += np.asarray(values)
        right.set_ylim(0, 1)
        right.set_ylabel("Sample fraction")
        right.set_title("Outcome composition")

        if references:
            inset = middle.inset_axes([0.57, 0.08, 0.4, 0.34])
            for system in systems:
                points = []
                for demos in (0, 1, 2, 4):
                    values = [number(entry, "nll") / number(entry, "target_tokens")
                              for entry in references if entry["model"] == model
                              and entry["system"] == system
                              and int(entry["demo_count"]) == demos]
                    if values:
                        points.append((demos, float(np.mean(values))))
                if points:
                    inset.plot(*zip(*points), lw=0.8, color=COLORS.get(system))
            inset.set_title("mean token NLL", fontsize=6)
            inset.tick_params(labelsize=5)

    for axis in axes.flat:
        axis.grid(axis="y", color="#E7E9EC", lw=0.5)
    handles, labels = axes[0, 0].get_legend_handles_labels()
    outcome_handles, outcome_labels = axes[0, 2].get_legend_handles_labels()
    fig.legend(handles + outcome_handles, labels + outcome_labels, ncol=4,
               loc="upper center", frameon=False)
    fig.tight_layout(rect=(0, 0, 1, 0.93))
    save(fig, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

