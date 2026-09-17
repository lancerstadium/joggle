# Emit an exposed kernel as C

The standard `c` module consumes computation only after its dependency calls
have been exposed into local scalar, tensor-access, loop, and branch structure:

```sh
mkdir -p build/examples
joggle emit c.source test/data/c.jog -M build/modules > build/examples/model.c
joggle emit c.header test/data/c.jog -M build/modules > build/examples/model.h
cc -std=c99 -Wall -Wextra -Wstrict-prototypes -Werror \
  -include build/examples/model.h \
  build/examples/model.c test/data/c_main.c \
  -o build/examples/model
build/examples/model
```

`c.source` and `c.header` are ordinary read-only `fn(Mod) -> str` functions;
there is no host-level header artifact case. The commands do not choose a
target pipeline or mutate the input. Static tensors become flat C arrays and
tensor results use an output-pointer parameter. If a `tensor` or `nn` call has
not been exposed, emission fails and names that call rather than performing an
implicit lowering.

Bounded dynamic tensors use the same flat representation. For example, a
parameter `x: tensor<i32, [_, 2]>` is emitted as `const int32_t* x,
int64_t x_dim_0`. A result of the same type is emitted as `int32_t* out,
int64_t* out_dim_0`. Run `mem.plan` before emission when a local
`tensor.make` needs deterministic fixed-capacity storage. `tensor.view` changes
the returned logical extents without allocating or introducing a descriptor.

The corresponding application interface is available as structured JSON:

```sh
joggle query c.api build/examples/prepared.jog -M build/modules
joggle query c.api test/data/c.jog --arg '"weights"' -M build/modules
```

The first command is the ordinary module introspection command; the second
reports exact C declarations together with shapes, element and byte counts,
representation classes, pointer passing, result names, and payload use.
Harnesses can consume this record instead of parsing `model.h`. Configured ABI
dictionaries are accepted by the matching `c.api` overloads just as they are
by preparation and emission. For a dynamic tensor, the record separates its
logical `shape` from its proved `capacity`; unknown allocation size is reported
as `-1`, not guessed. The ONNX example's `make_harness.py` is one such
consumer; it is not part of the compiler core or another ABI definition.

Passing a data name to `c.source` and `c.header` externalizes tensor constants.
The resulting pointer parameter is propagated through the generated call graph
only where a function directly or transitively reads that data. A scalar helper
that does not use model constants therefore keeps the same C signature in
embedded-data and external-data modes.

When the shared function bodies are the desired implementation, preparation is
another explicit function call:

```sh
joggle run c.prepare test/data/c_open.jog \
  -M build/modules > build/examples/prepared.jog
joggle emit c.source build/examples/prepared.jog \
  -M build/modules > build/examples/add.c
```

`c.prepare` expands only unsupported, metadata-free calls with a visible body
and stops at the C module's scalar/tensor-access/control-flow boundary. It is
transactional and bounded; it is not run by `emit` or module loading.

Shared tensor bodies contain small `[stage: "shape"]` loops for rank- and
layout-dependent indexing. C preparation explicitly applies
`opt.specialize(m, "stage", "shape")` after exposure. The loop bounds and list
positions become compile-time structure, while coordinates remain ordinary
runtime values. Thus a custom layout function can use the same readable list
code without forcing the generated C to execute a rank loop inside every
element access. Other targets may select this convention, another attribute,
or no structural specialization at all.

A symbolic model entry can be bound before static target preparation without
changing the frontend. For example, instantiate batch `N` as one while keeping
the public function name `main`:

```sh
joggle run opt.instantiate semantic.jog \
  --arg '"main"' --arg '["1"]' -M modules > static.jog
```

Arguments follow the generic declaration order. Choosing a deployment shape is
therefore explicit user or target policy, not an ONNX decoding rule.

The default ABI uses signed 64-bit `int` and `index`. A target module can pass
sparse replacement descriptors to the configured overloads. The same value
must be used for preparation and emission because it defines both legality and
representation:

```jog
let abi = {
  index: {
    name: "int32_t", bytes: 4, kind: "signed", include: "stdint.h"
  },
  int: {
    name: "int32_t", bytes: 4, kind: "signed", include: "stdint.h"
  }
}
let changed = c.prepare(m, abi)
let source = c.source(m, abi)
let header = c.header(m, abi)
```

Each descriptor is one atomic record rather than independent spelling and size
switches. `c.header` gathers required includes from exported types;
`c.source` also examines internal values and adds `string.h` only if emitted
tensor copies use `memcpy`. Narrowing is an explicit ABI decision, not a range
proof; a deployment policy that claims equivalence should check `bounds`
facts first.

Plan reusable storage only when the experiment needs it:

```sh
joggle run mem.plan build/examples/prepared.jog \
  -M build/modules > build/examples/planned.jog
joggle query mem.buffers build/examples/planned.jog -M build/modules
joggle emit c.source build/examples/planned.jog \
  -M build/modules > build/examples/planned.c
```

`mem.plan` is an ordinary idempotent transform. It handles fixed-shape local
tensors and dynamic `tensor.make` results whose runtime extents have finite
nonnegative bounds. The underlying `mem.bound` transform records the proved
capacity shape as open `mem.capacity` metadata; an unknown extent, a mismatch
with a fixed type dimension, or element-count overflow remains unplanned. The
planner excludes parameters, constants, and returned bindings, and reuses a
slot only after the prior binding's last real use. Returned bindings represent
caller-owned storage rather than local workspace; a target may consequently
write the last tensor computation directly into its result buffer. `c.source`
reads `mem.slot` metadata if present; it does not run the planner. The planner
also marks a tensor fill as dead when a single carried loop provably covers the
whole linear or rectangular domain and writes each corresponding element
unconditionally before yielding. The C target omits only those marked fills;
conditional or indirect writes keep their initialized value. A device-specific
module may instead interpret or replace the same open metadata with its own
allocation policy.

The parameterized `c.place(m, "static")` transform changes only the C module's
workspace placement metadata. It is useful when a large deterministic
workspace must not consume the host stack; `"local"` removes that request.
Static placement is deliberately explicit because it trades reentrancy for a
fixed program-lifetime workspace.

For a caller that guarantees all pointer arguments of exported entries are
disjoint, annotate the contract explicitly:

```sh
joggle run c.noalias prepared.jog -M modules > noalias.jog
joggle emit c.source noalias.jog -M modules > model.c
```

Generated definitions already state the static minimum element count of each
non-empty fixed-shape tensor through C11 array parameters. The no-alias
contract adds `restrict` to those tensor parameters and to any external weight
payload. Headers retain their compatible, unqualified pointer form and `c.api`
records `noalias: true`. Use
`joggle run c.noalias noalias.jog --arg false` to remove the field without
deleting an existing `[c: {name: ...}]` binding. This is a user assertion, not
a dependence proof; do not enable it for in-place or otherwise overlapping
calls.

For private functions whose callers already use planned storage, request the
conservative proof instead of asserting an entry contract:

```sh
joggle run c.restrict planned.jog -M modules > restricted.jog
joggle emit c.source restricted.jog -M modules > model.c
```

The pass examines all calls to each private function. It accepts only distinct
workspace slots, distinct immutable payload tensors, and slot/payload pairs.
One repeated argument or otherwise unclassified pointer keeps the whole
function unqualified. Entries and functions that implicitly consume the
external payload are never inferred. Inspect `[c: {restrict: true}]` in the
resulting IR to see exactly which contract was proved.

Inspect deterministic structural measurements before or after any step:

```sh
joggle query stat.summary build/examples/prepared.jog -M build/modules
joggle query stat.summary build/examples/planned.jog -M build/modules
```

The returned dictionary is stable and directly diffable. `tensor_vals` and
`static_tensor_elems` describe represented IR values; `mem_slots` and
`mem_elems` describe an explicit storage plan. Device bytes, cycles, and
energy belong in a separate policy module rather than being guessed by `stat`.
