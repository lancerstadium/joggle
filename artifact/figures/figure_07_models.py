#!/usr/bin/env python3
"""Audit, export, and plot the complete model population from native run CSVs.

The compact authoring layout uses a configurable font size. EuroSys 2027's
submission instructions require >=10 pt, including figure text; the default
6 pt authoring view is not a claim of submission-format compliance.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.lines import Line2D
from matplotlib.patches import Patch
from matplotlib.ticker import LogLocator, MultipleLocator

from common import COLORS, PERFORMANCE_SIZE, configure, read_rows, save
from figure_07_performance import HATCHES, LABELS, MODEL_LABELS, VARIANTS, plot_variants, summarize

ARTIFACT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ARTIFACT))
from merge_benchmark_rows import audited_input, audit_protocols, convert, header, sha256, write_json
from validate_figure import performance


def write_csv(path: Path, rows: list[dict[str, object]], fields: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def assemble(sources: list[Path], output: Path) -> None:
    """Reuse the native merge adapter; additionally audit cross-run protocols."""
    if output.exists() or output.with_suffix(".merge.json").exists():
        raise ValueError(f"refusing to replace source-data export {output}")
    audited = [audited_input(path) for path in sources]
    policies = {json.dumps(item["correctness_oracle"], sort_keys=True) for item in audited}
    if len(policies) != 1:
        raise ValueError("refusing to mix different numerical oracle policies")
    records = [json.loads(path.with_suffix(".run.json").read_text()) for path in sources]
    spec_path = ARTIFACT / "manifests/benchmark-cases.json"
    audit_protocols(sources, "models", json.loads(spec_path.read_text()), sha256(spec_path))
    shared = ("benchmark_spec_sha256", "input_index_sha256", "execution_iterations",
              "execution_batches", "warmups", "seed", "host_controls",
              "model_files", "cases", "group")
    rows = [row for source in sources
            for row in convert(source, "model", header(ARTIFACT / "templates/benchmark-models.csv"))]
    rows.sort(key=lambda row: (row["subject"], row["variant"], int(row["iteration"] or -1)))
    validate(rows)
    write_csv(output, rows, header(ARTIFACT / "templates/figure-07-performance.csv"))
    write_json(output.with_suffix(".merge.json"), {
        "schema": "model-performance-merge/v1", "complete_model_population": True,
        "inputs": audited, "shared_protocol": {key: records[0][key] for key in shared},
        "source_revisions": [record["git_revision"] for record in records],
        "output": {"path": str(output), "sha256": sha256(output)}, "rows": len(rows),
    })


def validate(rows: list[dict[str, str]]) -> None:
    spec_path = ARTIFACT / "manifests/benchmark-cases.json"
    spec = json.loads(spec_path.read_text())
    selected = sorted({row["variant"] for row in rows})
    performance(rows, True, spec, sha256(spec_path), selected_variants=selected)
    wanted = {case["id"] for case in spec["model_cases"]}
    if {row["subject_kind"] for row in rows} != {"model"}:
        raise ValueError("model-only source data required")
    if {row["subject"] for row in rows} != wanted:
        raise ValueError("model population differs from the frozen manifest")
    summary = summarize(rows)
    indexed = {(row["subject"], row["variant"]): row for row in summary}
    all_variants = (*plot_variants(summary), "onnxruntime")
    if set(indexed) != {(name, variant) for name in wanted for variant in all_variants}:
        raise ValueError("missing model/variant combination")
    for name in wanted:
        entries = [indexed[name, variant] for variant in all_variants]
        if len({(entry["subject_hash"], entry["input_digest"]) for entry in entries}) != 1:
            raise ValueError(f"unmatched input/model identity for {name}")
        for entry in entries:
            if entry["correct"] and entry["samples"] != spec["measurement"]["execution_iterations"]:
                raise ValueError(f"incomplete sample count for {name}/{entry['variant']}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path, help="Audited model source-data export")
    parser.add_argument("--models", type=Path, nargs="+",
                        help="Build source-data CSV from matched native model runs; base is optional")
    parser.add_argument("--output", type=Path, default=Path("paper/figures/figure-07-models.pdf"))
    parser.add_argument("--summary", type=Path, default=Path("paper/data/figure-07-models-summary.csv"))
    parser.add_argument("--font-size", type=float, default=6.0)
    args = parser.parse_args()
    if args.models:
        assemble(args.models, args.csv)
    rows = read_rows(args.csv, header(ARTIFACT / "templates/figure-07-performance.csv"))
    rows = [row for row in rows if row["subject_kind"] == "model"]
    validate(rows)
    summary = summarize(rows)
    variants = plot_variants(summary)
    write_csv(args.summary, summary, list(summary[0]))
    indexed = {(row["subject"], row["variant"]): row for row in summary}
    names = list(MODEL_LABELS)
    if set(names) != {row["subject"] for row in summary}:
        raise ValueError("labels and model population differ")
    paired = [name for name in names
              if all(indexed[name, variant]["correct"] for variant in (*variants, "onnxruntime"))]
    geometric = {variant: float(np.exp(np.mean(np.log([
        indexed[name, variant]["latency_over_ort"] for name in paired])))) if paired else None
                 for variant in variants}
    aggregate = {
        "population": len(names), "paired_correct": len(paired),
        "correct": {variant: sum(indexed[name, variant]["correct"] for name in names)
                    for variant in (*variants, "onnxruntime")},
        "geometric_latency_over_ort": geometric,
        "geometric_base_over_opt": (geometric[VARIANTS[0]] / geometric[VARIANTS[1]]
                                    if paired and all(v in variants for v in VARIANTS) else None),
        "source_csv": str(args.csv), "source_sha256": sha256(args.csv),
        "estimator": "median of 100 execution samples; whisker from median to p95",
        "normalization": "each candidate latency divided by same-model ORT median",
        "aggregate_population": paired,
        "failed_latency": "absent, not zero; no failed model included in the geometric mean",
    }
    write_json(args.summary.with_suffix(".json"), aggregate)

    configure()
    font = args.font_size
    if not np.isfinite(font) or font < 3:
        raise ValueError("font size must be finite and at least 3 points")
    with plt.rc_context({"font.size": font, "savefig.bbox": None,
                         "axes.linewidth": 0.5, "legend.fontsize": font,
                         "xtick.labelsize": font, "ytick.labelsize": font}):
        fig, axes = plt.subplots(2, 3, figsize=PERFORMANCE_SIZE,
                                 sharey=True)
        panels = [
            ("Dense CNNs", [("densenet-12", "Dense"), ("googlenet-12", "Google"),
                            ("resnet18-v1-7", "Res18")]),
            ("Mobile CNNs", [("mobilenetv2-7", "MBV2"), ("shufflenet-v2-12", "Shuffle"),
                             ("squeezenet1.1-7", "SqNet")]),
            ("Detectors", [("ssd-mobilenetv1-12", "SSD"), ("tiny-yolov3-11", "YOLOv3"),
                           ("tinyyolov2-8", "YOLOv2"), ("ultraface-rfb-320", "UFace")]),
            ("Quantized", [("efficientnet-lite4-11-int8", "Eff-I8"),
                           ("efficientnet-lite4-11-qdq", "Eff-QDQ"),
                           ("squeezenet1.0-13-qdq", "Sq-QDQ")]),
            ("Other models", [("mnist-8", "MNIST"),
                              ("xcit-tiny-12-p8-224-opset17", "XCiT")]),
            ("Aggregate", [("geomean", f"GeoMean\n{len(paired)}/{len(names)} correct")]),
        ]
        finite = [float(row[key]) for row in summary if row["variant"] in variants
                  for key in ("latency_over_ort", "p95_over_ort_median") if row[key] != ""]
        low, upper = min([1.0, *finite]) / 1.5, max([1.0, *finite]) * 1.5
        for panel_index, (ax, (title, entries)) in enumerate(zip(axes.flat, panels)):
            for position, (name, _) in enumerate(entries):
                width = 0.76 / len(variants)
                offsets = (np.arange(len(variants)) - (len(variants) - 1) / 2) * width
                for variant, offset in zip(variants, offsets):
                    row = indexed.get((name, variant))
                    if ((name == "geomean" and not paired) or
                            (name != "geomean" and row["latency_over_ort"] == "")):
                        mark = "–" if name == "geomean" or row["correct"] else "×"
                        ax.text(position + offset, 0.03, mark,
                                transform=ax.get_xaxis_transform(), ha="center", va="bottom",
                                fontsize=font - 1, color=COLORS[variant])
                        continue
                    value = geometric[variant] if name == "geomean" else row["latency_over_ort"]
                    ax.bar(position + offset, value - 1, bottom=1, width=width * 0.9,
                           color=COLORS[variant], edgecolor="#26333D", linewidth=0.35,
                           hatch=HATCHES[variant], zorder=3)
                    if name == "geomean":
                        ax.text(position + offset, value * 1.2, f"{value:.1f}",
                                ha="center", va="bottom", fontsize=font - 1.5)
                    else:
                        tail = row["p95_over_ort_median"]
                        ax.errorbar(position + offset, value,
                                    yerr=[[0], [max(0, tail - value)]], fmt="none",
                                    color="#26333D", linewidth=0.55, capsize=1, zorder=4)
            ax.axhline(1, color="#58616B", ls="--", lw=0.65, zorder=4)
            ax.set_yscale("log")
            ax.set_ylim(low, upper)
            ticks = [10.0 ** exponent for exponent in range(-6, 7)
                     if low <= 10.0 ** exponent <= upper]
            ax.set_yticks(ticks, [f"{tick:g}" for tick in ticks])
            ax.yaxis.set_minor_locator(LogLocator(base=10, subs=np.arange(2, 10)))
            ax.set_xlim(-0.6, len(entries) - 0.4)
            ax.set_xticks(range(len(entries)), [label for _, label in entries],
                          rotation=35 if len(entries) > 1 else 0,
                          ha="right" if len(entries) > 1 else "center")
            ax.xaxis.set_minor_locator(MultipleLocator(0.5))
            ax.set_title(f"({chr(97 + panel_index)}) {title}", loc="left", pad=2,
                         fontsize=font - 0.7)
            for spine in ax.spines.values():
                spine.set_visible(True)
                spine.set_linewidth(0.5)
            ax.tick_params(axis="both", which="major", direction="in", top=True,
                           right=True, labeltop=False, labelright=False,
                           labelleft=panel_index % 3 == 0, length=2.2, width=0.5,
                           pad=1.5, labelsize=font - 1.5)
            ax.tick_params(axis="both", which="minor", direction="in", top=True,
                           right=True, length=1.1, width=0.35)
            ax.grid(axis="y", which="major", color="#D9DFE4", linewidth=0.35)
            ax.set_axisbelow(True)
        handles = [Patch(facecolor=COLORS[variant], edgecolor="#26333D", linewidth=0.35,
                         hatch=HATCHES[variant],
                         label=LABELS[variant]) for variant in variants]
        handles.append(Line2D([], [], color="#58616B", ls="--", lw=0.75, label="ORT = 1"))
        fig.legend(handles=handles, loc="upper center", ncol=3, frameon=False,
                   handletextpad=0.3, handlelength=1.1, columnspacing=0.8,
                   bbox_to_anchor=(0.5, 1.0))
        fig.text(0.012, 0.54, "Latency / ORT ↓ (log)", rotation=90,
                 va="center", fontsize=font - 0.5)
        fig.text(0.12, 0.012, "Median → p95   × invalid   – no reference/aggregate", fontsize=font - 1)
        fig.subplots_adjust(left=0.13, right=0.986,
                            top=0.81 if len(handles) > 3 else 0.88, bottom=0.185,
                            wspace=0.17, hspace=0.78)
        save(fig, args.output)
        plt.close(fig)
    print(json.dumps(aggregate, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
