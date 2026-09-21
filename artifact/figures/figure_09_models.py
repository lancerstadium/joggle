#!/usr/bin/env python3
"""Plot Figure 9 from one model-artifact CSV."""

from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.ticker import LogLocator, NullFormatter

from common import COLORS, configure, median_p95, number, read_rows, save, truth


VARIANT_ORDER = ("joggle-unoptimized", "joggle-optimized", "onnxruntime")
VARIANT_LABELS = {
    "joggle-unoptimized": "Joggle base",
    "joggle-optimized": "Joggle opt",
    "onnxruntime": "ONNX Runtime",
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument("--output", type=Path, default=Path("figure-09-models.pdf"))
    parser.add_argument("--reference", default="onnxruntime")
    args = parser.parse_args()
    rows = read_rows(args.csv, {"model", "system", "variant", "record_kind",
                                "supported", "latency_ns", "peak_bytes",
                                "artifact_bytes", "correct"})
    grouped: dict[tuple[str, str, str], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        if truth(row["supported"]):
            if row["record_kind"] != "execute" or truth(row["correct"]):
                grouped[(row["model"], row["variant"], row["record_kind"])].append(row)
    models = sorted({row["model"] for row in rows})
    observed_variants = {row["variant"] for row in rows}
    variants = [variant for variant in VARIANT_ORDER if variant in observed_variants]
    reference: dict[str, tuple[float, float]] = {}
    for model in models:
        sample = grouped.get((model, args.reference, "execute"), [])
        if sample:
            reference[model] = median_p95(number(row, "latency_ns") for row in sample)

    configure()
    fig, axes = plt.subplots(1, 3, figsize=(7.0, max(3.0, 0.22 * len(models))), sharey=True)
    specs = [("latency_ratio", "Latency / reference"),
             ("peak_bytes", "Peak memory (bytes)"),
             ("artifact_bytes", "Artifact bytes")]
    y = np.arange(len(models))
    offsets = np.linspace(-0.18, 0.18, max(1, len(variants)))
    for axis, (metric, title) in zip(axes, specs):
        for offset, variant in zip(offsets, variants):
            xs, tails, ys = [], [], []
            for index, model in enumerate(models):
                kind = ("prepare" if metric == "artifact_bytes" else
                        "memory" if metric == "peak_bytes" else "execute")
                sample = grouped.get((model, variant, kind), [])
                if not sample:
                    continue
                field = "latency_ns" if metric == "latency_ratio" else metric
                values = [number(row, field) for row in sample if row[field]]
                if not values:
                    continue
                value, p95 = median_p95(values)
                if metric == "latency_ratio":
                    if model not in reference:
                        continue
                    reference_median, reference_p95 = reference[model]
                    value /= reference_median
                    p95 /= reference_p95
                xs.append(value)
                tails.append(p95)
                ys.append(index + offset)
            axis.hlines(ys, xs, tails, color=COLORS.get(variant, None), lw=0.65)
            axis.scatter(xs, ys, s=15, label=VARIANT_LABELS.get(variant, variant),
                         color=COLORS.get(variant, None))
        if metric != "latency_ratio":
            axis.set_xscale("log")
            axis.xaxis.set_major_locator(LogLocator(base=10, numticks=4))
            axis.xaxis.set_minor_formatter(NullFormatter())
        else:
            axis.axvline(1.0, color="#9AA0A6", lw=0.7)
        axis.set_title(title)
        axis.grid(axis="x", color="#E7E9EC", lw=0.5)
    axes[0].set_yticks(y, models)
    axes[0].invert_yaxis()
    unsupported = {(row["model"], row["variant"]) for row in rows
                   if not truth(row["supported"])}
    for index, model in enumerate(models):
        for offset, variant in zip(offsets, variants):
            if (model, variant) in unsupported:
                axes[0].scatter([0.02], [index + offset], marker="x", s=12,
                                color=COLORS.get(variant),
                                transform=axes[0].get_yaxis_transform(), clip_on=False)
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, ncol=max(1, len(labels)), loc="upper center", frameon=False)
    fig.tight_layout(rect=(0, 0, 1, 0.95))
    save(fig, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
