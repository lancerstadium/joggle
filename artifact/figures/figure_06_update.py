#!/usr/bin/env python3
"""Plot paired production rebuild/update time as dense vertical small multiples."""

from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import Patch

from common import COLORS, configure, number, read_rows, save, truth

BACKENDS = ("joggle", "tvm", "onnx-mlir")
LABELS = {"joggle": "Joggle", "tvm": "TVM", "onnx-mlir": "ONNX-MLIR"}
SHORT = {
    "densenet-12": "DN121", "efficientnet-lite4-11-qdq": "EN-QDQ",
    "googlenet-12": "GN", "mnist-8": "MNIST", "mobilenetv2-7": "MNV2",
    "resnet18-v1-7": "RN18", "shufflenet-v2-12": "SNv2",
    "squeezenet1.1-7": "SqN", "ssd-mobilenetv1-12": "SSD",
    "tiny-yolov3-11": "TYv3", "tinyyolov2-8": "TYv2",
    "ultraface-rfb-320": "Ultra", "xcit-tiny-12-p8-224-opset17": "XCiT-T",
    "common-geomean": "Geo.",
}
PANELS = (
    ("(a) Classic CNNs", ("densenet-12", "googlenet-12", "resnet18-v1-7")),
    ("(b) Mobile CNNs", ("mobilenetv2-7", "shufflenet-v2-12", "squeezenet1.1-7")),
    ("(c) Quant. / transformer", ("efficientnet-lite4-11-qdq", "xcit-tiny-12-p8-224-opset17")),
    ("(d) Detection", ("ssd-mobilenetv1-12", "ultraface-rfb-320")),
    ("(e) YOLO", ("tinyyolov2-8", "tiny-yolov3-11")),
    ("(f) Small / common", ("mnist-8", "common-geomean")),
)


def summary(values: list[float]) -> tuple[float, float, float]:
    low, center, high = np.quantile(np.asarray(values), (0.25, 0.5, 0.75))
    return float(center), float(center - low), float(high - center)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument("--output", type=Path, default=Path("figure-06-update.pdf"))
    args = parser.parse_args()
    rows = read_rows(args.csv, {"backend", "case_id", "edit_id", "iteration",
                                "policy", "ready_ns", "output_digest", "correct"})
    unknown = sorted({row["backend"] for row in rows} - set(BACKENDS))
    if unknown:
        raise ValueError(f"unexpected backends: {unknown}")

    grouped: dict[tuple[str, str, str], list[float]] = defaultdict(list)
    digest: dict[tuple[str, str, str, str], dict[str, str]] = defaultdict(dict)
    for row in rows:
        pair = (row["backend"], row["case_id"], row["edit_id"], row["iteration"])
        digest[pair][row["policy"]] = row["output_digest"] if truth(row["correct"]) else ""
        if truth(row["correct"]):
            grouped[(row["backend"], row["case_id"], row["policy"])].append(
                number(row, "ready_ns") / 1e9)
    for key, values in digest.items():
        if set(values) != {"update", "rebuild"}:
            raise ValueError(f"unpaired coordinate: {key}")
        if values["update"] and values["rebuild"] and values["update"] != values["rebuild"]:
            raise ValueError(f"output mismatch: {key}")

    real_subjects = sorted({row["case_id"] for row in rows})
    common = [subject for subject in real_subjects if all(
        grouped.get((backend, subject, policy))
        for backend in BACKENDS for policy in ("rebuild", "update"))]
    for backend in BACKENDS:
        for policy in ("rebuild", "update"):
            medians = [np.median(grouped[(backend, subject, policy)]) for subject in common]
            if medians:
                grouped[(backend, "common-geomean", policy)] = [
                    float(np.exp(np.mean(np.log(medians))))
                ]

    configure()
    fig, axes = plt.subplots(2, 3, figsize=(3.35, 2.34), sharey=True)
    axes = axes.flat
    width = 0.115
    policy_shift = {"rebuild": -width / 2, "update": width / 2}
    backend_shift = dict(zip(BACKENDS, (-0.29, 0.0, 0.29)))
    global_values = [value for sample in grouped.values() for value in sample if value > 0]
    lower = max(min(global_values) * 0.55, 1e-3)
    upper = max(global_values) * 2.5

    for axis, (title, subjects) in zip(axes, PANELS):
        x = np.arange(len(subjects), dtype=float)
        for subject_index, subject in enumerate(subjects):
            for backend in BACKENDS:
                stats = {}
                for policy in ("rebuild", "update"):
                    sample = grouped.get((backend, subject, policy), [])
                    if sample:
                        stats[policy] = summary(sample)
                        center, low, high = stats[policy]
                        position = subject_index + backend_shift[backend] + policy_shift[policy]
                        axis.bar(position, center - lower, bottom=lower, width=width,
                                 facecolor=COLORS[backend] if policy == "update" else "white",
                                 edgecolor=COLORS[backend], linewidth=0.55,
                                 hatch="///" if policy == "rebuild" else None, zorder=2)
                        axis.errorbar(position, center, yerr=([low], [high]), fmt="none",
                                      ecolor="#27313B", elinewidth=0.4,
                                      capsize=0.8, capthick=0.4, zorder=3)
                    elif policy == "update":
                        position = subject_index + backend_shift[backend]
                        axis.text(position, lower * 1.3, "$\\times$", color=COLORS[backend],
                                  ha="center", va="bottom", fontsize=4.8, fontweight="bold")
                if "rebuild" in stats and "update" in stats:
                    rebuild = stats["rebuild"][0]
                    update = stats["update"][0]
                    axis.text(subject_index + backend_shift[backend], max(rebuild, update) * 1.22,
                              f"{rebuild / update:.1f}$\\times$", ha="center", va="bottom",
                              fontsize=2.8, color=COLORS[backend], clip_on=True)
        axis.set_yscale("log")
        axis.set_ylim(lower, upper)
        axis.set_xticks(x, [SHORT.get(subject, subject) for subject in subjects],
                        rotation=28, ha="right")
        axis.set_title(title, loc="left", fontsize=5.6, pad=1.0)
        axis.tick_params(axis="both", labelsize=3.8, pad=0.8, length=1.8)
        axis.grid(axis="y", color="#DDE2E8", linewidth=0.35, zorder=0)
    axes[0].set_ylabel("ready time (s)", fontsize=4.8, labelpad=1)
    axes[3].set_ylabel("ready time (s)", fontsize=4.8, labelpad=1)

    handles = [Patch(facecolor=COLORS[b], edgecolor=COLORS[b], label=LABELS[b])
               for b in BACKENDS]
    handles.extend((Patch(facecolor="white", edgecolor="#4D5965", hatch="///", label="rebuild"),
                    Patch(facecolor="#7A8793", edgecolor="#4D5965", label="update")))
    fig.legend(handles=handles, ncol=5, frameon=False, loc="outside upper center",
               fontsize=4.1, handlelength=0.9, columnspacing=0.55)
    fig.supxlabel("model", fontsize=4.8, y=-0.005)
    save(fig, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
