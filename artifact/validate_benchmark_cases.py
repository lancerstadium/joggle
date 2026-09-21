#!/usr/bin/env python3
"""Validate the frozen operator and model benchmark population."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import re
from collections import Counter
from pathlib import Path
from typing import Any


FAMILIES = {"elementwise", "reduction", "matmul", "convolution", "quantization", "fusion"}
DTYPES = {"float32", "uint8", "int8", "int32"}
INTEGER_RANGES = {
    "uint8": (0, 255),
    "int8": (-128, 127),
    "int32": (-(1 << 31), (1 << 31) - 1),
}
DISTRIBUTIONS = {"uniform", "uniform-integer"}
VARIANTS = {"joggle-unoptimized", "joggle-optimized", "onnxruntime"}
SHA256 = re.compile(r"^[0-9a-f]{64}$")


def fail(where: str, message: str) -> None:
    raise SystemExit(f"{where}: {message}")


def fields(value: dict[str, Any], required: set[str], allowed: set[str], where: str) -> None:
    missing = required - value.keys()
    extra = value.keys() - allowed
    if missing or extra:
        fail(where, f"field mismatch; missing={sorted(missing)} extra={sorted(extra)}")


def finite(value: Any, where: str, *, nonnegative: bool = False) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        fail(where, "must be numeric")
    result = float(value)
    if not math.isfinite(result) or (nonnegative and result < 0):
        fail(where, "must be finite and non-negative")
    return result


def tensor(spec: Any, where: str, seeds: set[int], *, named: bool) -> None:
    if not isinstance(spec, dict):
        fail(where, "must be an object")
    required = {"dtype", "shape"} | ({"name"} if named else set())
    allowed = required | {"distribution", "low", "high", "seed", "values"}
    fields(spec, required, allowed, where)
    if named and (not isinstance(spec["name"], str) or not spec["name"]):
        fail(f"{where}.name", "must be non-empty")
    if spec["dtype"] not in DTYPES:
        fail(f"{where}.dtype", f"unknown dtype {spec['dtype']}")
    shape = spec["shape"]
    if (not isinstance(shape, list)
            or any(isinstance(x, bool) or not isinstance(x, int) or x <= 0 for x in shape)):
        fail(f"{where}.shape", "must contain positive integer extents")
    count = math.prod(shape)
    has_values = "values" in spec
    has_distribution = "distribution" in spec
    if has_values == has_distribution:
        fail(where, "must define exactly one of values or distribution")
    if has_values:
        values = spec["values"]
        if not isinstance(values, list) or len(values) != count:
            fail(f"{where}.values", f"must contain exactly {count} values")
        for index, value in enumerate(values):
            finite(value, f"{where}.values[{index}]")
            if spec["dtype"] in INTEGER_RANGES:
                lower, upper = INTEGER_RANGES[spec["dtype"]]
                if (isinstance(value, bool) or not isinstance(value, int)
                        or not lower <= value <= upper):
                    fail(
                        f"{where}.values[{index}]",
                        f"must be an in-range {spec['dtype']} integer",
                    )
        return
    distribution = spec["distribution"]
    if distribution not in DISTRIBUTIONS:
        fail(f"{where}.distribution", "unknown random distribution")
    if not {"low", "high", "seed"} <= spec.keys():
        fail(where, "random distribution requires low, high, and seed")
    low = finite(spec["low"], f"{where}.low")
    high = finite(spec["high"], f"{where}.high")
    if not low < high:
        fail(where, "low must be less than high")
    if distribution == "uniform" and spec["dtype"] != "float32":
        fail(where, "uniform distribution requires float32")
    if distribution == "uniform-integer":
        if spec["dtype"] not in INTEGER_RANGES:
            fail(where, "uniform-integer requires an integer dtype")
        if (isinstance(spec["low"], bool) or not isinstance(spec["low"], int)
                or isinstance(spec["high"], bool) or not isinstance(spec["high"], int)):
            fail(where, "uniform-integer bounds must be integers")
        lower, upper = INTEGER_RANGES[spec["dtype"]]
        if not lower <= spec["low"] <= spec["high"] <= upper:
            fail(where, f"uniform-integer bounds exceed {spec['dtype']}")
    seed = spec["seed"]
    if isinstance(seed, bool) or not isinstance(seed, int) or seed < 0:
        fail(f"{where}.seed", "must be an unsigned integer")
    if seed in seeds:
        fail(f"{where}.seed", f"duplicate seed {seed}")
    seeds.add(seed)


def tolerance(case: dict[str, Any], where: str) -> None:
    finite(case["rtol"], f"{where}.rtol", nonnegative=True)
    finite(case["atol"], f"{where}.atol", nonnegative=True)


def file_sha256(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            value.update(block)
    return value.hexdigest()


def validate(spec: Any, model_manifest: list[dict[str, str]]) -> None:
    if not isinstance(spec, dict):
        fail("root", "must be an object")
    root_fields = {"schema_version", "measurement", "variants", "operator_cases", "model_cases"}
    fields(spec, root_fields, root_fields, "root")
    if spec["schema_version"] != 1:
        fail("schema_version", "must equal 1")

    measurement = spec["measurement"]
    measurement_fields = {
        "warmups", "execution_iterations", "preparation_iterations",
        "memory_iterations", "threads", "operator_opset", "random_generator", "input_policy",
        "reference_provider", "reference_graph_optimization", "reference_execution_mode",
        "preparation_boundary", "execution_boundary", "memory_boundary",
        "artifact_boundary",
    }
    if not isinstance(measurement, dict):
        fail("measurement", "must be an object")
    fields(measurement, measurement_fields, measurement_fields, "measurement")
    for name in (
        "warmups", "execution_iterations", "preparation_iterations",
        "memory_iterations", "threads", "operator_opset",
    ):
        value = measurement[name]
        if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
            fail(f"measurement.{name}", "must be a positive integer")
    for name in (
        "random_generator", "input_policy", "preparation_boundary",
        "execution_boundary", "memory_boundary", "artifact_boundary",
        "reference_provider", "reference_graph_optimization", "reference_execution_mode",
    ):
        if not isinstance(measurement[name], str) or not measurement[name].strip():
            fail(f"measurement.{name}", "must be non-empty")

    variants = spec["variants"]
    if not isinstance(variants, list) or len(variants) != 3:
        fail("variants", "must contain exactly three variants")
    seen_variants: set[str] = set()
    reference_count = 0
    for index, variant in enumerate(variants):
        where = f"variants[{index}]"
        required = {"id", "system", "role", "pipeline", "artifact_size"}
        if not isinstance(variant, dict):
            fail(where, "must be an object")
        fields(variant, required, required, where)
        if variant["id"] in seen_variants:
            fail(f"{where}.id", "duplicate")
        seen_variants.add(variant["id"])
        if variant["role"] not in {"candidate", "reference"}:
            fail(f"{where}.role", "must be candidate or reference")
        reference_count += variant["role"] == "reference"
        if not isinstance(variant["artifact_size"], bool):
            fail(f"{where}.artifact_size", "must be boolean")
        for name in ("system", "pipeline"):
            if not isinstance(variant[name], str) or not variant[name].strip():
                fail(f"{where}.{name}", "must be non-empty")
    if seen_variants != VARIANTS or reference_count != 1:
        fail("variants", "IDs or reference role differ from the frozen design")
    reference = next(variant for variant in variants if variant["role"] == "reference")
    if reference["artifact_size"]:
        fail("variants", "shared-runtime reference cannot report comparable artifact size")

    seeds: set[int] = set()
    operator_cases = spec["operator_cases"]
    if not isinstance(operator_cases, list) or len(operator_cases) != 24:
        fail("operator_cases", "must contain exactly 24 cases")
    operator_ids: set[str] = set()
    family_counts: Counter[str] = Counter()
    operator_fields = {
        "id", "family", "nodes", "outputs", "inputs", "initializers", "rtol", "atol"
    }
    for index, case in enumerate(operator_cases):
        where = f"operator_cases[{index}]"
        if not isinstance(case, dict):
            fail(where, "must be an object")
        fields(case, operator_fields, operator_fields, where)
        if not isinstance(case["id"], str) or not case["id"] or case["id"] in operator_ids:
            fail(f"{where}.id", "must be unique and non-empty")
        operator_ids.add(case["id"])
        if case["family"] not in FAMILIES:
            fail(f"{where}.family", "unknown family")
        family_counts[case["family"]] += 1
        if not isinstance(case["inputs"], list) or not case["inputs"]:
            fail(f"{where}.inputs", "must contain at least one input")
        defined: set[str] = set()
        for input_index, value in enumerate(case["inputs"]):
            tensor(value, f"{where}.inputs[{input_index}]", seeds, named=True)
            if value["name"] in defined:
                fail(f"{where}.inputs[{input_index}].name", "duplicate")
            defined.add(value["name"])
        if not isinstance(case["initializers"], dict):
            fail(f"{where}.initializers", "must be an object")
        for name, value in case["initializers"].items():
            if not name:
                fail(f"{where}.initializers", "initializer name is empty")
            if name in defined:
                fail(f"{where}.initializers.{name}", "duplicates an input or initializer")
            tensor(value, f"{where}.initializers.{name}", seeds, named=False)
            defined.add(name)
        nodes = case["nodes"]
        if not isinstance(nodes, list) or not nodes:
            fail(f"{where}.nodes", "must contain at least one node")
        for node_index, node in enumerate(nodes):
            node_where = f"{where}.nodes[{node_index}]"
            if not isinstance(node, dict):
                fail(node_where, "must be an object")
            node_fields = {"op", "inputs", "outputs", "attributes"}
            fields(node, node_fields, node_fields, node_where)
            if not isinstance(node["op"], str) or not node["op"]:
                fail(f"{node_where}.op", "must be non-empty")
            for name in ("inputs", "outputs"):
                values = node[name]
                if (not isinstance(values, list) or not values
                        or any(not isinstance(value, str) or not value for value in values)):
                    fail(f"{node_where}.{name}", "must contain non-empty value names")
            if not isinstance(node["attributes"], dict):
                fail(f"{node_where}.attributes", "must be an object")
            missing_inputs = set(node["inputs"]) - defined
            if missing_inputs:
                fail(f"{node_where}.inputs", f"undefined values {sorted(missing_inputs)}")
            for output_name in node["outputs"]:
                if output_name in defined:
                    fail(f"{node_where}.outputs", f"redefines value {output_name}")
                defined.add(output_name)
        outputs = case["outputs"]
        if not isinstance(outputs, list) or not outputs:
            fail(f"{where}.outputs", "must contain at least one graph output")
        output_names: set[str] = set()
        for output_index, output in enumerate(outputs):
            output_where = f"{where}.outputs[{output_index}]"
            if not isinstance(output, dict):
                fail(output_where, "must be an object")
            output_fields = {"name", "dtype", "shape"}
            fields(output, output_fields, output_fields, output_where)
            name = output["name"]
            if not isinstance(name, str) or not name or name in output_names:
                fail(f"{output_where}.name", "must be unique and non-empty")
            if name not in defined:
                fail(f"{output_where}.name", "is not produced by the graph")
            output_names.add(name)
            if output["dtype"] not in DTYPES:
                fail(f"{output_where}.dtype", f"unknown dtype {output['dtype']}")
            shape = output["shape"]
            if (not isinstance(shape, list)
                    or any(isinstance(x, bool) or not isinstance(x, int) or x <= 0
                           for x in shape)):
                fail(f"{output_where}.shape", "must contain positive integer extents")
        tolerance(case, where)
    if family_counts != Counter({family: 4 for family in FAMILIES}):
        fail("operator_cases", f"expected four per family; found {dict(family_counts)}")

    frozen_models = {row["model"]: row["sha256"] for row in model_manifest}
    model_cases = spec["model_cases"]
    if not isinstance(model_cases, list) or len(model_cases) != len(frozen_models):
        fail("model_cases", "must cover the complete reactive model manifest")
    seen_models: set[str] = set()
    model_fields = {"id", "sha256", "inputs", "rtol", "atol"}
    for index, case in enumerate(model_cases):
        where = f"model_cases[{index}]"
        if not isinstance(case, dict):
            fail(where, "must be an object")
        fields(case, model_fields, model_fields, where)
        model = case["id"]
        if model in seen_models or frozen_models.get(model) != case["sha256"]:
            fail(f"{where}.id", "duplicate, absent, or hash differs from reactive manifest")
        if not SHA256.fullmatch(case["sha256"]):
            fail(f"{where}.sha256", "must be a lowercase SHA-256")
        seen_models.add(model)
        if not isinstance(case["inputs"], list) or not case["inputs"]:
            fail(f"{where}.inputs", "must contain at least one input")
        input_names: set[str] = set()
        for input_index, value in enumerate(case["inputs"]):
            tensor(value, f"{where}.inputs[{input_index}]", seeds, named=True)
            if value["name"] in input_names:
                fail(f"{where}.inputs[{input_index}].name", "duplicate")
            input_names.add(value["name"])
        tolerance(case, where)
    if seen_models != set(frozen_models):
        fail("model_cases", "model identities differ from reactive manifest")


def validate_model_signatures(spec: dict[str, Any], root: Path) -> None:
    try:
        import onnxruntime as ort
    except ImportError as error:
        fail("--model-root", "onnxruntime is required for signature validation")
    dtype_names = {
        "tensor(float)": "float32",
        "tensor(uint8)": "uint8",
        "tensor(int8)": "int8",
        "tensor(int32)": "int32",
    }
    options = ort.SessionOptions()
    options.log_severity_level = 3
    for case in spec["model_cases"]:
        path = root / f"{case['id']}.onnx"
        if not path.is_file():
            fail(case["id"], f"missing model {path}")
        if file_sha256(path) != case["sha256"]:
            fail(case["id"], "model file hash differs from manifest")
        session = ort.InferenceSession(
            str(path), sess_options=options, providers=["CPUExecutionProvider"]
        )
        actual = {item.name: item for item in session.get_inputs()}
        expected = {item["name"]: item for item in case["inputs"]}
        if set(actual) != set(expected):
            fail(case["id"], "input names differ from model signature")
        for name, item in actual.items():
            declared = expected[name]
            if dtype_names.get(item.type) != declared["dtype"]:
                fail(f"{case['id']}.{name}", f"dtype differs: {item.type}")
            if len(item.shape) != len(declared["shape"]):
                fail(f"{case['id']}.{name}", "rank differs from model signature")
            for source, fixed in zip(item.shape, declared["shape"]):
                if isinstance(source, int) and source != fixed:
                    fail(
                        f"{case['id']}.{name}",
                        f"static extent differs: model={source}, manifest={fixed}",
                    )


def main() -> int:
    parser = argparse.ArgumentParser()
    root = Path(__file__).resolve().parent
    parser.add_argument("spec", type=Path, nargs="?",
                        default=root / "manifests" / "benchmark-cases.json")
    parser.add_argument("--models", type=Path,
                        default=root / "manifests" / "reactive-models.csv")
    parser.add_argument("--model-root", type=Path)
    args = parser.parse_args()
    with args.spec.open(encoding="utf-8") as stream:
        spec = json.load(stream)
    with args.models.open(newline="", encoding="utf-8") as stream:
        models = list(csv.DictReader(stream))
    validate(spec, models)
    if args.model_root:
        validate_model_signatures(spec, args.model_root)
    print(
        f"validated {len(spec['operator_cases'])} operator and "
        f"{len(spec['model_cases'])} model benchmark cases"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
