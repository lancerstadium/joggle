# ONNX-MLIR S0: external convolution, portable epilogue

This snapshot consumes the two frozen `Conv -> Add -> Relu` models unchanged.
The pinned ONNX-MLIR checkout is `4a13c34a`. The existing
`--ops-for-call=Conv` path creates one `krnl.call`, while ONNX-MLIR retains
Add and ReLU in its normal compilation path.

The unmodified compiler stops during Krnl-to-LLVM conversion because its
`KrnlCall.cpp` handler does not accept the integer `ArrayAttr` values copied
from ONNX Conv's `pads` and `strides`. It exits 134 with
`This type of Attribute used by krnl.call is not yet implemented`. This is
an observed host-boundary edit, not evidence that ONNX-MLIR lacks an external
Conv route. Apply the exact
[`krnl-call-array-attr.patch`](krnl-call-array-attr.patch) to a clean checkout
of that revision before building the `onnx-mlir` target. The patch passes
`git apply --reverse --check` in the checkout used here.

[`conv.c`](conv.c) is ONNX-MLIR-specific ABI glue. The emitted LLVM declaration
is `Conv(ptr, ptr, ptr, ptr, i64, ptr, i64, i64, i64, i64, i64, i64)`:
three OMTensors, `auto_pad`, `group`, `node_name`, four pads, and two strides.
The wrapper validates ranks, dtype, shapes, and this fixture's symmetric-pad
contract, then calls the byte-identical supplied
[`common/s0/kernel.c`](../../common/s0/kernel.c). It does not implement Add,
ReLU, a model entry, or a reference-runtime fallback. The normal ONNX-MLIR
`--EmitLib` path links the wrapper and shared kernel from a static archive via
`-L` and `-l`.

From the Joggle repository root, with the pinned checkout at
`/Users/lancer/Documents/Item/joggle-study/onnx-mlir`:

```sh
git -C /Users/lancer/Documents/Item/joggle-study/onnx-mlir apply \
  "$(pwd)/paper/evolution/onnx-mlir/s0/krnl-call-array-attr.patch"
cmake --build /Users/lancer/Documents/Item/joggle-study/onnx-mlir/build-sat \
  --target onnx-mlir -j 4
mkdir -p build-study/evolution/onnx-mlir/s0/lib
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-strict-prototypes \
  -I /Users/lancer/Documents/Item/joggle-study/onnx-mlir/include \
  -c paper/evolution/onnx-mlir/s0/conv.c \
  -o build-study/evolution/onnx-mlir/s0/lib/conv.o
cc -std=c11 -O2 -Wall -Wextra -Wstrict-prototypes -Werror \
  -c paper/evolution/common/s0/kernel.c \
  -o build-study/evolution/onnx-mlir/s0/lib/kernel.o
ar rcs build-study/evolution/onnx-mlir/s0/lib/libevolution.a \
  build-study/evolution/onnx-mlir/s0/lib/conv.o \
  build-study/evolution/onnx-mlir/s0/lib/kernel.o
```

The ONNX-MLIR installed header itself has a legacy non-prototype declaration;
`-Wno-strict-prototypes` is limited to the wrapper compilation. Complete the
artifact and numerical gates with:

```sh
for case_name in eligible fallback; do
  out_dir="build-study/evolution/onnx-mlir/s0/$case_name"
  mkdir -p "$out_dir"
  /Users/lancer/Documents/Item/joggle-study/onnx-mlir/build-sat/Release/bin/onnx-mlir \
    --ops-for-call=Conv --O0 --EmitLib \
    -L"$(pwd)/build-study/evolution/onnx-mlir/s0/lib" -levolution \
    "paper/fixtures/evolution/$case_name/model.onnx" -o "$out_dir/model"
  PYTHONPATH=/Users/lancer/Documents/Item/joggle-study/onnx-mlir/build-sat/Release/lib \
    uv run --python 3.14 --with numpy python \
    paper/evolution/onnx-mlir/s0/run.py "$case_name" "$out_dir/model.so"
done
```

The generated `.so` and intermediate IR belong under ignored
`build-study/evolution/onnx-mlir/s0/<case>/`. [`run.py`](run.py) consumes only
the frozen `input.bin`/`expected.bin` oracle. The tested artifacts match both
100-element oracles bitwise; digests and boundary classification are in
[`result.json`](result.json).

This S0 implementation is a functional preflight, not a cross-system-frozen
measurement. No timing from this macOS smoke test enters the manuscript.
