#!/usr/bin/env python3
"""Render the complete, provenance-checked agent matrix as six grouped-bar panels."""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from merge_agent_rows import audited_assembly, sha256, summarize, success_intervals
from common import COLORS, PERFORMANCE_FONT_SIZE, PERFORMANCE_SIZE, configure

FAMILIES = ("definition", "analysis", "rewrite", "conversion", "emission", "vertical")
FAMILY_LABELS = ("Def", "Ana", "Rew", "Conv", "Emit", "Vert")
SYSTEMS = ("Joggle", "MLIR", "xDSL")


def draw(summary: list[dict], output: Path, *, test_preview: bool = False) -> None:
    """Internal renderer; the command-line entry point always audits source records."""
    configure()
    models = sorted({row["model"] for row in summary})
    if len(models) != 2 or len(summary) != 36:
        raise ValueError("expected two complete model/system/family summaries")
    indexed = {(r["model"], r["system"], r["family"]): r for r in summary}
    style = {"font.size": PERFORMANCE_FONT_SIZE, "axes.labelsize": PERFORMANCE_FONT_SIZE,
             "axes.titlesize": PERFORMANCE_FONT_SIZE, "xtick.labelsize": 4.3,
             "ytick.labelsize": 4.7, "hatch.linewidth": 0.25, "savefig.bbox": None}
    with plt.rc_context(style):
        fig, axes = plt.subplots(2, 3, figsize=PERFORMANCE_SIZE)
        x, width = np.arange(6), 0.24
        metrics = (("success_percent", "Success (%)", 1, 105, (0, 50, 100)),
                   ("completion_tokens", "Tokens (k)", 1000, 33.6, (0, 16, 32)),
                   ("tool_calls", "Tool calls", 1, 31.5, (0, 15, 30)))
        for row_index, model in enumerate(models):
            for column, (field, title, divisor, ceiling, ticks) in enumerate(metrics):
                axis = axes[row_index, column]
                for system_index, system in enumerate(SYSTEMS):
                    values = [indexed[model, system, family][field] for family in FAMILIES]
                    positions = x + (system_index - 1) * width
                    finite = [(pos, value/divisor) for pos, value in zip(positions, values)
                              if value is not None]
                    axis.bar([p for p, _ in finite], [v for _, v in finite], width,
                             color=COLORS[system], hatch=(None, "\\\\", "..")[system_index],
                             edgecolor="#26333D", linewidth=0.3, label=system, zorder=3)
                    for pos, value in zip(positions, values):
                        if value is None:
                            axis.text(pos, ceiling*0.025, "–", color=COLORS[system],
                                      ha="center", va="bottom", fontsize=5)
                        elif value == 0:
                            # A hollow circle denotes an observed zero, not a missing cost.
                            axis.plot(pos, 0, "o", markersize=1.9, markerfacecolor="white",
                                      markeredgecolor=COLORS[system], markeredgewidth=0.45,
                                      clip_on=False, zorder=4)
                axis.set_xticks(x, FAMILY_LABELS, rotation=60, ha="right")
                axis.set_ylim(0, ceiling)
                axis.set_yticks(ticks)
                axis.set_xlim(-0.55, 5.55)
                axis.set_title(f"({chr(97+row_index*3+column)}) {title}", loc="left", pad=2)
                axis.tick_params(which="both", direction="in", top=True, right=True,
                                 length=2, width=0.4, pad=1)
                axis.set_axisbelow(True)
                axis.grid(axis="y", color="#DDE2E8", linewidth=0.35)
            label = {"qwen3.5:9b": "Qwen 3.5 · 9B",
                     "qwen3:14b-q4_K_M": "Qwen 3 · 14B"}.get(model, model)
            axes[row_index, 0].set_ylabel(label, labelpad=1.2)
        handles, labels = axes[0, 0].get_legend_handles_labels()
        fig.legend(handles, labels, ncol=3, frameon=False, loc="upper center",
                   bbox_to_anchor=(0.54, 1.005), fontsize=5.5,
                   handlelength=1.15, handletextpad=0.35, columnspacing=1.3)
        fig.text(0.54, 0.005, "○ observed zero    – no successful cost sample",
                 fontsize=4.4, ha="center", va="bottom")
        fig.text(0.54, 0.04, "Cost bars average successful tasks only",
                 fontsize=4.4, ha="center", va="bottom")
        if test_preview:
            fig.text(0.5, 0.5, "LAYOUT TEST — SYNTHETIC", fontsize=11,
                     ha="center", va="center", color="#8B2635", alpha=0.6, rotation=20)
        fig.subplots_adjust(left=0.105, right=0.992, bottom=0.145, top=0.835,
                            wspace=0.32, hspace=0.83)
        output.parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(output, bbox_inches=None)
        fig.savefig(output.with_suffix(".png"), dpi=600, bbox_inches=None)
        plt.close(fig)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument("--output", type=Path, default=Path("figure-04-extension.pdf"))
    args = parser.parse_args()
    rows = audited_assembly(args.csv)
    summary = summarize(rows)
    intervals = success_intervals(rows)
    draw(summary, args.output)
    with args.output.with_suffix(".summary.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(summary[0]))
        writer.writeheader()
        writer.writerows(summary)
    args.output.with_suffix(".plot.json").write_text(json.dumps({
        "source": str(args.csv), "source_sha256": sha256(args.csv),
        "assembly_sha256": sha256(args.csv.with_suffix(".json")),
        "rows": len(rows), "layout_inches": PERFORMANCE_SIZE,
        "font_pt": PERFORMANCE_FONT_SIZE, "scale": "linear; zero origin; matched limits by column",
        "aggregation": "two-task family success; mean cost over successful tasks only",
        "missing_cost": "null in JSON; empty CSV field; dash in plot",
        "zero_success": "zero value; hollow circle on baseline",
        "cost_comparison": "conditional populations may differ; successes retained in summary CSV",
        "uncertainty": "family bars are descriptive; overall 95% stratified task-bootstrap intervals below",
        "overall_success": intervals}, indent=2, allow_nan=False)+"\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
