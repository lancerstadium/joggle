#!/usr/bin/env python3
"""Generate a numerical and latency harness from one c.api entry."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any


IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
HEADER = re.compile(r"[A-Za-z0-9_.-]+\Z")


def fail(message: str) -> None:
    raise SystemExit(f"make_harness: {message}")


def identifier(value: Any, what: str) -> str:
    if not isinstance(value, str) or not IDENT.fullmatch(value):
        fail(f"{what} is not a C identifier: {value!r}")
    return value


def descriptor(value: Any, what: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        fail(f"{what} is not an object")
    required = {
        "name", "type", "c", "kind", "shape", "elements", "bytes", "pointer"
    }
    missing = sorted(required - value.keys())
    if missing:
        fail(f"{what} lacks {', '.join(missing)}")
    if value["pointer"] is not True:
        fail(f"{what} is scalar-valued; the model harness accepts tensor buffers")
    if value["kind"] not in {"float", "signed", "unsigned", "bool"}:
        fail(f"{what} has unsupported ABI kind {value['kind']!r}")
    if not isinstance(value["elements"], int) or value["elements"] <= 0:
        fail(f"{what} has invalid element count")
    if not isinstance(value["bytes"], int) or value["bytes"] <= 0:
        fail(f"{what} has invalid byte count")
    if value["bytes"] % value["elements"] != 0:
        fail(f"{what} byte count is not an integral number of elements")
    if (value["kind"] == "float" and
            value["bytes"] // value["elements"] not in {4, 8}):
        fail(f"{what} uses an unsupported floating-point width")
    identifier(value["name"], f"{what} name")
    identifier(value["c"], f"{what} C type")
    return value


def select(document: Any, requested: str | None) -> dict[str, Any]:
    if not isinstance(document, list) or not document:
        fail("c.api document must be a non-empty array")
    entries = [entry for entry in document if isinstance(entry, dict)]
    if len(entries) != len(document):
        fail("c.api array contains a non-object entry")
    if requested is None:
        if len(entries) != 1:
            names = ", ".join(str(entry.get("name", "?")) for entry in entries)
            fail(f"multiple entries ({names}); select one with --entry")
        return entries[0]
    matches = [entry for entry in entries if entry.get("name") == requested]
    if len(matches) != 1:
        fail(f"entry {requested!r} occurs {len(matches)} times")
    return matches[0]


def call(
    entry: dict[str, Any], params: list[dict[str, Any]],
    results: list[dict[str, Any]]
) -> str:
    arguments = [f"input_{i}" for i in range(len(params))]
    if entry["data"]:
        arguments.append("weights")
    arguments.extend(f"output_{i}" for i in range(len(results)))
    return f"  {entry['name']}({', '.join(arguments)});"


def cleanup(
    params: list[dict[str, Any]], results: list[dict[str, Any]], has_data: bool
) -> str:
    lines = [f"  free(expected_{i});\n  free(output_{i});"
             for i in reversed(range(len(results)))]
    if has_data:
        lines.append("  free(weights);")
    lines.extend(f"  free(input_{i});" for i in reversed(range(len(params))))
    return "\n".join(lines)


def comparison(result: dict[str, Any], index: int) -> str:
    count = result["elements"]
    size = result["bytes"]
    if result["kind"] == "float":
        c_type = "float" if size // count == 4 else "double"
        return f"""  {{
    const {c_type}* actual = (const {c_type}*)output_{index};
    const {c_type}* reference = (const {c_type}*)expected_{index};
    double maximum = 0.0;
    for (size_t i = 0; i < {count}u; ++i) {{
      const double got = (double)actual[i];
      const double want = (double)reference[i];
      const double error = fabs(got - want);
      if (!isfinite(got) || error > 1.0e-4 + 1.0e-4 * fabs(want)) {{
        fprintf(stderr, "output {index} differs at %zu: %.17g versus %.17g\\n",
                i, got, want);
        status = 5;
        goto done;
      }}
      if (error > maximum)
        maximum = error;
    }}
    fprintf(stderr,
            "output,{index},elements,{count},max_abs_error,%.17g\\n", maximum);
  }}"""
    return f"""  if (memcmp(output_{index}, expected_{index}, {size}u) != 0) {{
    fprintf(stderr, "output {index} differs from its reference bytes\\n");
    status = 5;
    goto done;
  }}
  fprintf(stderr, "output,{index},elements,{count},exact,1\\n");"""


def render(entry: dict[str, Any], header: str) -> str:
    name = identifier(entry.get("name"), "entry name")
    data = entry.get("data")
    if not isinstance(data, str):
        fail("entry data field is not a string")
    if data:
        identifier(data, "external-data parameter")
    params_raw = entry.get("params")
    results_raw = entry.get("results")
    if not isinstance(params_raw, list) or not params_raw:
        fail(f"entry {name} has no tensor inputs")
    if not isinstance(results_raw, list) or not results_raw:
        fail(f"entry {name} has no tensor outputs")
    params = [descriptor(value, f"parameter {i}")
              for i, value in enumerate(params_raw)]
    results = [descriptor(value, f"result {i}")
               for i, value in enumerate(results_raw)]
    argc = 1 + len(params) + len(results) + (1 if data else 0) + 2
    input_loads = []
    arg = 1
    for i, value in enumerate(params):
        input_loads.append(
            f"  void* input_{i} = read_exact(argv[{arg}], {value['bytes']}u);")
        arg += 1
    data_load = ""
    if data:
        data_load = f"  void* weights = read_nonempty(argv[{arg}]);"
        arg += 1
        data_reader = """
static void* read_nonempty(const char* path) {
  FILE* file = fopen(path, "rb");
  if (!file || fseek(file, 0, SEEK_END) != 0) {
    if (file)
      fclose(file);
    return NULL;
  }
  const long end = ftell(file);
  if (end <= 0 || (unsigned long long)end > (unsigned long long)SIZE_MAX ||
      fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return NULL;
  }
  const size_t bytes = (size_t)end;
  void* data = malloc(bytes);
  if (!data || fread(data, 1, bytes, file) != bytes || fclose(file) != 0) {
    free(data);
    return NULL;
  }
  return data;
}
"""
    else:
        data_reader = ""
    output_loads = []
    for i, value in enumerate(results):
        output_loads.append(
            f"  void* output_{i} = calloc(1, {value['bytes']}u);\n"
            f"  void* expected_{i} = read_exact(argv[{arg}], {value['bytes']}u);")
        arg += 1
    warmup_arg = arg
    repetitions_arg = arg + 1
    allocations = [f"input_{i}" for i in range(len(params))]
    if data:
        allocations.append("weights")
    for i in range(len(results)):
        allocations.extend([f"output_{i}", f"expected_{i}"])
    guard = " || ".join(f"!{value}" for value in allocations)
    invoke = call(entry, params, results)
    checks = "\n".join(comparison(value, i) for i, value in enumerate(results))
    hash_lines = "\n".join(
        f"    checksum = hash_bytes(checksum, output_{i}, {value['bytes']}u);"
        for i, value in enumerate(results)
    )
    release = cleanup(params, results, bool(data))
    return f"""/* Generated from c.api by make_harness.py. */
#define _POSIX_C_SOURCE 200809L

#include \"{header}\"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void* read_exact(const char* path, size_t expected) {{
  FILE* file = fopen(path, "rb");
  if (!file || fseek(file, 0, SEEK_END) != 0) {{
    if (file)
      fclose(file);
    return NULL;
  }}
  const long end = ftell(file);
  if (end < 0 || (unsigned long long)end != (unsigned long long)expected ||
      fseek(file, 0, SEEK_SET) != 0) {{
    fclose(file);
    return NULL;
  }}
  void* data = malloc(expected);
  if (!data || fread(data, 1, expected, file) != expected || fclose(file) != 0) {{
    free(data);
    return NULL;
  }}
  return data;
}}

{data_reader}

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

static uint64_t hash_bytes(uint64_t hash, const void* data, size_t bytes) {{
  const unsigned char* values = (const unsigned char*)data;
  for (size_t i = 0; i < bytes; ++i) {{
    hash ^= (uint64_t)values[i];
    hash *= UINT64_C(1099511628211);
  }}
  return hash;
}}

int main(int argc, char** argv) {{
  if (argc != {argc}) {{
    fprintf(stderr, "expected {argc - 1} arguments: {len(params)} input file(s), "
            "{1 if data else 0} weight file(s), {len(results)} reference file(s), "
            "warmup, repetitions\\n");
    return 2;
  }}
  int status = 0;
  size_t warmup = 0;
  size_t repetitions = 0;
{chr(10).join(input_loads)}
{data_load}
{chr(10).join(output_loads)}
  if ({guard}) {{
    fprintf(stderr, "an input, weight, allocation, or reference file is invalid\\n");
    status = 3;
    goto done;
  }}
  if (!number(argv[{warmup_arg}], &warmup) ||
      !number(argv[{repetitions_arg}], &repetitions) || repetitions == 0) {{
    fprintf(stderr, "warmup and repetitions must be nonnegative integers; repetitions must be positive\\n");
    status = 4;
    goto done;
  }}

  for (size_t i = 0; i < warmup; ++i) {{
{invoke}
  }}

  puts("iteration,seconds,checksum");
  for (size_t i = 0; i < repetitions; ++i) {{
    const double begin = now();
{invoke}
    const double end = now();
    if (begin < 0.0 || end < begin) {{
      status = 6;
      goto done;
    }}
    uint64_t checksum = UINT64_C(1469598103934665603);
{hash_lines}
    printf("%zu,%.9f,%016llx\\n", i, end - begin,
           (unsigned long long)checksum);
  }}
{checks}

done:
{release}
  return status;
}}
"""


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("api", type=Path, help="JSON emitted by c.api")
    parser.add_argument("output", type=Path, help="generated C file")
    parser.add_argument("--header", default="model-blob.h",
                        help="generated header included by the harness")
    parser.add_argument(
        "--entry", help="C symbol to select when the API has several entries"
    )
    args = parser.parse_args()
    if not HEADER.fullmatch(args.header):
        fail("--header must be a simple local file name")
    try:
        document = json.loads(args.api.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        fail(f"cannot read c.api document: {error}")
    source = render(select(document, args.entry), args.header)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(source, encoding="utf-8")


if __name__ == "__main__":
    main()
