#!/usr/bin/env python3
"""Paired analysis of the campaign: bootstrap intervals over trial pairs.

The timing driver rotates the variants within each trial, so trials are paired
and a per-trial ratio is the right unit. A percentile bootstrap over those
ratios yields an interval per model; an interval that contains one is reported
as a tie, not as a win.
"""

import csv
import json
import random
import statistics as st
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MATRIX = ROOT / "data" / "locality-matrix.csv"
OUT = ROOT / "data" / "locality-matrix-paired.json"
DRAWS = 10000
SEED = 42


def load():
    series = defaultdict(lambda: defaultdict(dict))
    with MATRIX.open(newline="") as stream:
        for row in csv.DictReader(stream):
            series[row["model"]][row["variant"]][int(row["trial"])] = \
                float(row["seconds"]) * 1000
    return series


def interval(values, draws=DRAWS, seed=SEED):
    rnd = random.Random(seed)
    medians = sorted(st.median([values[rnd.randrange(len(values))]
                                for _ in values]) for _ in range(draws))
    return medians[int(0.025 * draws)], medians[int(0.975 * draws)]


def compare(series, baseline):
    out = {}
    for model in sorted(series):
        if baseline not in series[model]:
            continue
        trials = sorted(set(series[model][baseline]) & set(series[model]["locality"]))
        ratios = [series[model][baseline][t] / series[model]["locality"][t]
                  for t in trials]
        low, high = interval(ratios)
        out[model] = {"pairs": len(ratios), "median": st.median(ratios),
                      "ci95": [low, high], "baseline": baseline,
                      "verdict": ("locality faster" if low > 1.0 else
                                  "baseline faster" if high < 1.0 else "tie")}
    return out


def main():
    series = load()
    payload = {
        "record": MATRIX.name, "draws": DRAWS, "seed": SEED,
        "unit": "per-trial ratio of the baseline to the locality artifact",
        "statistic": "median of paired ratios, percentile bootstrap interval",
        "versus_first_fit": compare(series, "base"),
        "versus_tvm": compare(series, "tvm"),
    }
    OUT.write_text(json.dumps(payload, indent=2) + "\n")
    wins = [m for m, v in payload["versus_tvm"].items() if v["verdict"] == "locality faster"]
    ties = [m for m, v in payload["versus_tvm"].items() if v["verdict"] == "tie"]
    wins_ff = [m for m, v in payload["versus_first_fit"].items()
               if v["verdict"] == "locality faster"]
    print(f"vs first-fit: {len(wins_ff)}/{len(payload['versus_first_fit'])} supported")
    print(f"vs TVM: {len(wins)} faster, {len(ties)} tie {ties}")
    print(f"wrote {OUT.name}")


if __name__ == "__main__":
    main()
