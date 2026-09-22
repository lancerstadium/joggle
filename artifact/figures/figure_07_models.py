#!/usr/bin/env python3
"""Audit, export, and plot the complete model population from native run CSVs.

The compact authoring layout uses a configurable font size. EuroSys 2027's
submission instructions require >=10 pt, including figure text; the default
7 pt authoring view is not a claim of submission-format compliance.
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

from common import COLORS, configure, read_rows, save
from figure_07_performance import LABELS, MODEL_LABELS, VARIANTS, summarize

ARTIFACT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ARTIFACT))
from merge_benchmark_rows import audited_input, convert, header, sha256, write_json
from validate_figure import performance


def write_csv(path: Path, rows: list[dict[str, object]], fields: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def assemble(sources: list[Path], output: Path) -> None:
    """Reuse the native merge adapter; additionally audit cross-run protocols."""
    if output.exists() or output.with_suffix(".merge.json").exists():
        raise ValueError(f"refusing to replace source-data export {output}")
    audited = [audited_input(path) for path in sources]
    records = [json.loads(path.with_suffix(".run.json").read_text()) for path in sources]
    shared = ("benchmark_spec_sha256", "input_index_sha256", "execution_iterations",
              "execution_batches", "warmups", "seed", "thread_environment",
              "host_controls", "model_files", "cases", "group")
    for field in shared:
        if any(record[field] != records[0][field] for record in records[1:]):
            raise ValueError(f"incompatible native runs: {field}")
    joggle = [record for record in records if record.get("variant") in VARIANTS]
    if len(joggle) != 2 or {record["variant"] for record in joggle} != set(VARIANTS):
        raise ValueError("one base and one optimized Joggle run are required")
    for field in ("git_revision", "joggle_sha256", "compiler", "compile_flags"):
        if joggle[0][field] != joggle[1][field]:
            raise ValueError(f"unmatched Joggle variants: {field}")
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
    performance(rows, True, spec, sha256(spec_path))
    wanted = {case["id"] for case in spec["model_cases"]}
    if {row["subject_kind"] for row in rows} != {"model"}:
        raise ValueError("model-only source data required")
    if {row["subject"] for row in rows} != wanted:
        raise ValueError("model population differs from the frozen manifest")
    indexed = {(row["subject"], row["variant"]): row for row in summarize(rows)}
    all_variants = (*VARIANTS, "onnxruntime")
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
    parser.add_argument("--models", type=Path, nargs=3,
                        help="Build source-data CSV from base, opt, and ORT native runs")
    parser.add_argument("--output", type=Path, default=Path("paper/figures/figure-07-models.pdf"))
    parser.add_argument("--summary", type=Path, default=Path("paper/data/figure-07-models-summary.csv"))
    parser.add_argument("--font-size", type=float, default=7.0)
    args = parser.parse_args()
    if args.models:
        assemble(args.models, args.csv)
    rows = read_rows(args.csv, header(ARTIFACT / "templates/figure-07-performance.csv"))
    validate(rows)
    summary = summarize(rows)
    write_csv(args.summary, summary, list(summary[0]))
    indexed = {(row["subject"], row["variant"]): row for row in summary}
    names = list(MODEL_LABELS)
    if set(names) != {row["subject"] for row in summary}:
        raise ValueError("labels and model population differ")
    paired = [name for name in names
              if all(indexed[name, variant]["correct"] for variant in (*VARIANTS, "onnxruntime"))]
    geometric = {variant: float(np.exp(np.mean(np.log([
        indexed[name, variant]["latency_over_ort"] for name in paired])))) for variant in VARIANTS}
    aggregate = {
        "population": len(names), "paired_correct": len(paired),
        "correct": {variant: sum(indexed[name, variant]["correct"] for name in names)
                    for variant in (*VARIANTS, "onnxruntime")},
        "geometric_latency_over_ort": geometric,
        "geometric_base_over_opt": geometric[VARIANTS[0]] / geometric[VARIANTS[1]],
        "source_csv": str(args.csv), "source_sha256": sha256(args.csv),
        "estimator": "median of 100 execution samples; whisker from median to p95",
        "normalization": "each Joggle latency divided by same-model ORT median",
        "aggregate_population": paired,
        "failed_latency": "absent, not zero; no failed model included in the geometric mean",
    }
    write_json(args.summary.with_suffix(".json"), aggregate)

    configure()
    font = args.font_size
    with plt.rc_context({"font.size": font, "savefig.bbox": None,
                         "axes.linewidth": 0.5, "legend.fontsize": font,
                         "xtick.labelsize": font, "ytick.labelsize": font}):
        fig, ax = plt.subplots(figsize=(3.35, 3.25 * max(1, font / 7)))
        markers = {VARIANTS[0]: "s", VARIANTS[1]: "o"}
        positions = np.arange(len(names))
        finite = [float(row["p95_over_ort_median"]) for row in summary
                  if row["variant"] in VARIANTS and row["correct"]]
        upper = max(finite) * 1.15
        for position, name in zip(positions, names):
            if position % 2 == 0:
                ax.axhspan(position - 0.48, position + 0.48, color="#F1F4F6", zorder=0)
            entries = [indexed[name, variant] for variant in VARIANTS]
            if all(entry["correct"] for entry in entries):
                ax.plot([entry["latency_over_ort"] for entry in entries],
                        [position + 0.13, position - 0.13], color="#C2C9CF", lw=0.6, zorder=2)
            for variant, offset in zip(VARIANTS, (0.13, -0.13)):
                row = indexed[name, variant]
                if not row["correct"]:
                    continue
                value, tail = row["latency_over_ort"], row["p95_over_ort_median"]
                ax.errorbar(value, position + offset,
                            xerr=[[0], [max(0, tail - value)]], fmt=markers[variant],
                            color=COLORS[variant], markeredgecolor="#26333D",
                            markeredgewidth=0.35, markersize=3.0, lw=0.65, capsize=1,
                            zorder=4)
            opt = indexed[name, VARIANTS[1]]
            if opt["correct"]:
                latency = opt["median_ns"] / 1e6
                label = f"{latency:.0f}" if latency >= 100 else f"{latency:.3g}"
            else:
                code = "C" if opt["reason"] == "unsupported:c.prepare" else "N"
                if any(row["reason"] not in {"unsupported:c.prepare", "unsupported:oracle:tolerance"}
                       or row["correct"] for row in entries):
                    raise ValueError(f"unhandled failure/coverage difference: {name}")
                label = "—"
                ax.text(0.52, position, f"×{code}", transform=ax.get_yaxis_transform(),
                        ha="center", va="center", fontsize=font, color="#454E58")
            ax.text(1.035, position, label, transform=ax.get_yaxis_transform(),
                    ha="left", va="center", fontsize=font)
        aggregate_y = len(names) + 0.25
        ax.axhline(len(names) - 0.38, color="#9AA4AD", lw=0.6)
        for variant, offset in zip(VARIANTS, (0.13, -0.13)):
            ax.plot(geometric[variant], aggregate_y + offset, markers[variant],
                    color=COLORS[variant], markeredgecolor="#26333D", markeredgewidth=0.35,
                    markersize=3.5, zorder=4)
        ax.axvline(1, color="#58616B", ls="--", lw=0.75)
        ax.set_xscale("log")
        ax.set_xlim(0.8, upper)
        ax.set_ylim(aggregate_y + 0.65, -0.7)
        ticks = [tick for tick in (1, 10, 100, 1000) if tick <= upper]
        ax.set_xticks(ticks, [str(tick) for tick in ticks])
        ax.set_yticks([*positions, aggregate_y],
                     [*[MODEL_LABELS[name] for name in names], f"GeoMean ({len(paired)})"])
        ax.set_xlabel("Latency / ORT  ↓  (log scale)", labelpad=2, fontsize=font)
        ax.tick_params(axis="y", length=0, pad=3)
        ax.tick_params(axis="x", which="both", length=2, pad=2)
        ax.grid(axis="x", which="major", color="#D9DFE4", lw=0.4)
        ax.spines["left"].set_visible(False)
        ax.text(1.035, 1.022, "Opt\n(ms)", transform=ax.transAxes,
                ha="left", va="bottom", fontsize=font, linespacing=1)
        handles = [Line2D([], [], marker=markers[variant], linestyle="none", markersize=3.5,
                          color=COLORS[variant], markeredgecolor="#26333D", markeredgewidth=0.35,
                          label=LABELS[variant]) for variant in VARIANTS]
        handles.append(Line2D([], [], color="#58616B", ls="--", lw=0.75, label="ORT = 1"))
        fig.legend(handles=handles, loc="upper center", ncol=3, frameon=False,
                   handletextpad=0.3, handlelength=1.1, columnspacing=0.8,
                   bbox_to_anchor=(0.5, 1.0))
        fig.text(0.03, 0.011, "×C  lowering     ×N  numerical check     n = 100", fontsize=font)
        fig.subplots_adjust(left=0.35, right=0.85, top=0.88, bottom=0.13)
        save(fig, args.output)
        plt.close(fig)
    print(json.dumps(aggregate, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
