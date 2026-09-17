#!/usr/bin/env python3
"""Compile and alternately time generated-C variants with matched outputs."""

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


def error_fields(count: int) -> list[str]:
    if count == 1:
        return ["max_error"]
    if count == 2:
        return ["max_first_error", "max_second_error"]
    return [f"max_output_{index}_error" for index in range(count)]


def harness(count: int) -> str:
    output_parameters = ", ".join("float*" for _ in range(count))
    parameters = ", ".join(f"float* output_{index}" for index in range(count))
    arguments = ", ".join(f"output_{index}" for index in range(count))
    count_declarations = "\n".join(
        f"  size_t count_{index} = 0;" for index in range(count)
    )
    count_checks = " ||\n      ".join(
        f"!number(argv[{index + 3}], &count_{index}) || count_{index} == 0 ||\n"
        f"      count_{index} > SIZE_MAX / sizeof(float)"
        for index in range(count)
    )
    allocations = "\n".join(
        f"  float* baseline_{index} = calloc(count_{index}, sizeof(float));\n"
        f"  float* candidate_{index} = calloc(count_{index}, sizeof(float));"
        for index in range(count)
    )
    allocation_checks = " ||\n      ".join(
        f"!baseline_{index} || !candidate_{index}" for index in range(count)
    )
    baseline_arguments = ", ".join(
        f"baseline_{index}" for index in range(count)
    )
    candidate_arguments = ", ".join(
        f"candidate_{index}" for index in range(count)
    )
    errors = "\n".join(
        f"    const float error_{index} = error(baseline_{index}, "
        f"candidate_{index}, count_{index});"
        for index in range(count)
    )
    finite_errors = " ||\n        ".join(
        f"!isfinite(error_{index})" for index in range(count)
    )
    header = ",".join([
        "iteration", "first", "baseline_seconds", "candidate_seconds",
        *error_fields(count),
    ])
    formats = ",".join("%.9g" for _ in range(count))
    error_arguments = ", ".join(f"error_{index}" for index in range(count))
    frees = "\n".join(
        f"  free(candidate_{index});\n  free(baseline_{index});"
        for index in reversed(range(count))
    )
    argc = count + 4
    repetitions_index = count + 3

    return f"""#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void baseline_main(const float*, const unsigned char*, {output_parameters});
void candidate_main(const float*, const unsigned char*, {output_parameters});

static void* read_file(const char* path, size_t* bytes) {{
  FILE* file = fopen(path, "rb");
  if (!file || fseek(file, 0, SEEK_END) != 0) {{
    if (file)
      fclose(file);
    return NULL;
  }}
  const long end = ftell(file);
  if (end <= 0 || fseek(file, 0, SEEK_SET) != 0) {{
    fclose(file);
    return NULL;
  }}
  *bytes = (size_t)end;
  void* data = malloc(*bytes);
  if (!data || fread(data, 1, *bytes, file) != *bytes || fclose(file) != 0) {{
    free(data);
    return NULL;
  }}
  return data;
}}

static int number(const char* text, size_t* value) {{
  char* end = NULL;
  errno = 0;
  if (*text == '-')
    return 0;
  const unsigned long long parsed = strtoull(text, &end, 10);
  if (errno != 0 || end == text || *end != '\\0' ||
      parsed > (unsigned long long)SIZE_MAX)
    return 0;
  *value = (size_t)parsed;
  return 1;
}}

static double now(void) {{
  struct timespec value;
  if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
    return -1.0;
  return (double)value.tv_sec + (double)value.tv_nsec * 1.0e-9;
}}

static double run_baseline(const float* input, const unsigned char* data,
                           {parameters}) {{
  const double begin = now();
  baseline_main(input, data, {arguments});
  const double end = now();
  return begin < 0.0 || end < begin ? -1.0 : end - begin;
}}

static double run_candidate(const float* input, const unsigned char* data,
                            {parameters}) {{
  const double begin = now();
  candidate_main(input, data, {arguments});
  const double end = now();
  return begin < 0.0 || end < begin ? -1.0 : end - begin;
}}

static float error(const float* left, const float* right, size_t count) {{
  float worst = 0.0f;
  for (size_t i = 0; i < count; ++i) {{
    if (!isfinite(left[i]) || !isfinite(right[i]))
      return INFINITY;
    const float difference = fabsf(left[i] - right[i]);
    if (difference > worst)
      worst = difference;
  }}
  return worst;
}}

int main(int argc, char** argv) {{
  if (argc != {argc})
    return 2;
{count_declarations}
  size_t repetitions = 0;
  if ({count_checks} ||
      !number(argv[{repetitions_index}], &repetitions) || repetitions == 0)
    return 3;

  size_t input_bytes = 0;
  size_t data_bytes = 0;
  float* input = read_file(argv[1], &input_bytes);
  unsigned char* data = read_file(argv[2], &data_bytes);
{allocations}
  if (!input || !data || {allocation_checks} ||
      input_bytes % sizeof(float) != 0 || data_bytes == 0)
    return 4;

  for (int i = 0; i < 3; ++i) {{
    baseline_main(input, data, {baseline_arguments});
    candidate_main(input, data, {candidate_arguments});
  }}

  puts("{header}");
  for (size_t i = 0; i < repetitions; ++i) {{
    double baseline_time = 0.0;
    double candidate_time = 0.0;
    if (i % 2 == 0) {{
      baseline_time = run_baseline(input, data, {baseline_arguments});
      candidate_time = run_candidate(input, data, {candidate_arguments});
    }} else {{
      candidate_time = run_candidate(input, data, {candidate_arguments});
      baseline_time = run_baseline(input, data, {baseline_arguments});
    }}
{errors}
    if (baseline_time < 0.0 || candidate_time < 0.0 ||
        {finite_errors})
      return 5;
    printf("%zu,%s,%.9f,%.9f,{formats}\\n", i,
           i % 2 == 0 ? "baseline" : "candidate", baseline_time,
           candidate_time, {error_arguments});
  }}

{frees}
  free(data);
  free(input);
  return 0;
}}
"""


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
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", action="append", required=True,
                        metavar="LABEL=SOURCE.c")
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--weights", type=Path, required=True)
    parser.add_argument("--output-count", action="append", type=int,
                        required=True)
    parser.add_argument("--repetitions", type=int, default=10)
    parser.add_argument("--out-dir", type=Path,
                        default=Path("build-study/pair"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    cc = args.cc.resolve()
    baseline = args.baseline.resolve()
    input_path = args.input.resolve()
    weights = args.weights.resolve()
    required = [cc, baseline, input_path, weights]
    if any(not path.is_file() for path in required):
        parser.error("cc, baseline, input, and weights must exist")
    if (any(count <= 0 for count in args.output_count) or
            args.repetitions <= 0):
        parser.error("output counts and repetitions must be positive")
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
    harness_path = root / "harness.c"
    harness_path.write_text(harness(len(args.output_count)), encoding="utf-8")
    baseline_object = root / "baseline.o"
    checked([
        str(cc), *FLAGS, "-Dmodel_main=baseline_main", "-c", str(baseline),
        "-o", str(baseline_object),
    ])

    fields = [
        "iteration", "first", "baseline_seconds", "candidate_seconds",
        *error_fields(len(args.output_count)),
    ]
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
            str(cc), *FLAGS, str(harness_path), str(baseline_object),
            str(candidate_object), "-lm", "-o", str(program),
        ])
        output = checked([
            str(program), str(input_path), str(weights),
            *(str(count) for count in args.output_count),
            str(args.repetitions),
        ], capture=True)
        measurements = list(csv.DictReader(output.splitlines()))
        if len(measurements) != args.repetitions or (
            measurements and list(measurements[0]) != fields
        ):
            raise RuntimeError(f"unexpected harness output for {label}")
        for index, measurement in enumerate(measurements):
            numbers = [float(measurement[key]) for key in fields[2:]]
            if (measurement["iteration"] != str(index) or
                    measurement["first"] !=
                    ("baseline" if index % 2 == 0 else "candidate") or
                    any(not math.isfinite(value) for value in numbers) or
                    numbers[0] <= 0.0 or numbers[1] <= 0.0 or
                    any(value < 0.0 for value in numbers[2:])):
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
