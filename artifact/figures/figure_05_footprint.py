#!/usr/bin/env python3
"""Plot Figure 5 from one change-footprint CSV."""

from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

from common import COLORS, configure, number, read_rows, save, truth


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument("--output", type=Path, default=Path("figure-05-footprint.pdf"))
    args = parser.parse_args()
    rows = read_rows(
        args.csv,
        {"system", "task", "family", "source_files", "source_added",
         "source_deleted", "zones", "registrations", "oracle_passed"},
    )
    rows = [row for row in rows if truth(row["oracle_passed"])]
    tasks = sorted({(row["family"], row["task"]) for row in rows})
    systems = sorted({row["system"] for row in rows})
    by_key = {(row["task"], row["system"]): row for row in rows}
    missing = [(task, system) for _, task in tasks for system in systems
               if (task, system) not in by_key]
    if missing:
        raise SystemExit(f"incomplete task/system pairing: {missing[:4]}")

    configure()
    metrics = [
        ("source_files", "Files"),
        ("changed_lines", "Lines"),
        ("zones", "Zones"),
        ("registrations", "Registry/build"),
    ]
    fig, axes = plt.subplots(1, 4, figsize=(7.0, 4.25), sharey=True)
    y = np.arange(len(tasks))
    for axis, (metric, title) in zip(axes, metrics):
        for index, (_, task) in enumerate(tasks):
            values = []
            for system in systems:
                row = by_key[(task, system)]
                value = (number(row, "source_added") + number(row, "source_deleted")
                         if metric == "changed_lines" else number(row, metric))
                values.append((system, value))
            axis.plot([np.log1p(value) for _, value in values],
                      [index] * len(values), color="#C8CDD3", lw=0.8, zorder=1)
            for system, value in values:
                axis.scatter(np.log1p(value), index, s=15,
                             color=COLORS.get(system, "#333333"), zorder=2,
                             label=system if index == 0 else None)
        axis.set_title(title)
        axis.set_xlabel("log(1 + count)")
        axis.grid(axis="x", color="#E7E9EC", lw=0.5)
    axes[0].set_yticks(y, [task for _, task in tasks])
    axes[0].invert_yaxis()
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, ncol=len(labels), loc="upper center", frameon=False)
    fig.tight_layout(rect=(0, 0, 1, 0.96))
    save(fig, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

