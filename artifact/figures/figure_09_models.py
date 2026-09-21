#!/usr/bin/env python3
"""Plot Figure 9 from one model-artifact CSV."""

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
    parser.add_argument("--output", type=Path, default=Path("figure-09-models.pdf"))
    parser.add_argument("--reference", default="reference")
    args = parser.parse_args()
    rows = read_rows(args.csv, {"model", "system", "variant", "supported", "latency_ns",
                                "peak_bytes", "artifact_bytes", "correct"})
    supported = [row for row in rows if truth(row["supported"]) and truth(row["correct"])]
    grouped: dict[tuple[str, str], list[dict[str, str]]] = defaultdict(list)
    for row in supported:
        grouped[(row["model"], row["variant"])].append(row)
    models = sorted({row["model"] for row in rows})
    variants = sorted({row["variant"] for row in supported})
    reference = {}
    for model in models:
        sample = grouped.get((model, args.reference), [])
        if sample:
            reference[model] = float(np.median([number(row, "latency_ns") for row in sample]))

    configure()
    fig, axes = plt.subplots(1, 3, figsize=(7.0, max(3.0, 0.22 * len(models))), sharey=True)
    specs = [("latency_ratio", "Latency / reference"),
             ("peak_bytes", "Peak memory (bytes)"),
             ("artifact_bytes", "Artifact bytes")]
    y = np.arange(len(models))
    offsets = np.linspace(-0.18, 0.18, max(1, len(variants)))
    for axis, (metric, title) in zip(axes, specs):
        for offset, variant in zip(offsets, variants):
            xs, ys = [], []
            for index, model in enumerate(models):
                sample = grouped.get((model, variant), [])
                if not sample:
                    continue
                field = "latency_ns" if metric == "latency_ratio" else metric
                value = float(np.median([number(row, field) for row in sample]))
                if metric == "latency_ratio":
                    if model not in reference:
                        continue
                    value /= reference[model]
                xs.append(value)
                ys.append(index + offset)
            axis.scatter(xs, ys, s=15, label=variant,
                         color=COLORS.get(variant, None))
        if metric != "latency_ratio":
            axis.set_xscale("log")
        else:
            axis.axvline(1.0, color="#9AA0A6", lw=0.7)
        axis.set_title(title)
        axis.grid(axis="x", color="#E7E9EC", lw=0.5)
    axes[0].set_yticks(y, models)
    axes[0].invert_yaxis()
    unsupported = {row["model"] for row in rows if not truth(row["supported"])}
    for index, model in enumerate(models):
        if model in unsupported and not any(key[0] == model for key in grouped):
            axes[0].text(0.02, index, "unsupported", va="center", fontsize=6, color="#8A8F98")
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, ncol=max(1, len(labels)), loc="upper center", frameon=False)
    fig.tight_layout(rect=(0, 0, 1, 0.95))
    save(fig, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
