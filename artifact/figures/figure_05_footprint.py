#!/usr/bin/env python3
"""Plot Figure 5 from one change-footprint CSV."""

from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

from common import COLORS, configure, number, read_rows, save, truth


SYSTEM_MARKERS = {"Joggle": "o", "MLIR": "s", "xDSL": "^"}
FAMILY_ORDER = {
    "definition": 0, "analysis": 1, "rewrite": 2,
    "conversion": 3, "emission": 4, "vertical": 5,
}
TASK_LABELS = {
    "def-parametric-type": "Def · parametric",
    "def-quantized-op": "Def · quantized",
    "ana-broadcast-shape": "Ana · broadcast",
    "ana-numeric-range": "Ana · range",
    "rew-add-zero": "Rew · add-zero",
    "rew-redundant-cast": "Rew · cast",
    "con-gelu-expand": "Con · GELU",
    "con-quant-expand": "Con · quantized",
    "emit-graph-manifest": "Emit · manifest",
    "emit-kernel-wrapper": "Emit · wrapper",
    "vert-int4": "Vert · int4",
    "vert-fused-op": "Vert · fused",
}


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
    tasks = sorted(
        {(row["family"], row["task"]) for row in rows},
        key=lambda item: (FAMILY_ORDER.get(item[0], 99), item[1]),
    )
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
    fig, axes = plt.subplots(2, 2, figsize=(3.35, 5.0), sharey=True)
    axes = axes.flat
    y = np.arange(len(tasks))
    offsets = np.linspace(-0.12, 0.12, len(systems))
    boundaries = [index - 0.5 for index in range(1, len(tasks))
                  if tasks[index - 1][0] != tasks[index][0]]
    for axis, (metric, title) in zip(axes, metrics):
        for index, (_, task) in enumerate(tasks):
            values = []
            for system in systems:
                row = by_key[(task, system)]
                value = (number(row, "source_added") + number(row, "source_deleted")
                         if metric == "changed_lines" else number(row, metric))
                values.append((system, value))
            axis.hlines(index, min(value for _, value in values),
                        max(value for _, value in values), color="#C8CDD3",
                        lw=0.75, zorder=1)
            for system_index, (system, value) in enumerate(values):
                axis.scatter(value, index + offsets[system_index], s=12,
                             marker=SYSTEM_MARKERS.get(system, "o"),
                             color=COLORS.get(system, "#333333"), zorder=2,
                             label=system if index == 0 else None)
        axis.set_title(title)
        axis.set_xscale("symlog", linthresh=1, linscale=0.65)
        axis.set_xlim(left=-0.12)
        axis.set_xlabel("Count")
        axis.grid(axis="x", color="#E7E9EC", lw=0.45)
        for boundary in boundaries:
            axis.axhline(boundary, color="#D7DADE", lw=0.5, zorder=0)
    labels = [TASK_LABELS.get(task, task) for _, task in tasks]
    axes[0].set_yticks(y, labels)
    axes[2].set_yticks(y, labels)
    axes[1].tick_params(labelleft=False)
    axes[3].tick_params(labelleft=False)
    axes[0].invert_yaxis()
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, ncol=len(labels), loc="upper center", frameon=False)
    fig.tight_layout(rect=(0, 0, 1, 0.955), w_pad=0.45, h_pad=0.65)
    save(fig, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
