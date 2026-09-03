# Precision Module

This optional extension implements a resource-aware precision transformation
as one ordinary typed function:

```joggle
fn f32_to_f16(
  input: module,
  resources: resource.set
) -> (module, resource.set);
```

Both arguments are values and both results are published together. Failure to
rebuild or verify any Function, resolve any constant, or validate any payload
returns neither result. There is no pass class, global resource manager, or
mutation of the caller's Module.

A complete source-defined composition remains ordinary code:

```joggle
fn compile(input: bytes) -> (module, resource.set) {
  model, resources = onnx.read(input);
  output, converted = precision.f32_to_f16(model, resources);
  return output, converted;
}
```

The current function changes every `tensor.ranked<f32, S>` signature and
result to `tensor.ranked<f16, S>`, rebuilds dependent NN calls through their
existing declarations, and re-encodes referenced f32 constants using
deterministic round-to-nearest-even IEEE binary16. New payloads receive
`sha256:<digest>` names and replaced f32 payloads are removed.

The exact initial profile accepts fully materialized, straight-line Functions
without calls between members of the input Module. It rejects missing
resources, payload/type size disagreements, unsupported calls, internal calls,
and control flow rather than publishing a partial conversion. Other element
types pass through unchanged. This bounded profile matches the current ONNX
importer; CFG- and internal-call-preserving reconstruction is future work, not
implied by the function name.

Build and test independently with:

```sh
cmake -S . -B build-precision -DJOGGLE_BUILD_PRECISION=ON
cmake --build build-precision
ctest --test-dir build-precision -R '^precision$' --output-on-failure
```

When both optional extensions are enabled and `JOGGLE_ONNX_MODEL` names the
official opset-18 ResNet-18 file, CMake also registers `precision_onnx`. That
test composes ONNX decoding and precision conversion in Joggle source. The
current reference model converts 42 resources from 46,738,848 to 23,369,424
bytes and produces a deterministic 91-instruction f16 Module. This is a
compiler semantic and artifact-size test; it makes no accuracy or performance
claim.
