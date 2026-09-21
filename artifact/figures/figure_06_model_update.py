#!/usr/bin/env python3
"""Plot Figure 6 from one model-backed reactive-update CSV."""

from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

from common import COLORS, configure, number, read_rows, save, truth


def condition(row: dict[str, str]) -> str:
    if row["edit_class"] == "no_op":
        return "no-op"
    return f"{row['edit_class'].replace('_', ' ')}\n{row['edit_scope']}"


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
    conditions = sorted({condition(row) for row in rows})
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
    fig = plt.figure(figsize=(7.0, max(3.8, 0.21 * len(subjects))),
                     constrained_layout=True)
    grid = fig.add_gridspec(2, 2, width_ratios=(1.6, 1), hspace=0.38, wspace=0.35)
    heat = fig.add_subplot(grid[:, 0])
    image = heat.imshow(speed, aspect="auto", cmap="YlGnBu", vmin=1)
    heat.set_xticks(range(len(conditions)), conditions, rotation=35, ha="right")
    heat.set_yticks(range(len(subjects)), subjects)
    heat.set_title("Full / Reactive speedup")
    for i in range(len(subjects)):
        for j in range(len(conditions)):
            if np.isfinite(speed[i, j]):
                heat.text(j, i, f"{speed[i,j]:.1f}×\n{executed[i,j]:.0f}/{total[i,j]:.0f}",
                          ha="center", va="center", fontsize=5.2,
                          color="white" if speed[i, j] > np.nanmedian(speed) else "#1F2933")
    fig.colorbar(image, ax=heat, fraction=0.035, pad=0.02)

    ecdf = fig.add_subplot(grid[0, 1])
    independent: dict[str, list[float]] = defaultdict(list)
    for (_subject, _edit, _site, policy), values in summaries.items():
        if policy in {"full", "reactive"}:
            independent[policy].append(values["wall_ns"] / 1e6)
    for policy, values in independent.items():
        ordered = np.sort(values)
        ecdf.step(ordered, np.arange(1, len(ordered) + 1) / len(ordered), where="post",
                  color=COLORS[policy], label=policy)
    ecdf.set_xscale("log")
    ecdf.set_xlabel("Edit-to-result latency (ms)")
    ecdf.set_ylabel("ECDF")
    ecdf.legend(frameon=False)
    ecdf.grid(color="#E7E9EC", lw=0.5)

    breakdown = fig.add_subplot(grid[1, 1])
    reactive = [values for key, values in summaries.items() if key[3] == "reactive"]
    fields = [("select_ns", "select", "#66C2A5"),
              ("evaluate_ns", "evaluate", "#3288BD"),
              ("verify_ns", "verify", "#FDAE61")]
    bottom = np.zeros(2)
    for field, label, color in fields:
        values = np.asarray([entry[field] / 1e6 for entry in reactive])
        quantiles = np.quantile(values, (0.5, 0.95))
        breakdown.bar(("p50", "p95"), quantiles, bottom=bottom, color=color, label=label)
        bottom += quantiles
    breakdown.set_ylabel("Component time (ms)")
    breakdown.set_title("Reactive component quantiles")
    breakdown.legend(frameon=False)
    breakdown.grid(axis="y", color="#E7E9EC", lw=0.5)

    save(fig, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
