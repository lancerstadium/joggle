#!/usr/bin/env python3
"""TVM S0: import the frozen ONNX graph and replace only its Conv call."""

import argparse
import hashlib
import json
import os
import subprocess
from collections import Counter
from pathlib import Path

import numpy as np
import onnx
import tvm
from tvm import relax
from tvm.relax.frontend.onnx import from_onnx
from tvm.relax.expr_functor import mutator
from tvm.script import tirx as T


def static_shape(ty):
    return tuple(int(dim) for dim in ty.shape)


def make_external_conv(call):
    x_shape = static_shape(call.args[0].ty)
    w_shape = static_shape(call.args[1].ty)
    out_shape = static_shape(call.ty)
    n, c, h, w = x_shape
    m, q, r, s = w_shape
    _, _, oh, ow = out_shape
    stride_h, stride_w = [int(v) for v in call.attrs.strides]
    pad_top, pad_left, _, _ = [int(v) for v in call.attrs.padding]
    dil_h, dil_w = [int(v) for v in call.attrs.dilation]
    groups = int(call.attrs.groups)

    @T.prim_func(s_tir=True)
    def external_conv(x: T.handle, weight: T.handle, out: T.handle) -> None:
        image = T.match_buffer(x, x_shape, "float32")
        kernel = T.match_buffer(weight, w_shape, "float32")
        result = T.match_buffer(out, out_shape, "float32")
        T.evaluate(
            T.call_extern(
                "evolution_conv", image.data, kernel.data,
                T.int64(n), T.int64(h), T.int64(w), T.int64(m), T.int64(q),
                T.int64(r), T.int64(s), T.int64(oh), T.int64(ow),
                T.int64(stride_h), T.int64(stride_w), T.int64(pad_top),
                T.int64(pad_left), T.int64(dil_h), T.int64(dil_w),
                T.int64(groups), T.int64(c * h * w), T.int64(h * w),
                T.int64(w), T.int64(1), T.int64(q * r * s),
                T.int64(r * s), T.int64(s), T.int64(1),
                T.int64(m * oh * ow), T.int64(oh * ow), T.int64(ow),
                T.int64(1), result.data, dtype="void",
            )
        )

    return external_conv


@mutator
class ReplaceConv(relax.PyExprMutator):
    def __init__(self, mod, target):
        super().__init__(mod)
        self.target = target
        self.replaced = 0

    def visit_call_(self, call):
        rewritten = super().visit_call_(call)
        if isinstance(call.op, tvm.ir.Op) and call.op.name == "relax.nn.conv2d":
            self.replaced += 1
            return relax.call_tir(self.target, relax.Tuple(rewritten.args), call.ty)
        return rewritten


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("fixture", type=Path)
    parser.add_argument("out", type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    model = onnx.load(args.fixture)
    mod = from_onnx(model)
    fn = mod["main"]
    calls = [binding.value for block in fn.body.blocks
             for binding in block.bindings
             if isinstance(binding.value, relax.Call)]
    counts = Counter(call.op.name for call in calls
                     if isinstance(call.op, tvm.ir.Op))
    assert counts == {"relax.nn.conv2d": 1, "relax.add": 1,
                      "relax.nn.relu": 1}, counts
    conv = next(call for call in calls if call.op.name == "relax.nn.conv2d")
    (args.out / "imported.ir").write_text(
        mod.script(show_meta=True), encoding="utf-8"
    )
    rewrite = ReplaceConv(mod, None)
    rewrite.target = rewrite.builder_.add_func(
        make_external_conv(conv), "evolution_external_conv"
    )
    rewrite.builder_.update_func(mod.get_global_var("main"),
                                 rewrite.visit_expr(fn))
    mod = rewrite.builder_.get()
    assert rewrite.replaced == 1, rewrite.replaced
    (args.out / "selected.ir").write_text(
        mod.script(show_meta=True), encoding="utf-8"
    )

    repo = Path(__file__).resolve().parents[4]
    kernel = repo / "paper/evolution/common/s0/kernel.c"
    kernel_object = args.out / "kernel.o"
    subprocess.run(
        [os.environ.get("CC", "cc"), "-std=c11", "-O2", "-c",
         str(kernel), "-o", str(kernel_object)], check=True
    )
    executable = relax.build(mod, target="llvm")
    library = args.out / "model.so"
    executable.export_library(str(library), addons=[str(kernel_object)])
    loaded = tvm.runtime.load_module(str(library))
    vm = relax.VirtualMachine(loaded, tvm.cpu())
    x_shape = static_shape(fn.params[0].ty)
    input_values = np.fromfile(args.fixture.parent / "input.bin", dtype=np.float32)
    expected = np.fromfile(args.fixture.parent / "expected.bin", dtype=np.float32)
    actual = vm["main"](
        tvm.runtime.tensor(input_values.reshape(x_shape), tvm.cpu())
    ).numpy().ravel()
    if actual.size != expected.size:
        raise AssertionError((actual.size, expected.size))
    error = float(np.max(np.abs(actual - expected)))
    if not np.allclose(actual, expected, rtol=1e-5, atol=1e-6):
        raise AssertionError(f"output differs; max abs error={error}")
    result = {
        "case": args.fixture.parent.name,
        "source_ops": dict(counts),
        "replaced_conv_calls": rewrite.replaced,
        "output_elements": int(actual.size),
        "max_abs_error": error,
        "artifact_sha256": hashlib.sha256(library.read_bytes()).hexdigest(),
        "artifact_bytes": library.stat().st_size,
    }
    (args.out / "result.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()
