# Reference target Module

`reference_target` is a concrete, inspectable example of a user-defined AI
target. It is not a compiler-core target registry. The Module owns its storage
spaces, layouts, reference type, supported operator vocabulary, mapping
function, and resource analysis using the same declarations and native binding
mechanism available to an external package.

`map(input, tile_rows, tile_columns)` converts ranked `tensor` values and the
supported `nn` calls to `reference_target.ref` values and target calls while
preserving CFG, SSA edges, named properties, Function signatures, and immutable
Module data. Function inputs use linear `io` storage, immutable data uses
linear `read_only` storage, and rank-4 intermediates use tiled `local` storage.
The choice is explicit in every resulting type; no device state is hidden in
the core IR.

`local_bytes_upper_bound(input)` sums all local SSA references using the
element type's declared `storage_bits`, including sub-byte custom formats. It
is intentionally named as a conservative upper bound rather than a liveness
peak. A later allocation or reuse function can tighten the bound without
changing the target IR contract.

Build and test the package with:

```sh
cmake -S . -B build-reference \
  -DJOGGLE_BUILD_MODULES=reference_target
cmake --build build-reference
ctest --test-dir build-reference \
  -R '^module\.reference_target$' --output-on-failure
```

The current supported calls cover the ResNet-18 path: immutable constants,
convolution with or without bias, batch normalization, ReLU, residual add,
max pooling, global average pooling, flatten, and linear. Unsupported tensor
calls fail transactionally instead of leaking a mixed tensor/reference graph.
When `onnx` and the official `JOGGLE_ONNX_MODEL` are enabled, the
`module.reference_target.onnx` integration test exercises the complete
`onnx.read -> onnx.to_nn -> reference_target.map` composition.
