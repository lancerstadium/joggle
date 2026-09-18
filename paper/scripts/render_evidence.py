#!/usr/bin/env python3
"""Two results a sentence carries badly, drawn vertically to match the runtime figure.

(a) The per-model runtime ratio of TVM's unscheduled lowering to the policy, with
    its bootstrap interval: whether an interval crosses one decides between a win
    and a tie, and nine of them are hard to compare in prose.
(b) The compilation span for three systems, every run shown, on a log axis.

Full width and vertical bars, so it reads the same way as the runtime chart.
Both series come from the records the manuscript cites.
"""

import csv
import json
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "data"
INK = "#1B2733"
BLUE = "#28609A"
ORANGE = "#C1652A"
GREY = "#9AA5AE"
SIZE = 10.0
SHORT = {"mnist-8": "mnist", "squeezenet1.1-7": "squeezenet1.1",
         "squeezenet1.0-13-qdq": "squeezenet-qdq", "shufflenet-v2-12": "shufflenet",
         "mobilenetv2-7": "mobilenetv2", "googlenet-12": "googlenet",
         "densenet-12": "densenet", "resnet18-v1-7": "resnet18",
         "tinyyolov2-8": "tinyyolov2", "ultraface-rfb-320": "ultraface"}


def paired_rows():
    record = json.loads((DATA / "locality-matrix-paired.json").read_text())
    return sorted(record["versus_tvm"].items(), key=lambda kv: kv[1]["median"])


def compile_series():
    series = {}
    with (DATA / "compile-time-same-host.csv").open(newline="") as stream:
        for row in csv.DictReader(stream):
            series.setdefault(row["system"], []).append(float(row["seconds"]))
    return series


def main():
    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": SIZE,
                         "pdf.fonttype": 42, "figure.dpi": 150})
    fig = plt.figure(figsize=(7.0, 1.42))
    left = fig.add_axes([.078, .520, .390, .380])
    right = fig.add_axes([.645, .520, .310, .380])

    # (a) paired ratio against TVM, vertical intervals.
    rows = paired_rows()
    for index, (model, entry) in enumerate(rows):
        lower, upper = entry["ci95"]
        tie = entry["verdict"] == "tie"
        colour = ORANGE if tie else BLUE
        left.plot([index, index], [lower, upper], color=colour, linewidth=3.0,
                  solid_capstyle="butt", zorder=3)
        left.plot([index], [entry["median"]], marker="o", markersize=4.0,
                  color=colour, zorder=4)
    left.axhline(1.0, color="#7A8791", linewidth=.8, linestyle=(0, (3, 3)), zorder=1)
    left.set_xticks(range(len(rows)), [SHORT.get(m, m) for m, _ in rows],
                    rotation=34, ha="right", rotation_mode="anchor",
                    fontsize=SIZE - 0.5)
    left.set_xlim(-.7, len(rows) - .3)
    left.set_ylim(.85, 3.05)
    left.set_yticks([1, 1.5, 2, 2.5, 3], ["1.0", "1.5", "2.0", "2.5", "3.0"])
    left.set_ylabel("ratio", fontsize=SIZE - 0.5)
    left.grid(axis="y", color="#D5DADF", linewidth=.5)
    left.set_axisbelow(True)
    for side in ("top", "right"):
        left.spines[side].set_visible(False)
    left.set_title("(a) Paired ratio vs TVM, 95% CI", fontsize=SIZE, color=INK,
                   loc="left")
    left.legend(handles=[Patch(facecolor=BLUE, label="faster"),
                         Patch(facecolor=ORANGE, label="interval covers one")],
                loc="upper left", frameon=False, fontsize=SIZE - 1.5,
                handlelength=1.0, handletextpad=.4, borderpad=.2)

    # (b) cost of one revision: this is the comparison the mechanism is for.
    summary = json.loads((ROOT / "experiments" / "reuse" / "external" / "summary.json").read_text())
    joggle = summary["joggle"]["stage_seconds_median"]["derive_and_plan_derived"]
    tvm = summary["tvm"]["incremental_rebuild_seconds"]["apply"]
    values = [("Joggle\nno rebuild", joggle, BLUE), ("TVM\n1 unit + relink", tvm, "#178477")]
    for index, (label, value, colour) in enumerate(values):
        right.bar(index, value, width=.48, color=colour, zorder=3)
        right.text(index, value * 1.03, f"{value:.2f}s", fontsize=SIZE - 1.0,
                   ha="center", va="bottom", color=INK)
    right.set_xticks(range(len(values)), [v[0] for v in values],
                     fontsize=SIZE - 1.5)
    right.set_ylim(0, max(v[1] for v in values) * 1.35)
    right.set_ylabel("seconds", fontsize=SIZE - 0.5)
    right.grid(axis="y", color="#D5DADF", linewidth=.5)
    right.set_axisbelow(True)
    for side in ("top", "right"):
        right.spines[side].set_visible(False)
    right.set_title("(b) One revision, to the revised plan", fontsize=SIZE,
                    color=INK, loc="left")

    out = ROOT / "figures" / "evidence.pdf"
    fig.savefig(out, bbox_inches=None)
    fig.savefig(out.with_suffix(".png"), dpi=300, bbox_inches=None)
    plt.close(fig)
    print("wrote", out.name)


if __name__ == "__main__":
    main()
