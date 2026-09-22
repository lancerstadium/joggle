#!/usr/bin/env python3
"""Plot individual operator/model measurements and explicit correct coverage."""

from __future__ import annotations

import argparse
import csv
import textwrap
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.lines import Line2D
from matplotlib.patches import Patch

from common import COLORS, configure, number, read_rows, save, truth

VARIANTS = ("joggle-unoptimized", "joggle-optimized")
LABELS = {"joggle-unoptimized": "Joggle base", "joggle-optimized": "Joggle opt"}
FAMILIES = ("elementwise", "reduction", "matmul", "convolution", "quantization", "fusion")
FAMILY_LABELS = ("Elementwise", "Reductions", "Matmul", "Convolution", "Quantization", "Fusion")
MODEL_LABELS = {
    "densenet-12": "DenseNet", "efficientnet-lite4-11-int8": "EffNet-int8",
    "efficientnet-lite4-11-qdq": "EffNet-QDQ", "googlenet-12": "GoogLeNet",
    "mnist-8": "MNIST", "mobilenetv2-7": "MobileNetV2", "resnet18-v1-7": "ResNet18",
    "shufflenet-v2-12": "ShuffleNetV2", "squeezenet1.0-13-qdq": "SqueezeNet-QDQ",
    "squeezenet1.1-7": "SqueezeNet", "ssd-mobilenetv1-12": "SSD-MobileNet",
    "tiny-yolov3-11": "TinyYOLOv3", "tinyyolov2-8": "TinyYOLOv2",
    "ultraface-rfb-320": "UltraFace", "xcit-tiny-12-p8-224-opset17": "XCiT-Tiny",
}


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
    plt.rcParams.update({"font.size": 7, "legend.fontsize": 7,
                         "savefig.bbox": None})
    panels = [("operator", label, [s for s in subjects["operator"] if family["operator", s] == name])
              for name, label in zip(FAMILIES, FAMILY_LABELS)]
    panels = [panel for panel in panels if panel[2]]
    for start in range(0, len(subjects["model"]), 5):
        panels.append(("model", f"Models {start + 1}–{min(start + 5, len(subjects['model']))}",
                       subjects["model"][start:start + 5]))
    if not panels:
        raise ValueError("no recognized operator families or model subjects")
    ncols = 2
    nrows = (len(panels) + ncols - 1) // ncols
    fig, axes = plt.subplots(nrows, ncols, figsize=(3.35, 0.93 * nrows + 0.32),
                             squeeze=False, sharey=True)
    finite = [float(r[k]) for r in summary if r["variant"] in VARIANTS
              for k in ("latency_over_ort", "p95_over_ort_median") if r[k] != ""]
    low, high = min([1.0, *finite]) / 1.5, max([1.0, *finite]) * 1.5
    for panel_index, (ax, (kind, title, names)) in enumerate(zip(axes.flat, panels)):
        for i, subject in enumerate(names):
            for variant, offset in zip(VARIANTS, (-0.19, 0.19)):
                row = indexed.get((kind, subject, variant))
                if row and row["latency_over_ort"] != "":
                    value, tail = row["latency_over_ort"], row["p95_over_ort_median"]
                    ax.bar(i + offset, value - 1, bottom=1, width=0.34,
                           color=COLORS[variant], edgecolor="#27333D", linewidth=0.35,
                           hatch="///" if variant == "joggle-unoptimized" else None,
                           zorder=3)
                    ax.errorbar(i + offset, value, yerr=[[0], [max(0, tail - value)]],
                                color="#27333D", lw=0.6, capsize=1.5, zorder=4)
                else:
                    ax.text(i + offset, 0.03, "×" if row else "?",
                            transform=ax.get_xaxis_transform(), va="bottom", ha="center",
                            color=COLORS[variant], fontsize=7)
        ax.axhline(1, color="#565F69", ls="--", lw=0.8, zorder=4)
        ax.set_yscale("log")
        ax.set_ylim(low, high)
        ticks = [10.0 ** exponent for exponent in range(-6, 7)
                 if low <= 10.0 ** exponent <= high]
        ax.set_yticks(ticks, [f"{tick:g}" for tick in ticks])
        ax.set_xlim(-0.6, len(names) - 0.4)
        labels = [MODEL_LABELS.get(s, s) if kind == "model" else
                  textwrap.fill(s.split("-", 1)[-1].replace("-", " "), width=10,
                                break_long_words=False, break_on_hyphens=False)
                  for s in names]
        ax.set_xticks(range(len(names)), labels, rotation=35, ha="right")
        ax.tick_params(axis="both", labelsize=6, length=1.5, pad=1)
        ax.set_title(f"({chr(97 + panel_index)}) {title}", loc="left", fontsize=7, pad=2)
        ax.grid(axis="y", which="major", color="#DDE3E8", lw=0.45)
        ax.set_axisbelow(True)
    for ax in list(axes.flat)[len(panels):]:
        ax.set_visible(False)
    handles = [Patch(facecolor=COLORS[v], edgecolor="#27333D", linewidth=0.35,
                      hatch="///" if v == "joggle-unoptimized" else None,
                      label=LABELS[v]) for v in VARIANTS]
    handles.append(Line2D([], [], color="#565F69", ls="--", lw=0.8, label="ORT = 1"))
    fig.legend(handles=handles, loc="upper center", ncol=3, frameon=False, bbox_to_anchor=(0.5, 1.0))
    fig.text(0.012, 0.53, "Latency / ORT (log scale)", va="center", rotation=90, fontsize=7)
    fig.subplots_adjust(left=0.135, right=0.985, bottom=0.145,
                        top=0.89, wspace=0.18, hspace=1.0)
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
