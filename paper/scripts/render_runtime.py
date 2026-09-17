#!/usr/bin/env python3
"""Render one grouped bar chart over the whole pinned model set.

The x axis lists every pinned model, in the order the coverage ledger defines,
so no model is dropped for lacking a measurement. A group holds the systems
measured on that model and is empty where none were. All systems come from one
job, so the columns are measured under one set of conditions. Bars are vertical
and carry the recorded median absolute deviation as an error bar; runtime is
normalized to the model's own first-fit artifact so one zero baseline is valid
across models whose absolute runtime spans three orders of magnitude.
"""

import csv
import hashlib
import json
from pathlib import Path
from statistics import median

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch
import numpy as np

ROOT = Path(__file__).resolve().parent.parent
MATRIX = "locality-matrix"
LEDGER = "model-coverage-pilot.csv"
SERIES = [
    ("base", "Joggle", "#9AA4B0"),
    ("locality", "Joggle + locality", "#28609A"),
    ("tvm", "TVM (unscheduled C)", "#178477"),
    ("onnxmlir", "ONNX-MLIR -O3", "#C2761F"),
    ("ort", "ONNX Runtime", "#7D3C98"),
]
TRIALS = 20
WIDTH = 0.155


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pinned():
    with (ROOT / "data" / LEDGER).open(newline="") as stream:
        return [row["model"] for row in csv.DictReader(stream)]


def measured():
    """Medians and deviations per model and system, admitted only if validated."""
    csv_path = ROOT / "data" / f"{MATRIX}.csv"
    meta_path = ROOT / "data" / f"{MATRIX}.json"
    meta = json.loads(meta_path.read_text())
    if meta["trials"] != TRIALS or meta["threads"] != 1:
        raise ValueError(f"Unexpected admission state: {MATRIX}")
    with csv_path.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    series = {}
    for row in rows:
        value = float(row["seconds"]) * 1000
        if not np.isfinite(value) or value <= 0:
            raise ValueError(f"Invalid observation: {row['model']}")
        fields = row["validation"].split(",")
        if fields[-1] != "pass" or fields[fields.index("failed") + 1] != "0":
            raise ValueError(f"Unvalidated row reached the figure: {row['model']}")
        model = series.setdefault(row["model"], {})
        model.setdefault("_checksum", {}).setdefault(row["variant"], set())
        model["_checksum"][row["variant"]].add(row["checksum"])
        model.setdefault(row["variant"], []).append((int(row["trial"]), value))

    clean = {}
    for model, variants in series.items():
        checksums = variants.pop("_checksum")
        if len({next(iter(checksums[n])) for n in ("base", "locality")}) != 1:
            raise ValueError(f"Joggle variants disagree bitwise: {model}")
        summary = meta["summary"][model]
        if "speedup" not in summary:
            raise ValueError(f"Incomplete summary for a measured model: {model}")
        if not summary["checksum_identical_across_variants"]:
            raise ValueError(f"Checksum flag contradicts rows: {model}")
        if not summary["weights_identical"]:
            raise ValueError(f"Weights differ between variants: {model}")
        reference = median(v for _, v in variants["base"])
        if abs(summary["speedup"] - reference / median(
                v for _, v in variants["locality"])) > 1e-9:
            raise ValueError(f"Recorded speedup disagrees: {model}")
        clean[model] = {
            name: {
                "median": median(v for _, v in values) / reference,
                "mad": summary[name]["mad_ms"] / reference,
                "n": len(values),
            }
            for name, values in variants.items()
        }
    return clean, csv_path, meta_path, meta


def main():
    # matplotlib defaults: sans-serif, the standard look for a venue figure.
    plt.rcParams.update({
        "font.size": 10.5, "axes.labelsize": 11, "xtick.labelsize": 10,
        "ytick.labelsize": 10.5, "legend.fontsize": 10, "axes.linewidth": 0.7,
        "pdf.fonttype": 42, "ps.fonttype": 42, "savefig.bbox": None,
        "figure.dpi": 150,
    })
    runs, csv_path, meta_path, meta = measured()
    order = [m for m in pinned() if m in runs]
    order.sort(key=lambda m: runs[m]["locality"]["median"])
    order += [m for m in pinned() if m not in runs]

    fig = plt.figure(figsize=(7, 2.6))
    ax = fig.add_axes([.088, .300, .890, .560])

    for index, model in enumerate(order):
        variants = runs.get(model)
        if not variants:
            continue
        present = [s for s in SERIES if s[0] in variants]
        for slot, (name, _, color) in enumerate(present):
            entry = variants[name]
            x = index + (slot - (len(present) - 1) / 2) * WIDTH
            ax.bar(x, entry["median"], width=WIDTH * .88, color=color,
                   edgecolor="white", linewidth=.4, zorder=3)
            ax.errorbar(x, entry["median"], yerr=entry["mad"], fmt="none",
                        ecolor="#1B2733", elinewidth=.7, capsize=1.5,
                        capthick=.7, zorder=4)

    ax.axhline(1.0, color="#7A8791", linewidth=.7, linestyle=(0, (3, 3)), zorder=1)
    ax.set_xticks(range(len(order)), order, rotation=32, ha="right",
                  rotation_mode="anchor")
    ax.set_xlim(-.65, len(order) - .35)
    ax.set_ylim(0, 1.06)
    ax.set_yticks([0, .25, .5, .75, 1.0], ["0", "0.25", "0.50", "0.75", "1.00"])
    ax.set_ylabel("Runtime relative to Joggle first-fit")
    ax.grid(axis="y", color="#D5DADF", linewidth=.5)
    ax.set_axisbelow(True)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)

    handles = [Patch(facecolor=color, label=label) for _, label, color in SERIES]
    ax.legend(handles=handles, loc="upper center", bbox_to_anchor=(.5, 1.15),
              ncol=5, frameon=False, columnspacing=1.1, handletextpad=.45)
    out = ROOT / "figures"
    # Fixed canvas: PDF width remains exactly 7 inches; no shrink-to-fit surprises.
    fig.savefig(out / "runtime.pdf", bbox_inches=None)
    fig.savefig(out / "runtime.png", dpi=300, bbox_inches=None)
    plt.close(fig)

    speedups = [1 / runs[m]["locality"]["median"] for m in runs]
    payload = {
        "matplotlib": matplotlib.__version__, "numpy": np.__version__,
        "statistic": ("bar: median over %d fresh processes, normalized to the "
                      "same model's first-fit median; error bar: recorded MAD"
                      % TRIALS),
        "units": "runtime relative to that model's Joggle first-fit artifact",
        "record": MATRIX, "csv_sha256": digest(csv_path),
        "metadata_sha256": digest(meta_path), "host": meta["host"],
        "cpu": meta["cpu"], "date_utc": meta["date_utc"], "atol": meta["atol"],
        "pinned_models": len(order), "measured_models": len(runs),
        "speedup_min": min(speedups), "speedup_max": max(speedups),
        "speedup_median": median(speedups),
        "models": [{
            "model": m,
            "systems": runs.get(m, {}),
            "speedup": 1 / runs[m]["locality"]["median"] if m in runs else None,
        } for m in order],
    }
    (out / "runtime.json").write_text(json.dumps(payload, indent=2) + "\n")
    print(f"{len(order)} pinned models, {len(runs)} measured; "
          f"{min(speedups):.3f}x to {max(speedups):.3f}x, "
          f"median {median(speedups):.3f}x")


if __name__ == "__main__":
    main()
