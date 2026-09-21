#!/usr/bin/env python3
"""Plot Figure 6: cross-system incremental update cost."""

from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

from common import COLORS, configure, number, read_rows, save, truth

SYSTEMS = ("Joggle", "MLIR", "xDSL")


def paired(rows: list[dict[str, str]], metric: str) -> dict[tuple[str, str], list[float]]:
    groups: dict[tuple[str, ...], dict[str, dict[str, str]]] = defaultdict(dict)
    for row in rows:
        key = (row["system"], row["subject"], row["edit_class"], row["edit_scope"],
               row["edit_site"], row["iteration"], row["seed"])
        groups[key][row["policy"]] = row
    values: dict[tuple[str, str], list[float]] = defaultdict(list)
    for key, policies in groups.items():
        if "full" not in policies:
            continue
        candidates = [name for name in policies if name != "full"]
        for policy in candidates:
            denominator = number(policies["full"], metric)
            if denominator:
                values[(key[0], key[1])].append(number(policies[policy], metric) / denominator)
    return values


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument("--output", type=Path, default=Path("figure-06-update.pdf"))
    args = parser.parse_args()
    rows = read_rows(args.csv, {
        "system", "subject", "edit_class", "edit_scope", "edit_site",
        "policy", "iteration", "wall_ns", "visited_ops", "total_ops",
        "output_digest", "correct", "seed",
    })
    rows = [row for row in rows if truth(row["correct"])]
    latency = paired(rows, "wall_ns")
    work = paired(rows, "visited_ops")
    subjects = sorted({row["subject"] for row in rows})

    configure()
    fig = plt.figure(figsize=(7.0, max(2.55, 0.16 * len(subjects))), constrained_layout=True)
    grid = fig.add_gridspec(1, 3, width_ratios=(1.18, 1.18, 1.05))
    y = np.arange(len(subjects))
    offsets = dict(zip(SYSTEMS, (-0.18, 0.0, 0.18)))

    for axis, values, title, xlabel in (
        (fig.add_subplot(grid[0, 0]), latency, "(a) Update time", "Update / Full"),
        (fig.add_subplot(grid[0, 1]), work, "(b) Revisited work", "Visited / Full"),
    ):
        for system in SYSTEMS:
            centers, lows, highs, ys = [], [], [], []
            for index, subject in enumerate(subjects):
                sample = values.get((system, subject), [])
                if sample:
                    low, center, high = np.quantile(sample, (0.25, 0.5, 0.75))
                    lows.append(center - low); centers.append(center); highs.append(high - center)
                    ys.append(index + offsets[system])
            axis.errorbar(centers, ys, xerr=(lows, highs), fmt="o", ms=3.2,
                          capsize=1.4, lw=0.7, color=COLORS[system], label=system)
        axis.axvline(1, color="#737B87", ls="--", lw=0.7)
        axis.set_xscale("log")
        axis.set_xlabel(xlabel)
        axis.set_title(title, loc="left")
        axis.set_yticks(y, subjects if axis is fig.axes[0] else [])
        axis.invert_yaxis()
        axis.grid(axis="x", color="#E1E5EA", lw=0.5)

    axis = fig.add_subplot(grid[0, 2])
    absolute: dict[str, list[float]] = defaultdict(list)
    for row in rows:
        if row["policy"] != "full":
            absolute[row["system"]].append(number(row, "wall_ns") / 1e6)
    for system in SYSTEMS:
        values = np.sort(absolute[system])
        if len(values):
            axis.step(values, np.arange(1, len(values) + 1) / len(values),
                      where="post", color=COLORS[system], label=system)
    axis.set_xscale("log")
    axis.set_xlabel("Update latency (ms)")
    axis.set_ylabel("ECDF")
    axis.set_ylim(0, 1.02)
    axis.set_title("(c) Absolute latency", loc="left")
    axis.grid(color="#E1E5EA", lw=0.5)
    axis.legend(frameon=False, fontsize=5.8, loc="lower right")

    save(fig, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
