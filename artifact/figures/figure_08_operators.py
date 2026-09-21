#!/usr/bin/env python3
"""Plot Figure 8 from one operator-artifact CSV."""

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
    parser.add_argument("--output", type=Path, default=Path("figure-08-operators.pdf"))
    args = parser.parse_args()
    rows = read_rows(args.csv, {"operator", "family", "shape", "dtype", "system",
                                "variant", "latency_ns", "prepare_ns", "code_bytes",
                                "correct"})
    rows = [row for row in rows if truth(row["correct"])]
    grouped: dict[tuple[str, str], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        subject = f"{row['operator']}\n{row['shape']} {row['dtype']}"
        grouped[(subject, row["variant"])].append(row)
    subjects = sorted({key[0] for key in grouped})
    variants = sorted({key[1] for key in grouped})

    configure()
    fig, axes = plt.subplots(1, 3, figsize=(7.0, max(2.4, 0.24 * len(subjects))))
    specs = [("latency_ns", "Latency (ns)"), ("prepare_ns", "Preparation (ns)"),
             ("code_bytes", "Code bytes")]
    y = np.arange(len(subjects))
    offsets = np.linspace(-0.18, 0.18, len(variants))
    for axis, (metric, title) in zip(axes, specs):
        for offset, variant in zip(offsets, variants):
            xs, ys = [], []
            for index, subject in enumerate(subjects):
                sample = grouped.get((subject, variant), [])
                if sample:
                    xs.append(float(np.median([number(row, metric) for row in sample])))
                    ys.append(index + offset)
            axis.scatter(xs, ys, s=15, label=variant,
                         color=COLORS.get(variant, None))
        axis.set_xscale("log")
        axis.set_title(title)
        axis.grid(axis="x", color="#E7E9EC", lw=0.5)
    axes[0].set_yticks(y, subjects)
    axes[0].invert_yaxis()
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, ncol=max(1, len(labels)), loc="upper center", frameon=False)
    fig.tight_layout(rect=(0, 0, 1, 0.95))
    save(fig, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

