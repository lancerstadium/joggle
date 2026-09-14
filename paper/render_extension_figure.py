#!/usr/bin/env python3
"""Render the measured extension-surface comparison from preserved records."""

from __future__ import annotations

import csv
import json
from pathlib import Path

import matplotlib as mpl
import matplotlib.pyplot as plt
import numpy as np


ROOT = Path(__file__).resolve().parents[1]
TASKS = ["implementation", "policy", "external-kernel", "numeric-format"]
SYSTEMS = ["Joggle", "TVM", "ONNX-MLIR"]
COLORS = {"Joggle": "#0072B2", "TVM": "#E69F00", "ONNX-MLIR": "#009E73"}
SOURCES = {
    "Joggle": ROOT / "paper/data/extension-footprint-pilot.csv",
    "TVM": ROOT / "paper/data/extension-tvm-pilot.csv",
    "ONNX-MLIR": ROOT / "paper/data/extension-onnx-mlir-pilot.csv",
}
UNSUPPORTED = {
    ("TVM", "numeric-format"),
    ("ONNX-MLIR", "external-kernel"),
    ("ONNX-MLIR", "numeric-format"),
}


def records(path: Path) -> dict[str, dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    out = {row["task"]: row for row in rows}
    if len(out) != len(rows) or any(task not in TASKS for task in out):
        raise ValueError(f"unexpected or duplicate tasks in {path}")
    return out


def load() -> dict[str, dict[str, dict[str, str]]]:
    data = {system: records(path) for system, path in SOURCES.items()}
    if set(data["Joggle"]) != set(TASKS):
        raise ValueError("Joggle record must contain all frozen tasks")
    for system in SYSTEMS:
        for task, row in data[system].items():
            status = row["validation"]
            expected = "unsupported" if (system, task) in UNSUPPORTED else "pass"
            if status != expected:
                raise ValueError(
                    f"{system}/{task}: expected {expected}, observed {status}"
                )
    return data


def configure() -> None:
    mpl.rcParams.update({
        "font.family": "sans-serif",
        "font.sans-serif": ["Arial", "Helvetica", "DejaVu Sans"],
        "font.size": 7,
        "axes.labelsize": 7,
        "axes.titlesize": 8,
        "axes.linewidth": 0.6,
        "xtick.labelsize": 6.5,
        "ytick.labelsize": 6.5,
        "legend.fontsize": 7,
        "pdf.fonttype": 42,
        "ps.fonttype": 42,
    })


def main() -> None:
    data = load()
    configure()
    metrics = [
        ("source_files", "Authored files", 1.0),
        ("source_sloc", "Authored lines", 1.0),
        ("source_bytes", "Authored source (KiB)", 1.0 / 1024.0),
    ]
    x = np.arange(len(TASKS), dtype=float)
    width = 0.23
    fig, axes = plt.subplots(
        1, 3, figsize=(7.05, 2.15), sharex=True, constrained_layout=True
    )
    for axis, (field, title, scale) in zip(axes, metrics, strict=True):
        for index, system in enumerate(SYSTEMS):
            offset = (index - 1) * width
            values: list[float] = []
            present: list[bool] = []
            unsupported: list[bool] = []
            for task in TASKS:
                row = data[system].get(task)
                values.append(float(row[field]) * scale if row else 0.0)
                present.append(row is not None)
                unsupported.append((system, task) in UNSUPPORTED)
            bars = axis.bar(
                x + offset,
                values,
                width,
                color=COLORS[system],
                edgecolor="black",
                linewidth=0.45,
                label=system,
                zorder=2,
            )
            for task_index, (bar, has_value, stopped) in enumerate(
                zip(bars, present, unsupported, strict=True)
            ):
                if stopped and has_value:
                    bar.set_hatch("////")
                elif stopped:
                    axis.scatter(
                        x[task_index] + offset,
                        0.0,
                        marker="x",
                        s=20,
                        linewidths=1.0,
                        color=COLORS[system],
                        clip_on=False,
                        zorder=4,
                    )
        axis.set_title(title, pad=4)
        axis.set_ylim(bottom=0)
        axis.grid(axis="y", color="#D9D9D9", linewidth=0.45, zorder=0)
        axis.spines[["top", "right"]].set_visible(False)
        axis.set_xticks(x, ["compute", "policy", "external", "format"])
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(
        handles,
        labels,
        loc="upper center",
        bbox_to_anchor=(0.5, 1.07),
        ncol=3,
        frameon=False,
        handlelength=1.5,
        columnspacing=1.2,
    )
    fig.text(
        0.5,
        -0.03,
        "× = stopped before a measurable implementation; hatch = partial surface before an unsupported endpoint",
        ha="center",
        va="top",
        fontsize=6.5,
    )
    output = ROOT / "paper/figures"
    output.mkdir(parents=True, exist_ok=True)
    metadata = {
        "figure": "extension-surface",
        "tasks": TASKS,
        "systems": SYSTEMS,
        "sources": {key: str(value.relative_to(ROOT)) for key, value in SOURCES.items()},
        "unsupported": [list(value) for value in sorted(UNSUPPORTED)],
        "matplotlib": mpl.__version__,
    }
    (output / "extension-surface.json").write_text(
        json.dumps(metadata, indent=2) + "\n", encoding="utf-8"
    )
    fig.savefig(output / "extension-surface.pdf", bbox_inches="tight")
    fig.savefig(output / "extension-surface.png", dpi=600, bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    main()
