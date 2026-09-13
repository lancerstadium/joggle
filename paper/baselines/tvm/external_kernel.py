#!/usr/bin/env python3
"""TVM external-kernel baseline for the frozen extension study."""

import argparse
import json
import os
import subprocess
import tempfile
from pathlib import Path

import tvm
from tvm_ffi import libinfo as ffi_libinfo
from tvm.script import tirx as T


@T.prim_func(s_tir=True)
def matmul(
    a: T.handle,
    b: T.handle,
    out: T.handle,
    rows: T.int64,
    columns: T.int64,
    inner: T.int64,
) -> None:
    left = T.match_buffer(a, (rows, inner), "float32")
    right = T.match_buffer(b, (inner, columns), "float32")
    result = T.match_buffer(out, (rows, columns), "float32")
    T.evaluate(
        T.call_extern(
            "edge_matmul",
            left.data,
            right.data,
            rows,
            columns,
            inner,
            result.data,
            dtype="void",
        )
    )


@T.prim_func(s_tir=True)
def conv2d(
    x: T.handle,
    weight: T.handle,
    bias: T.handle,
    out: T.handle,
    batch: T.int64,
    channels: T.int64,
    height: T.int64,
    width: T.int64,
    outputs: T.int64,
    group_channels: T.int64,
    kernel_height: T.int64,
    kernel_width: T.int64,
    output_height: T.int64,
    output_width: T.int64,
    stride_height: T.int64,
    stride_width: T.int64,
    pad_top: T.int64,
    pad_left: T.int64,
    dilation_height: T.int64,
    dilation_width: T.int64,
    groups: T.int64,
    has_bias: T.int64,
    relu: T.int64,
) -> None:
    image = T.match_buffer(x, (batch, channels, height, width), "float32")
    kernel = T.match_buffer(
        weight,
        (outputs, group_channels, kernel_height, kernel_width),
        "float32",
    )
    values = T.match_buffer(bias, (outputs,), "float32")
    result = T.match_buffer(
        out,
        (batch, outputs, output_height, output_width),
        "float32",
    )
    T.evaluate(
        T.call_extern(
            "edge_conv2d",
            image.data,
            kernel.data,
            batch,
            height,
            width,
            outputs,
            group_channels,
            kernel_height,
            kernel_width,
            output_height,
            output_width,
            stride_height,
            stride_width,
            pad_top,
            pad_left,
            dilation_height,
            dilation_width,
            groups,
            channels * height * width,
            height * width,
            width,
            T.int64(1),
            group_channels * kernel_height * kernel_width,
            kernel_height * kernel_width,
            kernel_width,
            T.int64(1),
            outputs * output_height * output_width,
            output_height * output_width,
            output_width,
            T.int64(1),
            result.data,
            dtype="void",
        )
    )
    for n, m, h, w in T.grid(batch, outputs, output_height, output_width):
        result[n, m, h, w] = T.if_then_else(
            has_bias != 0,
            result[n, m, h, w] + values[m],
            result[n, m, h, w],
        )
        result[n, m, h, w] = T.if_then_else(
            (relu != 0) & (result[n, m, h, w] < T.float32(0)),
            T.float32(0),
            result[n, m, h, w],
        )


@T.prim_func(s_tir=True)
def extrema(x: T.handle, low: T.handle, high: T.handle) -> None:
    values = T.match_buffer(x, (4,), "float32")
    minimum = T.match_buffer(low, (1,), "float32")
    maximum = T.match_buffer(high, (1,), "float32")
    T.evaluate(
        T.call_extern(
            "edge_extrema",
            values.data,
            minimum.data,
            maximum.data,
            dtype="void",
        )
    )


def accepts_layout(actual: list[int], expected: list[int]) -> bool:
    return actual == expected


def validate_fixture(fixture: dict) -> None:
    assert fixture["dtype"] == "float32"
    assert fixture["matrix_shapes"] == [
        {"left": [2, 3], "right": [3, 2], "output": [2, 2]},
        {"left": [1, 3], "right": [3, 4], "output": [1, 4]},
    ]
    assert fixture["convolution_cases"] == [
        {"weights": "argument", "bias": False, "activation": "none"},
        {"weights": "constant", "bias": False, "activation": "none"},
        {"weights": "argument", "bias": True, "activation": "relu"},
    ]
    assert fixture["multi_result"] == {
        "input_elements": 4,
        "results": ["minimum", "maximum"],
    }


def compile_kernel(kernel: Path, directory: Path) -> Path:
    artifact = directory / "kernel.o"
    subprocess.run(
        [
            os.environ.get("CC", "cc"),
            "-std=c11",
            "-O2",
            "-c",
            str(kernel.resolve()),
            "-o",
            str(artifact),
        ],
        check=True,
    )
    return artifact


def build(kernel: Path, directory: Path):
    artifact = directory / "external.so"
    module = tvm.IRModule(
        {"matmul": matmul, "conv2d": conv2d, "extrema": extrema}
    )
    tvm.compile(module, target="c").export_library(
        artifact,
        addons=[str(kernel)],
    )
    return artifact


def run_harness(artifact: Path, harness: Path, directory: Path) -> None:
    source = Path(__file__).resolve().with_name("external_bridge.c")
    header = source.with_suffix(".h")
    library_path = os.environ.get("TVM_LIBRARY_PATH")
    if not library_path:
        raise RuntimeError("TVM_LIBRARY_PATH must name the pinned TVM build")
    library_dir = Path(library_path.split(os.pathsep)[0]).resolve()
    command = [
        os.environ.get("CC", "cc"),
        "-std=c11",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-include",
        str(header),
        "-I",
        str(source.parent),
    ]
    command.extend(["-I", ffi_libinfo.find_include_path()])
    executable = directory / "external-harness"
    command.extend(
        [
            str(harness.resolve()),
            str(source),
            str(artifact),
            "-L",
            str(library_dir),
            "-ltvm_runtime",
            "-ltvm_ffi",
            f"-Wl,-rpath,{library_dir}",
            "-o",
            str(executable),
        ]
    )
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("fixture", type=Path)
    parser.add_argument("kernel", type=Path)
    parser.add_argument("harness", type=Path)
    args = parser.parse_args()
    fixture = json.loads(args.fixture.read_text(encoding="utf-8"))
    validate_fixture(fixture)
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        kernel = compile_kernel(args.kernel, root)
        artifact = build(kernel, root)
        run_harness(artifact, args.harness, root)
    assert accepts_layout(
        fixture["layout"]["accepted"], fixture["layout"]["accepted"]
    )
    assert not accepts_layout(
        fixture["layout"]["rejected"], fixture["layout"]["accepted"]
    )
    print(json.dumps({"task": "external-kernel", "status": "pass"}))


if __name__ == "__main__":
    main()
