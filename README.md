# Joggle

Joggle is a compact C++ compiler for people who co-design neural-network
software and edge hardware. Its central idea is deliberately small:

- a versioned `Mod` is the package and namespace;
- a typed `Fn` represents both a model graph and an explicit loop program;
- every IR operation is an ordinary call;
- compiler work is an ordinary fn invoked explicitly with `@`;
- new operators are defined by bodies, not C++ subclasses or lowering tables.

There is no `GraphIR`, `KernelIR`, dialect hierarchy, pass registry, target base
class, or generated declaration header. Different compilation stages are
verified forms of the same `Fn` object.

## A neural-network pipeline

```joggle
joggle 1;

mod example@1.0.0 {
  import nn@3 as n;
  import tensor@8 as t;

  pub fn model(
    lhs: t.tensor<f32, [2, 4]>,
    rhs: t.tensor<f32, [4, 3]>
  ) -> t.tensor<f32, [2, 3]> {
    product = n.matmul(lhs, rhs);
    return n.relu(product);
  }

  pub fn prepare(input: fn) -> fn {
    fused = @t.fuse(input);
    return @t.loops(fused);
  }
}
```

`tensor.fuse` expands bodyful semantic functions and composes producer access
into consumer demand. For MatMul followed by Relu, it produces one tensor
construction containing the reduction and activation, with no intermediate
MatMul tensor. `tensor.loops` then converts that fused construction and its
reduction into ordinary CFG loops. The two passes are explicit and neither
silently invokes the other.

The loop form still has tensor value semantics. `tensor.set` returns the next
tensor value; it is not a physical store. Storage reuse, layouts, packed
formats, instructions, and emission belong to later target packages after
fusion and scheduling decisions are complete.

## Extension model

A package is one `mod.joggle` file plus an optional native library for work
that cannot be expressed portably, such as decoding ONNX or writing an object
file. The source file is always the ABI authority.

```joggle
pub fn read(input: bytes, name: string = "model") -> mod;
pub fn optimize(input: fn) -> fn;
pub fn emit(input: mod) -> bytes;
```

Ordinary calls remain in the program. `@read(...)` or `@optimize(...)` asks the
compiler to execute that fn now. Users compose their own pipeline in normal
source instead of registering magic pass names.

## Shipped packages

- `arith@2`: scalar operations used by portable bodies;
- `tensor@8`: the tensor value, its small semantic basis, fusion, and loop
  expansion;
- `nn@3`: frontend-independent neural-network functions;
- `transform@4`: generic inlining, equational rewriting, and resolution;
- `quant@4`: quantize/dequantize semantics;
- optional `onnx@6`: a Protobuf reader that resolves directly to linked
  semantic functions.

There is intentionally no generic memory or device package in the current
system. Such a package will be admitted only when a real target demonstrates a
portable contract that is not already expressed by tensor values and ordinary
functions.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Enable the optional ONNX reader with `-DJOGGLE_BUILD_ONNX=ON` and provide a
Protobuf installation.

## Documentation

- [Documentation map](docs/README.md)
- [Architecture](docs/architecture.md)
- [Tensor compilation pipeline](docs/pipeline.md)
- [Language](docs/language.md)
- [C++ API](docs/api.md)
- [Package design](docs/mods.md)
- [Research scope](docs/research.md)
