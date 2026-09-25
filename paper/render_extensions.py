#!/usr/bin/env python3
"""Collect checked reference sizes and render the native-extension comparison."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
PAPER = ROOT / "paper"
sys.path.insert(0, str(ROOT / "artifact/figures"))
from common import COLORS, PERFORMANCE_SIZE, configure
import matplotlib.pyplot as plt
import numpy as np

SYSTEMS = ("Joggle", "MLIR", "xDSL")
GROUPS = (
    ("Definition", ("def-parametric-type", "def-quantized-op"), ("Type", "Q-op")),
    ("Analysis", ("ana-broadcast-shape", "ana-numeric-range"), ("Shape", "Range")),
    ("Rewrite", ("rew-add-zero", "rew-redundant-cast"), ("Zero", "Cast")),
    ("Conversion", ("con-gelu-expand", "con-quant-expand"), ("GELU", "Q-add")),
    ("Emission", ("emit-graph-manifest", "emit-kernel-wrapper"), ("Graph", "Wrap")),
    ("Cross-stage", ("vert-int4", "vert-fused-op"), ("Int4", "Fuse")),
)
CSV = PAPER / "data/extension-size.csv"


def collect(manifest: Path) -> None:
    reports = json.loads(manifest.read_text())["admitted_references"]
    rows = []
    for report_path in reports:
        report = json.loads(Path(report_path).read_text())
        source = Path(report["source"])
        raw = source.read_bytes()
        digest = hashlib.sha256(raw).hexdigest()
        if not report.get("passed") or digest != report["source_sha256"]:
            raise ValueError(f"reference is not the checked implementation: {source}")
        rows.append(dict(task=report["task"], system=report["system"],
                         nonempty_lines=sum(bool(x.strip()) for x in raw.decode().splitlines()),
                         utf8_bytes=len(raw), source=str(source.relative_to(ROOT)),
                         source_sha256=digest, oracle_passed="true",
                         report=str(Path(report_path).relative_to(ROOT))))
    validate(rows)
    with CSV.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    CSV.with_suffix(".json").write_text(json.dumps({
        "metric": "nonempty physical source lines, including comments and imports",
        "scope": "task-specific reference file; excludes shared drivers, fixtures and build files",
        "population": "12 tasks x 3 systems; all admitted native references",
        "interpretation": "observed implementation size, not development time or agent performance",
        "analysis_status": "descriptive analysis added after agent collection was inspected",
        "uncertainty": "none: literal source counts, one implementation per condition",
        "format_sensitivity": "UTF-8 byte counts included as a separate descriptive measure",
        "admission_manifest": str(manifest.resolve().relative_to(ROOT)),
        "csv_sha256": hashlib.sha256(CSV.read_bytes()).hexdigest(),
    }, indent=2) + "\n")


def validate(rows):
    expected = {(task, system) for _, tasks, _ in GROUPS for task in tasks for system in SYSTEMS}
    indexed = {(r["task"], r["system"]): r for r in rows}
    if len(rows) != 36 or set(indexed) != expected:
        raise ValueError("require the complete 12-task, three-system population")
    for row in rows:
        raw = (ROOT / row["source"]).read_bytes()
        if (str(row["oracle_passed"]) != "true" or
                hashlib.sha256(raw).hexdigest() != row["source_sha256"] or
                int(row["nonempty_lines"]) != sum(bool(x.strip()) for x in raw.decode().splitlines()) or
                int(row["utf8_bytes"]) != len(raw)):
            raise ValueError(f"source/count mismatch: {row['source']}")
    return indexed


def render():
    with CSV.open() as stream:
        rows = list(csv.DictReader(stream))
    indexed = validate(rows)
    meta = json.loads(CSV.with_suffix(".json").read_text())
    if hashlib.sha256(CSV.read_bytes()).hexdigest() != meta["csv_sha256"]:
        raise ValueError("CSV differs from collected counts")
    configure()
    with plt.rc_context({"font.size": 5.5, "axes.titlesize": 5.5,
                         "axes.labelsize": 5.5, "xtick.labelsize": 5,
                         "ytick.labelsize": 5, "hatch.linewidth": .25,
                         "savefig.bbox": None}):
        fig, axes = plt.subplots(2, 3, figsize=PERFORMANCE_SIZE)
        for i, (axis, (family, tasks, labels)) in enumerate(zip(axes.flat, GROUPS)):
            for j, system in enumerate(SYSTEMS):
                x = np.arange(2) + (j - 1) * .24
                values = [int(indexed[task, system]["nonempty_lines"]) for task in tasks]
                axis.bar(x, values, .24, color=COLORS[system],
                         hatch=(None, "\\\\", "..")[j], edgecolor="#26333D",
                         linewidth=.3, zorder=3, label=system)
                for xx, value in zip(x, values):
                    axis.text(xx, value + 1, str(value), ha="center", va="bottom", fontsize=4.3)
            axis.set_title(f"({chr(97+i)}) {family}", loc="left", pad=2)
            axis.set_xticks(range(2), labels)
            axis.set_xlim(-.55, 1.55)
            axis.set_ylim(0, 104)
            axis.set_yticks((0, 50, 100))
            axis.grid(axis="y", color="#DDE2E8", linewidth=.35)
            axis.set_axisbelow(True)
            axis.tick_params(direction="in", top=True, right=True, length=2, width=.4, pad=1.1)
            if i % 3 == 0:
                axis.set_ylabel("Source lines", labelpad=1.3)
        handles, labels = axes[0, 0].get_legend_handles_labels()
        fig.legend(handles, labels, ncol=3, frameon=False, loc="upper center",
                   bbox_to_anchor=(.54, 1.005), fontsize=5.5, handlelength=1.15,
                   handletextpad=.35, columnspacing=1.3)
        fig.subplots_adjust(left=.105, right=.992, bottom=.095, top=.835, wspace=.31, hspace=.55)
        output = PAPER / "figures/extension-size"
        fig.savefig(output.with_suffix(".pdf"), bbox_inches=None)
        fig.savefig(output.with_suffix(".png"), dpi=600, bbox_inches=None)
        plt.close(fig)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--collect", type=Path, metavar="MANIFEST")
    args = parser.parse_args()
    if args.collect:
        collect(args.collect)
    render()
