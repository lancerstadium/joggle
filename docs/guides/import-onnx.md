---
title: Import ONNX
description: Decode source operations, infer types, convert semantics, and diagnose unsupported boundaries.
---

# Import ONNX

## Build frontend support

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DJOGGLE_BUILD_ONNX=ON
cmake --build build
```

## Decode

```sh
./build/joggle read onnx.read model.onnx \
  -M build/modules > source.jog
./build/joggle check source.jog -M build/modules > /dev/null
```

The source retains ONNX operation names. A simplified region may look like:

```jog
fn main(x: tensor<f32, [1, 4]>) -> _ {
  let y = onnx.Relu(x)
  return y
}
```

Actual declarations, names, and attributes come from the model.

## Infer

```sh
./build/joggle run onnx.nn.infer source.jog \
  -M build/modules > inferred.jog
```

Inference may close the result type while preserving source identity:

```jog
fn main(x: tensor<f32, [1, 4]>) -> tensor<f32, [1, 4]> {
  let y = onnx.Relu(x)
  return y
}
```

## Convert

```sh
./build/joggle run onnx.nn.convert inferred.jog \
  -M build/modules > semantic.jog
```

Supported calls become shared semantics:

```jog
fn main(x: tensor<f32, [1, 4]>) -> tensor<f32, [1, 4]> {
  let y = nn.relu(x)
  return y
}
```

## Inspect closure

```sh
./build/joggle query opt.unresolved semantic.jog -M build/modules
./build/joggle query opt.untyped semantic.jog -M build/modules
```

Non-empty results identify the remaining boundary. Do not add a target stage to
hide a missing semantic conversion.

## Evidence boundary

Resolved and typed IR proves structural closure. Numerical support additionally
uses a pinned model, inputs, and reference outputs.
