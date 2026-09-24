#!/usr/bin/env python3
"""Execute a native extension against the shared semantic task cases.

This is the task oracle, not an agent or a Figure 4 measurement collector.
Candidate programs are executable code; run untrusted candidates in the agent's
isolated execution environment, not directly on the host.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import signal
import struct
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SUPPORTED_TASKS = {"ana-broadcast-shape", "ana-storage-cost", "ana-numeric-range",
                   "ana-fusion-match", "emit-storage-plan", "emit-target-capability",
                   "emit-graph-manifest", "rew-add-zero", "rew-redundant-cast", "rew-transpose-pair",
                   "con-instruction-select", "con-gelu-expand", "con-quant-expand", "con-layout-legalize",
                   "rew-conv-bias-relu", "emit-kernel-wrapper", "def-parametric-type", "def-quantized-op"}

SUPPORTED_TASKS.update({"def-layout-attribute", "def-target-intrinsic"})
IMPORT_TASKS = {"vert-input-format"}
SUPPORTED_TASKS.update(IMPORT_TASKS)
COMPOUND_TASKS = {"vert-int4", "vert-fused-op"}
SUPPORTED_TASKS.update(COMPOUND_TASKS)
DEFINITION_TASKS = {"def-parametric-type", "def-quantized-op", "def-layout-attribute", "def-target-intrinsic"}


def layout_definition_fixture(request: dict, system: str) -> str:
    type = request["type"]
    src, dst = json.dumps(request["src"]), json.dumps(request["dst"])
    if system == "Joggle":
        match = re.fullmatch(r"tensor<((?:\d+x)*)(i\d+|f\d+)>", type)
        if match is None: raise ValueError("invalid layout fixture tensor type")
        shape = [int(d) for d in match[1].split("x") if d]
        return (f"mod fixture\nuse tensor\nuse extension\nfn subject(a: tensor<{match[2]}, {shape}>) -> _ {{\n"
                f"[src: {src}, dst: {dst}]\nlet y = extension.relayout(a)\nreturn y\n}}\n")
    return ("module attributes {study.attributes = {src = " + src + ", dst = " + dst + "}} { " +
            f"func.func @subject(%a: {type}) -> {type} {{ func.return %a : {type} }} }}\n")


def layout_definition_result(actual: object, request: dict) -> dict:
    if not isinstance(actual, dict) or set(actual) != {"inputs", "result", "attributes"}:
        raise ValueError("invalid layout operation observation")
    def normalized(value):
        if not isinstance(value, str):
            raise ValueError("invalid observed layout type")
        compact = re.sub(r"\s+", "", value)
        match = re.fullmatch(r"tensor<(i\d+|f\d+),\[([\d,]*)\]>", compact)
        return ("tensor<" + "".join(d + "x" for d in match[2].split(",") if d) + match[1] + ">"
                if match else compact)
    if (not isinstance(actual["inputs"], list) or len(actual["inputs"]) != 1 or
            normalized(actual["inputs"][0]) != request["type"] or
            actual["attributes"] != {key: request[key] for key in ("src", "dst")}):
        raise ValueError("candidate changed operand or layout attributes")
    src, dst = actual["attributes"]["src"], actual["attributes"]["dst"]
    if src not in ("NCHW", "NHWC") or dst not in ("NCHW", "NHWC"):
        raise ValueError("invalid observed layout")
    return {"result": normalized(actual["result"]), "permutation": [src.index(axis) for axis in dst]}


def quantized_definition_inputs(request: dict) -> tuple[list[str], dict]:
    operands = [request[name] if isinstance(request[name], dict) else {"type": request[name]}
                for name in ("lhs", "rhs")]
    attributes = {name + suffix: operand.get(key, default)
                  for name, operand in zip(("lhs", "rhs"), operands)
                  for suffix, key, default in (("_scale", "scale", 1.0), ("_zero", "zero", 0))}
    attributes.update(output_scale=request["output_scale"], output_zero=request.get("output_zero", 0))
    return [operand["type"] for operand in operands], attributes


def quantized_definition_fixture(request: dict, system: str) -> str:
    types, attributes = quantized_definition_inputs(request)
    if system == "Joggle":
        def jog(type):
            match = re.fullmatch(r"tensor<((?:\d+x)*)(i\d+|f\d+)>", type)
            if match is None: raise ValueError("invalid quantized fixture tensor type")
            shape = [int(d) for d in match[1].split("x") if d]
            return f"tensor<{match[2]}, {shape}>"
        lhs, rhs = map(jog, types)
        attrs = ", ".join(f"{name}: {json.dumps(value, allow_nan=False)}" for name, value in attributes.items())
        return (f"mod fixture\nuse tensor\nuse extension\nfn subject(a: {lhs}, b: {rhs}) -> {lhs} {{\n"
                f"[{attrs}]\nlet result = extension.qadd(a, b)\nreturn result\n}}\n")
    attrs = ", ".join(f"{name} = {value} : " + ("f64" if name.endswith("_scale") else "i32")
                      for name, value in attributes.items())
    lhs, rhs = types
    return ("module attributes {study.attributes = {" + attrs + "}} { " +
            f"func.func @subject(%a: {lhs}, %b: {rhs}) -> {lhs} {{ func.return %a : {lhs} }} }}\n")


def quantized_definition_result(actual: object, request: dict) -> dict:
    types, attributes = quantized_definition_inputs(request)
    if not isinstance(actual, dict) or set(actual) != {"inputs", "result", "attributes"}:
        raise ValueError("invalid quantized operation observation")
    def normalized(type):
        if not isinstance(type, str): raise ValueError("invalid observed type")
        compact = re.sub(r"\s+", "", type)
        match = re.fullmatch(r"tensor<(i\d+|f\d+),\[([\d,]*)\]>", compact)
        return ("tensor<" + "".join(d + "x" for d in match[2].split(",") if d) + match[1] + ">"
                if match else compact)
    if (not isinstance(actual["inputs"], list) or list(map(normalized, actual["inputs"])) != types or
            actual["attributes"] != attributes or any(
                type(actual["attributes"][name]) is not type(value) for name, value in attributes.items())):
        raise ValueError("candidate changed operands or quantization attributes")
    return {"result": normalized(actual["result"])}


def intrinsic_definition_fixture(request: dict, system: str) -> str:
    types = [request.get(name, default) for name, default in
             (("lhs_type", "vector<4xi8>"), ("rhs_type", "vector<4xi8>"), ("acc_type", "i32"))]
    if system == "Joggle":
        def jog(value):
            match = re.fullmatch(r"(vector|tensor)<((?:\d+x)+)(i\d+)>", value)
            if not match:
                return value
            dims = [int(d) for d in match[2].split("x") if d]
            extent = str(dims[0]) if match[1] == "vector" and len(dims) == 1 else str(dims)
            return f"{match[1]}<{match[3]}, {extent}>"
        arguments = ", ".join(f"a{i}: {jog(value)}" for i, value in enumerate(types))
        return (f"mod fixture\nuse extension\nfn subject({arguments}) -> i32 {{\n"
                "let result = extension.dot4_i8(a0, a1, a2)\nreturn result\n}\n")
    arguments = ", ".join(f"%a{i}: {value}" for i, value in enumerate(types))
    return ("module attributes {study.attributes = {}} { " +
            f"func.func @subject({arguments}) -> {types[2]} {{ func.return %a2 : {types[2]} }} }}\n")


def intrinsic_definition_result(actual: object, request: dict) -> dict:
    """Interpret the fixed ISA semantics after native construction is observed.

    This is a definition-task semantic oracle, not generated-machine-code timing.
    Native observers separately require the registered operation to consume the
    original three arguments and require its sole result to be returned.
    """
    if not isinstance(actual, dict) or set(actual) != {"inputs", "result", "attributes"}:
        raise ValueError("invalid intrinsic operation observation")
    def normalized(value):
        if not isinstance(value, str):
            raise ValueError("invalid intrinsic type")
        compact = re.sub(r"\s+", "", value)
        match = re.fullmatch(r"vector<(i\d+),(\d+)>", compact)
        return f"vector<{match[2]}x{match[1]}>" if match else compact
    if (not isinstance(actual["inputs"], list) or
            list(map(normalized, actual["inputs"])) != ["vector<4xi8>", "vector<4xi8>", "i32"] or
            normalized(actual["result"]) != "i32" or actual["attributes"] != {}):
        raise ValueError("candidate changed intrinsic signature or attributes")
    lhs, rhs, accumulator = request["lhs"], request["rhs"], request["acc"]
    if (not isinstance(lhs, list) or not isinstance(rhs, list) or len(lhs) != 4 or len(rhs) != 4 or
            any(type(value) is not int or not -128 <= value <= 127 for value in lhs + rhs) or
            type(accumulator) is not int or not -(1 << 31) <= accumulator < (1 << 31)):
        raise ValueError("invalid intrinsic test operands")
    result = accumulator + sum(a * b for a, b in zip(lhs, rhs, strict=True))
    # The task corpus currently stays within signed i32; overflow semantics
    # must be specified before adding overflow cases.
    if not -(1 << 31) <= result < (1 << 31):
        raise ValueError("intrinsic test result exceeds the defined test domain")
    return {"result": result, "type": "i32"}


def definition_fixture(request: dict, system: str) -> str:
    type = f'fx<{request["width"]},{request["frac"]}>'
    if system == "Joggle":
        return (f"mod fixture\nuse base\nuse extension\n"
                f"fn constructed(m: Mod) -> Ty {{ return {type}() }}\n"
                f"fn subject(x: {type}) -> {type} {{ return x }}\n")
    type = "!extension." + type
    return f"module {{ func.func @subject(%x: {type}) -> {type} {{ func.return %x : {type} }} }}\n"


def definition_result(actual: object) -> dict:
    if (not isinstance(actual, dict) or set(actual) != {"types"}
            or not isinstance(actual["types"], list) or len(actual["types"]) != 1
            or not isinstance(actual["types"][0], str)):
        raise ValueError("expected one native parameter type")
    match = re.fullmatch(r"(?:!extension\.)?fx<(-?\d+),(-?\d+)>",
                         re.sub(r"\s+", "", actual["types"][0]))
    if not match:
        raise ValueError("expected native fx type after round-trip")
    width, frac = map(int, match.groups())
    return {"canonical": f"fx<{width},{frac}>", "storage_bits": width}

REWRITE_TASKS = {"rew-add-zero", "rew-redundant-cast", "rew-transpose-pair", "con-instruction-select", "con-gelu-expand", "con-quant-expand", "con-layout-legalize", "rew-conv-bias-relu"}
GELU_TOLERANCES = {"f32": (1e-5, 1e-6), "f64": (1e-12, 1e-12)}


def conv_fusion_graph(request: dict, eliminate: bool) -> dict:
    graph = layout_graph(request)
    conv = graph["nodes"][0]
    conv["op"] = "conv2d"
    conv["attrs"]["layout"] = request["layout"]
    conv["attrs"]["tag"] = "fusion"
    conv["results"][0]["name"] = "c"
    channels = conv["results"][0]["shape"][3]
    if request["layout"] == "NCHW":
        for value, permutation in ((graph["inputs"][0], [0, 3, 1, 2]),
                                   (graph["inputs"][1], [3, 2, 0, 1]),
                                   (conv["results"][0], [0, 3, 1, 2])):
            value["shape"] = [value["shape"][i] for i in permutation]
    bias = {"name": "b", "element": "f32", "shape": [channels]}
    graph["inputs"].append(bias)
    result = conv["results"][0]
    fused = {"op": "fused_conv_bias_relu", "inputs": ["x", "w", "b"],
             "results": [dict(result, name="y")], "attrs": dict(conv["attrs"])}
    graph["declarations"] = [dict(fused, parameter_types=graph["inputs"])]
    graph["nodes"] = [fused] if eliminate else [conv,
        {"op": "bias_add", "inputs": ["c", "b"], "results": [dict(result, name="biased")],
         "attrs": {"axis": request.get("bias_axis", 1 if request["layout"] == "NCHW" else 3)}},
        {"op": request.get("activation", "relu"), "inputs": ["biased"],
         "results": [dict(result, name="y")]}]
    graph["outputs"] = ["y"] + (["y", "x"] if request.get("shared_output") else [])
    if request.get("shared_conv"):
        graph["outputs"].append("c")
    if request.get("shared_bias"):
        graph["outputs"].append("biased")
    return graph


def layout_graph(request: dict) -> dict:
    x, w = request["input"], request["weight"]
    stride, pad, dilation = request.get("stride", [1, 1]), request.get("pad", [0] * 4), request.get("dilation", [1, 1])
    y = [x[0], (x[1] + pad[0] + pad[2] - dilation[0] * (w[0] - 1) - 1) // stride[0] + 1,
         (x[2] + pad[1] + pad[3] - dilation[1] * (w[1] - 1) - 1) // stride[1] + 1, w[3]]
    def value(name, shape):
        return {"name": name, "element": "f32", "shape": shape}
    inputs = [value("x", x), value("w", w)]
    nx, nw, ny = [x[i] for i in [0, 3, 1, 2]], [w[i] for i in [3, 2, 0, 1]], [y[i] for i in [0, 3, 1, 2]]
    declarations = [{"op": name, "parameter_types": [value("p", dims) for dims in args],
                     "results": [value("r", out)]} for name, args, out in (
                         ("transpose_input", [x], nx), ("transpose_weight", [w], nw),
                         ("conv2d_nchw", [nx, nw], ny), ("transpose_output", [ny], y))]
    return {"inputs": inputs, "declarations": declarations,
            "nodes": [{"op": "conv2d_nhwc", "inputs": ["x", "w"], "results": [value("y", y)],
                       "attrs": {"stride": stride, "pad": pad, "dilation": dilation}}],
            "outputs": ["y", "x", "y"] if request.get("shared_output") else ["y"]}


def convolution_nhwc(x, w, attrs):
    """Scalar spatial reference shared by the two explicit layout semantics."""
    import numpy as np
    if x.shape[3] != w.shape[2]:
        raise ValueError("channel-mismatch")
    sh, sw = attrs["stride"]
    dh, dw = attrs["dilation"]
    top, left, bottom, right = attrs["pad"]
    height = (x.shape[1] + top + bottom - dh * (w.shape[0] - 1) - 1) // sh + 1
    width = (x.shape[2] + left + right - dw * (w.shape[1] - 1) - 1) // sw + 1
    result = np.zeros((x.shape[0], height, width, w.shape[3]), dtype=np.float32)
    for h in range(height):
        for v in range(width):
            for kh in range(w.shape[0]):
                ih = h * sh + kh * dh - top
                for kw in range(w.shape[1]):
                    iw = v * sw + kw * dw - left
                    if 0 <= ih < x.shape[1] and 0 <= iw < x.shape[2]:
                        for c in range(x.shape[3]):
                            result[:, h, v, :] += x[:, ih, iw, c, None] * w[kh, kw, c, :]
    return result


def quant_graph(request: dict) -> dict:
    shape = request.get("shape", [len(request.get("lhs", [0] * 4))])
    a = {"name": "a", "element": "i8", "shape": shape}
    b = {**a, "name": "b"}
    y = {**a, "name": "y"}
    floating = {**a, "element": "f32"}
    attrs = {key: request[key] for key in ("lhs_scale", "rhs_scale", "output_scale", "zeros")}
    return {"inputs": [a, b], "declarations": [
        {"op": "dequantize", "parameter_types": [a], "results": [floating]},
        {"op": "add", "parameter_types": [floating, floating], "results": [floating]},
        {"op": "quantize", "parameter_types": [floating], "results": [y]}],
        "nodes": [{"op": "qadd", "inputs": ["a", "b"], "results": [y], "attrs": attrs}],
        "outputs": ["y", "a", "y"] if request.get("shared_output") else ["y"]}


def gelu_graph(request: dict) -> dict:
    value = {"name": "x", "element": request["element"], "shape": request["shape"]}
    output = {**value, "name": "y"}
    declarations = [{"op": name, "inputs": ["x"] * arity, "results": [output]}
                    for name, arity in (("splat", 0), ("mul", 2), ("div", 2), ("add", 2), ("erf", 1))]
    return {"inputs": [value], "declarations": declarations,
            "nodes": [{"op": "gelu", "inputs": ["x"], "results": [output]}],
            "outputs": ["y", "x", "y"] if request.get("shared_output") else ["y"]}


def conversion_structure(actual: dict, original: dict, allowed: dict, required: set) -> bool:
    """Accept target graphs without fixing constant order or parenthesization."""
    if not isinstance(actual, dict) or actual.get("inputs") != original["inputs"]:
        return False
    nodes = actual.get("nodes", [])
    if not nodes or not required.issubset({node.get("op") for node in nodes}):
        return False
    value_types = {value["id"]: value["type"] for value in actual["inputs"]}
    for node in nodes:
        if node.get("op") not in allowed or len(node.get("inputs", [])) != allowed[node["op"]]:
            return False
        if any(value not in value_types for value in node["inputs"]) or len(node.get("results", [])) != 1:
            return False
        for result in node["results"]:
            if result["id"] in value_types:
                return False
            value_types[result["id"]] = result["type"]
    original_types = {value["id"]: value["type"] for value in original["inputs"]}
    original_types.update({value["id"]: value["type"] for node in original["nodes"] for value in node["results"]})
    return ([value_types.get(value) for value in actual.get("outputs", [])] ==
            [original_types[value] for value in original["outputs"]])


def gelu_structure(actual: dict, original: dict) -> bool:
    return conversion_structure(actual, original,
                                {"splat": 0, "mul": 2, "div": 2, "add": 2, "erf": 1}, {"erf"})


def quant_structure(actual: dict, original: dict) -> bool:
    return conversion_structure(actual, original, {"dequantize": 1, "add": 2, "quantize": 1},
                                {"dequantize", "add", "quantize"})


def layout_structure(actual: dict, original: dict) -> bool:
    operations = {"transpose_input": 1, "transpose_weight": 1, "conv2d_nchw": 2, "transpose_output": 1}
    return conversion_structure(actual, original, operations, set(operations))


def matmul_graph(request: dict, select: bool) -> dict:
    element = request["type"]
    a, b = request["lhs"], request["rhs"]
    result = {"name": "y", "element": "f32", "shape": a[:-1] + [b[-1]]}
    target = {"op": "mma_m16n16k16", "inputs": ["a", "b"], "results": [result]}
    node = {"op": "mma_m16n16k16" if select else "matmul",
            "inputs": ["a", "b"], "results": [result], "attrs": {"tag": "selection"}}
    if select:
        node["attrs"]["tiles"] = [a[-2] // 16, b[-1] // 16, a[-1] // 16]
    return {"inputs": [{"name": "a", "element": element, "shape": a},
                       {"name": "b", "element": element, "shape": b}],
            "declarations": [target], "nodes": [node],
            "outputs": ["y", "y"] if request.get("repeated_output") else ["y"]}


def expected_rejection(step: dict, expected: dict) -> bool:
    diagnostic = expected.get("error")
    code = step.get("exit_code")
    return (isinstance(diagnostic, str) and bool(diagnostic) and
            type(code) is int and code > 0 and step.get("timeout") is False and
            not step.get("stdout", "").strip() and diagnostic in step.get("stderr", ""))


def case_completed(case: dict) -> bool:
    expected = case.get("expected", {})
    if isinstance(expected, dict) and "error" in expected:
        return expected_rejection(case, expected)
    return case.get("exit_code") == 0 and not case.get("decode_error")


def transpose_graph(request: dict, eliminate: bool) -> dict:
    shape, element = request["shape"], request.get("element", "f32")
    operand = {"name": "x", "element": element, "shape": shape}
    nodes, source = [], "x"
    for index, permutation in enumerate((request["p"], request["q"])):
        source_shape = shape
        # Invalid permutations remain native SSA fixtures for candidate-side
        # validation. They have no numerical interpretation.
        if sorted(permutation) == list(range(len(shape))):
            shape = [shape[i] for i in permutation]
        result = f"transpose{index}"
        tag = lambda dims: "x".join(map(str, dims)) or "scalar"
        nodes.append({"op": f"transpose_{element}_{tag(source_shape)}_to_{tag(shape)}", "inputs": [source],
                      "results": [{"name": result, "element": element, "shape": shape}],
                      "attrs": {"perm": permutation}})
        source = result
    shared = request.get("return_intermediate", False)
    return {"inputs": [operand],
            "nodes": (nodes[:1] if shared else []) if eliminate else nodes,
            "outputs": (["x"] if eliminate else [source]) + (["transpose0"] if shared else [])}


def cast_graph(request: dict, eliminate: bool) -> dict:
    element, shape = request["element"], request["shape"]
    operand = {"name": "x", "element": element, "shape": shape}
    nodes, source = [], "x"
    for index, result_element in enumerate(request["casts"]):
        result = f"cast{index}"
        nodes.append({"op": f"cast_{element}_{result_element}", "inputs": [source],
                      "results": [{"name": result, "element": result_element,
                                   "shape": request.get("result_shape", shape)}]})
        element, source = result_element, result
    shared = request.get("return_intermediate", False)
    return {"inputs": [operand],
            "nodes": (nodes[:1] if shared else []) if eliminate else nodes,
            "outputs": (["x"] if eliminate else [source]) + (["cast0"] if shared else [])}


def rewrite_graph(request: dict, eliminate: bool = False) -> dict:
    """Build the tensor-dialect fixture; the extension sees native SSA, not this map."""
    if request.get("op") == "conv-bias-activation":
        return conv_fusion_graph(request, eliminate)
    if "weight" in request and "input" in request:
        return layout_graph(request)
    if "lhs_scale" in request:
        return quant_graph(request)
    if request.get("op") == "gelu":
        return gelu_graph(request)
    if "lhs" in request and "rhs" in request:
        return matmul_graph(request, eliminate)
    if "casts" in request:
        return cast_graph(request, eliminate)
    if "p" in request and "q" in request:
        return transpose_graph(request, eliminate)
    element, shape = request["element"], request["shape"]
    constant_shape = request.get("constant_shape", shape)
    rank = max(len(shape), len(constant_shape))
    a = [1] * (rank - len(shape)) + shape
    b = [1] * (rank - len(constant_shape)) + constant_shape
    if any(x != y and x != 1 and y != 1 for x, y in zip(a, b)):
        raise ValueError("incompatible fixture broadcast")
    result_shape = [y if x == 1 else x for x, y in zip(a, b)]
    operand = {"name": "x", "element": element, "shape": shape}
    constant = {"op": "splat", "inputs": [],
                "results": [{"name": "zero", "element": element, "shape": constant_shape}],
                "attrs": {"value": request["constant"]}}
    add = {"op": "add", "inputs": ["x", "zero"] if request["side"] == "rhs" else ["zero", "x"],
           "results": [{"name": "sum", "element": element, "shape": result_shape}],
           "attrs": {"no_signed_zeros": request.get("no_signed_zeros", False)}}
    shared = request.get("return_constant", False)
    nodes = ([constant] if shared else []) if eliminate else [constant, add]
    return {"inputs": [operand], "nodes": nodes,
            "outputs": (["x"] if eliminate else ["sum"]) + (["zero"] if shared else [])}


def graph_manifest(graph: dict) -> dict:
    ids = {}
    def define(value: dict) -> dict:
        key = f"v{len(ids)}"
        ids[value["name"]] = key
        return {"id": key, "type": {"element": value["element"], "shape": value["shape"]}}
    inputs = [define(value) for value in graph["inputs"]]
    nodes = []
    for node in graph["nodes"]:
        operands = [ids[name] for name in node["inputs"]]
        results = [define(value) for value in node["results"]]
        nodes.append({"id": f"n{len(nodes)}", "op": node["op"], "inputs": operands,
                      "results": results, "attrs": sorted(map(list, node.get("attrs", {}).items()))})
    return {"schema_version": 1, "inputs": inputs, "nodes": nodes,
            "outputs": [ids[name] for name in graph["outputs"]]}


def rewrite_numerics(actual: dict, original: dict, no_signed_zeros: bool,
                     tolerance: tuple[float, float] | None = None,
                     extra_feeds: dict | None = None) -> dict:
    """Interpret the independently observed post-IR, including strict zero bits."""
    import numpy as np
    dtypes = {"f16": np.float16, "f32": np.float32, "f64": np.float64, "i8": np.int8,
              "i16": np.int16, "i32": np.int32, "i64": np.int64}
    def evaluate(graph: dict, sample: list[float]) -> list:
        values = {}
        for index, value in enumerate(graph["inputs"]):
            ty = value["type"]
            count = math.prod(ty["shape"])
            items = sample[value["id"]] if isinstance(sample, dict) else np.roll(sample, index)
            values[value["id"]] = np.resize(np.asarray(items).astype(dtypes[ty["element"]]), count).reshape(ty["shape"])
        for node in graph["nodes"]:
            if len(node["results"]) != 1:
                raise ValueError("rewrite oracle requires one result per fixture operation")
            result = node["results"][0]
            ty = result["type"]
            attrs = dict(node["attrs"])
            if node["op"] == "splat" and not node["inputs"]:
                value = np.full(ty["shape"], attrs["value"], dtype=dtypes[ty["element"]])
            elif node["op"] in {"qadd", "dequantize", "quantize"}:
                def scale(key):
                    result = np.float32(attrs[key])
                    if not np.isfinite(result) or result <= 0:
                        raise ValueError("invalid-scale")
                    return result
                if node["op"] == "qadd":
                    a, b = (values[key].astype(np.float32) for key in node["inputs"])
                    zero_a, zero_b, zero_y = attrs["zeros"]
                    real = (a - np.float32(zero_a)) * scale("lhs_scale") + (b - np.float32(zero_b)) * scale("rhs_scale")
                    value = np.clip(np.rint(real / scale("output_scale")) + zero_y, -128, 127).astype(np.int8)
                elif node["op"] == "dequantize":
                    value = (values[node["inputs"][0]].astype(np.float32) - np.float32(attrs["zero"])) * scale("scale")
                else:
                    real = values[node["inputs"][0]].astype(np.float32)
                    value = np.clip(np.rint(real / scale("scale")) + attrs["zero"], -128, 127).astype(np.int8)
            elif node["op"] == "add" and len(node["inputs"]) == 2:
                a, b = (values[key] for key in node["inputs"])
                value = np.add(a, b, dtype=dtypes[ty["element"]])
            elif node["op"] in {"mul", "div"} and len(node["inputs"]) == 2:
                a, b = (values[key] for key in node["inputs"])
                operation = np.multiply if node["op"] == "mul" else np.divide
                value = operation(a, b, dtype=dtypes[ty["element"]])
            elif node["op"] in {"gelu", "erf"} and len(node["inputs"]) == 1:
                x = values[node["inputs"][0]].astype(np.float64)
                erf = np.vectorize(math.erf, otypes=[np.float64])
                value = (erf(x) if node["op"] == "erf" else
                         0.5 * x * (1.0 + erf(x / math.sqrt(2.0))))
                value = value.astype(dtypes[ty["element"]])
            elif node["op"].startswith("cast_") and len(node["inputs"]) == 1:
                converted = values[node["inputs"][0]].astype(dtypes[ty["element"]])
                value = np.broadcast_to(converted, ty["shape"])
            elif node["op"].startswith("transpose_") and len(node["inputs"]) == 1:
                value = np.transpose(values[node["inputs"][0]], attrs["perm"])
            elif node["op"] == "bias_add":
                x, b = (values[key] for key in node["inputs"])
                shape = [1] * x.ndim
                shape[attrs["axis"]] = b.size
                value = x + b.reshape(shape)
            elif node["op"] in {"relu", "sigmoid"}:
                x = values[node["inputs"][0]]
                if node["op"] == "relu":
                    value = np.maximum(x, np.float32(0))
                else:
                    e = np.exp(-np.abs(x))
                    value = np.where(x >= 0, np.float32(1) / (np.float32(1) + e), e / (np.float32(1) + e))
            elif node["op"] in {"conv2d_nhwc", "conv2d_nchw", "conv2d", "fused_conv_bias_relu"}:
                x, w = (values[key] for key in node["inputs"][:2])
                nchw = node["op"] == "conv2d_nchw" or attrs.get("layout") == "NCHW"
                if nchw:
                    value = convolution_nhwc(x.transpose(0, 2, 3, 1), w.transpose(2, 3, 1, 0), attrs).transpose(0, 3, 1, 2)
                else:
                    value = convolution_nhwc(x, w, attrs)
                if node["op"] == "fused_conv_bias_relu":
                    bias = values[node["inputs"][2]]
                    value = np.maximum(value + bias.reshape([1, -1, 1, 1] if nchw else [1, 1, 1, -1]), np.float32(0))
            elif node["op"] in {"matmul", "mma_m16n16k16"} and len(node["inputs"]) == 2:
                inputs = [values[key] for key in node["inputs"]]
                a, b = (value.astype(np.float32) for value in inputs)
                if node["op"] == "mma_m16n16k16":
                    if (any(value.dtype != np.float16 for value in inputs) or
                            a.ndim != 2 or b.ndim != 2 or a.shape[1] != b.shape[0] or
                            any(d <= 0 or d % 16 for d in (*a.shape, b.shape[1])) or
                            attrs.get("tiles") != [a.shape[0] // 16, b.shape[1] // 16, a.shape[1] // 16]):
                        raise ValueError("invalid matrix instruction selection")
                value = np.matmul(a, b)
            else:
                raise ValueError("unsupported operation in rewrite result")
            if list(value.shape) != ty["shape"]:
                raise ValueError("rewrite result has the wrong runtime shape")
            values[result["id"]] = value
        return [values[key] for key in graph["outputs"]]
    cases = []
    samples = [[-0.0, 0.0, 1.0, -1.0], [-17, 23, 255, -1024],
               [0, 0, 0, 0], [0.1, -0.3, 1.5, -2.75], [-128, 127, 256, -257]]
    if tolerance is not None:
        samples += [[-3, -1, 0, 1, 3], [-8, -4, -2, 0.5, 4]]
    if any(node["op"] in {"conv2d_nhwc", "conv2d"} for node in original["nodes"]):
        # Short repeating patterns can be invariant under a wrong spatial transpose.
        rng = np.random.Generator(np.random.PCG64(731))
        samples.append({value["id"]: rng.uniform(-1, 1, math.prod(value["type"]["shape"])).tolist()
                        for value in original["inputs"]})
    if extra_feeds is not None:
        samples.append(extra_feeds)
    for sample in samples:
        before, after = evaluate(original, sample), evaluate(actual, sample)
        passed = len(before) == len(after)
        for a, b in zip(before, after):
            passed = passed and a.shape == b.shape and a.dtype == b.dtype
            if tolerance is not None:
                passed = passed and bool(np.allclose(a, b, rtol=tolerance[0], atol=tolerance[1], equal_nan=False))
            else:
                passed = passed and (bool(np.array_equal(a, b)) if no_signed_zeros else a.tobytes() == b.tobytes())
        cases.append({"input": sample, "passed": passed,
                      "before_bits": [value.tobytes().hex() for value in before],
                      "after_bits": [value.tobytes().hex() for value in after]})
    return {"passed": all(case["passed"] for case in cases), "cases": cases}


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def canonical(value: object) -> str:
    return json.dumps(value, sort_keys=True, allow_nan=False)


def invalid_constant(token: str) -> None:
    raise ValueError(f"non-finite JSON constant: {token}")


def equivalent(actual: object, expected: object, numerical: bool, policy: dict) -> bool:
    if isinstance(expected, dict):
        return (isinstance(actual, dict) and actual.keys() == expected.keys() and
                all(equivalent(actual[key], value, numerical, policy)
                    for key, value in expected.items()))
    if isinstance(expected, list):
        return (isinstance(actual, list) and len(actual) == len(expected) and
                all(equivalent(a, b, numerical, policy) for a, b in zip(actual, expected)))
    if numerical and type(expected) in (int, float) and type(actual) in (int, float):
        return math.isfinite(actual) and math.isclose(
            actual, expected, rel_tol=policy["float_rtol"], abs_tol=policy["float_atol"])
    return canonical(actual) == canonical(expected)


def expected_result(task: str, case: dict) -> dict:
    if task == "emit-target-capability":
        request = case["input"]
        return {"target": request["target"], "pointer_bits": request["pointer_bits"],
                "little_endian": request["little_endian"],
                "features": sorted(request["features"]),
                "intrinsics": sorted(request["intrinsics"])}
    return case["expect"]


def native_attr(value: object) -> str:
    """Serialize the input only; expected outputs never enter native fixtures."""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return f"{value} : i64"
    if isinstance(value, float) and math.isfinite(value):
        return f"{value:.17e} : f64"
    if isinstance(value, list):
        return "[" + ", ".join(map(native_attr, value)) + "]"
    if isinstance(value, dict):
        return "{" + ", ".join(
            f"{json.dumps(key)} = {native_attr(item)}"
            for key, item in sorted(value.items())
        ) + "}"
    if isinstance(value, str):
        return json.dumps(value)
    raise ValueError(f"no native attribute serialization for {value!r}")


def fusion_fixture(request: dict, system: str) -> str:
    """Materialize types and SSA uses; never pass a graph description to the candidate."""
    layout = request.get("layout", "NCHW")
    channels = request["channels"]
    shape = [1, channels, 5, 7] if layout == "NCHW" else [1, 5, 7, channels]
    input_shape = [1, 3, 5, 7] if layout == "NCHW" else [1, 5, 7, 3]
    weight_shape = [channels, 3, 3, 3]
    bias_shape = request["bias"]
    conv, bias, activation = request["ops"]
    returned = ["activated"]
    for name, uses in zip(("convolved", "biased"), request["uses"], strict=True):
        returned.extend([name] * (uses - 1))
    if system == "Joggle":
        tensor = lambda dims: "tensor<f32, [" + ", ".join(map(str, dims)) + "]>"
        ty, bt, xt, wt = map(tensor, (shape, bias_shape, input_shape, weight_shape))
        returns = ty if len(returned) == 1 else "(" + ", ".join([ty] * len(returned)) + ")"
        declaration = f"fn side(x: {xt}) -> {xt};\n" if request.get("interleave") else ""
        side = "  let independent = side(x)\n" if request.get("interleave") else ""
        return (f"mod fixture\nuse tensor\n"
                f"{declaration}"
                f"fn {conv}(x: {xt}, w: {wt}) -> {ty};\n"
                f"fn {bias}(x: {ty}, b: {bt}) -> {ty};\n"
                f"fn {activation}(x: {ty}) -> {ty};\n"
                f"[layout: {json.dumps(layout)}]\n"
                f"fn subject(x: {xt}, w: {wt}, b: {bt}) -> {returns} {{\n"
                f"  let convolved = {conv}(x, w)\n"
                f"{side}"
                f"  let biased = {bias}(convolved, b)\n"
                f"  let activated = {activation}(biased)\n"
                f"  return {', '.join(returned)}\n}}\n")
    tensor = lambda dims: "tensor<" + "x".join(map(str, dims)) + "xf32>"
    ty, bt, xt, wt = map(tensor, (shape, bias_shape, input_shape, weight_shape))
    returns = ty if len(returned) == 1 else "(" + ", ".join([ty] * len(returned)) + ")"
    declaration = f"  func.func private @side({xt}) -> {xt}\n" if request.get("interleave") else ""
    side = f"    %independent = func.call @side(%x) : ({xt}) -> {xt}\n" if request.get("interleave") else ""
    return (f"module {{\n"
            f"{declaration}"
            f"  func.func private @{conv}({xt}, {wt}) -> {ty}\n"
            f"  func.func private @{bias}({ty}, {bt}) -> {ty}\n"
            f"  func.func private @{activation}({ty}) -> {ty}\n"
            f"  func.func @subject(%x: {xt}, %w: {wt}, %b: {bt}) -> {returns} "
            f"attributes {{layout = {json.dumps(layout)}}} {{\n"
            f"    %convolved = func.call @{conv}(%x, %w) : ({xt}, {wt}) -> {ty}\n"
            f"{side}"
            f"    %biased = func.call @{bias}(%convolved, %b) : ({ty}, {bt}) -> {ty}\n"
            f"    %activated = func.call @{activation}(%biased) : ({ty}) -> {ty}\n"
            f"    func.return {', '.join('%' + n for n in returned)} : "
            f"{', '.join([ty] * len(returned))}\n  }}\n}}\n")


def qconv_parameters(request: dict) -> dict:
    """Validate the fused-task contract independently of candidate code.

    Runtime tensors are retained here, never attached to the compiler graph.
    Shapes and quantization attributes are the emitter's only specialization
    inputs. The arithmetic oracle below uses Python integers for accumulation.
    """
    import numpy as np

    tensors = {}
    for name, rank, low, high in (("x", 4, -128, 127), ("w", 4, -128, 127),
                                 ("bias", 1, -(1 << 31), (1 << 31) - 1)):
        try:
            value = np.asarray(request[name], dtype=object)
        except (KeyError, ValueError, TypeError) as failure:
            raise ValueError("invalid-tensor") from failure
        if value.ndim != rank or any(extent <= 0 for extent in value.shape):
            raise ValueError("shape-mismatch")
        if any(type(item) is not int or not low <= item <= high for item in value.flat):
            raise ValueError("literal-out-of-range")
        tensors[name] = value
    n, h, width, ci = tensors["x"].shape
    kh, kw, wi, co = tensors["w"].shape
    if wi != ci or tensors["bias"].shape != (co,) or kh > h or kw > width:
        raise ValueError("shape-mismatch")
    scales = request.get("scales", {})
    if not isinstance(scales, dict):
        raise ValueError("invalid-scale")
    for name in ("acc", "out"):
        value = scales.get(name)
        if type(value) not in (int, float) or not math.isfinite(value) or value <= 0:
            raise ValueError("invalid-scale")
    zero = request.get("output_zero", 0)
    if type(zero) is not int or not -128 <= zero <= 127:
        raise ValueError("invalid-zero-point")
    return {**tensors, "output_shape": (n, h-kh+1, width-kw+1, co),
            "acc_scale": float(scales["acc"]), "out_scale": float(scales["out"]),
            "output_zero": zero}


def qconv_reference(request: dict) -> dict:
    """Valid NHWC/HWIO cross-correlation, RNE, saturation, then real ReLU."""
    import numpy as np

    p = qconv_parameters(request)
    x, weights, bias = p["x"], p["w"], p["bias"]
    output = np.empty(p["output_shape"], dtype=object)
    kh, kw, ci, _ = weights.shape
    for n, row, col, channel in np.ndindex(output.shape):
        accumulator = int(bias[channel])
        for dy, dx, inner in np.ndindex(kh, kw, ci):
            accumulator += int(x[n, row+dy, col+dx, inner]) * int(weights[dy, dx, inner, channel])
        if not -(1 << 31) <= accumulator < (1 << 31):
            raise ValueError("accumulator-overflow")
        scaled = accumulator * p["acc_scale"] / p["out_scale"]
        if math.isnan(scaled):
            raise ValueError("invalid-requantization")
        # Clamp before rounding only outside the representable output interval.
        # This also handles overflow of the floating intermediate without
        # changing finite ties-to-even behavior inside that interval.
        zero = p["output_zero"]
        rounded = 127-zero if scaled >= 127-zero else 0 if scaled <= 0 else round(scaled)
        output[n, row, col, channel] = max(zero, min(127, rounded+zero))
    return {"values": output.tolist(), "node_ops": ["fused_qconv_relu"]}


def qconv_graph(request: dict, fused: bool = False) -> dict:
    """Represent the chain and its real SSA fan-out, not a match-count answer."""
    shared = request.get("conv_uses", 1) != 1
    fixture = request
    if "x" not in fixture:
        if not shared:
            raise ValueError("missing-runtime-input")
        # The structural negative case has no runtime vectors in its contract.
        fixture = {"x": [[[[1, -2]]]], "w": [[[[2], [-3]]]], "bias": [1],
                   "scales": {"acc": 0.25, "out": 0.5}, "output_zero": 0}
    p = qconv_parameters(fixture)

    def value(name, element, shape):
        return {"name": name, "element": element, "shape": list(shape)}

    inputs = [value("x", "i8", p["x"].shape), value("w", "i8", p["w"].shape),
              value("b", "i32", p["bias"].shape)]
    convolved = value("conv", "i32", p["output_shape"])
    biased = value("biased", "i32", p["output_shape"])
    quantized = value("quantized", "i8", p["output_shape"])
    output = value("output", "i8", p["output_shape"])
    layout = {"layout": "NHWC", "kernel_layout": "HWIO"}
    quant = {key: p[key] for key in ("acc_scale", "out_scale", "output_zero")}
    nodes = [
        {"op": "qconv", "inputs": ["x", "w"], "results": [convolved], "attrs": layout},
        {"op": "bias", "inputs": ["conv", "b"], "results": [biased]},
        {"op": "requantize", "inputs": ["biased"], "results": [quantized], "attrs": quant},
        {"op": "relu", "inputs": ["quantized"], "results": [output],
         "attrs": {"output_zero": p["output_zero"]}}]
    if fused and not shared:
        nodes = [{"op": "fused_qconv_relu", "inputs": ["x", "w", "b"],
                  "results": [output], "attrs": {**layout, **quant}}]
    return {"inputs": inputs, "nodes": nodes,
            "outputs": ["output", "conv"] if shared else ["output"],
            "declarations": [{"op": "fused_qconv_relu", "parameter_types": inputs,
                              "results": [output]}]}


def qint4_reference(request: dict) -> dict:
    """Independent signed-nibble semantics; inputs are never pre-truncated."""
    def literals(values):
        if not isinstance(values, list):
            raise ValueError("invalid-literals")
        if any(type(value) is not int or not -8 <= value <= 7 for value in values):
            raise ValueError("literal-out-of-range")
        return values

    # Source-literal diagnostics are checked before any arithmetic or packing.
    if "values" in request:
        literals(request["values"])
    lhs, rhs = literals(request.get("lhs")), literals(request.get("rhs"))
    if len(lhs) != len(rhs):
        raise ValueError("shape-mismatch")
    values = [max(-8, min(7, left + right)) for left, right in zip(lhs, rhs)]
    packed = []
    for index in range(0, len(values), 2):
        low = values[index] & 15
        high = (values[index + 1] & 15) if index + 1 < len(values) else 0
        packed.append(low | (high << 4))
    return {"values": values, "bytes_hex": bytes(packed).hex(" "), "range": [-8, 7]}


def qint4_graph(request: dict) -> dict:
    """Build native typed input IR without exposing runtime vectors to emitters.

    Signed qint4 has an i4 representation. Arithmetic extends to i8 before
    saturation; packing produces bytes, whose bit patterns are unsigned.
    Invalid source literals remain wide metadata until the extension checks them.
    """
    def value(name, element, count):
        return {"name": name, "element": element, "shape": [count]}

    if "values" in request:
        count = len(request["values"])
        return {"inputs": [], "nodes": [
            {"op": "qint4_literal", "inputs": [],
             "results": [value("literal", "i4", count)],
             "attrs": {"values": request["values"]}}], "outputs": ["literal"]}
    count = len(request["lhs"])
    lhs, rhs = value("lhs", "i4", count), value("rhs", "i4", len(request["rhs"]))
    widened = value("wide", "i8", count)
    summed = value("sum", "i4", count)
    packed = value("packed", "i8", (count + 1) // 2)
    return {"inputs": [lhs, rhs], "nodes": [
        {"op": "qint4_add", "inputs": ["lhs", "rhs"], "results": [summed]},
        {"op": "qint4_pack", "inputs": ["sum"], "results": [packed]}],
        "outputs": ["sum", "packed"],
        "declarations": [
            {"op": "sext_i4_i8", "parameter_types": [lhs], "results": [widened]},
            {"op": "add_i8", "parameter_types": [widened, widened], "results": [widened]},
            {"op": "clamp_i8", "parameter_types": [widened], "results": [widened]},
            {"op": "trunc_i8_i4", "parameter_types": [widened], "results": [summed]}]}


def qint4_lowered_graph(request: dict) -> dict:
    graph = qint4_graph(request)
    count = len(request["lhs"])
    def node(op, operands, name, element, attrs=None):
        result = {"op": op, "inputs": operands,
                  "results": [{"name": name, "element": element, "shape": [count]}]}
        if attrs is not None:
            result["attrs"] = attrs
        return result
    graph["nodes"] = [node("sext_i4_i8", ["lhs"], "left", "i8"),
                      node("sext_i4_i8", ["rhs"], "right", "i8"),
                      node("add_i8", ["left", "right"], "wide_sum", "i8"),
                      node("clamp_i8", ["wide_sum"], "clamped", "i8", {"lower": -8, "upper": 7}),
                      node("trunc_i8_i4", ["clamped"], "sum", "i4"), graph["nodes"][1]]
    return graph


def input_format_graph(text: str) -> dict:
    """Independent source oracle for the line-oriented graph import task.

    Keep names and edges, including repeated operands. Native candidates must
    construct this graph; reporting operation names alone is not sufficient.
    """
    identifier = r"%([A-Za-z_][A-Za-z_0-9]*)"
    tensor_type = r"tensor<((?:\d+x)*)(f32|f64|i8|i16|i32|i64)>"
    number = r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?"
    graph = {"inputs": [], "nodes": [], "outputs": []}
    values = {}
    returned = False

    def define(name, element, shape):
        if name in values:
            raise ValueError("duplicate-value")
        value = {"name": name, "element": element, "shape": shape}
        values[name] = value
        return value

    def lookup(name):
        if name not in values:
            raise ValueError("undefined-value")
        return values[name]

    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        if returned:
            raise ValueError("syntax-error")
        match = re.fullmatch(r"input\s+" + identifier + r"\s+" + tensor_type, line)
        if match:
            if graph["nodes"]:
                raise ValueError("syntax-error")
            name, dims, element = match.groups()
            graph["inputs"].append(define(name, element, [int(d) for d in dims.split("x") if d]))
            continue
        match = re.fullmatch(identifier + r"\s*=\s*const\s+(" + number + r")\s+" + tensor_type, line)
        if match:
            name, literal, dims, element = match.groups()
            if element.startswith("i"):
                if re.fullmatch(r"[+-]?\d+", literal) is None:
                    raise ValueError("type-error")
                value = int(literal)
                bits = int(element[1:])
                if not -(1 << (bits - 1)) <= value < (1 << (bits - 1)):
                    raise ValueError("literal-out-of-range")
            else:
                value = float(literal)
                try:
                    if element == "f32":
                        value = struct.unpack("<f", struct.pack("<f", value))[0]
                except OverflowError as failure:
                    raise ValueError("literal-out-of-range") from failure
                if not math.isfinite(value):
                    raise ValueError("literal-out-of-range")
            result = define(name, element, [int(d) for d in dims.split("x") if d])
            graph["nodes"].append({"op": "splat", "inputs": [], "results": [result],
                                   "attrs": {"value": value}})
            continue
        match = re.fullmatch(identifier + r"\s*=\s*(add|mul|relu)\s+(.+)", line)
        if match:
            name, operation, arguments = match.groups()
            operands = arguments.split()
            if (len(operands) != (1 if operation == "relu" else 2) or
                    any(re.fullmatch(identifier, operand) is None for operand in operands)):
                raise ValueError("syntax-error")
            inputs = [operand[1:] for operand in operands]
            types = [lookup(operand) for operand in inputs]
            if any((value["element"], value["shape"]) !=
                   (types[0]["element"], types[0]["shape"]) for value in types[1:]):
                raise ValueError("type-error")
            result = define(name, types[0]["element"], list(types[0]["shape"]))
            graph["nodes"].append({"op": operation, "inputs": inputs, "results": [result]})
            continue
        match = re.fullmatch(r"return\s+" + identifier, line)
        if match:
            name = match.group(1)
            lookup(name)
            graph["outputs"] = [name]
            returned = True
            continue
        raise ValueError("syntax-error")
    if not returned:
        raise ValueError("syntax-error")
    return graph


def imported_graph(actual: dict) -> dict:
    """Normalize primitive spelling and typed splat semantics, not graph edges."""
    for node in actual["nodes"]:
        match = re.fullmatch(r"(splat|add|mul|relu)__type\d+", node["op"])
        if match:
            node["op"] = match[1]
        if node["op"] == "splat" and len(node["results"]) == 1:
            element = node["results"][0]["type"]["element"]
            for attribute in node["attrs"]:
                if attribute[0] == "value" and element == "f32":
                    value = attribute[1]
                    if isinstance(value, bool) or not isinstance(value, (int, float)):
                        raise ValueError("invalid splat constant")
                    try:
                        attribute[1] = struct.unpack("<f", struct.pack("<f", value))[0]
                    except OverflowError as failure:
                        raise ValueError("literal-out-of-range") from failure
                    if not math.isfinite(attribute[1]):
                        raise ValueError("literal-out-of-range")
    return actual


def graph_fixture(request: dict, system: str) -> str:
    """Create typed SSA calls with native attributes, not a serialized request."""
    def tensor(value: dict) -> str:
        dims, element = value["shape"], value["element"]
        if system == "Joggle":
            return f"tensor<{element}, [" + ", ".join("_" if d < 0 else str(d) for d in dims) + "]>"
        return "tensor<" + "".join(("?" if d < 0 else str(d)) + "x" for d in dims) + element + ">"

    def result_type(types: list[str]) -> str:
        return types[0] if len(types) == 1 else "(" + ", ".join(types) + ")"

    values = {value["name"]: tensor(value) for value in request["inputs"]}
    declarations, body = {}, []
    extra = request.get("declarations", [])
    for index, node in enumerate([*extra, *request["nodes"]]):
        operands = node.get("inputs", [])
        inputs = ([tensor(value) for value in node["parameter_types"]]
                  if index < len(extra) and "parameter_types" in node else [values[name] for name in operands])
        outputs = [tensor(value) for value in node["results"]]
        signature = (inputs, outputs)
        if node["op"] in declarations and declarations[node["op"]] != signature:
            raise ValueError("fixture symbols must have one function signature")
        declarations[node["op"]] = signature
        if index < len(extra):
            continue
        for result, ty in zip(node["results"], outputs, strict=True):
            if result["name"] in values:
                raise ValueError("fixture result redefines an SSA name")
            values[result["name"]] = ty
        names = [value["name"] for value in node["results"]]
        attrs = node.get("attrs", {})
        if system == "Joggle":
            if attrs:
                body.append("  [" + ", ".join(f"{key}: {json.dumps(value)}" for key, value in attrs.items()) + "]")
            body.append(f"  let {', '.join(names)} = {node['op']}({', '.join(operands)})")
        else:
            metadata = (" {" + ", ".join(f"{key} = {native_attr(value)}" for key, value in attrs.items()) + "}") if attrs else ""
            body.append(f"    {', '.join('%' + name for name in names)} = func.call @{node['op']}"
                        f"({', '.join('%' + name for name in operands)}){metadata} : "
                        f"({', '.join(inputs)}) -> {result_type(outputs)}")
    returns = [values[name] for name in request["outputs"]]
    if system == "Joggle":
        declarations_text = [f"fn {name}(" + ", ".join(f"a{i}: {ty}" for i, ty in enumerate(inputs)) +
                             f") -> {result_type(outputs)};" for name, (inputs, outputs) in declarations.items()]
        params = ", ".join(f"{value['name']}: {tensor(value)}" for value in request["inputs"])
        return "\n".join(["mod fixture", "use tensor", *declarations_text,
                          f"fn subject({params}) -> {result_type(returns)} {{", *body,
                          "  return " + ", ".join(request["outputs"]), "}", ""])
    declarations_text = [f"  func.func private @{name}({', '.join(inputs)}) -> {result_type(outputs)}"
                         for name, (inputs, outputs) in declarations.items()]
    params = ", ".join(f"%{value['name']}: {tensor(value)}" for value in request["inputs"])
    return "\n".join(["module {", *declarations_text,
                      f"  func.func @subject({params}) -> {result_type(returns)} {{", *body,
                      "    func.return " + ", ".join("%" + name for name in request["outputs"]) +
                      " : " + ", ".join(returns), "  }", "}", ""])


def sandbox_policy(args: argparse.Namespace, work: Path) -> str:
    """Grant native toolchains read access, with writes confined to this trial."""
    if sys.platform != "darwin" or not Path("/usr/bin/sandbox-exec").is_file():
        raise ValueError("--isolate requires macOS sandbox-exec")
    roots = [Path(p) for p in ("/System", "/usr", "/bin", "/sbin", "/opt/homebrew",
                              "/Library/Developer", "/Library/Apple")]
    roots += [work.resolve(), Path(sys.prefix).resolve(), *args.sandbox_read]
    if args.joggle:
        roots.append(args.joggle.resolve().parent)
    if args.builtin_mods:
        roots.append(args.builtin_mods.resolve())
    if args.xdsl_python:
        roots.append(args.xdsl_python.absolute().parent.parent.resolve())
    literals = [args.source.resolve(), ROOT / "extensions/CMakeLists.txt",
                ROOT / "extensions/mlir-driver.cpp", ROOT / "extensions/xdsl-driver.py",
                ROOT / "extensions/emit-graph-manifest/reference.py",
                Path("/"), Path("/dev/null"), Path("/dev/random"), Path("/dev/urandom")]
    read_rules = [f"(subpath {json.dumps(str(path.resolve()))})" for path in roots]
    read_rules += [f"(literal {json.dumps(str(path))})" for path in literals]
    return ("(version 1)\n(deny default)\n"
            "(allow process-exec process-fork sysctl-read file-read-metadata)\n"
            "(allow mach-lookup (global-name \"com.apple.system.logger\"))\n"
            "(allow file-read* " + " ".join(read_rules) + ")\n"
            "(allow file-write* (subpath " + json.dumps(str(work.resolve())) + ") "
            "(literal \"/dev/null\"))\n")


def execute(command: list[str | Path], timeout: float, policy: str | None = None,
            scratch: Path | None = None) -> dict:
    argv = list(map(str, command))
    environment = None
    if policy:
        argv = ["/usr/bin/sandbox-exec", "-p", policy, *argv]
        environment = {"PATH": "/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin",
                       "LANG": "C", "TMPDIR": str(scratch),
                       "PYTHONDONTWRITEBYTECODE": "1", "PYTHONNOUSERSITE": "1"}
        # Preserve the existing home path for tool discovery, without granting
        # access to its contents or inheriting credentials from the environment.
        if "HOME" in os.environ:
            environment["HOME"] = os.environ["HOME"]
    try:
        with subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              text=True, env=environment,
                              cwd=scratch.parent if policy else None,
                              start_new_session=True) as process:
            try:
                stdout, stderr = process.communicate(timeout=timeout)
                return {"command": list(map(str, command)), "exit_code": process.returncode,
                        "stdout": stdout, "stderr": stderr, "timeout": False}
            finally:
                # Also reap descendants after a successful parent exits.
                # No candidate process may survive into the next fixture.
                if os.name == "posix":
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                elif process.poll() is None:
                    process.kill()
                process.communicate()
    except subprocess.TimeoutExpired:
        return {"command": argv, "exit_code": None, "stdout": "",
                "stderr": "task process exceeded timeout", "timeout": True}
    except OSError as failure:
        return {"command": argv, "exit_code": None, "stdout": "",
                "stderr": str(failure), "timeout": False}


WRAPPER_DRIVER = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef char require_float32[sizeof(float) == 4 ? 1 : -1];
void task_kernel(const float *, float *, size_t);
static float value(uint32_t bits) { float x; memcpy(&x, &bits, 4); return x; }
static uint32_t bits(float x) { uint32_t b; memcpy(&b, &x, 4); return b; }
int main(int argc, char **argv) {
  size_t n = (size_t)(argc - 1);
  float *in = malloc((n + 2) * sizeof(float));
  float *out = malloc((n + 2) * sizeof(float));
  float *saved = malloc((n + 2) * sizeof(float));
  if (!in || !out || !saved) return 2;
  for (size_t i = 0; i < n + 2; ++i) in[i] = out[i] = value(0x7fc00123);
  for (size_t i = 0; i < n; ++i) in[i + 1] = value((uint32_t)strtoul(argv[i + 1], NULL, 16));
  memcpy(saved, in, (n + 2) * sizeof(float));
  task_kernel(in + 1, out + 1, n);
  if (memcmp(saved, in, (n + 2) * sizeof(float)) || bits(out[0]) != 0x7fc00123 ||
      bits(out[n + 1]) != 0x7fc00123) return 3;
  for (size_t i = 0; i < n; ++i) printf("%08x ", bits(out[i + 1]));
  task_kernel(in + 1, in + 1, n);
  if (bits(in[0]) != 0x7fc00123 || bits(in[n + 1]) != 0x7fc00123) return 4;
  for (size_t i = 0; i < n; ++i) printf("%08x ", bits(in[i + 1]));
  if (!n) task_kernel(NULL, NULL, 0);
  free(in); free(out); free(saved);
  return 0;
}
'''


QINT4_DRIVER = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int task_kernel(const int8_t*, const int8_t*, int8_t*, uint8_t*, size_t);
int main(int argc, char **argv) {
  if ((argc - 1) % 2) return 2;
  size_t n = (size_t)(argc - 1) / 2, packed_n = (n + 1) / 2;
  int8_t *a = malloc(n+2), *b = malloc(n+2), *v = malloc(n+2);
  int8_t *saved_a = malloc(n+2), *saved_b = malloc(n+2);
  uint8_t *p = malloc(packed_n+2);
  if (!a || !b || !v || !p || !saved_a || !saved_b) return 3;
  for (int probe = 0; probe <= (n ? 256 : 0); ++probe) {
    memset(a, 85, n+2); memset(b, 85, n+2);
    memset(v, 85, n+2); memset(p, 165, packed_n+2);
    for (size_t i = 0; i < n; ++i) {
      if (!probe) {
        char *end_a, *end_b;
        long left = strtol(argv[1+i], &end_a, 10);
        long right = strtol(argv[1+n+i], &end_b, 10);
        if (*end_a || *end_b || left < -128 || left > 127 || right < -128 || right > 127) return 4;
        a[i+1] = (int8_t)left; b[i+1] = (int8_t)right;
      } else {
        a[i+1] = (int8_t)(((probe-1)/16 + i) % 16) - 8;
        b[i+1] = (int8_t)(((probe-1)%16 + 2*i) % 16) - 8;
      }
    }
    memcpy(saved_a, a, n+2); memcpy(saved_b, b, n+2);
    int status = task_kernel(n ? a+1 : NULL, n ? b+1 : NULL,
                             n ? v+1 : NULL, n ? p+1 : NULL, n);
    int guards = v[0] == 85 && v[n+1] == 85 && p[0] == 165 && p[packed_n+1] == 165;
    int preserved = !memcmp(a, saved_a, n+2) && !memcmp(b, saved_b, n+2);
    printf("{\"status\":%d,\"guards\":%s,\"inputs_preserved\":%s,\"values\":[",
           status, guards ? "true":"false", preserved ? "true":"false");
    for (size_t i = 0; status == 0 && i < n; ++i) printf("%s%d", i ? ",":"", (int)v[i+1]);
    printf("],\"bytes_hex\":\"");
    for (size_t i = 0; status == 0 && i < packed_n; ++i) printf("%s%02x", i ? " ":"", (unsigned)p[i+1]);
    printf("\"}\n");
  }
  free(a); free(b); free(v); free(p); free(saved_a); free(saved_b);
  return 0;
}
'''


QCONV_DRIVER = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fenv.h>
int task_kernel(const int8_t*, const int8_t*, const int32_t*, int8_t*);
int main(int argc, char **argv) {
  if (argc < 5 || fesetround(FE_TONEAREST)) return 2;
  size_t nx=strtoul(argv[1],0,10), nw=strtoul(argv[2],0,10);
  size_t nb=strtoul(argv[3],0,10), ny=strtoul(argv[4],0,10);
  if ((size_t)argc != 5+nx+nw+nb) return 2;
  int8_t *x=malloc(nx+2), *w=malloc(nw+2), *y=malloc(ny+2);
  int32_t *b=malloc((nb+2)*sizeof(int32_t));
  if (!x || !w || !y || !b) return 3;
  memset(x,85,nx+2); memset(w,85,nw+2); memset(y,85,ny+2);
  for (size_t i=0;i<nb+2;i++) b[i]=123456789;
  for (size_t i=0;i<nx;i++) x[i+1]=(int8_t)strtol(argv[5+i],0,10);
  for (size_t i=0;i<nw;i++) w[i+1]=(int8_t)strtol(argv[5+nx+i],0,10);
  for (size_t i=0;i<nb;i++) b[i+1]=(int32_t)strtol(argv[5+nx+nw+i],0,10);
  int status=task_kernel(x+1,w+1,b+1,y+1);
  if (status || x[0]!=85 || x[nx+1]!=85 || w[0]!=85 || w[nw+1]!=85 ||
      y[0]!=85 || y[ny+1]!=85 || b[0]!=123456789 || b[nb+1]!=123456789) return 4;
  for (size_t i=0;i<nx;i++) if (x[i+1]!=(int8_t)strtol(argv[5+i],0,10)) return 5;
  for (size_t i=0;i<nw;i++) if (w[i+1]!=(int8_t)strtol(argv[5+nx+i],0,10)) return 5;
  for (size_t i=0;i<nb;i++) if (b[i+1]!=(int32_t)strtol(argv[5+nx+nw+i],0,10)) return 5;
  for (size_t i=0;i<ny;i++) printf("%d ",(int)y[i+1]);
  free(x); free(w); free(b); free(y);
  return 0;
}
'''


def qconv_execution(actual: object, case: dict, work: Path, compiler: Path,
                    timeout: float, policy: str | None, scratch: Path) -> dict:
    """Validate emitted kernels on independent runtime vectors, not constants."""
    import numpy as np

    if (not isinstance(actual, dict) or set(actual) != {"source", "symbol"}
            or actual["symbol"] != "task_kernel" or not isinstance(actual["source"], str)):
        return {"passed": False, "error": "invalid fused convolution emission"}
    root = work / case["id"]
    root.mkdir()
    source, driver, binary = root / "kernel.c", root / "driver.c", root / "run"
    source.write_text(actual["source"])
    driver.write_text(QCONV_DRIVER)
    build = execute([compiler, "-std=c99", "-O2", "-fno-fast-math", "-ffp-contract=off",
                     source, driver, "-lm", "-o", binary], timeout, policy, scratch)
    result = {"passed": False, "source_sha256": digest(source),
              "driver_sha256": digest(driver), "build": build, "runs": []}
    if build["exit_code"] != 0:
        return result
    request = case["input"]
    parameters = qconv_parameters(request)
    # The source sees shapes/quantization, never these seeded runtime probes.
    rng = np.random.default_rng(20260924)
    probes = [request]
    for _ in range(32):
        probes.append({**request,
            "x": rng.integers(-128, 128, size=parameters["x"].shape).tolist(),
            "w": rng.integers(-128, 128, size=parameters["w"].shape).tolist(),
            "bias": rng.integers(-256, 257, size=parameters["bias"].shape).tolist()})
    for probe in probes:
        p = qconv_parameters(probe)
        expected = np.asarray(qconv_reference(probe)["values"]).reshape(-1).tolist()
        arrays = [p[name].reshape(-1).tolist() for name in ("x", "w", "bias")]
        run = execute([binary, *map(str, [*map(len, arrays), len(expected)]),
                       *(str(value) for array in arrays for value in array)], timeout, policy, scratch)
        try:
            observed = [int(value) for value in run["stdout"].split()]
        except ValueError:
            observed = None
        passed = run["exit_code"] == 0 and observed == expected
        result["runs"].append({**run, "expected": expected, "observed": observed, "passed": passed})
        if not passed:
            return result
    result.update(passed=True, probes=len(probes), executable_sha256=digest(binary))
    return result


def qint4_execution(actual: object, case: dict, work: Path, compiler: Path,
                    timeout: float, policy: str | None, scratch: Path) -> dict:
    """Compile emitted code and exercise every qint4 pair at every lane."""
    if (not isinstance(actual, dict) or set(actual) != {"range", "source", "symbol"}
            or actual["range"] != [-8, 7] or actual["symbol"] != "task_kernel"
            or not isinstance(actual["source"], str)):
        return {"passed": False, "error": "invalid int4 emission"}
    root = work / case["id"]
    root.mkdir()
    source, driver, binary = root / "kernel.c", root / "driver.c", root / "run"
    source.write_text(actual["source"])
    driver.write_text(QINT4_DRIVER)
    build = execute([compiler, "-std=c99", "-O2", source, driver, "-o", binary],
                    timeout, policy, scratch)
    result = {"passed": False, "source_sha256": digest(source),
              "driver_sha256": digest(driver), "build": build}
    if build["exit_code"] != 0:
        return result
    request = case["input"]
    expected = qint4_reference(request)
    count = len(request["lhs"])
    probes = [request] + [{"lhs": [(pair // 16 + lane) % 16 - 8 for lane in range(count)],
                           "rhs": [(pair % 16 + 2 * lane) % 16 - 8 for lane in range(count)]}
                          for pair in range(256) if count]
    run = execute([binary, *map(str, request["lhs"]), *map(str, request["rhs"])],
                  timeout, policy, scratch)
    result["run"] = run
    try:
        observations = [json.loads(line) for line in run["stdout"].splitlines()]
        expected_rows = [{"status": 0, "guards": True, "inputs_preserved": True,
                          "values": value["values"], "bytes_hex": value["bytes_hex"]}
                         for value in (qint4_reference(probe) for probe in probes)]
        result.update({"probes": len(probes), "observations": observations,
                       "expected": expected,
                       "passed": run["exit_code"] == 0 and observations == expected_rows})
    except (ValueError, TypeError) as failure:
        result["error"] = str(failure)
    return result


def wrapper_execution(actual: object, case: dict, work: Path, compiler: Path,
                      timeout: float, policy: str | None, scratch: Path) -> dict:
    """Compile emitted C separately from the fixed driver; compare observed f32 bits."""
    if (not isinstance(actual, dict) or set(actual) != {"source", "symbol"}
            or actual["symbol"] != "task_kernel" or not isinstance(actual["source"], str)):
        return {"passed": False, "error": "expected source and task_kernel symbol"}
    root = work / case["id"]
    root.mkdir()
    source, driver, binary = root / "kernel.c", root / "driver.c", root / "run"
    source.write_text(actual["source"])
    driver.write_text(WRAPPER_DRIVER)
    build = execute([compiler, "-std=c99", "-O2", "-fno-fast-math", "-ffp-contract=off",
                     source, driver, "-o", binary], timeout, policy, scratch)
    result = {"passed": False, "source_sha256": digest(source),
              "driver_sha256": digest(driver), "build": build}
    if build["exit_code"] != 0:
        return result
    def hex32(value):
        return f'{struct.unpack("<I", struct.pack("<f", value))[0]:08x}'
    inputs = [hex32(value) for value in case["input"]["values"]]
    expected = [hex32(value) for value in case["expect"]["values"]] * 2
    run = execute([binary, *inputs], timeout, policy, scratch)
    result.update({"executable_sha256": digest(binary), "run": run,
                   "expected_bits": expected, "observed_bits": run["stdout"].split(),
                   "passed": run["exit_code"] == 0 and run["stdout"].split() == expected})
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--task", choices=sorted(SUPPORTED_TASKS | IMPORT_TASKS | COMPOUND_TASKS), required=True)
    parser.add_argument("--case", action="append", default=[],
                        help="run selected public fixtures; omitted for complete task scoring")
    parser.add_argument("--system", choices=("Joggle", "MLIR", "xDSL"), required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--build-root", type=Path, required=True)
    parser.add_argument("--joggle", type=Path)
    parser.add_argument("--builtin-mods", type=Path)
    parser.add_argument("--mlir-dir", type=Path)
    parser.add_argument("--xdsl-python", type=Path)
    parser.add_argument("--cc", type=Path, default=Path(shutil.which("clang") or "/usr/bin/cc"))
    parser.add_argument("--timeout", type=float, default=60)
    parser.add_argument("--isolate", action="store_true",
                        help="restrict native candidate processes to the trial workspace")
    parser.add_argument("--sandbox-read", type=Path, action="append", default=[],
                        help="additional read-only native toolchain directory")
    args = parser.parse_args()
    required = {"Joggle": (args.joggle, args.builtin_mods),
                "MLIR": (args.mlir_dir,), "xDSL": (args.xdsl_python,)}[args.system]
    if any(value is None for value in required):
        parser.error(f"missing native tool paths for {args.system}")
    if args.timeout <= 0:
        parser.error("timeout must be positive")
    if args.isolate and sys.platform != "darwin":
        parser.error("--isolate requires macOS sandbox-exec")
    for path in args.sandbox_read:
        if not path.is_dir() or path.resolve() in {Path("/"), Path.home(), ROOT.parent}:
            parser.error("--sandbox-read requires a specific toolchain directory")
    args.source = args.source.resolve(strict=True)
    for key in ("joggle", "builtin_mods", "xdsl_python"):
        path = getattr(args, key)
        if path is not None:
            setattr(args, key, path.absolute())
    if args.output.exists():
        parser.error("refusing to replace an existing oracle report")
    args.build_root.mkdir(parents=True, exist_ok=True)
    spec_path = ROOT / "manifests/extension-specs.json"
    spec = json.loads(spec_path.read_text())
    spec_hash = digest(spec_path)
    task = next(task for task in spec["tasks"] if task["id"] == args.task)
    compound = args.task in COMPOUND_TASKS
    rewriting = args.task in REWRITE_TASKS or compound
    importing = args.task in IMPORT_TASKS
    definition = args.task in DEFINITION_TASKS
    constructor = {
        "def-quantized-op": (quantized_definition_fixture, quantized_definition_result, "qadd"),
        "def-layout-attribute": (layout_definition_fixture, layout_definition_result, "relayout"),
        "def-target-intrinsic": (intrinsic_definition_fixture, intrinsic_definition_result, "dot4_i8"),
    }.get(args.task)
    constructing = constructor is not None
    all_cases = task["positive_cases"] + task["negative_cases"]
    if not set(args.case).issubset({case["id"] for case in all_cases}):
        parser.error("--case names a fixture outside the selected task")
    source_hash = digest(args.source)
    harness_files = [Path(__file__).resolve(), ROOT / "extensions/CMakeLists.txt",
                     ROOT / "extensions/mlir-driver.cpp", ROOT / "extensions/xdsl-driver.py"]
    if rewriting or importing:
        harness_files += [ROOT / "extensions/emit-graph-manifest/reference.jog",
                          ROOT / "extensions/emit-graph-manifest/reference.py"]
    if definition:
        harness_files.append(ROOT / "extensions/definition-observer.jog")
    record = {"schema": "extension-task-oracle/v1", "task": args.task,
              "system": args.system, "source": str(args.source),
              "source_sha256": source_hash, "task_spec_sha256": spec_hash,
              "harness_sha256": {str(path.relative_to(ROOT)): digest(path)
                                 for path in harness_files},
              "setup": [], "cases": [], "passed": False}
    record["complete_task"] = not args.case
    wrapper = args.task == "emit-kernel-wrapper"
    if wrapper or compound:
        args.cc = args.cc.resolve(strict=True)
        record["compiler_identity"] = {"path": str(args.cc), "sha256": digest(args.cc),
                                       "version": execute([args.cc, "--version"], args.timeout)}
        record["setup"].append(record["compiler_identity"]["version"])
    if rewriting and not compound:
        import numpy as np
        record["numerical_oracle"] = {"numpy_version": np.__version__,
                                      "comparison": "bitwise-except-explicit-nsz"}
        if args.task == "con-gelu-expand":
            record["numerical_oracle"].update({
                "comparison": "allclose", "reference": "binary64-formula-cast-to-result-type",
                "tolerances": {key: {"rtol": value[0], "atol": value[1]}
                               for key, value in GELU_TOLERANCES.items()}})
    with tempfile.TemporaryDirectory(prefix="task-", dir=args.build_root) as directory:
        work = Path(directory).resolve()
        scratch = work / "tmp"
        scratch.mkdir()
        policy = sandbox_policy(args, work) if args.isolate else None
        record["execution_isolation"] = {"kind": "macos-seatbelt" if policy else "none",
                                         "policy": policy}
        if ((rewriting or importing) and args.system != "Joggle") or (definition and args.system == "xDSL") or (constructing and args.system == "MLIR"):
            observer_identity = execute([args.xdsl_python or sys.executable, "-c",
                "import importlib.metadata,json,sys; print(json.dumps({'python':sys.version,"
                "'xdsl':importlib.metadata.version('xdsl')}))"], args.timeout, policy, scratch)
            record["setup"].append(observer_identity)
            if observer_identity["exit_code"] == 0:
                record["observer_identity"] = json.loads(observer_identity["stdout"])
        if args.system == "Joggle":
            mod = work / "mods/extension"
            mod.mkdir(parents=True)
            shutil.copyfile(args.source, mod / "module.jog")
            command = ([args.joggle, "read", "extension.read"] if importing else
                       [args.joggle, "run", "extension.verify"] if definition else
                       [args.joggle, "run", "extension.transform"] if rewriting else [args.joggle, "query", "extension.analyze"])
            flags = ["-M", args.builtin_mods, "-M", work / "mods"]
            if definition:
                observer = work / "mods/observer"
                observer.mkdir()
                shutil.copyfile(ROOT / "extensions/definition-observer.jog", observer / "module.jog")
            if rewriting or importing:
                observer = work / "mods/observer"
                observer.mkdir()
                observer.joinpath("module.jog").write_text(
                    (ROOT / "extensions/emit-graph-manifest/reference.jog").read_text().replace(
                        "mod extension\n", "mod observer\n", 1).replace(
                        "for op in ir.ops(subject) {",
                        'for op in ir.ops(subject) {\n    assert(ir.kind(op) == "call" || '
                        'ir.kind(op) == "return", "rewrite result contains unsupported control or operations")'))
        elif args.system == "xDSL":
            command = [args.xdsl_python, ROOT / "extensions/xdsl-driver.py", args.source]
            flags = (["--input-format"] if importing else ["--construct"] if constructing else
                     ["--definition"] if definition else ["--rewrite"] if rewriting else [])
        else:
            # A candidate must never inherit another candidate's executable.
            # Hash-separated builds also avoid timestamp-resolution races when
            # alternating sources or testing several patches in quick succession.
            build_key = hashlib.sha256(canonical({
                "source": str(args.source), "sha256": source_hash,
                "harness": record["harness_sha256"],
                "mlir_dir": str(args.mlir_dir.resolve()),
                "rewrite": rewriting,
                "definition": definition,
                "constructing": constructing,
                "importing": importing,
                "compound": compound,
            }).encode()).hexdigest()
            build = (work / "build" if args.isolate else
                     args.build_root.resolve() / "mlir" / build_key)
            configure = ["cmake", "-S", ROOT / "extensions", "-B", build,
                          f"-DMLIR_DIR={args.mlir_dir.resolve()}",
                          f"-DEXTENSION_SOURCE={args.source}",
                          f"-DEXTENSION_INPUT_FORMAT={'ON' if importing else 'OFF'}",
                          f"-DEXTENSION_REWRITE={'ON' if rewriting and not compound else 'OFF'}",
                          f"-DEXTENSION_COMPOUND={'ON' if compound else 'OFF'}",
                          f"-DEXTENSION_DEFINITION={'ON' if definition else 'OFF'}",
                          f"-DEXTENSION_CONSTRUCT={'ON' if constructing else 'OFF'}", "-DCMAKE_BUILD_TYPE=Release"]
            for argv in (configure, ["cmake", "--build", build, "--parallel", "1"]):
                step = execute(argv, args.timeout, policy, scratch)
                record["setup"].append(step)
                if step["exit_code"] != 0:
                    break
            command, flags = [build / "extension-oracle"], []
            if command[0].is_file():
                record["executable_sha256"] = digest(command[0])
        setup_ok = all(step["exit_code"] == 0 for step in record["setup"])
        if setup_ok:
            for case in all_cases:
                if args.case and case["id"] not in args.case:
                    continue
                path = work / ("input.jog" if args.system == "Joggle" else "input.mlir")
                # Runtime test vectors are oracle inputs, not emitter metadata.
                request = {"kernel": case["input"]["kernel"]} if wrapper else case["input"]
                if compound:
                    source = graph_fixture(qconv_graph(request) if args.task == "vert-fused-op"
                                           else qint4_graph(request), args.system)
                elif importing:
                    source = request["text"]
                elif constructing:
                    source = constructor[0](request, args.system)
                elif definition:
                    source = definition_fixture(request, args.system)
                elif rewriting:
                    source = graph_fixture(rewrite_graph(case["input"]), args.system)
                elif args.task == "ana-fusion-match":
                    source = fusion_fixture(case["input"], args.system)
                elif args.task == "emit-graph-manifest":
                    source = graph_fixture(case["input"], args.system)
                elif args.system == "Joggle":
                    source = ("mod fixture\n[request: " + json.dumps(request) +
                              "]\nfn subject() -> int { return 0 }\n")
                else:
                    source = "module attributes {study.request = " + native_attr(request) + "} {}\n"
                path.write_text(source)
                construction = None
                if definition and not constructing and args.system == "Joggle":
                    construction = execute([args.joggle, "query", "observer.construct", path, *flags],
                                           args.timeout, policy, scratch)
                step = (construction if construction is not None and construction["exit_code"] != 0 else
                        execute([*command, path, *flags], args.timeout, policy, scratch))
                if (rewriting or definition or importing) and "error" in case["expect"]:
                    passed = expected_rejection(step, case["expect"])
                    record["cases"].append({"id": case["id"], "input": case["input"],
                                            "expected": case["expect"], "passed": passed,
                                            "actual": None, "decode_error": "", "construction": construction, **step})
                    continue
                actual = None
                error = ""
                observation = None
                numerics = None
                emission = None
                checked_step = step
                if (rewriting or importing or constructing or (definition and args.system == "Joggle")) and step["exit_code"] == 0:
                    transformed = work / ("transformed.jog" if args.system == "Joggle" else "transformed.mlir")
                    transformed.write_text(step["stdout"])
                    if args.system == "Joggle":
                        inspect = [args.joggle, "query", "observer." + constructor[2] if constructing else
                                   "observer.analyze", transformed, *flags]
                    else:
                        inspect = [args.xdsl_python or sys.executable, ROOT / "extensions/xdsl-driver.py",
                                   "--inspect-input-format" if importing else
                                   "--inspect-definition" if constructing else "--inspect", transformed]
                        if constructing:
                            inspect.append("extension." + constructor[2])
                    observation = execute(inspect, args.timeout, policy, scratch)
                    checked_step = observation
                    if observation["exit_code"] != 0:
                        error = "post-rewrite IR observation failed: " + observation["stderr"][-6000:]
                if checked_step["exit_code"] == 0:
                    try:
                        actual = json.loads(checked_step["stdout"], parse_constant=invalid_constant)
                        canonical(actual)
                        if importing:
                            actual = imported_graph(actual)
                        if constructing:
                            actual = constructor[1](actual, request)
                        elif definition:
                            actual = definition_result(actual)
                    except ValueError as failure:
                        actual = None
                        error = str(failure)
                # Numeric tasks use the shared tolerance only for numbers;
                # object keys, sequence lengths, Booleans, and errors stay exact.
                conversion_check = {"con-gelu-expand": gelu_structure, "con-quant-expand": quant_structure,
                                    "con-layout-legalize": layout_structure}.get(args.task)
                conversion = conversion_check is not None
                expected = (graph_manifest(qconv_graph(request, fused=True)) if args.task == "vert-fused-op" else
                            graph_manifest(qint4_lowered_graph(request)) if compound else
                            graph_manifest(input_format_graph(request["text"])) if importing else
                            case["expect"] if conversion else
                            graph_manifest(rewrite_graph(case["input"], case["expect"]["eliminate"]))
                            if rewriting else expected_result(args.task, case))
                original = graph_manifest(rewrite_graph(case["input"])) if rewriting and not compound else None
                passed = (step["exit_code"] == 0 and checked_step["exit_code"] == 0 and not error and
                          (conversion_check(actual, original) if conversion else
                           equivalent(actual, expected, task["oracle"]["comparison"] == "numerical",
                                      spec["comparison_policy"])))
                if wrapper and step["exit_code"] == 0 and not error:
                    numerics = wrapper_execution(actual, case, work, args.cc, args.timeout, policy, scratch)
                    passed = numerics["passed"]
                if args.task == "def-target-intrinsic" and actual is not None:
                    numerics = {"boundary": "independent-intrinsic-interpretation",
                                "passed": passed, "result": actual["result"]}
                if compound and passed and not (args.task == "vert-fused-op" and "matches" in case["expect"]):
                    emit = ([args.joggle, "query", "extension.analyze", transformed, *flags]
                            if args.system == "Joggle" else
                            [*command, transformed] if args.system == "xDSL" else
                            [*command, transformed, "--emit"])
                    emission = execute(emit, args.timeout, policy, scratch)
                    passed = emission["exit_code"] == 0
                    if passed:
                        try:
                            emitted = json.loads(emission["stdout"], parse_constant=invalid_constant)
                            execution = qconv_execution if args.task == "vert-fused-op" else qint4_execution
                            numerics = execution(emitted, case, work, args.cc, args.timeout, policy, scratch)
                            passed = numerics["passed"]
                        except (ValueError, TypeError) as failure:
                            passed, error = False, str(failure)
                if rewriting and not compound and passed:
                    try:
                        tolerance = GELU_TOLERANCES[case["input"]["element"]] if args.task == "con-gelu-expand" else None
                        feeds = {"v0": case["input"]["lhs"], "v1": case["input"]["rhs"]} if args.task == "con-quant-expand" else None
                        numerics = rewrite_numerics(actual, original,
                                                    case["input"].get("no_signed_zeros", False), tolerance, feeds)
                        passed = numerics["passed"]
                    except (ValueError, KeyError, TypeError) as failure:
                        passed = False
                        error = str(failure)
                repeat = None
                if importing and args.system == "Joggle" and step["exit_code"] == 0:
                    # The native read command already parses, verifies, and
                    # prints. Reparse that IR without invoking the candidate.
                    repeat = execute([args.joggle, "check", transformed, *flags], args.timeout, policy, scratch)
                    passed = passed and repeat["exit_code"] == 0 and repeat["stdout"] == step["stdout"]
                if task["family"] == "emission" and step["exit_code"] == 0:
                    repeat = execute([*command, path, *flags], args.timeout, policy, scratch)
                    passed = passed and repeat["exit_code"] == 0 and repeat["stdout"] == step["stdout"]
                record["cases"].append({"id": case["id"], "input": case["input"],
                                        "expected": expected, "actual": actual,
                                        "expected_sha256": hashlib.sha256(canonical(expected).encode()).hexdigest(),
                                        "passed": passed, "repeat": repeat,
                                        "observation": observation, "numerics": numerics,
                                        "emission": emission,
                                        "construction": construction,
                                        "decode_error": error, **step})
        record["passed"] = (setup_ok and bool(record["cases"]) and
                            all(case["passed"] for case in record["cases"]) and
                            digest(args.source) == source_hash and
                            digest(spec_path) == spec_hash and
                            (not (wrapper or compound) or digest(args.cc) == record["compiler_identity"]["sha256"]) and
                            all(digest(ROOT / path) == value
                                for path, value in record["harness_sha256"].items()))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x") as stream:
        json.dump(record, stream, indent=2, sort_keys=True, allow_nan=False)
        stream.write("\n")
    passed = sum(case["passed"] for case in record["cases"])
    print(f"{args.system}/{args.task}: {passed}/{len(record['cases'])} cases; {args.output}")
    return 0 if record["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
