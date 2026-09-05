# Getting started

This guide runs the implemented MatMul–Relu path. It demonstrates a normal NN
composition rather than a declaration-only or synthetic rewrite fixture.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The tensor package has a native compiler-time implementation at
`build/mods/tensor/native` with the platform library suffix.

## Define the model

Create `model.joggle`:

```joggle
joggle 1;

mod model@1.0.0 {
  import nn@2 as n;
  import tensor@7 as t;

  fn main(
    lhs: t.tensor<f32, [2, 4]>,
    rhs: t.tensor<f32, [4, 3]>
  ) -> t.tensor<f32, [2, 3]> {
    product = n.matmul(lhs, rhs);
    return n.relu(product);
  }
}
```

Validate it with the shipped semantic packages:

```bash
build/joggle check model.joggle \
  --with mods/arith/mod.joggle \
  --with mods/tensor/mod.joggle \
  --with mods/nn/mod.joggle
```

## Run the passes from C++

```cpp
#include <iostream>
#include <joggle/joggle.h>

int main() {
  joggle::Compiler compiler;
  compiler.load("mods/arith/mod.joggle");
  compiler.load("mods/tensor/mod.joggle");
  compiler.load("mods/nn/mod.joggle");
  compiler.load("model.joggle");

  if (!compiler.link() ||
      !compiler.load_native("tensor", "build/mods/tensor/native.dylib")) {
    compiler.diag().print(std::cerr);
    return 1;
  }

  auto model = compiler.materialize("model.main");
  auto fused = model
      ? compiler.run<joggle::Fn>("tensor.fuse", *model)
      : std::nullopt;
  auto looped = fused
      ? compiler.run<joggle::Fn>("tensor.loops", *fused)
      : std::nullopt;

  if (!looped || !compiler.verify(*looped)) {
    compiler.diag().print(std::cerr);
    return 1;
  }

  std::cout << joggle::format(*looped, "main");
}
```

Use `.so` on Linux and `.dll` on Windows. Installed packages place the native
library beside `mods/tensor/mod.joggle`.

## What to inspect

The original Fn contains calls to `nn.matmul` and `nn.relu`.

The fused Fn contains one `tensor.tensor` call. Its callback contains input
indexing, a `tensor.reduce`, scalar multiplication/addition, and `arith.max`.
There is no intermediate MatMul tensor and no `tensor.map`.

The loop Fn contains `tensor.empty`, input `tensor.[]`, output `tensor.set`, and
CFG loops. It contains no `tensor.tensor`, `tensor.map`, or `tensor.reduce`.

No stage contains a `Graph`, `Fusion`, `Kernel`, `view`, `sink`, memory token,
or NN-name-specific lowering operation.

## Next

- [Tensor pipeline](pipeline.md) defines the transformation contracts.
- [Language](language.md) defines source syntax and staging.
- [C++ API](api.md) covers programmatic construction and editing.
- [Packages](mods.md) explains how extensions are divided.
