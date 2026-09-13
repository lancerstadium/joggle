#!/usr/bin/env python3
"""TVM policy-task baseline for the frozen extension study."""

import argparse
import json
import tempfile
from pathlib import Path

import numpy as np
import tvm
from tvm.script import tirx as T


@T.prim_func
def measured(x: T.int32) -> None:
    T.evaluate(T.call_extern("opaque", x, dtype="int32"))


@tvm.script.ir_module
class Chain:
    @T.prim_func(s_tir=True)
    def main(
        a: T.Buffer((4,), "float32"),
        b: T.Buffer((4,), "float32"),
        c: T.Buffer((4,), "float32"),
        d: T.Buffer((4,), "float32"),
        out: T.Buffer((4,), "float32"),
    ) -> None:
        first = T.sblock_alloc_buffer((4,), "float32")
        second = T.sblock_alloc_buffer((4,), "float32")
        for i in range(4):
            with T.sblock("first"):
                index = T.axis.spatial(4, i)
                first[index] = a[index] + b[index]
        for i in range(4):
            with T.sblock("second"):
                index = T.axis.spatial(4, i)
                second[index] = first[index] + c[index]
        for i in range(4):
            with T.sblock("third"):
                index = T.axis.spatial(4, i)
                out[index] = second[index] + d[index]


def weighted_cost(function, call_weight: int, other_weight: int) -> int:
    cost = 0

    def visit(node) -> None:
        nonlocal cost
        if isinstance(node, tvm.ir.Call):
            cost += call_weight
        elif isinstance(node, tvm.tirx.Evaluate):
            cost += other_weight

    tvm.tirx.stmt_functor.post_order_visit(function.body, visit)
    return cost


def static_extent(schedule, block) -> int | None:
    extent = 1
    for loop in schedule.get_loops(block):
        value = schedule.get(loop).extent
        if not isinstance(value, tvm.tirx.IntImm):
            return None
        extent *= int(value)
    return extent


def recursive_calls(schedule, block) -> int:
    count = 0

    def visit(node) -> None:
        nonlocal count
        if isinstance(node, tvm.ir.Call):
            count += 1

    tvm.tirx.stmt_functor.post_order_visit(schedule.get(block).body, visit)
    return count


def apply_policy(max_extent: int, max_calls: int):
    schedule = tvm.s_tir.Schedule(Chain)
    while True:
        root = schedule.get_sblock("root")
        selected = None
        for block in schedule.get_child_blocks(root):
            extent = static_extent(schedule, block)
            if (
                len(schedule.get_consumers(block)) == 1
                and extent is not None
                and extent <= max_extent
                and recursive_calls(schedule, block) <= max_calls
            ):
                selected = block
                break
        if selected is None:
            return schedule
        schedule.compute_inline(selected)


def loop_count(module) -> int:
    count = 0

    def visit(node) -> None:
        nonlocal count
        if isinstance(node, tvm.tirx.For):
            count += 1

    tvm.tirx.stmt_functor.post_order_visit(module["main"].body, visit)
    return count


def execute(schedule, fixture) -> None:
    inputs = [
        np.asarray(fixture["inputs"][name], dtype="float32")
        for name in ("a", "b", "c", "d")
    ]
    expected = np.asarray(fixture["expected"], dtype="float32")
    built = tvm.compile(schedule.mod, target="c")
    with tempfile.TemporaryDirectory() as directory:
        library = Path(directory) / "chain.so"
        built.export_library(library)
        loaded = tvm.runtime.load_module(library)
        actual = np.zeros_like(expected)
        loaded(*inputs, actual)
    np.testing.assert_allclose(
        actual,
        expected,
        rtol=0,
        atol=fixture["absolute_tolerance"],
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("fixture", type=Path)
    args = parser.parse_args()
    fixture = json.loads(args.fixture.read_text(encoding="utf-8"))
    for case in fixture["measurement"]["cases"]:
        actual = weighted_cost(
            measured,
            case["call_weight"],
            case["other_weight"],
        )
        assert actual == case["expected"], (actual, case)
    for case in fixture["fusion"]["cases"]:
        schedule = apply_policy(case["max_extent"], case["max_calls"])
        assert loop_count(schedule.mod) == case["expected_loops"]
        execute(schedule, fixture["fusion"])
    print(json.dumps({"task": "policy", "status": "pass"}))


if __name__ == "__main__":
    main()
