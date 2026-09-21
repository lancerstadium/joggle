# IKJ matrix multiplication mod

This package replaces the shared rank-two `tensor.matmul` body with an
`i-k-j` loop order. It is deliberately an implementation module, not a target,
schedule object, kernel IR, or C++ registration.

The complete implementation is [`module.jog`](module.jog). `impls` selects
ordinary body-bearing functions using open metadata, and `apply` hands them to
the reusable overload matcher and body expander. Generic element type and
dimensions are inferred from each call.

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

build/joggle run ikj.apply c.prepare examples/ikj/model.jog \
  -M examples -M build/modules > build/ikj.jog
build/joggle emit c.source build/ikj.jog \
  -M examples -M build/modules > build/ikj.c
build/joggle emit c.header build/ikj.jog \
  -M examples -M build/modules > build/ikj.h
cc -std=c99 -Wall -Wextra -Werror \
  -include build/ikj.h \
  build/ikj.c examples/ikj/main.c -o build/ikj
build/ikj
```

The first command sequence performs two explicit ordinary transforms:

1. `ikj.apply` selects and exposes the alternative function body for `f32`
   matrix multiplication; integer and quantized calls remain on their own
   implementation path.
2. `c.prepare` exposes only the remaining computation that C cannot emit
   directly.

`c.source` and `c.header` stay read-only. The harness consumes only the
generated declaration. Inspect `build/ikj.jog` to see the actual loop body
received by the emitter; no hidden lowering or target registry is used.

Every test build with the optional ONNX module enabled runs the frozen
`onnx-matched-implementation` fixture from the paper workspace. It imports the
contract's exact `2x3` by `3x2` model, converts it to shared tensor semantics,
applies this same module, and checks both VM and compiled C results against the
same TensorProto output. When a separate official ONNX backend checkout is
configured, `onnx-execution` additionally runs ONNX's pinned
`test_matmul_2d`; the two gates intentionally serve different purposes.
