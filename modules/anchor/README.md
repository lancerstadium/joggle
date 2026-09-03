# Anchor Module

`anchor` is a concrete, inspectable reference point for a user-defined AI
target. It is not a compiler-core target registry. The Module owns its storage
spaces, layouts, reference type, supported operator vocabulary, mapping
function, and resource analysis using the same declarations and native binding
mechanism available to an external package.

The implementation boundary is visible in the source. `load` and `store` are
bodyless target primitives tagged with the ordinary `mem.read` and `mem.write`
interfaces. Their `offset` is a logical row-major ordinal; the reference layout
is responsible for mapping that ordinal to physical storage. `relu`, `add`,
unbiased `conv2d_nchw`, and `linear` are not opaque target calls: they are
generic Joggle bodies built from `mem.alloc`, `load`, `store`, arithmetic, and
typed residual `for`. Each materializes a fixed-size CFG instead of
trip-count-sized unrolling. `linear` uses three nested loops and preserves the
declared row-major reduction order, including bias initialization. Convolution
uses seven nested NCHW loops, ordered `C -> KH -> KW` accumulation, stride and
dilation arithmetic, and guarded top/left subtraction so zero-padding never
underflows the unsigned `index` type; bottom/right padding participates in the
output extent. Concrete target calls recover their element, shape, layout, and
space generics before the body is materialized. A target package may replace
these bodies or use different primitives without adding a kernel class or
declaration kind. Compiler functions that inspect or rewrite a whole Module
remain native behavior for now; target computation itself does not require a
host callback.

The biased `conv2d_nchw` overload composes that same convolution body with an
in-place `N -> O -> H -> W` bias epilogue. This avoids maintaining a duplicate
seven-loop convolution and makes the unfused epilogue an explicit typed call
graph boundary that a later target optimization can inline or fuse.

`max_pool2d_nchw` uses six residual loops and carries both an accumulator and
an initialization bit. The first in-bounds element seeds the maximum, so
zero-padding is excluded rather than incorrectly competing with negative input
values. If a window has no in-bounds element, the reference body stores zero;
targets that admit such windows and require a format-specific minimum or
infinity can replace this explicit policy. `global_average_pool_nchw`
accumulates in deterministic `H -> W` order and materializes the divisor in
the element type. `flatten_nchw`
copies logical row-major ordinals, allowing the destination layout to differ
without changing tensor semantics.

`batch_norm_nchw` accepts floating-point elements and implements the inference
equation `scale * (input - mean) / sqrt(variance + epsilon) + bias`. Scale,
bias, mean, and variance are loaded once per channel; values are traversed in
deterministic `N -> C -> H -> W` order. Square root remains an ordinary typed
`arith.sqrt` call, so a target can lower or replace it through the same Module
and function mechanisms as other arithmetic.

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

`config<...>` is an ordinary Module-defined type conforming to `machine`. Its
fields state parallel lanes, MACs per lane, local and external byte rates,
scratch capacity, and launch latency. `cycles(input, target)` validates the
storage plan and then evaluates every supported call with the explicit model

```text
launch + max(compute work / throughput,
             local bytes / local rate,
             external bytes / external rate)
```

before summing the calls in the materialized Functions. Compute and transfers
are therefore assumed to overlap within a call, while calls execute in source
order. The result is a deterministic analytical estimate under those declared
assumptions, not measured device latency. A target whose scratch capacity is
smaller than the validated plan is rejected.

`emit(input, target)` returns a portable byte manifest containing the Module
digest, target parameters, required scratch, estimated cycles, and the
canonical planned Module. It is an inspectable deployment artifact rather
than machine code. A source Module can compose the complete path directly:

```joggle
fn deploy(input: bytes, target: type) -> bytes {
  source = onnx.read(input);
  model = onnx.to_nn(source);
  mapped = anchor.map(model, 8, 8);
  planned = anchor.plan_storage(mapped);
  return anchor.emit(planned, target);
}
```

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
`onnx.read -> onnx.to_nn -> anchor.map -> anchor.plan_storage -> anchor.emit`
composition. With `config<16, 4, 32, 16, 16777216, 4>`, the checked result is
10,946,464 scratch bytes and 29,453,374 analytical cycles. Repeated compilation
produces the same Module digest and byte-identical manifest.
