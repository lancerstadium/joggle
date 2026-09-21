#!/usr/bin/env python3
"""Plot Figure 7: operator and model performance plus coverage."""

from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

from common import COLORS, configure, number, read_rows, save, truth

VARIANTS = ("joggle-unoptimized", "joggle-optimized")
LABELS = {"joggle-unoptimized": "Joggle base", "joggle-optimized": "Joggle opt"}


def geometric(values: list[float]) -> float:
    sample = np.asarray(values, dtype=float)
    return float(np.exp(np.mean(np.log(sample))))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument("--output", type=Path, default=Path("figure-07-performance.pdf"))
    args = parser.parse_args()
    rows = read_rows(args.csv, {
        "subject_kind", "subject", "family", "variant", "supported",
        "latency_ns", "correct", "iteration",
    })

    samples: dict[tuple[str, str, str], list[float]] = defaultdict(list)
    support: dict[tuple[str, str, str], bool] = {}
    family: dict[str, str] = {}
    for row in rows:
        key = (row["subject_kind"], row["subject"], row["variant"])
        support[key] = truth(row["supported"])
        family[row["subject"]] = row["family"]
        if truth(row["supported"]):
            if not truth(row["correct"]):
                raise SystemExit(f"incorrect row for {key}")
            samples[key].append(number(row, "latency_ns"))

    medians = {key: float(np.median(values)) for key, values in samples.items()}
    ratios: dict[tuple[str, str, str], float] = {}
    for (kind, subject, variant), latency in medians.items():
        reference = medians.get((kind, subject, "onnxruntime"))
        if variant in VARIANTS and reference:
            ratios[(kind, subject, variant)] = latency / reference

    configure()
    fig = plt.figure(figsize=(7.0, 2.45), constrained_layout=True)
    grid = fig.add_gridspec(1, 3, width_ratios=(1.12, 1.35, 0.72))

    ax = fig.add_subplot(grid[0, 0])
    families = sorted({value for value in family.values() if value})
    y = np.arange(len(families))
    for variant, offset in zip(VARIANTS, (-0.13, 0.13)):
        points = []
        for name in families:
            values = [value for (kind, subject, candidate), value in ratios.items()
                      if kind == "operator" and candidate == variant and family[subject] == name]
            points.append(geometric(values) if values else np.nan)
        ax.scatter(points, y + offset, s=19, color=COLORS[variant], label=LABELS[variant])
    ax.axvline(1, color="#737B87", ls="--", lw=0.7)
    ax.set_xscale("log")
    ax.set_yticks(y, families)
    ax.invert_yaxis()
    ax.set_xlabel("Latency / ONNX Runtime")
    ax.set_title("(a) 24 operators · geometric mean", loc="left")
    ax.grid(axis="x", color="#E1E5EA", lw=0.5)

    ax = fig.add_subplot(grid[0, 1])
    models = sorted({subject for kind, subject, _variant in medians if kind == "model"})
    y = np.arange(len(models))
    for variant, offset in zip(VARIANTS, (-0.13, 0.13)):
        xs = [ratios.get(("model", model, variant), np.nan) for model in models]
        ax.scatter(xs, y + offset, s=17, color=COLORS[variant], label=LABELS[variant])
    ax.axvline(1, color="#737B87", ls="--", lw=0.7)
    ax.set_xscale("log")
    ax.set_yticks(y, models)
    ax.invert_yaxis()
    ax.set_xlabel("Latency / ONNX Runtime")
    ax.set_title("(b) 15 models", loc="left")
    ax.grid(axis="x", color="#E1E5EA", lw=0.5)

    ax = fig.add_subplot(grid[0, 2])
    kinds = ("operator", "model")
    matrix = np.zeros((len(VARIANTS), len(kinds)))
    labels = [["" for _ in kinds] for _ in VARIANTS]
    for i, variant in enumerate(VARIANTS):
        for j, kind in enumerate(kinds):
            keys = [key for key in support if key[0] == kind and key[2] == variant]
            ok = sum(support[key] for key in keys)
            matrix[i, j] = ok / len(keys) if keys else 0
            labels[i][j] = f"{ok}/{len(keys)}"
    ax.imshow(matrix, vmin=0, vmax=1, cmap="Blues", aspect="auto")
    for i in range(len(VARIANTS)):
        for j in range(len(kinds)):
            ax.text(j, i, labels[i][j], ha="center", va="center",
                    color="white" if matrix[i, j] > 0.6 else "#1F2933", fontsize=7)
    ax.set_xticks(range(len(kinds)), ("Ops", "Models"))
    ax.set_yticks(range(len(VARIANTS)), [LABELS[value] for value in VARIANTS])
    ax.set_title("(c) Correct coverage", loc="left")
    for spine in ax.spines.values():
        spine.set_visible(False)

    save(fig, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
