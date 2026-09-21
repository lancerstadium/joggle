#!/usr/bin/env python3
"""Plot Figure 8 from one operator-artifact CSV."""

from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.ticker import LogLocator, NullFormatter

from common import COLORS, configure, median_p95, number, read_rows, save, truth


FAMILY_ORDER = ("elementwise", "reduction", "matmul", "convolution", "quantization", "fusion")
VARIANT_ORDER = ("joggle-unoptimized", "joggle-optimized", "onnxruntime")
VARIANT_LABELS = {
    "joggle-unoptimized": "Joggle base",
    "joggle-optimized": "Joggle opt",
    "onnxruntime": "ONNX Runtime",
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument("--output", type=Path, default=Path("figure-08-operators.pdf"))
    args = parser.parse_args()
    rows = read_rows(args.csv, {"case_id", "family", "system", "variant",
                                "record_kind", "supported", "latency_ns",
                                "prepare_ns", "artifact_bytes", "correct"})
    grouped: dict[tuple[str, str, str], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        if truth(row["supported"]):
            if row["record_kind"] != "execute" or truth(row["correct"]):
                grouped[(row["case_id"], row["variant"], row["record_kind"])].append(row)
    family_of = {row["case_id"]: row["family"] for row in rows}
    subjects = sorted(family_of, key=lambda case: (FAMILY_ORDER.index(family_of[case]), case))
    observed_variants = {row["variant"] for row in rows}
    variants = [variant for variant in VARIANT_ORDER if variant in observed_variants]
    unsupported = {(row["case_id"], row["variant"]) for row in rows
                   if not truth(row["supported"])}

    configure()
    fig, axes = plt.subplots(1, 3, figsize=(7.0, max(2.4, 0.24 * len(subjects))))
    specs = [("execute", "latency_ns", "Execution (ns)"),
             ("prepare", "prepare_ns", "Preparation (ns)"),
             ("prepare", "artifact_bytes", "Generated artifact (bytes)")]
    y = np.arange(len(subjects))
    offsets = np.linspace(-0.18, 0.18, len(variants))
    for axis, (kind, metric, title) in zip(axes, specs):
        for offset, variant in zip(offsets, variants):
            xs, tails, ys = [], [], []
            for index, subject in enumerate(subjects):
                sample = grouped.get((subject, variant, kind), [])
                if sample:
                    values = [number(row, metric) for row in sample if row[metric]]
                    if not values:
                        continue
                    median, p95 = median_p95(values)
                    xs.append(median)
                    tails.append(p95)
                    ys.append(index + offset)
            axis.hlines(ys, xs, tails, color=COLORS.get(variant, None), lw=0.65)
            axis.scatter(xs, ys, s=15, label=VARIANT_LABELS.get(variant, variant),
                         color=COLORS.get(variant, None))
        axis.set_xscale("log")
        axis.xaxis.set_major_locator(LogLocator(base=10, numticks=4))
        axis.xaxis.set_minor_formatter(NullFormatter())
        axis.set_title(title)
        axis.grid(axis="x", color="#E7E9EC", lw=0.5)
    axes[0].set_yticks(y, subjects)
    axes[0].invert_yaxis()
    for index, subject in enumerate(subjects):
        for offset, variant in zip(offsets, variants):
            if (subject, variant) in unsupported:
                axes[0].scatter([0.02], [index + offset], marker="x", s=12,
                                color=COLORS.get(variant),
                                transform=axes[0].get_yaxis_transform(), clip_on=False)
    for boundary in range(1, len(subjects)):
        if family_of[subjects[boundary]] != family_of[subjects[boundary - 1]]:
            for axis in axes:
                axis.axhline(boundary - 0.5, color="#D9DEE7", lw=0.55)
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, ncol=max(1, len(labels)), loc="upper center", frameon=False)
    fig.tight_layout(rect=(0, 0, 1, 0.95))
    save(fig, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
