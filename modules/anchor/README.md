# Anchor Module

`anchor` is a concrete, inspectable reference point for a user-defined AI
target. It is not a compiler-core target registry. The Module owns its storage
spaces, layouts, reference type, supported operator vocabulary, mapping
function, and resource analysis using the same declarations and native binding
mechanism available to an external package.

`map(input, tile_rows, tile_columns)` converts ranked `tensor` values and the
supported `nn` calls to `anchor.ref` values and target calls while
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

`plan_storage(input)` performs deterministic first-fit reuse over exact
reference types. It records each decision as the ordinary typed call
`place(value, slot)`; this is a compile-time placement annotation on the
value's producer, not a runtime copy or a sidecar plan. `scratch_bytes(input)`
reconstructs every placed live interval, rejects incompatible or overlapping
slot reuse, and sums the unique slot sizes. For the official ResNet-18 model,
the current plan reduces the conservative 22,988,704-byte SSA sum to a
validated 10,946,464-byte static scratch arena.

Build and test the package with:

```sh
cmake -S . -B build-anchor \
  -DJOGGLE_BUILD_MODULES=anchor
cmake --build build-anchor
ctest --test-dir build-anchor \
  -R '^module\.anchor$' --output-on-failure
```

The current supported calls cover the ResNet-18 path: immutable constants,
convolution with or without bias, batch normalization, ReLU, residual add,
max pooling, global average pooling, flatten, and linear. Unsupported tensor
calls fail transactionally instead of leaking a mixed tensor/reference graph.
When `onnx` and the official `JOGGLE_ONNX_MODEL` are enabled, the
`module.anchor.onnx` integration test exercises the complete
`onnx.read -> onnx.to_nn -> anchor.map -> anchor.plan_storage` composition.
