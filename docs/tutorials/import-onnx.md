---
title: Import ONNX
description: Decode an ONNX model, inspect source operations, and convert semantics explicitly.
---

# Import ONNX

This guide requires Protobuf and an ONNX-enabled build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DJOGGLE_BUILD_ONNX=ON
cmake --build build
```

## Decode without hidden conversion

```sh
./build/joggle read onnx.read model.onnx \
  -M build/modules > source.jog
```

`onnx.read` preserves the source-format boundary. Unsupported operations remain
visible as ONNX calls; the reader does not silently replace them with shared
semantics.

Check what the frontend can resolve:

```sh
./build/joggle query opt.unresolved source.jog -M build/modules
```

## Refine and convert explicitly

```sh
./build/joggle run onnx.nn.infer onnx.nn.convert source.jog \
  -M build/modules > semantic.jog
```

Inference refines types and attributes. Conversion rewrites supported ONNX
relations to shared `nn`, `tensor`, `quant`, and scalar functions. Keeping the
two names visible makes the stage boundary inspectable and testable.

## Validate the maintained surface

The `onnx-codec` test checks decoding behavior. Pinned model gates use the
`model` label and require the configured fixtures:

```sh
ctest --test-dir build -R onnx-codec --output-on-failure
ctest --test-dir build -L model --output-on-failure
```

Unknown models are not evidence of support. Add a pinned input, reference
behavior, and named regression before documenting a new model family.

Next: [Emit C](emit-c.md).
