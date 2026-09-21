#!/usr/bin/env python3
"""Shared, data-only helpers for the paper figure scripts."""

from __future__ import annotations

import csv
from collections.abc import Iterable
from pathlib import Path

import matplotlib.pyplot as plt


COLORS = {
    "Joggle": "#087E8B",
    "MLIR": "#2E5AAC",
    "xDSL": "#E07A2D",
    "full": "#5B6472",
    "reactive": "#087E8B",
    "whole-mod": "#8C6BB1",
    "no-plan-cache": "#C44E52",
}


def configure() -> None:
    plt.rcParams.update(
        {
            "font.family": "DejaVu Sans",
            "font.size": 7.5,
            "axes.labelsize": 7.5,
            "axes.titlesize": 8,
            "legend.fontsize": 6.5,
            "xtick.labelsize": 6.5,
            "ytick.labelsize": 6.5,
            "axes.spines.top": False,
            "axes.spines.right": False,
            "pdf.fonttype": 42,
            "ps.fonttype": 42,
            "savefig.bbox": "tight",
        }
    )


def read_rows(path: Path, required: Iterable[str]) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        fields = set(reader.fieldnames or [])
        missing = sorted(set(required) - fields)
        if missing:
            raise SystemExit(f"{path}: missing columns: {', '.join(missing)}")
        rows = list(reader)
    if not rows:
        raise SystemExit(f"{path}: no data rows")
    return rows


def truth(value: str) -> bool:
    if value.lower() not in {"true", "false"}:
        raise ValueError(f"expected true/false, found {value!r}")
    return value.lower() == "true"


def number(row: dict[str, str], key: str) -> float:
    try:
        return float(row[key])
    except (KeyError, ValueError) as error:
        raise ValueError(f"invalid numeric {key}={row.get(key)!r}") from error


def save(fig: plt.Figure, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output)
    if output.suffix.lower() != ".png":
        fig.savefig(output.with_suffix(".png"), dpi=300)
