#!/usr/bin/env python3
"""Plot matched feature-patch footprints as dense grouped bars."""

from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

from common import COLORS, configure, number, read_rows, save, truth

SYSTEMS = ("Joggle", "MLIR", "xDSL")
FAMILY_ORDER = {"definition": 0, "analysis": 1, "rewrite": 2,
                "conversion": 3, "emission": 4, "vertical": 5}
TASK_LABELS = {
    "def-parametric-type": "D-type", "def-quantized-op": "D-qop",
    "ana-broadcast-shape": "A-bcast", "ana-numeric-range": "A-range",
    "rew-add-zero": "R-zero", "rew-redundant-cast": "R-cast",
    "con-gelu-expand": "C-gelu", "con-quant-expand": "C-quant",
    "emit-graph-manifest": "E-json", "emit-kernel-wrapper": "E-wrap",
    "vert-int4": "V-int4", "vert-fused-op": "V-fused",
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument("--output", type=Path, default=Path("figure-05-footprint.pdf"))
    args = parser.parse_args()
    rows = read_rows(args.csv, {"system", "task", "family", "source_files",
                                "source_added", "source_deleted", "zones",
                                "registrations", "oracle_passed"})
    rows = [row for row in rows if truth(row["oracle_passed"])]
    tasks = sorted({(row["family"], row["task"]) for row in rows},
                   key=lambda pair: (FAMILY_ORDER.get(pair[0], 99), pair[1]))
    by_key = {(row["task"], row["system"]): row for row in rows}
    missing = [(task, system) for _, task in tasks for system in SYSTEMS
               if (task, system) not in by_key]
    if missing:
        raise SystemExit(f"incomplete task/system pairing: {missing[:4]}")

    configure()
    fig, axes = plt.subplots(2, 2, figsize=(3.35, 2.34))
    axes = axes.flat
    metrics = (("source_files", "(a) Files"), ("changed_lines", "(b) Changed lines"),
               ("zones", "(c) Ownership zones"), ("registrations", "(d) Registry/build"))
    x = np.arange(len(tasks))
    width = 0.24
    for axis, (metric, title) in zip(axes, metrics):
        maximum = 1.0
        for system_index, system in enumerate(SYSTEMS):
            values = []
            for _, task in tasks:
                row = by_key[(task, system)]
                value = (number(row, "source_added") + number(row, "source_deleted")
                         if metric == "changed_lines" else number(row, metric))
                values.append(value)
            maximum = max(maximum, max(values, default=0.0))
            axis.bar(x + (system_index - 1) * width, values, width=width,
                     color=COLORS[system], edgecolor="#26333D", linewidth=0.25,
                     label=system, zorder=2)
        axis.set_title(title, loc="left", fontsize=5.6, pad=1.0)
        axis.set_yscale("symlog", linthresh=1, linscale=0.55)
        axis.set_ylim(0, maximum * 1.45)
        axis.set_xticks(x, [TASK_LABELS.get(task, task) for _, task in tasks],
                        rotation=55, ha="right")
        axis.tick_params(axis="both", labelsize=3.5, pad=0.7, length=1.8)
        axis.grid(axis="y", color="#DDE2E8", linewidth=0.35, zorder=0)
        for boundary in range(2, len(tasks), 2):
            axis.axvline(boundary - 0.5, color="#C9CFD6", linewidth=0.35, zorder=1)
    axes[0].set_ylabel("count", fontsize=4.7, labelpad=1)
    axes[2].set_ylabel("count", fontsize=4.7, labelpad=1)
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, ncol=3, frameon=False, loc="outside upper center",
               fontsize=4.5, handlelength=1.0, columnspacing=0.75)
    save(fig, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
