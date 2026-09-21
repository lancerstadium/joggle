#!/usr/bin/env python3
"""Plot Figure 7 from one generated-graph reactive CSV."""

from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

from common import COLORS, configure, number, read_rows, save, truth


def median_groups(rows: list[dict[str, str]], value: str):
    grouped: dict[tuple[int, int, str], list[float]] = defaultdict(list)
    for row in rows:
        key = (int(row["total_ops"]), int(row["affected_ops"]), row["policy"])
        grouped[key].append(number(row, value))
    return {key: float(np.median(values)) for key, values in grouped.items()}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument("--output", type=Path, default=Path("figure-07-scaling.pdf"))
    args = parser.parse_args()
    rows = read_rows(args.csv, {"total_ops", "affected_ops", "policy", "wall_ns",
                                "observed_ops", "observed_values", "correct"})
    rows = [row for row in rows if truth(row["correct"])]
    latency = median_groups(rows, "wall_ns")
    observed_rows = [dict(row, observed=str(number(row, "observed_ops") +
                                           number(row, "observed_values")))
                     for row in rows]
    observed = median_groups(observed_rows, "observed")
    affected = sorted({int(row["affected_ops"]) for row in rows})

    configure()
    fig, (top, bottom) = plt.subplots(2, 1, figsize=(3.35, 3.8), sharex=True,
                                      gridspec_kw={"height_ratios": [2.2, 1]})
    for cone in affected:
        points = sorted((total, value / 1e6) for (total, a, policy), value in latency.items()
                        if a == cone and policy == "reactive")
        if points:
            top.plot(*zip(*points), marker="o", ms=3, lw=1, label=f"Reactive Δ={cone}")
        obs = sorted((total, value) for (total, a, policy), value in observed.items()
                     if a == cone and policy == "reactive")
        if obs:
            bottom.plot(*zip(*obs), marker="o", ms=3, lw=1, label=f"Δ={cone}")
    full = defaultdict(list)
    for (total, _affected, policy), value in latency.items():
        if policy == "full":
            full[total].append(value / 1e6)
    if full:
        points = sorted((total, float(np.median(values))) for total, values in full.items())
        top.plot(*zip(*points), color=COLORS["full"], marker="s", ms=3, lw=1.2,
                 label="Full")
    for axis in (top, bottom):
        axis.set_xscale("log")
        axis.set_yscale("log")
        axis.grid(color="#E7E9EC", lw=0.5)
    top.set_ylabel("Update latency (ms)")
    bottom.set_ylabel("Observed entities")
    bottom.set_xlabel("Graph operations")
    top.legend(frameon=False, ncol=2)
    fig.tight_layout()
    save(fig, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

