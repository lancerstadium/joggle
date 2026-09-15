# TVM S0 external Conv preflight

This is a **functional preflight**, not a publication timing result or a
cross-stage boundary measurement. It uses pinned TVM revision
`c7b458e946bc4266915da582457476bdcd9705ae`, built separately with
LLVM 21.1.8 on macOS arm64. The earlier non-LLVM TVM build remains untouched.

For each frozen ONNX fixture, `build.py` imports the original graph through
TVM's ONNX-to-Relax frontend. It checks that Relax contains exactly one
`relax.nn.conv2d`, one `relax.add`, and one `relax.nn.relu`. It adds one
shape-specialized TensorIR function whose body calls the supplied C
`evolution_conv` kernel, then replaces **only** the Relax Conv call with
`relax.call_tir`. Relax Add and ReLU remain in the imported program and are
handled by the ordinary TVM build pipeline. The generated `imported.ir` and
`selected.ir` under ignored `build-study/evolution/tvm/s0/<case>/` preserve
the before/after evidence, including constant metadata.

`relax.build(target="llvm")` exports a native `.so` with the common C kernel
object as an addon. `run.py` reloads that `.so` in a fresh process and checks
the frozen binary oracle bitwise. Both eligible and fallback fixtures pass;
see [`result.json`](result.json). The exported artifacts are *not* standalone
from TVM's runtime library, but do not depend on an ephemeral Python
`PackedFunc` registration or a live Python callback.

Reproduction from the Joggle repository root, after building pinned TVM with
LLVM support in a separate `build-llvm` directory:

```sh
export TVM_LIBRARY_PATH=/Users/lancer/Documents/Item/joggle-study/tvm/build-llvm/lib
export PYTHONPATH=/Users/lancer/Documents/Item/joggle-study/tvm/python
for case in eligible fallback; do
  uv run --with-requirements paper/fixtures/requirements.txt \
    --with apache-tvm-ffi==0.1.13.post3 python \
    paper/evolution/tvm/s0/build.py \
    paper/fixtures/evolution/$case/model.onnx \
    build-study/evolution/tvm/s0/$case
  uv run --with-requirements paper/fixtures/requirements.txt \
    --with apache-tvm-ffi==0.1.13.post3 python \
    paper/evolution/tvm/s0/run.py \
    paper/fixtures/evolution/$case \
    build-study/evolution/tvm/s0/$case/model.so
done
```

The absolute library paths identify the preflight checkout and are not a
portable installation recipe. Do not read the smoke-test elapsed time as a
compiler-efficiency comparison. The S0 boundary is only an initial extension
surface; adjacent-stage S1/S2 patches and matched Linux measurements are
required before any relative engineering-cost claim.
