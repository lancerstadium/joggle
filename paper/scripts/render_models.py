#!/usr/bin/env python3
"""Render recorded model latencies; never pool hosts, revisions, or jobs."""

import csv
import hashlib
import json
from pathlib import Path
from statistics import median

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
import numpy as np

# The records and figures this renders live in the paper workspace, one level
# above the scripts directory.
ROOT = Path(__file__).resolve().parent.parent
PANELS = [
    ("(a) Whole-model latency", [
        ("MNIST", "linux-replication/mnist", 0),
        ("MobileNetV2\nONNX", "linux-replication/mobilenetv2", 1.6),
        ("MobileNetV2\nTFLite", "tflite-linux/tflite-mobilenetv2", 3.2),
    ]),
    ("(b) Body transformations", [
        ("Base A", "mobilenetv2-policy/plain", 0),
        ("Policy A", "mobilenetv2-policy/canon", 1),
        ("Base B", "mobilenetv2-policy/replication/plain", 2.5),
        ("Policy B", "mobilenetv2-policy/replication/canon", 3.5),
        ("Unfused", "mobilenetv2-fusion-linux-unfused-pilot", 5),
        ("Fused", "mobilenetv2-fusion-linux-fused-pilot", 6),
    ]),
]
STYLES = {
    "joggle": ("#28609A", "o", "Joggle C"),
    "onnxruntime": ("#178477", "s", "ONNX Runtime"),
    "litert": ("#AC6724", "^", "LiteRT"),
}


def load(stem):
    path = ROOT / "data" / stem
    meta_path, csv_path = path.with_suffix(".json"), path.with_suffix(".csv")
    meta = json.loads(meta_path.read_text())
    if meta["repo_dirty"] or meta["trials"] != 20:
        raise ValueError(f"Unexpected run admission state: {stem}")
    with csv_path.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) != 40:
        raise ValueError(f"Expected 20 two-system trials: {stem}")
    series = {}
    for row in rows:
        value = float(row["seconds"]) * 1000
        if not np.isfinite(value) or value <= 0 or not row["validation"]:
            raise ValueError(f"Invalid observation: {stem}")
        series.setdefault(row["system"], []).append((int(row["trial"]), value))
    if "joggle" not in series or len(series) != 2:
        raise ValueError(f"Unexpected systems: {stem}")
    for name, values in series.items():
        if sorted(t for t, _ in values) != list(range(20)):
            raise ValueError(f"Duplicate or missing trials: {stem}/{name}")
        series[name] = [v for _, v in sorted(values)]
    record = {
        "record": stem,
        "csv_sha256": hashlib.sha256(csv_path.read_bytes()).hexdigest(),
        "metadata_sha256": hashlib.sha256(meta_path.read_bytes()).hexdigest(),
        "revision": meta["repo_revision"],
        "processor": meta["host"]["processor"],
        "started_utc": meta["started_utc"],
        "trials_per_system": 20,
        "median_ms": {name: median(v) for name, v in series.items()},
    }
    return series, record


def main():
    # Explicit local style keeps reproduction independent of an installed skill.
    # 7-inch text block and >=10-point labels follow EuroSys 2027's CFP.
    plt.rcParams.update({
        "font.family": "DejaVu Serif", "font.size": 10,
        "axes.labelsize": 10, "axes.titlesize": 10,
        "xtick.labelsize": 10, "ytick.labelsize": 10,
        "legend.fontsize": 10, "axes.linewidth": 0.6,
        "pdf.fonttype": 42, "ps.fonttype": 42,
        "axes.spines.top": False, "axes.spines.right": False,
        "figure.constrained_layout.use": False,
        "savefig.bbox": None, "figure.dpi": 150,
    })
    fig, axes = plt.subplots(1, 2, figsize=(7, 3.5), gridspec_kw={"width_ratios": [1, 1.05]})
    fig.subplots_adjust(left=.20, right=.985, bottom=.17, top=.79, wspace=.60)
    records = []
    for index, (ax, (title, cases)) in enumerate(zip(axes, PANELS)):
        for label, stem, y in cases:
            series, record = load(stem)
            records.append(record)
            ax.axhspan(y - .40, y + .40, color="#F3F5F7", zorder=0)
            for name, values in series.items():
                color, marker, _ = STYLES[name]
                center = y + (-.16 if name == "joggle" else .16)
                # Deterministic trial-order offset: all observations remain visible.
                offset = np.linspace(-.065, .065, len(values))
                ax.scatter(values, center + offset, s=7, c=color, alpha=.45,
                           marker=marker, edgecolors="none", zorder=2)
                ax.scatter([median(values)], [center], s=34, c=color,
                           marker=marker, edgecolors="white", linewidths=.6, zorder=3)
        ax.set_xscale("log")
        ax.set_yticks([v[2] for v in cases], [v[0] for v in cases])
        ax.set_ylim(cases[-1][2] + .65, -.65)
        ax.tick_params(axis="y", length=0, pad=5)
        ax.set_title(title, loc="left", pad=12)
        ax.set_xlabel("Latency (ms, log scale)")
        ax.grid(axis="x", color="#CFD4D8", linewidth=.5)
        ax.set_axisbelow(True)
        ax.spines["left"].set_visible(False)
        if index == 0:
            ax.set_xlim(.025, 500)
            ax.set_xticks([.1, 1, 10, 100], ["0.1", "1", "10", "100"])
        else:
            ax.set_xlim(4, 650)
            ax.set_xticks([10, 100], ["10", "100"])
            for boundary in [1.75, 4.25]:
                ax.axhline(boundary, color="#ADB7BE", linewidth=.6, linestyle=(0, (3, 3)))
    handles = [Line2D([], [], marker=m, color=c, linestyle="none", markersize=5,
                      label=label) for c, m, label in STYLES.values()]
    fig.legend(handles=handles, loc="upper center", bbox_to_anchor=(.54, .99),
               ncol=3, frameon=False, columnspacing=1.2, handletextpad=.4)
    out = ROOT / "figures"
    # Fixed canvas: PDF width remains exactly 7 inches; no shrink-to-fit surprises.
    fig.savefig(out / "models.pdf", bbox_inches=None)
    fig.savefig(out / "models.png", dpi=300, bbox_inches=None)
    plt.close(fig)
    payload = {"matplotlib": matplotlib.__version__, "numpy": np.__version__,
               "statistic": "large markers: medians; small markers: all 20 trials",
               "units": "milliseconds", "records": records}
    (out / "models.json").write_text(json.dumps(payload, indent=2) + "\n")
    for record in records:
        print(record["record"], record["median_ms"])


if __name__ == "__main__":
    main()
