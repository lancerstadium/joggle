#!/usr/bin/env python3
"""Plot Figure 6: cross-system incremental update cost."""

from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import LogNorm
from matplotlib.ticker import LogLocator

from common import COLORS, configure, number, read_rows, save, truth

SYSTEMS = ("Joggle", "MLIR", "xDSL")
PRODUCTION_STAGES = (
    "decode", "infer_convert", "c_prepare", "scalar_lowering",
    "storage_plan", "storage_place", "c_emit",
)
STAGE_LABELS = ("read", "convert", "C prep", "scalar", "plan", "place", "emit")


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
    matched = [row for row in rows if row["path"] == "matched"]
    production = [row for row in rows if row["path"] == "production"]
    latency = paired(matched, "wall_ns")
    work = paired(matched, "visited_ops")
    subjects = sorted({row["subject"] for row in matched})

    configure()
    fig = plt.figure(figsize=(7.0, max(2.55, 0.16 * len(subjects))), constrained_layout=True)
    grid = fig.add_gridspec(1, 3, width_ratios=(1.05, 1.05, 1.5))
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
        axis.set_xlim(1e-3, 1.2)
        axis.xaxis.set_major_locator(LogLocator(base=10, numticks=4))
        axis.set_xlabel(xlabel)
        axis.set_title(title, loc="left")
        axis.set_yticks(y, subjects if axis is fig.axes[0] else [])
        axis.invert_yaxis()
        axis.grid(axis="x", color="#E1E5EA", lw=0.5)

    axis = fig.add_subplot(grid[0, 2])
    samples: dict[tuple[str, str], list[float]] = defaultdict(list)
    for row in production:
        samples[(row["subject"], row["stage"])].append(number(row, "wall_ns") / 1e6)
    matrix = np.full((len(subjects), len(PRODUCTION_STAGES) + 1), np.nan)
    for row_index, subject in enumerate(subjects):
        for column, stage in enumerate(PRODUCTION_STAGES):
            values = samples.get((subject, stage), [])
            if values:
                matrix[row_index, column] = np.median(values)
        if np.all(np.isfinite(matrix[row_index, :-1])):
            matrix[row_index, -1] = np.sum(matrix[row_index, :-1])
    finite = matrix[np.isfinite(matrix)]
    lower = max(float(np.min(finite)), 1e-3) if finite.size else 1e-3
    upper = max(float(np.max(finite)), lower * 1.01) if finite.size else 1.0
    image = axis.imshow(
        matrix, aspect="auto", interpolation="none", cmap="Blues",
        norm=LogNorm(vmin=lower, vmax=upper),
    )
    labels = (*STAGE_LABELS, "total")
    axis.set_xticks(np.arange(len(labels)), labels, rotation=50, ha="right")
    axis.set_yticks(y, [])
    axis.set_title("(c) Full Joggle rebuild", loc="left")
    axis.set_xlabel("Stage (cell color: log ms)")
    for row_index in range(len(subjects)):
        for column in range(len(labels)):
            value = matrix[row_index, column]
            if np.isfinite(value):
                label = f"{value:.0f}" if value >= 10 else f"{value:.1f}"
                axis.text(column, row_index, label, ha="center", va="center",
                          fontsize=3.8, color="#172033")
    bar = fig.colorbar(image, ax=axis, fraction=0.045, pad=0.02)
    bar.set_label("ms", fontsize=5.8)

    save(fig, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
