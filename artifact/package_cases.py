"""Frozen package-change fixtures, independent semantics, and reference edits."""

from __future__ import annotations

import copy
import math

import numpy as np

from run_extension_task import (qconv_graph, qconv_parameters, qint4_graph,
                                qint4_lowered_graph)


def replace(source: str, old: str, new: str, count: int = 1) -> str:
    if source.count(old) != count:
        raise ValueError(f"reference edit anchor differs: {old!r}")
    return source.replace(old, new)


def candidate_source(source: str, system: str, change: str) -> str:
    """Apply the same behavior change to each admitted native implementation."""
    if change.endswith("-integrate"):
        return source
    if change == "lowbit-symmetric":
        old, new = {
            "Joggle": ('"lower", -8)', '"lower", -7)'),
            "MLIR": ('"lower", builder.getI64IntegerAttr(-8)', '"lower", builder.getI64IntegerAttr(-7)'),
            "xDSL": ('["lower"] = IntegerAttr(-8, 64)', '["lower"] = IntegerAttr(-7, 64)'),
        }[system]
        return replace(source, old, new)
    if change == "lowbit-subtract":
        source = replace(source, "qint4_add", "qint4_sub", 2)
        source = replace(source, '"add_i8"', '"sub_i8"')
        old, new = ("(int)a[i]+(int)b[i]", "(int)a[i]-(int)b[i]") if system == "Joggle" else (
            "(int)a[i] + (int)b[i]", "(int)a[i] - (int)b[i]")
        return replace(source, old, new)
    if change == "lowbit-msb-first":
        if system == "Joggle":
            return replace(source,
                "packed[i/2]=nibble; else packed[i/2]|=(uint8_t)(nibble<<4);",
                "packed[i/2]=(uint8_t)(nibble<<4); else packed[i/2]|=nibble;")
        return replace(source,
            "packed[i / 2] = nibble;\n    else packed[i / 2] |= (uint8_t)(nibble << 4);",
            "packed[i / 2] = (uint8_t)(nibble << 4);\n    else packed[i / 2] |= nibble;")
    if change == "qconv-ties-away":
        return replace(source, "nearbyint(scaled)", "round(scaled)")
    if change == "qconv-relu6":
        old, new = {
            "Joggle": ("for op in [conv, quant]", "for op in [conv, quant, relu]"),
            "MLIR": ("for (auto op : {conv, quant})", "for (auto op : {conv, quant, relu})"),
            "xDSL": ("for op in (conv, quant):", "for op in (conv, quant, relu):"),
        }[system]
        source = replace(source, old, new)
        if system == "Joggle":
            source = replace(source, '      let labels = ',
                '      source += "#define MAX_REAL (" + text(base.get(ir.meta(op), "max_value", 1e300)) + ")\\n"\n'
                '      let labels = ')
            source = replace(source, '  source += "    y[',
                '  source += "    if(value>nearbyint(MAX_REAL/OUT_SCALE)+ZERO) value=nearbyint(MAX_REAL/OUT_SCALE)+ZERO;\\n"\n'
                '  source += "    y[')
        elif system == "MLIR":
            source = replace(source, "  std::ostringstream source;",
                '  auto cap = fused->getAttrOfType<mlir::FloatAttr>("max_value");\n'
                '  const double maxReal = cap ? cap.getValueAsDouble() : 1e300;\n'
                "  std::ostringstream source;")
            source = replace(source, '  source << R"C(',
                '  source << "#define MAX_REAL (" << maxReal << ")\\n";\n  source << R"C(')
        else:
            source = replace(source, "    constants = dict(",
                '    cap = op.attributes.get("max_value")\n'
                '    max_real = cap.value.data if cap is not None else 1e300\n'
                "    constants = dict(MAX_REAL=max_real, ")
        if system != "Joggle":
            source = replace(source, "      y[",
                "      if (value > nearbyint(MAX_REAL/OUT_SCALE)+ZERO)\n"
                "        value = nearbyint(MAX_REAL/OUT_SCALE)+ZERO;\n      y[")
        return source
    if change == "qconv-stride2":
        if system == "Joggle":
            source = replace(source, '      let labels = ',
                '      let strides = base.get(ir.meta(op), "strides", [1, 1])\n'
                '      assert(len(strides) == 2 && int(strides[0]) > 0 && int(strides[1]) > 0, "invalid-strides")\n'
                '      source += "#define SY (" + text(int(strides[0])) + ")\\n#define SX (" + text(int(strides[1])) + ")\\n"\n'
                '      let labels = ')
        elif system == "MLIR":
            source = replace(source, "  std::ostringstream source;",
                '  auto strides = fused->getAttrOfType<mlir::ArrayAttr>("strides");\n'
                '  if (strides && strides.size() != 2) throw std::runtime_error("invalid-strides");\n'
                '  int64_t sy = strides ? mlir::cast<mlir::IntegerAttr>(strides[0]).getInt() : 1;\n'
                '  int64_t sx = strides ? mlir::cast<mlir::IntegerAttr>(strides[1]).getInt() : 1;\n'
                '  if (sy <= 0 || sx <= 0) throw std::runtime_error("invalid-strides");\n'
                "  std::ostringstream source;")
            source = replace(source, '  source << R"C(',
                '  source << "#define SY (" << sy << ")\\n#define SX (" << sx << ")\\n";\n'
                '  source << R"C(')
        else:
            source = replace(source, "    constants = dict(",
                '    strides = op.attributes.get("strides")\n'
                '    steps = tuple(v.value.data for v in strides) if strides is not None else (1, 1)\n'
                '    if len(steps) != 2 or min(steps) <= 0:\n'
                '        raise ValueError("invalid-strides")\n'
                "    constants = dict(SY=steps[0], SX=steps[1], ")
        source = replace(source, "oh=H-KH+1, ow=W-KW+1", "oh=(H-KH)/SY+1, ow=(W-KW)/SX+1")
        return replace(source, "(n*H+h+kh)*W+col+kw", "(n*H+h*SY+kh)*W+col*SX+kw")
    raise ValueError(f"unknown package change: {change}")


def fixtures(change: str, original: dict) -> list[dict]:
    cases = copy.deepcopy(original["positive_cases"] + original["negative_cases"])
    if change == "qconv-ties-away":
        for case in cases:
            case["input"]["rounding"] = "nearest_away"
        # Positive and negative half ties; ReLU can mask negative differences.
        for value in (1, 3, -1, -3):
            cases.append({"id": f"half-tie-{value}", "input": {
                "x": [[[[value]]]], "w": [[[[1]]]], "bias": [0],
                "scales": {"acc": 0.5, "out": 1.0}, "output_zero": -4,
                "rounding": "nearest_away"}, "expect": {}})
    elif change == "qconv-relu6":
        bounded = copy.deepcopy(cases)
        for case in bounded:
            case["id"] = "bounded-" + case["id"]
            case["input"]["max_value"] = 6.0
        cases += bounded
        for value in (-8, 3, 12, 64):
            cases.append({"id": f"cap-{value}", "input": {
                "x": [[[[value]]]], "w": [[[[1]]]], "bias": [0],
                "scales": {"acc": 1.0, "out": 0.5}, "output_zero": -3,
                "max_value": 6.0}, "expect": {}})
    elif change == "qconv-stride2":
        rng = np.random.default_rng(20260924)
        for index, (n, h, w, ci, kh, kw, co) in enumerate(
                [(1, 5, 6, 1, 2, 3, 1), (2, 6, 5, 3, 3, 2, 2),
                 (1, 7, 7, 2, 3, 3, 3), (2, 4, 4, 1, 2, 2, 2)]):
            cases.append({"id": f"stride2-{index}", "input": {
                "x": rng.integers(-8, 8, size=(n, h, w, ci)).tolist(),
                "w": rng.integers(-4, 4, size=(kh, kw, ci, co)).tolist(),
                "bias": rng.integers(-4, 5, size=co).tolist(),
                "scales": {"acc": 0.25, "out": 0.5}, "output_zero": -3,
                "strides": [2, 2]}, "expect": {}})
        shared = copy.deepcopy(next(c for c in cases if c["id"] == "shared-conv"))
        shared["id"] = "stride2-shared"
        shared["input"]["strides"] = [2, 2]
        cases.append(shared)
    return cases


def graph(change: str, request: dict, *, transformed: bool = False) -> dict:
    if change.startswith("lowbit-"):
        result = (qint4_lowered_graph(request) if transformed else qint4_graph(request))
        if change == "lowbit-subtract":
            for node in result["nodes"] + result.get("declarations", []):
                node["op"] = {"qint4_add": "qint4_sub", "add_i8": "sub_i8"}.get(node["op"], node["op"])
        if change == "lowbit-symmetric" and transformed:
            for node in result["nodes"]:
                if node["op"] == "clamp_i8":
                    node["attrs"]["lower"] = -7
        return result
    result = qconv_graph(request, fused=transformed)
    if "strides" in request and "x" in request:
        p = qconv_parameters(request)
        n, h, w, _ = p["x"].shape
        kh, kw, _, co = p["w"].shape
        sy, sx = request["strides"]
        shape = [n, (h-kh)//sy+1, (w-kw)//sx+1, co]
        for node in result["nodes"] + result.get("declarations", []):
            for output in node["results"]:
                output["shape"] = shape
    for key, operations in (("strides", ("qconv", "fused_qconv_relu")),
                            ("rounding", ("requantize", "fused_qconv_relu")),
                            ("max_value", ("relu", "fused_qconv_relu"))):
        if key in request:
            for node in result["nodes"]:
                if node["op"] in operations:
                    node.setdefault("attrs", {})[key] = request[key]
    return result


def lowbit_expected(change: str, request: dict) -> dict:
    subtract = change == "lowbit-subtract"
    low = -7 if change == "lowbit-symmetric" else -8
    values = [max(low, min(7, a-b if subtract else a+b))
              for a, b in zip(request["lhs"], request["rhs"])]
    packed = []
    for index in range(0, len(values), 2):
        a = values[index] & 15
        b = values[index+1] & 15 if index+1 < len(values) else 0
        packed.append((a << 4) | b if change == "lowbit-msb-first" else a | (b << 4))
    return {"status": 0, "guards": True, "inputs_preserved": True,
            "values": values, "bytes_hex": bytes(packed).hex(" ")}


def qconv_expected(change: str, request: dict) -> list[int]:
    p = qconv_parameters(request)
    x, weights, bias = p["x"], p["w"], p["bias"]
    n, height, width, _ = x.shape
    kh, kw, _, co = weights.shape
    sy, sx = request.get("strides", (1, 1))
    zero = p["output_zero"]
    cap = min(127, round(request["max_value"]/p["out_scale"])+zero) if "max_value" in request else 127
    result = []
    for batch in range(n):
        for h in range(0, height-kh+1, sy):
            for w in range(0, width-kw+1, sx):
                for channel in range(co):
                    acc = int(np.sum(x[batch, h:h+kh, w:w+kw, :].astype(np.int64) *
                                     weights[:, :, :, channel].astype(np.int64))) + int(bias[channel])
                    scaled = acc * p["acc_scale"] / p["out_scale"]
                    rounded = (math.copysign(math.floor(abs(scaled)+0.5), scaled)
                               if change == "qconv-ties-away" else round(scaled))
                    result.append(max(zero, min(cap, int(rounded)+zero)))
    return result
