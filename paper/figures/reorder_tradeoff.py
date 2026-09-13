#!/usr/bin/env python3
"""Render the descriptive RQ4 loop-reordering pilot from committed records."""

from __future__ import annotations

import argparse
import csv
import json
import platform
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

from audit_panel_alignment import require_matplotlib_panel_alignment


plt.rcParams["font.family"] = "sans-serif"
plt.rcParams["font.sans-serif"] = ["Arial", "DejaVu Sans", "Liberation Sans"]
plt.rcParams["svg.fonttype"] = "none"

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "paper" / "data"
DEFAULT_OUT = ROOT / "paper" / "figures"
MODELS = (
    (
        "MobileNetV2",
        DATA / "reorder-mobilenetv2-runtime-pilot.csv",
        DATA / "reorder-mobilenetv2-pilot.csv",
    ),
    (
        "SqueezeNet",
        DATA / "reorder-squeezenet-runtime-pilot.csv",
        DATA / "reorder-squeezenet-pilot.csv",
    ),
    (
        "UltraFace",
        DATA / "reorder-ultraface-runtime-pilot.csv",
        DATA / "reorder-ultraface-pilot.csv",
    ),
)
SIGNAL = "#0F4D92"
SIGNAL_SOFT = "#8CB4D9"
NEUTRAL = "#767676"
TEXT = "#272727"


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        raise ValueError(f"{path}: expected at least one record")
    return rows


def quantile(values: np.ndarray, q: float) -> float:
    return float(np.quantile(values, q, method="linear"))


def load_model(model: str, runtime_path: Path, artifact_path: Path):
    runtime = read_csv(runtime_path)
    artifacts = read_csv(artifact_path)
    if len(runtime) != 20:
        raise ValueError(f"{runtime_path}: expected 20 paired technical calls")
    if {row["model"] for row in runtime} != {model}:
        raise ValueError(f"{runtime_path}: unexpected model label")

    baseline = next((row for row in artifacts if row["variant"] == "baseline"), None)
    candidate = next((row for row in artifacts if row["variant"] == "reorder"), None)
    if baseline is None or candidate is None or len(artifacts) != 2:
        raise ValueError(f"{artifact_path}: expected baseline and reorder records")
    if int(candidate["changed"]) != 1 or int(candidate["loops_changed"]) <= 0:
        raise ValueError(f"{artifact_path}: reordering did not change a loop body")

    baseline_hash = baseline["c_sha256"]
    candidate_hash = candidate["c_sha256"]
    observations = []
    for row in runtime:
        if row["baseline_c_sha256"] != baseline_hash:
            raise ValueError(f"{runtime_path}: baseline source hash mismatch")
        if row["candidate_c_sha256"] != candidate_hash:
            raise ValueError(f"{runtime_path}: candidate source hash mismatch")
        errors = [float(value) for key, value in row.items() if key.startswith("max_")]
        if not errors or any(value != 0 for value in errors):
            raise ValueError(f"{runtime_path}: paired outputs are not bit-identical")
        baseline_seconds = float(row["baseline_seconds"])
        candidate_seconds = float(row["candidate_seconds"])
        if baseline_seconds <= 0 or candidate_seconds <= 0:
            raise ValueError(f"{runtime_path}: timings must be positive")
        observations.append(
            {
                "model": model,
                "iteration": int(row["iteration"]),
                "paired_ratio": baseline_seconds / candidate_seconds,
            }
        )

    ratios = np.asarray([float(row["paired_ratio"]) for row in observations])
    baseline_bytes = int(baseline["c_bytes"])
    candidate_bytes = int(candidate["c_bytes"])
    summary = {
        "model": model,
        "technical_pairs": len(observations),
        "candidate_faster_pairs": int(np.count_nonzero(ratios > 1.0)),
        "paired_ratio_median": float(np.median(ratios)),
        "paired_ratio_q1": quantile(ratios, 0.25),
        "paired_ratio_q3": quantile(ratios, 0.75),
        "paired_ratio_min": float(ratios.min()),
        "paired_ratio_max": float(ratios.max()),
        "loops_changed": int(candidate["loops_changed"]),
        "baseline_c_bytes": baseline_bytes,
        "candidate_c_bytes": candidate_bytes,
        "c_source_growth_percent": 100.0 * (candidate_bytes / baseline_bytes - 1.0),
        "runtime_revision": runtime[0]["revision"],
        "artifact_revision": candidate["revision"],
        "baseline_c_sha256": baseline_hash,
        "candidate_c_sha256": candidate_hash,
    }
    return observations, summary


def configure_style() -> None:
    plt.rcParams.update(
        {
            "font.family": "sans-serif",
            "font.sans-serif": ["Arial", "DejaVu Sans", "Liberation Sans"],
            "svg.fonttype": "none",
            "font.size": 7,
            "axes.labelsize": 7,
            "xtick.labelsize": 6.5,
            "ytick.labelsize": 6.5,
            "axes.linewidth": 0.6,
            "lines.linewidth": 0.9,
            "pdf.fonttype": 42,
            "ps.fonttype": 42,
        }
    )


def add_panel_label(ax: plt.Axes, label: str) -> None:
    ax.text(
        -0.18,
        1.04,
        label,
        transform=ax.transAxes,
        fontsize=8,
        fontweight="bold",
        ha="left",
        va="bottom",
        color=TEXT,
    )


def render(observations, summaries, out: Path) -> None:
    configure_style()
    model_names = [str(row["model"]) for row in summaries]
    fig, axes = plt.subplots(
        1,
        2,
        figsize=(7.007874, 2.42),
        gridspec_kw={"width_ratios": [1.18, 1.0]},
    )
    fig.subplots_adjust(left=0.095, right=0.98, bottom=0.19, top=0.92, wspace=0.42)

    ax_ratio, ax_growth = axes
    for index, model in enumerate(model_names):
        values = np.asarray(
            [float(row["paired_ratio"]) for row in observations if row["model"] == model]
        )
        x = index + np.linspace(-0.105, 0.105, num=len(values))
        ax_ratio.scatter(
            x,
            values,
            s=12,
            facecolor=SIGNAL_SOFT,
            edgecolor="white",
            linewidth=0.3,
            alpha=0.88,
            zorder=2,
        )
        summary = summaries[index]
        ax_ratio.vlines(
            index,
            float(summary["paired_ratio_q1"]),
            float(summary["paired_ratio_q3"]),
            color=TEXT,
            linewidth=2.2,
            zorder=3,
        )
        median = float(summary["paired_ratio_median"])
        ax_ratio.hlines(
            median,
            index - 0.16,
            index + 0.16,
            color=TEXT,
            linewidth=1.1,
            zorder=4,
        )
        ax_ratio.text(
            index,
            float(summary["paired_ratio_max"]) + 0.19,
            f"{median:.2f}x",
            ha="center",
            va="bottom",
            fontsize=6.5,
            color=TEXT,
        )
    ax_ratio.axhline(1.0, color=NEUTRAL, linestyle=(0, (3, 2)), linewidth=0.7)
    ax_ratio.text(2.43, 1.08, "No change", ha="right", va="bottom", fontsize=5.8, color=NEUTRAL)
    ax_ratio.set_xticks(range(3), model_names)
    ax_ratio.set_ylabel("Paired latency ratio (baseline / reordered)")
    ax_ratio.set_ylim(0.8, 6.78)
    ax_ratio.set_xlim(-0.45, 2.45)
    add_panel_label(ax_ratio, "a")

    y = np.arange(len(summaries))
    growth = np.asarray([float(row["c_source_growth_percent"]) for row in summaries])
    ax_growth.barh(y, growth, height=0.42, color=SIGNAL_SOFT, edgecolor=SIGNAL, linewidth=0.6)
    for position, summary in zip(y, summaries, strict=True):
        value = float(summary["c_source_growth_percent"])
        ax_growth.text(
            value + 0.008,
            position,
            f"{value:.3f}%  ({summary['loops_changed']} loops)",
            ha="left",
            va="center",
            fontsize=6.3,
            color=TEXT,
        )
    ax_growth.set_yticks(y, model_names)
    ax_growth.invert_yaxis()
    ax_growth.set_xlim(0, 0.44)
    ax_growth.set_xlabel("Generated C source growth")
    ax_growth.set_xticks([0, 0.1, 0.2, 0.3, 0.4], ["0", "0.1%", "0.2%", "0.3%", "0.4%"])
    add_panel_label(ax_growth, "b")

    for ax in axes:
        ax.spines[["top", "right"]].set_visible(False)
        ax.tick_params(width=0.6, length=2.5)

    qa = out / "qa"
    qa.mkdir(parents=True, exist_ok=True)
    require_matplotlib_panel_alignment(
        fig,
        axes=list(axes),
        panel_ids=["a", "b"],
        row_groups=[["a", "b"]],
        exemptions=[
            {
                "panels": ["a", "b"],
                "checks": ["panel-width"],
                "reason": "The primary paired-latency panel intentionally receives more width.",
            }
        ],
        json_out=qa / "Fig1-alignment.json",
        overlay_svg=qa / "Fig1-alignment.svg",
        tolerance_pt=1.5,
        gutter_tolerance_pt=1.5,
        require_panel_labels=True,
        strict=True,
    )
    fig.savefig(out / "Fig1.svg")
    fig.savefig(out / "Fig1.pdf")
    fig.savefig(out / "Fig1.png", dpi=600)
    fig.savefig(
        out / "Fig1.tiff",
        dpi=600,
        pil_kwargs={"compression": "tiff_lzw"},
    )
    plt.close(fig)


def write_summary(path: Path, summaries) -> None:
    columns = list(summaries[0])
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=columns, lineterminator="\n")
        writer.writeheader()
        writer.writerows(summaries)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    observations = []
    summaries = []
    for model, runtime_path, artifact_path in MODELS:
        model_observations, model_summary = load_model(model, runtime_path, artifact_path)
        observations.extend(model_observations)
        summaries.append(model_summary)
    write_summary(args.out / "Fig1-summary.csv", summaries)
    render(observations, summaries, args.out)
    (args.out / "Fig1-environment.json").write_text(
        json.dumps(
            {
                "python": platform.python_version(),
                "matplotlib": matplotlib.__version__,
                "numpy": np.__version__,
                "platform": platform.platform(),
                "point_offset": "deterministic linear spacing within each model",
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
