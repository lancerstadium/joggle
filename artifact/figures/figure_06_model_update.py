#!/usr/bin/env python3
"""Plot Figure 6 from one model-backed reactive-update CSV."""

from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import TwoSlopeNorm
from matplotlib.ticker import LogLocator, NullFormatter

from common import COLORS, configure, number, read_rows, save, truth


def condition(row: dict[str, str]) -> str:
    if row["edit_class"] == "no_op":
        return "no-op"
    name = {"operation_metadata": "metadata", "value_type": "value type"}[
        row["edit_class"]
    ]
    scope = {"affected": "affected cone", "unrelated": "unrelated"}[
        row["edit_scope"]
    ]
    return f"{name}\n{scope}"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument("--output", type=Path, default=Path("figure-06-model-update.pdf"))
    args = parser.parse_args()
    rows = read_rows(args.csv, {"subject", "edit_class", "edit_scope", "edit_site",
                                "policy", "iteration", "wall_ns", "select_ns",
                                "evaluate_ns", "verify_ns", "executed_stages", "stages",
                                "output_digest", "correct", "seed"})
    rows = [row for row in rows if truth(row["correct"])]
    pairing: dict[tuple[str, ...], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        key = (row["subject"], row["edit_class"], row["edit_scope"], row["edit_site"],
               row["iteration"], row["seed"])
        pairing[key].append(row)
    for key, group in pairing.items():
        if len({row["output_digest"] for row in group}) != 1:
            raise SystemExit(f"digest mismatch for {key}")

    summaries: dict[tuple[str, str, str, str], dict[str, float]] = {}
    grouped: dict[tuple[str, str, str, str], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        grouped[(row["subject"], condition(row), row["edit_site"], row["policy"])].append(row)
    for key, group in grouped.items():
        summaries[key] = {field: float(np.median([number(row, field) for row in group]))
                          for field in ("wall_ns", "select_ns", "evaluate_ns", "verify_ns",
                                        "executed_stages", "stages")}

    subjects = sorted({row["subject"] for row in rows})
    condition_order = [
        "no-op",
        "metadata\naffected cone",
        "metadata\nunrelated",
        "value type\naffected cone",
        "value type\nunrelated",
    ]
    present = {condition(row) for row in rows}
    conditions = [name for name in condition_order if name in present]
    sites = sorted({row["edit_site"] for row in rows})
    speed = np.full((len(subjects), len(conditions)), np.nan)
    executed = np.full_like(speed, np.nan)
    total = np.full_like(speed, np.nan)
    for i, subject in enumerate(subjects):
        for j, edit in enumerate(conditions):
            ratios, run, stages = [], [], []
            for site in sites:
                full = summaries.get((subject, edit, site, "full"))
                reactive = summaries.get((subject, edit, site, "reactive"))
                if full and reactive:
                    ratios.append(full["wall_ns"] / reactive["wall_ns"])
                    run.append(reactive["executed_stages"])
                    stages.append(reactive["stages"])
            if ratios:
                speed[i, j] = float(np.median(ratios))
                executed[i, j] = float(np.median(run))
                total[i, j] = float(np.median(stages))

    configure()
    fig = plt.figure(figsize=(7.0, max(4.6, 0.25 * len(subjects))),
                     constrained_layout=True)
    grid = fig.add_gridspec(
        3, 2, width_ratios=(1.38, 1.08), height_ratios=(1.7, 1, 1),
        hspace=0.28, wspace=0.27,
    )
    heat = fig.add_subplot(grid[:, 0])
    log_speed = np.log2(speed)
    image = heat.imshow(
        log_speed,
        aspect="auto",
        cmap="RdBu",
        norm=TwoSlopeNorm(vmin=-2, vcenter=0, vmax=10),
    )
    heat.set_xticks(range(len(conditions)), conditions, rotation=35, ha="right")
    heat.set_yticks(range(len(subjects)), subjects)
    heat.set_title("(a) Full / Reactive · speedup and stages run", loc="left")
    for i in range(len(subjects)):
        for j in range(len(conditions)):
            if np.isfinite(speed[i, j]):
                saturated = log_speed[i, j] < -1.25 or log_speed[i, j] > 8
                value = speed[i, j]
                label = f"{value:.1f}×" if value < 10 else (
                    f"{value:.0f}×" if value < 1000 else f"{value / 1000:.1f}k×"
                )
                heat.text(
                    j,
                    i,
                    f"{label}\n{executed[i,j]:.0f}/{total[i,j]:.0f}",
                    ha="center",
                    va="center",
                    fontsize=5.2,
                    color="white" if saturated else "#1F2933",
                )
    colorbar = fig.colorbar(image, ax=heat, fraction=0.035, pad=0.02)
    colorbar.set_ticks((-2, 0, 2, 6, 10),
                       labels=("0.25×", "1×", "4×", "64×", "1024×+"))

    ablation = fig.add_subplot(grid[0, 1])
    policy_order = ("reactive", "whole-mod", "no-plan-cache")
    policy_labels = {
        "reactive": "Reactive",
        "whole-mod": "Whole-mod",
        "no-plan-cache": "No plan cache",
    }
    policy_colors = {
        "reactive": COLORS["reactive"],
        "whole-mod": "#8E6BBE",
        "no-plan-cache": "#E08B3E",
    }
    offsets = {"reactive": -0.22, "whole-mod": 0.0, "no-plan-cache": 0.22}
    for policy in policy_order:
        for index, edit in enumerate(conditions):
            ratios = []
            for subject in subjects:
                for site in sites:
                    full = summaries.get((subject, edit, site, "full"))
                    candidate = summaries.get((subject, edit, site, policy))
                    if full and candidate and candidate["wall_ns"] > 0:
                        ratios.append(full["wall_ns"] / candidate["wall_ns"])
            if not ratios:
                continue
            low, center, high = np.quantile(ratios, (0.25, 0.5, 0.75))
            y = index + offsets[policy]
            ablation.plot((low, high), (y, y), color=policy_colors[policy], lw=1.1)
            ablation.scatter(center, y, s=12, color=policy_colors[policy], zorder=3,
                             label=policy_labels[policy] if index == 0 else None)
    ablation.axvline(1, color="#6B7280", lw=0.7, ls="--")
    ablation.set_xscale("log")
    compact_conditions = {
        "no-op": "no-op",
        "metadata\naffected cone": "metadata / cone",
        "metadata\nunrelated": "metadata / other",
        "value type\naffected cone": "type / cone",
        "value type\nunrelated": "type / other",
    }
    ablation.set_yticks(range(len(conditions)),
                        [compact_conditions[name] for name in conditions])
    ablation.set_ylim(len(conditions) - 0.5, -1.5)
    ablation.tick_params(axis="y", labelsize=5.5)
    ablation.set_xlabel("Full / policy latency (median, IQR)")
    ablation.set_title("(b) Policy ablation", loc="left")
    ablation.grid(axis="x", color="#E7E9EC", lw=0.5)
    for x, policy in zip((0.04, 0.36, 0.70), policy_order):
        ablation.text(x, 0.94, policy_labels[policy], color=policy_colors[policy],
                      fontsize=5.3, transform=ablation.transAxes, va="top")

    ecdf = fig.add_subplot(grid[1, 1])
    independent: dict[str, list[float]] = defaultdict(list)
    for (_subject, _edit, _site, policy), values in summaries.items():
        if policy in {"full", "reactive"}:
            independent[policy].append(values["wall_ns"] / 1e6)
    for policy, values in independent.items():
        ordered = np.sort(values)
        ecdf.step(ordered, np.arange(1, len(ordered) + 1) / len(ordered), where="post",
                  color=COLORS[policy], label=policy)
    ecdf.set_xscale("log")
    ecdf.xaxis.set_major_locator(LogLocator(base=10, numticks=4))
    ecdf.xaxis.set_minor_formatter(NullFormatter())
    ecdf.set_xlabel("Edit-to-result latency (ms)")
    ecdf.set_ylabel("ECDF")
    ecdf.legend(frameon=False, fontsize=6, ncol=2, loc="lower right")
    ecdf.grid(color="#E7E9EC", lw=0.5)
    ecdf.set_title("(c) Edit-to-result distribution", loc="left")

    breakdown = fig.add_subplot(grid[2, 1])
    reactive = [values for key, values in summaries.items() if key[3] == "reactive"]
    fields = [("select_ns", "select", "#66C2A5"),
              ("evaluate_ns", "evaluate", "#3288BD"),
              ("verify_ns", "verify", "#FDAE61")]
    cases = np.asarray([[entry[field] / 1e6 for field, _label, _color in fields]
                        for entry in reactive])
    case_totals = np.sum(cases, axis=1)
    targets = np.quantile(case_totals, (0.5, 0.95))
    selected = [int(np.argmin(np.abs(case_totals - target))) for target in targets]
    components = cases[selected].T
    totals = case_totals[selected]
    shares = components / totals
    bottom = np.zeros(2)
    for index, (_field, label, color) in enumerate(fields):
        breakdown.bar(("p50", "p95"), shares[index], bottom=bottom,
                      color=color)
        if shares[index, 0] >= 0.12:
            breakdown.text(
                0, bottom[0] + shares[index, 0] / 2, label,
                ha="center", va="center", fontsize=5.5,
            )
        bottom += shares[index]
    for index, total in enumerate(totals):
        breakdown.text(index, 1.02, f"{total:.2g} ms", ha="center", va="bottom",
                       fontsize=6.5)
    breakdown.set_ylim(0, 1.12)
    breakdown.set_ylabel("Component share")
    breakdown.set_title("(d) Reactive time composition", loc="left")
    breakdown.grid(axis="y", color="#E7E9EC", lw=0.5)

    save(fig, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
