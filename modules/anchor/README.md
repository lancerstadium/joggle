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

The biased `conv_relu_nchw` and `batch_norm_relu_nchw` compose their existing
operator bodies with an in-place ReLU epilogue. `fuse_relu(input)` rewrites only a
same-Block producer whose result has that ReLU as its sole user; it therefore
cannot discard a value observed by another call. The transform replaces the
pair with the matching fused typed call and remains a normal `module -> module`
function rather than a target-specific pass class.

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
scratch capacity, and launch latency. `simulate(input, target)` validates the
storage plan and creates one ordered event per executable call with the model

```text
launch + max(compute work / throughput,
             local bytes / local rate,
             external bytes / external rate)
```

Compute and transfers are therefore assumed to overlap within a call, while
calls execute in source order. Each event records its Function and Op index,
start and end cycle, compute demand, local and external transfer demand, and
launch cost. The Module-owned `timeline` is an ordinary typed value;
`cycles(input, target)` is defined in Joggle as
`duration(simulate(input, target))`, while `trace(input, target)` serializes
the same timeline. This is a deterministic analytical simulation under the
declared assumptions, not measured device latency. A target whose scratch
capacity is smaller than the validated plan is rejected.

`bundle(program)` performs target-owned semantic linking. It copies the planned
Module, recursively specializes every non-administrative source call, inserts
each unique concrete body as a deterministic local Function, and rewrites its
callers to those local declarations. Known shape, stride, layout, and other
compile-time arguments disappear from the linked call edge; only its Residual
SSA operands remain. Content-addressed model data stays attached to the Module.
The result fails closed if any leaf is neither source-defined nor admitted by
the arithmetic, literal, allocation, memory-access, alias, or release
interfaces. A new target can define a different boundary without changing the
compiler core.

`emit(input, target)` first bundles the program, then returns an `anchor 3`
binary artifact. Its zero-terminated manifest contains both source and bundle
digests, target parameters, required scratch, estimated cycles, and the
canonical bundled Module. A versioned binary table then stores every immutable
payload under its content-addressed name. `unpack(artifact)` checks each
payload identity and the complete bundle digest, reconstructs a fresh linked
compilation, and rematerializes the exact bundle. Thus emission consumes the
linked source bodies rather than the high-level operator graph and the result
is self-contained with respect to model data. It is still not machine code. A
source Module can compose the complete path directly:

```joggle
fn deploy(input: bytes, target: type) -> bytes {
  source = onnx.read(input);
  model = onnx.to_nn(source);
  mapped = anchor.map(model, 8, 8);
  planned = anchor.plan_storage(mapped);
  return anchor.emit(planned, target);
}

fn inspect(input: bytes, target: type) -> bytes {
  source = onnx.read(input);
  model = onnx.to_nn(source);
  mapped = anchor.map(model, 8, 8);
  planned = anchor.plan_storage(mapped);
  return anchor.trace(planned, target);
}
```

`kernel_report(program)` reports the same fail-closed bundling traversal. For
every non-administrative call in a materialized program it specializes the
declared function body from the concrete call, recursively follows nested
bodies, and accepts leaves only when they implement the shared arithmetic,
literal, allocation, memory-access, alias, or release interfaces. An opaque
external call therefore fails instead of being silently treated as a kernel.
The report records root calls, unique concrete source specializations,
primitive sites, and maximum source-expansion depth. A separately declared
user Module is covered by the same analysis without registering its function
name with
Anchor.

On the checked ResNet-18 path, all 49 unfused compute calls close through 35
unique source specializations and 1,547 primitive sites at maximum depth two.
After nine fusions, 40 root calls close through 42 specializations and 1,609
primitive sites at maximum depth three. The increase is expected: each fused
body composes an existing convolution or normalization body with its residual
epilogue. These counts characterize source coverage; they are not instruction
counts or performance measurements.

`execute_f32(program, input)` is a bounded semantic executor for the current
f32 target vocabulary. It accepts one materialized, straight-line Function
with one f32 reference input and output, resolves immutable tensors from the
Module's content-addressed data, follows `place` aliases, and evaluates the
supported Anchor calls in Function order. The input and output are portable
little-endian f32 bytes whose shapes come from the Function signature. This is
an executable specification for differential testing, not the target's code
generator: it does not model physical tiled addressing, enact scratch slots,
or turn analytical cycles into measured latency.

Build and test the package with:

```sh
cmake -S . -B build-anchor \
  -DJOGGLE_BUILD_MODULES=anchor
cmake --build build-anchor
ctest --test-dir build-anchor \
  -R '^module\.anchor' --output-on-failure
```

The current supported calls cover the ResNet-18 path: immutable constants,
convolution with or without bias, batch normalization, ReLU, residual add,
max pooling, global average pooling, flatten, and linear. Unsupported tensor
calls fail transactionally instead of leaking a mixed tensor/reference graph.
When `onnx` and the official `JOGGLE_ONNX_MODEL` are enabled, the
`module.anchor.onnx` integration test exercises the complete
`onnx.read -> onnx.to_nn -> anchor.map -> [anchor.fuse_relu] ->
anchor.plan_storage -> anchor.simulate/emit` composition. With
`config<16, 4, 32, 16, 16777216, 4>`, the unfused reference has 140 planned
Ops, 49 simulated events, 10,946,464 scratch bytes, and 29,453,374 analytical
cycles. Nine Conv-ReLU fusions reduce these to 122 Ops, 40 events, 7,735,200
scratch bytes, and 29,161,690 cycles. Repeated compilation and simulation
produce the same Module digest and byte-identical artifacts. The checked f32
artifact contains 46,738,848 payload bytes across 42 resources; changing one
payload byte makes `unpack` reject it instead of publishing a partial Module.

If the configured Python interpreter provides NumPy and ONNX Runtime, CMake
also registers `module.anchor.onnx.numeric`. Its oracle is generated in the
build directory from the same pinned model with graph optimizations disabled
and a deterministic f32 input. The test executes the fused, storage-planned
Anchor Module through `execute_f32` and compares all 1,000 output logits. On
the checked development configuration the maximum absolute error is
`1.62125e-05`, the maximum error scaled by `1 + |reference|` is
`2.58669e-06`, and both implementations select class 905. The acceptance
bound is `1e-4` scaled error plus exact top-1 agreement. The test also rejects
an input whose byte count disagrees with the Function signature.

When `precision` is enabled, `module.anchor.precision.onnx` applies
`f32_to_f16` both before and after `onnx.to_nn`. The two legal compositions
converge to the same planned Module, resources, and timeline, demonstrating
that the format transform is not tied to one fixed conversion rung. The f16
artifact retains 122 Ops, nine fusions, and 40 events while reducing immutable
data to 23,369,424 bytes and scratch to 3,867,600 bytes. It therefore fits a
4 MiB `config` rejected by the f32 plan; under the same rates its analytical
duration is 28,848,157 cycles. This test covers representation, composition,
resource feasibility, and the declared performance model, not numerical
accuracy or measured latency.
