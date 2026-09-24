#!/usr/bin/env python3
"""Export the completed repeated-update batch and render its figure and tables.

First import: python3 paper/render_update.py --collect PATH_TO_COMPLETED_BATCH
Reproduction: python3 paper/render_update.py
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import Patch
from matplotlib.ticker import FuncFormatter, LogLocator, NullFormatter
from matplotlib.colors import to_rgb

PAPER = Path(__file__).resolve().parent
sys.path.insert(0, str(PAPER.parent / "artifact" / "figures"))
from common import COLORS as SYSTEM_COLORS, PERFORMANCE_FONT_SIZE, PERFORMANCE_SIZE, SYSTEM_HATCHES

DATA = PAPER / "data"
SYSTEMS = ("joggle", "tvm", "onnx-mlir")
LABELS = ("Joggle", "TVM", "ONNX-MLIR")
COLORS = tuple(SYSTEM_COLORS[system] for system in SYSTEMS)
MODELS = ("densenet-12", "squeezenet1.1-7", "tiny-yolov3-11")
NAMES = ("DenseNet-121", "SqueezeNet-1.1", "TinyYOLOv3")
POLICIES = ("rebuild", "update")
STAGES = ("decode_specialize", "parse", "lower", "emit", "host_compile", "bind")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_csv(path):
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream))


def write_csv(path, rows):
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def audit(rows):
    indexed = {}
    identities = defaultdict(set)
    for row in rows:
        key = tuple(row[k] for k in ("backend", "case_id", "edit_id", "iteration", "policy"))
        if key in indexed:
            raise ValueError(f"duplicate measurement {key}")
        indexed[key] = row
        if row["correct"] not in ("true", "false"):
            raise ValueError(f"invalid oracle status {key}")
        if row["correct"] == "true" and (int(row["ready_ns"]) <= 0 or not row["output_digest"]):
            raise ValueError(f"invalid timing or digest {key}")
        for field in ("edit_sha256", "model_sha256", "input_digest"):
            if row[field]:
                identities[(row["case_id"], row["edit_id"], field)].add(row[field])
            elif row["correct"] == "true":
                raise ValueError(f"passing measurement lacks {field}: {key}")
    if any(len(v) != 1 for v in identities.values()):
        raise ValueError("cross-system edit, model, or input mismatch")
    wanted = set()
    for model in MODELS:
        edits = {r["edit_id"] for r in rows if r["case_id"] == model}
        if len(edits) != 3:
            raise ValueError(f"expected three edits for {model}")
        wanted.update((s, model, e, str(i), p) for s in SYSTEMS for e in edits
                      for i in range(10) for p in POLICIES)
    if set(indexed) != wanted:
        raise ValueError(f"incomplete batch: {len(indexed)}/540 rows")
    for key, row in indexed.items():
        other = indexed[(*key[:-1], "update" if key[-1] == "rebuild" else "rebuild")]
        if row["correct"] == other["correct"] == "true":
            if row["output_digest"] != other["output_digest"]:
                raise ValueError(f"paired output mismatch {key}")
    for system in SYSTEMS:
        if len({r["compiler_identity_sha256"] for r in rows
                if r["backend"] == system and r["compiler_identity_sha256"]}) != 1:
            raise ValueError(f"mixed compiler identity: {system}")
    return indexed


def source_export(directory):
    rows, stages, sources = [], [], []
    for system in SYSTEMS:
        path = directory / f"{system}.csv"
        metadata = json.loads(path.with_suffix(".json").read_text())
        raw = directory / f"{system}.samples.jsonl"
        if metadata["output_sha256"] != digest(path) or metadata["raw_sha256"] != digest(raw):
            raise ValueError(f"source checksum mismatch: {system}")
        if not metadata["stable"] or metadata["dirty"]:
            raise ValueError(f"unstable collector: {system}")
        rows.extend(read_csv(path))
        sources.append({"system": system, "csv_path": str(path.resolve()),
                        "samples_path": str(raw.resolve()),
                        "record_path": str(path.with_suffix(".json").resolve()),
                        "record_sha256": digest(path.with_suffix(".json")),
                        "csv_sha256": digest(path),
                        "samples_sha256": digest(raw), "collection": metadata})
        if system == "joggle":
            for line in raw.read_text().splitlines():
                item = json.loads(line)
                sample = item["sample"]["replacement"]
                stage = {k: item["key"][k] for k in ("case_id", "edit_id", "iteration", "policy")}
                stage.update(sample["stages_ns"])
                stage["ready_ns"] = sample["ready_ns"]
                stage["prepare_ns"] = next(s["total_ns"] for s in sample["lowering_profile"]["steps"]
                                           if s["function"] == "c.prepare")
                stage["imported_instances"] = sample["lowering_profile"]["imported_instances"]
                stage["materialized_instances"] = sample["lowering_profile"]["materialized_instances"]
                stages.append(stage)
    for field in ("revision", "population_sha256", "seed", "iterations", "host"):
        if len({json.dumps(s["collection"][field], sort_keys=True) for s in sources}) != 1:
            raise ValueError(f"mixed collection controls: {field}")
    audit(rows)
    rows.sort(key=lambda r: tuple(r[k] for k in ("case_id", "edit_id", "backend", "iteration", "policy")))
    provenance = {"schema": "repeated-update-export/v1", "models": list(MODELS),
                  "edits_per_model": 3, "paired_repetitions": 10,
                  "rows": len(rows), "sources": sources,
                  "speedup_estimator": "median of within-edit paired rebuild/update ratios",
                  "whiskers": "25th to 75th percentile across ten paired repetitions"}
    return rows, stages, provenance


def collect(directory):
    rows, stages, provenance = source_export(directory)
    write_csv(DATA / "figure-06-update.csv", rows)
    write_csv(DATA / "figure-06-stages.csv", stages)
    provenance.update(csv_sha256=digest(DATA / "figure-06-update.csv"),
                      stages_sha256=digest(DATA / "figure-06-stages.csv"))
    (DATA / "figure-06-update.json").write_text(json.dumps(provenance, indent=2) + "\n")


def audit_export(directory):
    """Bind the paper export to original rows and phase records, without rerunning a compiler."""
    provenance = json.loads((directory / "figure-06-update.json").read_text())
    if provenance.get("schema") != "repeated-update-export/v1":
        raise ValueError("unexpected repeated-update export schema")
    for name, key in (("update", "csv_sha256"), ("stages", "stages_sha256")):
        if digest(directory / f"figure-06-{name}.csv") != provenance[key]:
            raise ValueError("source-data export checksum mismatch")
    sources = provenance["sources"]
    if {s["system"] for s in sources} != set(SYSTEMS) or len(sources) != len(SYSTEMS):
        raise ValueError("incomplete update provenance")
    parents = {Path(s["csv_path"]).parent for s in sources}
    if len(parents) != 1:
        raise ValueError("update source files must belong to one collected batch")
    rows, stages, expected = source_export(parents.pop())
    for key, value in expected.items():
        if provenance.get(key) != value:
            raise ValueError(f"update provenance differs from raw source: {key}")
    canonical = lambda records: [{k: str(v) for k, v in row.items()} for row in records]
    if read_csv(directory / "figure-06-update.csv") != canonical(rows):
        raise ValueError("exported update rows differ from original measurements")
    if read_csv(directory / "figure-06-stages.csv") != canonical(stages):
        raise ValueError("exported phase rows differ from raw measurements")
    return rows, provenance


def stats(values):
    return tuple(float(x) for x in np.quantile(values, (.25, .5, .75)))


def summarize(rows, indexed):
    result = []
    for model in MODELS:
        edits = sorted({r["edit_id"] for r in rows if r["case_id"] == model},
                       key=lambda e: int(e.split("-")[1]))
        for edit in edits:
            for system in SYSTEMS:
                entry = dict(case_id=model, edit_id=edit, backend=system)
                for policy in POLICIES:
                    values = [int(indexed[(system, model, edit, str(i), policy)]["ready_ns"]) / 1e9
                              for i in range(10)
                              if indexed[(system, model, edit, str(i), policy)]["correct"] == "true"]
                    entry[f"{policy}_correct"] = len(values)
                    for key, value in zip(("q25_s", "median_s", "q75_s"), stats(values) if values else ("",)*3):
                        entry[f"{policy}_{key}"] = value
                ratios = []
                for i in range(10):
                    full, update = [indexed[(system, model, edit, str(i), p)] for p in POLICIES]
                    if full["correct"] == update["correct"] == "true":
                        ratios.append(int(full["ready_ns"]) / int(update["ready_ns"]))
                for key, value in zip(("q25", "median", "q75"), stats(ratios) if ratios else ("",)*3):
                    entry[f"speedup_{key}"] = value
                result.append(entry)
    return result


def render(summary, output=None):
    font = PERFORMANCE_FONT_SIZE
    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": font, "font.stretch": "condensed",
                         "pdf.fonttype": 42, "axes.linewidth": .45,
                         "xtick.direction": "in", "ytick.direction": "in",
                         "xtick.top": True, "ytick.right": True,
                         "savefig.bbox": None})
    fig, axes = plt.subplots(2, 3, figsize=PERFORMANCE_SIZE, sharex="col", sharey="row")
    values = [r["rebuild_q75_s"] for r in summary if r["rebuild_q75_s"] != ""]
    ymax = max(values) * 1.6
    for col, (model, name) in enumerate(zip(MODELS, NAMES)):
        edits = list(dict.fromkeys(r["edit_id"] for r in summary if r["case_id"] == model))
        for x, edit in enumerate(edits):
            for j, (system, color) in enumerate(zip(SYSTEMS, COLORS)):
                row = next(r for r in summary if (r["case_id"], r["edit_id"], r["backend"]) == (model, edit, system))
                pos = x + (j - 1) * .26
                if not row["update_correct"] or not row["rebuild_correct"]:
                    for ax in axes[:, col]:
                        ax.text(pos, .03, "×", transform=ax.get_xaxis_transform(),
                                color=color, ha="center", va="bottom", fontsize=font)
                    continue
                for k, policy in enumerate(POLICIES):
                    y = row[f"{policy}_median_s"]
                    ax = axes[0, col]
                    bx = pos + (k - .5) * .115
                    face = tuple(.72 + .28 * c for c in to_rgb(color)) if k == 0 else color
                    ax.bar(bx, y - .35, bottom=.35, width=.11, color=face,
                           edgecolor="#26333D", lw=.35,
                           hatch="////" if k == 0 else SYSTEM_HATCHES[system], zorder=3)
                    ax.errorbar(bx, y, yerr=[[y-row[f"{policy}_q25_s"]], [row[f"{policy}_q75_s"]-y]],
                                fmt="none", ecolor="#26333D", elinewidth=.4, capsize=.6, zorder=4)
                y = row["speedup_median"]
                ax = axes[1, col]
                ax.bar(pos, y, width=.23, color=color, edgecolor="#26333D", lw=.3,
                       hatch=SYSTEM_HATCHES[system], zorder=3)
                ax.errorbar(pos, y, yerr=[[y-row["speedup_q25"]], [row["speedup_q75"]-y]],
                            fmt="none", ecolor="#26333D", elinewidth=.4, capsize=.7, zorder=4)
        axes[0, col].set_title(f"({chr(97+col)}) {name}", loc="left", fontsize=font, pad=2)
        axes[1, col].set_title(f"({chr(100+col)}) Reuse gain", loc="left", fontsize=font, pad=2)
        axes[0, col].set_yscale("log")
        axes[0, col].set_ylim(.35, ymax)
        axes[0, col].yaxis.set_major_locator(LogLocator(base=10))
        axes[0, col].yaxis.set_major_formatter(FuncFormatter(lambda value, _: f"{value:g}"))
        axes[0, col].yaxis.set_minor_locator(LogLocator(base=10, subs=(2, 5)))
        axes[0, col].yaxis.set_minor_formatter(NullFormatter())
        axes[1, col].set_ylim(0, max(3., max(r["speedup_q75"] for r in summary if r["speedup_q75"] != "")*1.2))
        axes[1, col].set_yticks([0, 1, 2, 3])
        axes[1, col].axhline(1, color="#56616C", lw=.5, ls="--", zorder=4)
        axes[1, col].set_xticks(range(3), [e.replace("node-", "").replace("-", "\n") for e in edits])
        for ax in axes[:, col]:
            ax.set_xlim(-.52, 2.52)
            ax.tick_params(which="both", labelsize=font, length=2, pad=1, width=.4)
            ax.tick_params(which="minor", length=.9)
            ax.grid(axis="y", lw=.35, color="#DDE3E8", zorder=0)
    axes[0, 0].set_ylabel("Ready (s) ↓", fontsize=font, labelpad=0)
    axes[1, 0].set_ylabel("Rebuild / update ↑", fontsize=font, labelpad=0)
    handles = [Patch(facecolor=c, edgecolor="#26333D", hatch=SYSTEM_HATCHES[system], label=s)
               for system,s,c in zip(SYSTEMS,LABELS,COLORS)]
    handles += [Patch(facecolor="#D8DCE2", edgecolor="#56616C", hatch="////", label="rebuild"),
                Patch(facecolor="#7B8494", label="update")]
    fig.legend(handles=handles, loc="upper center", ncol=len(handles), frameon=False,
               fontsize=font, columnspacing=.7, handlelength=1, handletextpad=.3,
               labelspacing=.2, bbox_to_anchor=(.53,1.02))
    fig.subplots_adjust(left=.10,right=.99,bottom=.15,top=.86,wspace=.14,hspace=.40)
    output = output or PAPER / "figures/figure-06-update.pdf"
    output.parent.mkdir(parents=True, exist_ok=True)
    for suffix in (".pdf", ".png"):
        fig.savefig(output.with_suffix(suffix), dpi=400)
    plt.close(fig)


def tables(summary):
    md = ["## Appendix D. Repeated Model Updates", "",
          "Ten paired repetitions at each of nine edit sites. Times are seconds; brackets give the interquartile range. Speedup is the median of paired rebuild/update ratios. Each pass count covers ten rebuilds and ten updates.", "",
          "| Model | Edit node | System | Rebuild [Q1, Q3] | Update [Q1, Q3] | Speedup | Pass |",
          "| --- | --- | --- | ---: | ---: | ---: | ---: |"]
    lines = [r"\section{Repeated Model Updates}", r"\label{sec:update-data}",
             r"\begin{table}[!htbp]", r"\centering\scriptsize",
             r"\caption{Ten paired repetitions at each of nine edit sites. Times are seconds; brackets give the interquartile range. Speedup is the median of paired rebuild/update ratios. Each pass count covers ten rebuilds and ten updates.}",
             r"\label{tab:update-sites}", r"\setlength{\tabcolsep}{4pt}",
             r"\renewcommand{\arraystretch}{1.12}",
             r"\begin{tabular*}{\textwidth}{@{\extracolsep{\fill}}lllrrrc@{}}",
             r"\toprule Model & Edit node & System & Rebuild [Q1, Q3] & Update [Q1, Q3] & Speedup & Pass \\", r"\midrule"]
    for i, row in enumerate(summary):
        if i and i % 9 == 0: lines.append(r"\midrule")
        model = NAMES[MODELS.index(row["case_id"])] if i % 9 == 0 else ""
        edit = row["edit_id"].removeprefix("node-") if i % 3 == 0 else ""
        parts = [model, edit, LABELS[SYSTEMS.index(row["backend"])]]
        for policy in POLICIES:
            parts.append((f"{row[f'{policy}_median_s']:.3f} [{row[f'{policy}_q25_s']:.3f}, {row[f'{policy}_q75_s']:.3f}]"
                          if row[f"{policy}_correct"] else r"$\times$"))
        parts.append(f"${row['speedup_median']:.2f}\\times$" if row["speedup_median"] != "" else "---")
        parts.append(f"{row['rebuild_correct']+row['update_correct']}/20")
        lines.append(" & ".join(parts) + r" \\")
        md.append("| " + " | ".join(p.replace(r"$\times$", "×").replace("$", "").replace(r"\times", "×") for p in parts) + " |")
    lines.extend([r"\bottomrule\end{tabular*}\end{table}"])
    stages = read_csv(DATA / "figure-06-stages.csv")
    md.extend(["", "Joggle phase medians in seconds over 30 runs per model and policy. Prepare is a component of Lower; columns have separately computed medians. Decode includes input specialization; CC is native compilation.", "",
               "| Model | Policy | Decode | Parse | Lower | Prepare | Emit | CC | Bind |",
               "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"])
    lines.extend([r"\begin{table}[!htbp]",r"\centering\scriptsize",
                  r"\caption{Joggle phase medians in seconds over 30 runs per model and policy. Prepare is a component of Lower; columns have separately computed medians. Decode includes input specialization; CC is native compilation.}",
                  r"\label{tab:update-phases}",r"\setlength{\tabcolsep}{3pt}",
                  r"\begin{tabular*}{\textwidth}{@{\extracolsep{\fill}}llrrrrrrr@{}}",
                  r"\toprule Model & Policy & Decode & Parse & Lower & Prepare & Emit & CC & Bind \\",r"\midrule"])
    for model, name in zip(MODELS,NAMES):
        for policy in POLICIES:
            selected = [r for r in stages if r["case_id"] == model and r["policy"] == policy]
            numbers = [np.median([int(r[k]) for r in selected])/1e9
                       for k in ("decode_specialize","parse","lower","prepare_ns","emit","host_compile","bind")]
            lines.append(" & ".join([name if policy == "rebuild" else "", policy, *(f"{v:.3f}" for v in numbers)]) + r" \\")
            md.append("| " + " | ".join([name, policy, *(f"{v:.3f}" for v in numbers)]) + " |")
    lines.extend([r"\bottomrule\end{tabular*}\end{table}"])
    manuscript = PAPER / "README.md"
    start, end = "<!-- UPDATE DATA BEGIN -->", "<!-- UPDATE DATA END -->"
    text = manuscript.read_text()
    block = start + "\n\n" + "\n".join(md) + "\n\n" + end
    if start in text:
        before, rest = text.split(start, 1)
        _, after = rest.split(end, 1)
        text = before + block + after
    else:
        text = text.rstrip() + "\n\n" + block + "\n"
    manuscript.write_text(text)
    (PAPER / "sections/appendix-update-data.tex").write_text("\n".join(lines) + "\n")


def main():
    global DATA
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--collect", type=Path)
    parser.add_argument("--data-dir", type=Path, default=DATA)
    parser.add_argument("--output-dir", type=Path,
                        help="render figures and summary here without rewriting manuscript files")
    args = parser.parse_args()
    DATA = args.data_dir.resolve()
    if args.collect: collect(args.collect)
    rows, provenance = audit_export(DATA)
    summary = summarize(rows, audit(rows))
    destination = args.output_dir or DATA
    destination.mkdir(parents=True, exist_ok=True)
    write_csv(destination / "figure-06-update-summary.csv", summary)
    render(summary, args.output_dir / "figure-06-update.pdf" if args.output_dir else None)
    if not args.output_dir:
        tables(summary)
    print("Rendered 540 measurements, 27 edit/system summaries, and six panels."
          + (" Updated two appendix tables." if not args.output_dir else " Manuscript unchanged."))


if __name__ == "__main__":
    main()
