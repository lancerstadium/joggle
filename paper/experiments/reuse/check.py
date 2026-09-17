#!/usr/bin/env python3
"""Correctness and declared-slot accounting for the UltraFace derivation pilot.

No latency or process-memory measurement. Input/reference files are the existing
UltraFace RFB-320 fixture; C, headers, and weights must be freshly emitted.
"""

import argparse
import ctypes
import hashlib
import json
import math
from pathlib import Path
import platform
import re
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def array(path, scalar, count):
    data = path.read_bytes()
    expected = ctypes.sizeof(scalar) * count
    if len(data) != expected:
        raise ValueError(f"{path}: expected {expected} bytes, got {len(data)}")
    return (scalar * count).from_buffer_copy(data)


def slots(path):
    sizes = {"float": ctypes.sizeof(ctypes.c_float),
             "int64_t": ctypes.sizeof(ctypes.c_int64)}
    totals = {kind: {"arrays": 0, "elements": 0, "bytes": 0} for kind in sizes}
    for line in path.read_text().splitlines():
        if not re.match(r"\s*static\s+\w+\s+slot_", line):
            continue
        match = re.fullmatch(r"\s*static (\w+) slot_\w+\[(\d+)\];", line)
        if not match or match[1] not in sizes:
            raise ValueError(f"Unaccounted slot declaration: {line}")
        kind, count = match[1], int(match[2])
        totals[kind]["arrays"] += 1
        totals[kind]["elements"] += count
        totals[kind]["bytes"] += count * sizes[kind]
    if not sum(item["arrays"] for item in totals.values()):
        raise ValueError("No emitted static slots found")
    return {"by_type": totals,
            "declared_bytes": sum(item["bytes"] for item in totals.values())}


def error(observed, expected, atol):
    if any(not math.isfinite(v) for v in (*observed, *expected)):
        raise ValueError("Non-finite output or reference")
    differences = [abs(float(a) - float(b)) for a, b in zip(observed, expected)]
    return {"count": len(differences), "max_absolute_error": max(differences),
            "failed_elements": sum(d > atol for d in differences)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", type=Path)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--atol", type=float, default=1e-5)
    args = parser.parse_args()
    if not math.isfinite(args.atol) or args.atol < 0:
        parser.error("--atol must be finite and nonnegative")
    root, fixture = args.artifacts.resolve(), args.fixture.resolve()
    inputs = array(fixture / "input.bin", ctypes.c_float, 230400)
    references = {"scores": array(fixture / "scores.ref.bin", ctypes.c_float, 8840),
                  "boxes": array(fixture / "boxes.ref.bin", ctypes.c_float, 17680)}
    report = {"workload": "UltraFace RFB-320", "scope": "correctness-only",
              "platform": platform.platform(), "absolute_tolerance": args.atol,
              "relative_tolerance": 0, "calls_per_variant": 2,
              "compiler": subprocess.check_output([args.cc, "--version"], text=True),
              "fixture_sha256": {name: digest(fixture / name) for name in
                                 ("input.bin", "scores.ref.bin", "boxes.ref.bin")},
              "variants": {}}
    report["preparation_sha256"] = {
        name: digest(root / name) for name in
        ("prepared.jog", "modules/planned/module.jog")}
    report["checker_sha256"] = digest(Path(__file__))
    outputs, libraries = {}, []
    for variant in ("original", "derived"):
        directory = root / variant
        obj = directory / "model.o"
        library = directory / ("model.dylib" if platform.system() == "Darwin" else "model.so")
        commands = [[args.cc, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                     "-fPIC", "-c", str(directory / "model.c"), "-o", str(obj)],
                    [args.cc, "-dynamiclib" if platform.system() == "Darwin" else "-shared",
                     str(obj), "-lm", "-o", str(library)]]
        for command in commands:
            subprocess.run(command, check=True)
        loaded = ctypes.CDLL(str(library))
        libraries.append(loaded)
        entry = loaded.model_main
        entry.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_ubyte),
                          ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_float)]
        entry.restype = None
        weights = array(directory / "weights.bin", ctypes.c_ubyte, 1230688)
        calls, previous = [], None
        for _ in range(2):
            scores = (ctypes.c_float * 8840)(*([float("nan")] * 8840))
            boxes = (ctypes.c_float * 17680)(*([float("nan")] * 17680))
            entry(inputs, weights, scores, boxes)
            calls.append({"scores": error(scores, references["scores"], args.atol),
                          "boxes": error(boxes, references["boxes"], args.atol)})
            current = (bytes(scores), bytes(boxes))
            if previous is not None and current != previous:
                raise ValueError(f"{variant}: repeated output changed")
            previous = current
        outputs[variant] = previous
        report["variants"][variant] = {
            "commands": commands, "calls": calls, "repeated_output_identical": True,
            "static_slots": slots(directory / "model.c"),
            "sha256": {name: digest(directory / name) for name in
                       ("planned.jog", "placed.jog", "model.c", "model.h", "weights.bin")}}
        if platform.system() == "Darwin":
            report["variants"][variant]["object_sections"] = subprocess.check_output(
                ["size", "-m", str(obj)], text=True)
    report["variant_outputs_identical"] = outputs["original"] == outputs["derived"]
    report["weights_identical"] = (report["variants"]["original"]["sha256"]["weights.bin"] ==
                                   report["variants"]["derived"]["sha256"]["weights.bin"])
    report["passed"] = (report["variant_outputs_identical"] and report["weights_identical"] and
                        all(output["failed_elements"] == 0
                            for variant in report["variants"].values()
                            for call in variant["calls"] for output in call.values()))
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
