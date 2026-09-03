# Precision Module

This optional Module implements a representation-changing compiler function:

```joggle
fn f32_to_f16(input: module) -> module;
```

IR and immutable data are copied on write and published as one result. Failure
to rebuild or verify any Function, resolve any constant, or validate any
payload returns no result. There is no pass class, sidecar resource value, or
mutation of the caller's Module.

A complete source-defined composition remains ordinary code:

```joggle
fn compile(input: bytes) -> module {
  model = onnx.read(input);
  return precision.f32_to_f16(model);
}
```

The current function changes every `tensor.ranked<f32, S>` signature and
result to `tensor.ranked<f16, S>`, rebuilds calls through their existing
declarations, and re-encodes referenced f32 constants using deterministic
round-to-nearest-even IEEE binary16. It recognizes constants through the
`tensor.immutable_data` interface, so it works before or after `onnx.to_nn`.
New payloads receive `sha256:<digest>` names and replaced f32 payloads are
removed.

The transformation preserves arbitrary committed CFGs through the shared
`joggle::clone` facility. It rejects missing data, payload/type size
disagreements, unsupported calls, and calls between members of the input
Module rather than publishing a partial conversion. Other element types pass
through unchanged. Symbol-aware interprocedural reconstruction remains future
work.

Build and test independently with:

```sh
cmake -S . -B build-precision -DJOGGLE_BUILD_PRECISION=ON
cmake --build build-precision
ctest --test-dir build-precision -R '^precision$' --output-on-failure
```

When both optional Modules are enabled and `JOGGLE_ONNX_MODEL` names the
official opset-18 ResNet-18 file, CMake also registers `precision_onnx`. That
test composes ONNX decoding and precision conversion in Joggle source. The
current reference model converts 42 payloads from 46,738,848 to 23,369,424
bytes and produces a deterministic 91-op f16 Module. This is a
compiler semantic and artifact-size test; it makes no accuracy or performance
claim.
