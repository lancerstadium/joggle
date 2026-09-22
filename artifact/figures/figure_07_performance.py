#!/usr/bin/env python3
"""Plot individual operator/model measurements and explicit correct coverage."""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.lines import Line2D

from common import COLORS, configure, number, read_rows, save, truth

VARIANTS = ("joggle-unoptimized", "joggle-optimized")
LABELS = {"joggle-unoptimized": "Joggle base", "joggle-optimized": "Joggle opt"}
MARKERS = {"joggle-unoptimized": "o", "joggle-optimized": "D"}
FAMILIES = ("elementwise", "reduction", "matmul", "convolution", "quantization", "fusion")


def summarize(rows: list[dict[str, str]]) -> list[dict[str, object]]:
    groups: dict[tuple[str, str, str], list[dict[str, str]]] = defaultdict(list)
    seen = set()
    for row in rows:
        key = (row["subject_kind"], row["subject"], row["variant"])
        identity = (*key, row["iteration"])
        if identity in seen:
            raise ValueError(f"duplicate measurement: {identity}")
        seen.add(identity)
        groups[key].append(row)
    summary = []
    for (kind, subject, variant), sample in sorted(groups.items()):
        signatures = {(r["subject_hash"], r["system_revision"], r["input_digest"],
                       r["supported"], r["correct"]) for r in sample}
        if len(signatures) != 1:
            raise ValueError(f"mixed workload/revision/status for {subject}/{variant}")
        correct = truth(sample[0]["supported"]) and all(truth(r["correct"]) for r in sample)
        values = [number(r, "latency_ns") for r in sample] if correct else []
        if values and (not np.all(np.isfinite(values)) or min(values) <= 0):
            raise ValueError(f"invalid latency: {subject}/{variant}")
        summary.append({
            "subject_kind": kind, "subject": subject, "variant": variant,
            "family": sample[0]["family"], "system_revision": sample[0]["system_revision"],
            "subject_hash": sample[0]["subject_hash"], "input_digest": sample[0]["input_digest"],
            "correct": correct, "reason": sample[0]["reason"], "samples": len(values),
            "median_ns": float(np.median(values)) if values else "",
            "p95_ns": float(np.percentile(values, 95)) if values else "",
        })
    indexed = {(r["subject_kind"], r["subject"], r["variant"]): r for r in summary}
    for row in summary:
        ref = indexed.get((row["subject_kind"], row["subject"], "onnxruntime"))
        usable = row["correct"] and ref and ref["correct"]
        if usable and (row["input_digest"] != ref["input_digest"] or
                       row["subject_hash"] != ref["subject_hash"]):
            raise ValueError(f"unmatched reference inputs for {row['subject']}")
        row["latency_over_ort"] = row["median_ns"] / ref["median_ns"] if usable else ""
        row["p95_over_ort_median"] = row["p95_ns"] / ref["median_ns"] if usable else ""
    return summary


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument("--output", type=Path, default=Path("figure-07-performance.pdf"))
    parser.add_argument("--summary", type=Path, help="Export displayed medians, p95s, and ratios")
    args = parser.parse_args()
    rows = read_rows(args.csv, {
        "subject_kind", "subject", "family", "variant", "supported", "reason",
        "latency_ns", "correct", "iteration", "subject_hash", "system_revision", "input_digest",
    })
    summary = summarize(rows)
    indexed = {(r["subject_kind"], r["subject"], r["variant"]): r for r in summary}
    family = {(r["subject_kind"], r["subject"]): r["family"] for r in summary}
    rank = {value: i for i, value in enumerate(FAMILIES)}
    subjects = {kind: sorted({r["subject"] for r in summary if r["subject_kind"] == kind},
                            key=lambda s: (rank.get(family[kind, s], 99), s))
                for kind in ("operator", "model")}
    configure()
    fig = plt.figure(figsize=(7.0, max(3.5, 0.15 * len(subjects["operator"]) + 0.9)))
    grid = fig.add_gridspec(1, 2)
    right = grid[0, 1].subgridspec(2, 1, height_ratios=(3.2, 0.85), hspace=0.52)
    axes = (fig.add_subplot(grid[0, 0]), fig.add_subplot(right[0, 0]))
    finite = [float(r[k]) for r in summary if r["variant"] in VARIANTS
              for k in ("latency_over_ort", "p95_over_ort_median") if r[k] != ""]
    low, high = min([1.0, *finite]) / 1.5, max([1.0, *finite]) * 1.8
    for ax, kind, letter in zip(axes, ("operator", "model"), ("a", "b")):
        names = subjects[kind]
        previous = None
        for i, subject in enumerate(names):
            if i % 2 == 0:
                ax.axhspan(i - 0.5, i + 0.5, color="#F2F5F7", zorder=0)
            current = family[kind, subject]
            if kind == "operator" and previous is not None and current != previous:
                ax.axhline(i - 0.5, color="#B9C3CC", lw=0.65)
            previous = current
            for variant, offset in zip(VARIANTS, (-0.17, 0.17)):
                row = indexed.get((kind, subject, variant))
                if row and row["latency_over_ort"] != "":
                    x, tail = row["latency_over_ort"], row["p95_over_ort_median"]
                    ax.plot([x, tail], [i + offset] * 2, color=COLORS[variant], lw=0.8)
                    ax.plot(x, i + offset, MARKERS[variant], color=COLORS[variant], ms=3)
                else:
                    ax.text(1.03, i + offset, "×" if row else "?",
                            transform=ax.get_yaxis_transform(), va="center", ha="right",
                            color=COLORS[variant], fontsize=7)
        ax.axvline(1, color="#6A737E", ls="--", lw=0.75)
        ax.set_xscale("log")
        ax.set_xlim(low, high)
        ax.set_ylim(max(len(names), 1) - 0.5, -0.5)
        ax.set_yticks(range(len(names)), names)
        ax.tick_params(axis="y", length=0, labelsize=6)
        ax.set_xlabel("Latency / ONNX Runtime  (lower is faster)", fontsize=6.5)
        ax.set_title(f"({letter}) {len(names)} {kind}s", loc="left")
        ax.grid(axis="x", color="#DDE3E8", lw=0.45)
        ax.set_axisbelow(True)
    ax = fig.add_subplot(right[1, 0])
    ax.axis("off")
    cells = []
    for variant in (*VARIANTS, "onnxruntime"):
        cells.append([LABELS.get(variant, "ONNX Runtime"), *[
            f"{sum(bool(indexed.get((kind, s, variant), {}).get('correct')) for s in subjects[kind])}/{len(subjects[kind])}"
            for kind in ("operator", "model")]])
    table = ax.table(cellText=cells, colLabels=("Correct coverage", "Operators", "Models"),
                     loc="center", cellLoc="center", colWidths=(0.5, 0.25, 0.25))
    table.auto_set_font_size(False)
    table.set_fontsize(6.5)
    table.scale(1, 1.1)
    for (row, _), cell in table.get_celld().items():
        cell.set_edgecolor("#DBE2E8")
        cell.set_linewidth(0.5)
        cell.set_facecolor("#E8EFF5" if row == 0 else "white")
    handles = [Line2D([], [], marker=MARKERS[v], color=COLORS[v], lw=0.8, ms=3,
                      label=LABELS[v]) for v in VARIANTS]
    fig.legend(handles=handles, loc="upper center", ncol=2, frameon=False,
               bbox_to_anchor=(0.52, 1.005))
    fig.text(0.5, 0.005, "Point: median · line: median to p95 · ×: no correct result · ?: missing measurement",
             ha="center", fontsize=6.2)
    fig.subplots_adjust(left=0.19, right=0.99, bottom=0.12, top=0.92, wspace=1.15)
    save(fig, args.output)
    if args.summary:
        args.summary.parent.mkdir(parents=True, exist_ok=True)
        with args.summary.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(summary[0]))
            writer.writeheader()
            writer.writerows(summary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
