#!/usr/bin/env python3
"""Compile and alternately time two-result generated-C variants."""

from __future__ import annotations

import argparse
import csv
import hashlib
import math
import re
import subprocess
from pathlib import Path


FLAGS = [
    "-std=c11", "-O3", "-DNDEBUG", "-Wall", "-Wextra",
    "-Wstrict-prototypes", "-Werror",
]
FIELDS = [
    "iteration", "first", "baseline_seconds", "candidate_seconds",
    "max_first_error", "max_second_error",
]


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def candidates(items: list[str]) -> list[tuple[str, Path]]:
    out: list[tuple[str, Path]] = []
    labels: set[str] = set()
    for item in items:
        label, separator, source = item.partition("=")
        if not separator or not label or not source:
            raise ValueError("candidates use LABEL=SOURCE.c")
        if (not re.fullmatch(r"[A-Za-z0-9._-]+", label) or
                label in {".", ".."}):
            raise ValueError(f"unsafe candidate label: {label}")
        path = Path(source).resolve()
        if label in labels:
            raise ValueError(f"duplicate candidate label: {label}")
        if not path.is_file():
            raise ValueError(f"candidate does not exist: {path}")
        labels.add(label)
        out.append((label, path))
    return out


def checked(command: list[str], *, capture: bool = False) -> str:
    result = subprocess.run(
        command, stdout=subprocess.PIPE if capture else subprocess.DEVNULL,
        stderr=subprocess.PIPE, text=True
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"{result.stderr}"
        )
    return result.stdout if capture else ""


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", required=True)
    parser.add_argument("--cc", type=Path, required=True)
    parser.add_argument("--harness", type=Path,
                        default=Path("paper/paired2.c"))
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", action="append", required=True,
                        metavar="LABEL=SOURCE.c")
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--weights", type=Path, required=True)
    parser.add_argument("--first-count", type=int, required=True)
    parser.add_argument("--second-count", type=int, required=True)
    parser.add_argument("--repetitions", type=int, default=10)
    parser.add_argument("--out-dir", type=Path,
                        default=Path("build-study/pair2"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    cc = args.cc.resolve()
    harness = args.harness.resolve()
    baseline = args.baseline.resolve()
    input_path = args.input.resolve()
    weights = args.weights.resolve()
    required = [cc, harness, baseline, input_path, weights]
    if any(not path.is_file() for path in required):
        parser.error("cc, harness, baseline, input, and weights must exist")
    if args.first_count <= 0 or args.second_count <= 0 or args.repetitions <= 0:
        parser.error("counts and repetitions must be positive")
    try:
        selected = candidates(args.candidate)
    except ValueError as error:
        parser.error(str(error))

    revision = checked(
        ["git", "rev-parse", "HEAD"], capture=True
    ).strip()
    compiler = checked([str(cc), "--version"], capture=True).splitlines()[0]
    root = args.out_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    baseline_object = root / "baseline.o"
    checked([
        str(cc), *FLAGS, "-Dmodel_main=baseline_main", "-c", str(baseline),
        "-o", str(baseline_object),
    ])

    rows: list[dict[str, object]] = []
    baseline_hash = digest(baseline)
    for label, source in selected:
        directory = root / label
        directory.mkdir(parents=True, exist_ok=True)
        candidate_object = directory / "candidate.o"
        program = directory / "paired"
        checked([
            str(cc), *FLAGS, "-Dmodel_main=candidate_main", "-c",
            str(source), "-o", str(candidate_object),
        ])
        checked([
            str(cc), *FLAGS, str(harness), str(baseline_object),
            str(candidate_object), "-lm", "-o", str(program),
        ])
        output = checked([
            str(program), str(input_path), str(weights),
            str(args.first_count), str(args.second_count),
            str(args.repetitions),
        ], capture=True)
        measurements = list(csv.DictReader(output.splitlines()))
        if len(measurements) != args.repetitions or (
            measurements and list(measurements[0]) != FIELDS
        ):
            raise RuntimeError(f"unexpected harness output for {label}")
        for index, measurement in enumerate(measurements):
            numbers = [
                float(measurement[key]) for key in FIELDS[2:]
            ]
            if (measurement["iteration"] != str(index) or
                    measurement["first"] !=
                    ("baseline" if index % 2 == 0 else "candidate") or
                    any(not math.isfinite(value) for value in numbers) or
                    numbers[0] <= 0.0 or numbers[1] <= 0.0 or
                    numbers[2] < 0.0 or numbers[3] < 0.0):
                raise RuntimeError(f"invalid harness row for {label}")
        candidate_hash = digest(source)
        for measurement in measurements:
            rows.append({
                "model": args.model,
                "revision": revision,
                "candidate": label,
                **measurement,
                "baseline_c_sha256": baseline_hash,
                "candidate_c_sha256": candidate_hash,
                "compiler": compiler,
            })

    output_path = args.output.resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


if __name__ == "__main__":
    main()
