#!/usr/bin/env python3
"""Plot matched feature-patch footprints as dense grouped bars."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

from common import (COLORS, PERFORMANCE_FONT_SIZE, PERFORMANCE_SIZE,
                    configure, number, read_rows, save, truth)

SYSTEMS = ("Joggle", "MLIR", "xDSL")
FAMILY_ORDER = {"definition": 0, "analysis": 1, "rewrite": 2,
                "conversion": 3, "emission": 4, "vertical": 5}
TASK_LABELS = {
    "def-parametric-type": "D-type", "def-quantized-op": "D-qop",
    "ana-broadcast-shape": "A-bcast", "ana-numeric-range": "A-range",
    "rew-add-zero": "R-zero", "rew-redundant-cast": "R-cast",
    "con-gelu-expand": "C-gelu", "con-quant-expand": "C-quant",
    "emit-graph-manifest": "E-json", "emit-kernel-wrapper": "E-wrap",
    "vert-int4": "V-int4", "vert-fused-op": "V-fused",
}


def package_plot(source: Path, output: Path) -> None:
    """Show integration separately from maintenance; retain equal footprints."""
    rows = read_rows(source, {"task", "kind", "system", "source_files", "source_added",
                             "source_deleted", "publication_lines", "zones", "passed"})
    record = json.loads(source.with_suffix(".json").read_text())
    if (not record.get("matrix_complete") or record.get("output_sha256") !=
            hashlib.sha256(source.read_bytes()).hexdigest()):
        raise ValueError("package matrix is incomplete or its source hash changed")
    groups = (
        ("lowbit", "Signed i4", ("symmetric", "subtract", "msb-first"), ("Sym.", "Sub.", "Pack")),
        ("qconv", "Quantized conv.", ("ties-away", "relu6", "stride2"), ("Round", "Cap", "Stride")),
    )
    expected = {(f"{prefix}-{case}", system) for prefix, _, changes, _ in groups
                for case in ("integrate", *changes) for system in SYSTEMS}
    indexed = {(row["task"], row["system"]): row for row in rows}
    if len(rows) != len(expected) or set(indexed) != expected or not all(truth(r["passed"]) for r in rows):
        raise ValueError("require all 24 matched, correct native package implementations")
    configure()
    style = {"font.size": PERFORMANCE_FONT_SIZE, "axes.labelsize": PERFORMANCE_FONT_SIZE,
             "axes.titlesize": PERFORMANCE_FONT_SIZE, "xtick.labelsize": PERFORMANCE_FONT_SIZE-0.5,
             "ytick.labelsize": PERFORMANCE_FONT_SIZE-0.5, "hatch.linewidth": 0.25,
             "savefig.bbox": None}
    with plt.rc_context(style):
        fig, axes = plt.subplots(2, 3, figsize=PERFORMANCE_SIZE)
        hatches = (None, "\\\\", "..")
        for row_index, (prefix, name, changes, labels) in enumerate(groups):
            integration = [indexed[prefix+"-integrate", system] for system in SYSTEMS]
            values_by_panel = (
                [number(r, "source_files") for r in integration],
                [number(r, "source_added")+number(r, "source_deleted") for r in integration],
            )
            for column, values in enumerate(values_by_panel):
                axis = axes[row_index, column]
                for index, (system, value) in enumerate(zip(SYSTEMS, values)):
                    axis.bar(index, value, 0.64, color=COLORS[system], hatch=hatches[index],
                             edgecolor="#26333D", linewidth=0.35, label=system, zorder=3)
                    axis.text(index, value, f"{value:g}", ha="center", va="bottom", fontsize=5)
                axis.set_xticks(range(3), ("J", "M", "X"))
                axis.set_xlim(-0.55, 2.55)
                axis.set_ylim(0, 3.65 if column == 0 else 225)
                axis.set_yticks((0, 1, 2, 3) if column == 0 else (0, 100, 200))
            axis = axes[row_index, 2]
            x, width = np.arange(3), 0.24
            for index, system in enumerate(SYSTEMS):
                entries = [indexed[f"{prefix}-{change}", system] for change in changes]
                if any((number(r, "source_files"), number(r, "zones"), number(r, "publication_lines")) != (1, 1, 0) for r in entries):
                    raise ValueError("maintenance annotation no longer matches data")
                values = [number(r, "source_added")+number(r, "source_deleted") for r in entries]
                positions = x+(index-1)*width
                axis.bar(positions, values, width, color=COLORS[system], hatch=hatches[index],
                         edgecolor="#26333D", linewidth=0.3, zorder=3)
                for position, value in zip(positions, values):
                    axis.text(position, value+0.08+(0.65 if index == 1 else 0),
                              f"{value:g}", ha="center", va="bottom", fontsize=4.3)
            axis.set_xticks(x, labels)
            axis.set_xlim(-0.6, 2.6)
            axis.set_ylim(0, 12.8)
            axis.set_yticks((0, 4, 8, 12))
            axis.text(0.5, 0.98, "1 file · 1 owner · 0 reg.", transform=axis.transAxes,
                      ha="center", va="top", fontsize=4.35, color="#33424B")
            for column, label in enumerate(("Install · files", "Install · lines", "Change · lines")):
                axis = axes[row_index, column]
                axis.set_title(f"({chr(97+row_index*3+column)}) {label}", loc="left", pad=2)
                axis.tick_params(which="both", direction="in", top=True, right=True,
                                 length=2, width=0.4, pad=1.1)
                axis.set_axisbelow(True)
                axis.grid(axis="y", color="#DDE2E8", linewidth=0.35)
            axes[row_index, 0].set_ylabel(name, labelpad=1.3)
        handles, labels = axes[0, 0].get_legend_handles_labels()
        fig.legend(handles, labels, ncol=3, frameon=False, loc="upper center",
                   bbox_to_anchor=(0.54, 1.005), fontsize=5.5,
                   handlelength=1.15, handletextpad=0.35, columnspacing=1.3)
        fig.subplots_adjust(left=0.105, right=0.992, bottom=0.095, top=0.835,
                            wspace=0.31, hspace=0.55)
        output.parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(output, bbox_inches=None)
        fig.savefig(output.with_suffix(".png"), dpi=600, bbox_inches=None)
        output.with_suffix(".plot.json").write_text(json.dumps({
            "source_sha256": record["output_sha256"], "layout_inches": PERFORMANCE_SIZE,
            "panels": "2 features x (integration files, integration lines, maintenance lines)",
            "aggregation": "none; one observed package patch per condition; no error bars",
            "scale": "linear, zero origin, matched limits within columns", "rows": len(rows),
            "source": str(source), "font_pt": PERFORMANCE_FONT_SIZE}, indent=2)+"\n")
        plt.close(fig)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument("--output", type=Path, default=Path("figure-05-footprint.pdf"))
    parser.add_argument("--packages", action="store_true", help="plot the native-package matrix")
    args = parser.parse_args()
    if args.packages:
        package_plot(args.csv, args.output)
        return 0
    rows = read_rows(args.csv, {"system", "task", "family", "source_files",
                                "source_added", "source_deleted", "zones",
                                "registrations", "oracle_passed"})
    rows = [row for row in rows if truth(row["oracle_passed"])]
    tasks = sorted({(row["family"], row["task"]) for row in rows},
                   key=lambda pair: (FAMILY_ORDER.get(pair[0], 99), pair[1]))
    by_key = {(row["task"], row["system"]): row for row in rows}
    missing = [(task, system) for _, task in tasks for system in SYSTEMS
               if (task, system) not in by_key]
    if missing:
        raise SystemExit(f"incomplete task/system pairing: {missing[:4]}")

    configure()
    fig, axes = plt.subplots(2, 2, figsize=(3.35, 2.34))
    axes = axes.flat
    metrics = (("source_files", "(a) Files"), ("changed_lines", "(b) Changed lines"),
               ("zones", "(c) Ownership zones"), ("registrations", "(d) Registry/build"))
    x = np.arange(len(tasks))
    width = 0.24
    for axis, (metric, title) in zip(axes, metrics):
        maximum = 1.0
        for system_index, system in enumerate(SYSTEMS):
            values = []
            for _, task in tasks:
                row = by_key[(task, system)]
                value = (number(row, "source_added") + number(row, "source_deleted")
                         if metric == "changed_lines" else number(row, metric))
                values.append(value)
            maximum = max(maximum, max(values, default=0.0))
            axis.bar(x + (system_index - 1) * width, values, width=width,
                     color=COLORS[system], edgecolor="#26333D", linewidth=0.25,
                     label=system, zorder=2)
        axis.set_title(title, loc="left", fontsize=5.6, pad=1.0)
        axis.set_yscale("symlog", linthresh=1, linscale=0.55)
        axis.set_ylim(0, maximum * 1.45)
        axis.set_xticks(x, [TASK_LABELS.get(task, task) for _, task in tasks],
                        rotation=55, ha="right")
        axis.tick_params(axis="both", labelsize=3.5, pad=0.7, length=1.8)
        axis.grid(axis="y", color="#DDE2E8", linewidth=0.35, zorder=0)
        for boundary in range(2, len(tasks), 2):
            axis.axvline(boundary - 0.5, color="#C9CFD6", linewidth=0.35, zorder=1)
    axes[0].set_ylabel("count", fontsize=4.7, labelpad=1)
    axes[2].set_ylabel("count", fontsize=4.7, labelpad=1)
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, ncol=3, frameon=False, loc="outside upper center",
               fontsize=4.5, handlelength=1.0, columnspacing=0.75)
    save(fig, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
