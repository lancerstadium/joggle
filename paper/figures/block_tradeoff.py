#!/usr/bin/env python3
"""Render the descriptive RQ4 block-policy pilot figure from committed data."""

from __future__ import annotations

import argparse
import json
import platform
from pathlib import Path

import matplotlib
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "paper" / "data"
DEFAULT_OUT = ROOT / "paper" / "figures"
MODELS = [
    ("MobileNetV2-7", DATA / "mobilenetv2-block-pilot.csv"),
    ("SqueezeNet-1.1", DATA / "squeezenet-block-pilot.csv"),
    ("UltraFace-RFB-320", DATA / "ultraface-block-pilot.csv"),
]
COLORS = ["#0072B2", "#E69F00", "#009E73"]


def read_runs(model: str, path: Path) -> pd.DataFrame:
    frame = pd.read_csv(path)
    required = {"baseline_seconds", "candidate_seconds"}
    missing = required - set(frame.columns)
    if missing:
        raise ValueError(f"{path}: missing columns {sorted(missing)}")
    if len(frame) == 0 or (frame[list(required)] <= 0).any().any():
        raise ValueError(f"{path}: timings must be nonempty and positive")
    error_columns = [column for column in frame if column.startswith("max_")]
    if not error_columns or (frame[error_columns] != 0).any().any():
        raise ValueError(f"{path}: candidate and baseline outputs must match exactly")
    frame = frame.copy()
    frame["model"] = model
    frame["paired_ratio"] = frame["baseline_seconds"] / frame["candidate_seconds"]
    return frame


def summarize(runs: pd.DataFrame, artifacts: pd.DataFrame) -> pd.DataFrame:
    rows: list[dict[str, object]] = []
    for model, _ in MODELS:
        group = runs[runs["model"] == model]
        artifact = artifacts[artifacts["model"] == model]
        if len(artifact) != 1:
            raise ValueError(f"artifact metadata must contain one row for {model}")
        baseline_bytes = int(artifact.iloc[0]["baseline_c_bytes"])
        candidate_bytes = int(artifact.iloc[0]["candidate_c_bytes"])
        ratios = group["paired_ratio"]
        rows.append(
            {
                "model": model,
                "observations": len(group),
                "baseline_median_ms": 1000 * group["baseline_seconds"].median(),
                "candidate_median_ms": 1000 * group["candidate_seconds"].median(),
                "paired_ratio_median": ratios.median(),
                "paired_ratio_q1": ratios.quantile(0.25),
                "paired_ratio_q3": ratios.quantile(0.75),
                "candidate_faster_pairs": int((ratios > 1).sum()),
                "baseline_c_bytes": baseline_bytes,
                "candidate_c_bytes": candidate_bytes,
                "c_source_ratio": candidate_bytes / baseline_bytes,
            }
        )
    return pd.DataFrame(rows)


def style() -> None:
    matplotlib.rcParams.update(
        {
            "font.family": "Arial",
            "font.size": 10,
            "axes.labelsize": 10,
            "axes.titlesize": 11,
            "xtick.labelsize": 10,
            "ytick.labelsize": 10,
            "axes.linewidth": 0.6,
            "lines.linewidth": 0.8,
            "pdf.fonttype": 42,
            "ps.fonttype": 42,
        }
    )


def render(runs: pd.DataFrame, summary: pd.DataFrame, out: Path) -> None:
    style()
    width = 178 / 25.4
    fig, axes = plt.subplots(
        1,
        2,
        figsize=(width, 2.75),
        gridspec_kw={"width_ratios": [1.55, 1]},
        constrained_layout=True,
    )
    rng = np.random.default_rng(20260914)

    left = axes[0]
    for index, ((model, _), color) in enumerate(zip(MODELS, COLORS, strict=True)):
        values = runs.loc[runs["model"] == model, "paired_ratio"].to_numpy()
        jitter = rng.uniform(-0.14, 0.14, size=len(values))
        left.scatter(
            index + jitter,
            values,
            s=12,
            facecolor=color,
            edgecolor="white",
            linewidth=0.35,
            alpha=0.72,
            zorder=2,
        )
        row = summary.loc[summary["model"] == model].iloc[0]
        left.vlines(
            index,
            row["paired_ratio_q1"],
            row["paired_ratio_q3"],
            color="black",
            linewidth=2.0,
            zorder=3,
        )
        left.hlines(
            row["paired_ratio_median"],
            index - 0.19,
            index + 0.19,
            color="black",
            linewidth=1.2,
            zorder=4,
        )
    left.axhline(1, color="#666666", linestyle="--", linewidth=0.7, zorder=1)
    left.set_xticks(range(len(MODELS)), ["MobileNetV2", "SqueezeNet", "UltraFace"])
    left.set_ylabel("Paired latency ratio (baseline / candidate)")
    left.set_title("A  Function-body block policy")

    right = axes[1]
    positions = np.arange(len(MODELS))
    ratios = summary["c_source_ratio"].to_numpy()
    for position, ratio, color in zip(positions, ratios, COLORS, strict=True):
        right.hlines(position, 1, ratio, color=color, linewidth=2)
        right.scatter(ratio, position, s=28, color=color, edgecolor="white", linewidth=0.4)
        right.text(ratio + 0.035, position, f"{ratio:.2f}×", va="center", fontsize=10)
    right.axvline(1, color="#666666", linestyle="--", linewidth=0.7)
    right.set_yticks(positions, ["MobileNetV2", "SqueezeNet", "UltraFace"])
    right.invert_yaxis()
    right.set_xlim(0.95, max(ratios) + 0.22)
    right.set_xlabel("Candidate / baseline C source bytes")
    right.set_title("B  Generated-source cost")

    for ax in axes:
        ax.spines[["top", "right"]].set_visible(False)
        ax.grid(axis="y" if ax is left else "x", color="#DDDDDD", linewidth=0.45)
        ax.set_axisbelow(True)

    fig.savefig(out / "Fig1.pdf", bbox_inches="tight")
    fig.savefig(out / "Fig1.png", dpi=600, bbox_inches="tight")
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    runs = pd.concat([read_runs(model, path) for model, path in MODELS], ignore_index=True)
    artifacts = pd.read_csv(DATA / "block-artifact-pilot.csv")
    if set(artifacts["model"]) != {model for model, _ in MODELS}:
        raise ValueError("artifact metadata model set does not match timing inputs")
    summary = summarize(runs, artifacts)
    summary.to_csv(args.out / "Fig1-summary.csv", index=False, float_format="%.9f")
    render(runs, summary, args.out)
    (args.out / "Fig1-environment.json").write_text(
        json.dumps(
            {
                "python": platform.python_version(),
                "matplotlib": matplotlib.__version__,
                "numpy": np.__version__,
                "pandas": pd.__version__,
                "platform": platform.platform(),
                "random_seed": 20260914,
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
