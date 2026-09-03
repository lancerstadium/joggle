# Intermediate representation

Joggle has one executable IR, not a graph object beside a low-level IR. A
`Module` owns named Functions and immutable data. Each materialized `Function`
owns Blocks, Ops, Values, and terminators. This representation is sufficient
for acyclic neural networks, nested calls, and arbitrary control flow.

```text
Module
├─ data[sha256:…] -> immutable bytes
└─ Function
   ├─ Block arguments
   ├─ Op(callee, operands, properties) -> results
   └─ return | jump | branch
```

## Op schema

An Op is a residual call to a declared `fn`. The function signature is its only
schema. Joggle derives two views over the ordered call arguments:

- `operands()` contains inputs whose declared types are IR value types. They
  form SSA def-use edges.
- `properties()` contains inputs in compiler domains such as `int`, `real`,
  `string`, `type`, `attr`, and their lists. They are immutable and named by
  the declaration.

Knownness does not change the role. A Known value passed to an `i32` input is
still an operand; a Known integer passed to an `int` input is a property. The
same declaration therefore drives source calls, parsing, verification, C++
construction, pattern matching, and native behavior.

For example, `onnx.conv` has tensor operands and list/integer properties:

```joggle
fn conv<X: type, W: type, Y: type>(
  input: X,
  weight: W,
  strides: list<int>,
  dilations: list<int>,
  pads: list<int>,
  group: int
) -> Y;
```

A fusion rule follows `Value::defining_op()` and `Value::users()`. A
quantization or layout rule reads named properties. Neither needs to decode a
flat argument list or keep a private copy of the operator definition.

## Transformations

A transformation is an ordinary compiler function such as
`fn fuse(module) -> module`. Its implementation can use `rewrite`,
`rewrite_to_fixpoint`, or `convert`. These utilities edit a private copy,
verify the complete result, and publish it only on success. `convert` adds a
caller-defined legality check, so a conversion can be partial internally but
cannot leak a mixed representation accidentally.

`clone` reconstructs an arbitrary CFG while mapping each source Value's type
and, optionally, callees. It preserves Known properties, Blocks, edges, Block
arguments, function references, and result positions, then verifies the
complete cloned Function once. Representation-changing passes therefore do
not need a private straight-line graph copier.

Joggle does not number IR levels. Vocabularies are Modules and conversions are
explicit edges. The shipped ONNX path demonstrates this rule:

```text
bytes ──onnx.read──> onnx.* IR ──onnx.to_nn──> nn.* IR
```

Source-level fusion may run on `onnx.*`; portable inference fusion may run on
`nn.*`; a project-specific tensor-to-storage conversion may produce its own
memory or hardware vocabulary. These are all the same IR ownership model, so
analyses and rewrite infrastructure remain reusable.

## Data and storage

Large constants are not another graph and are not pass side channels.
`Module::store` content-addresses immutable bytes, and constant Ops reference
the returned name. Copies share payloads until a transformation publishes a
new Module. `tensor.immutable_data` is the semantic interface used to discover
constant-producing functions across vocabularies.

Tensor storage planning is intentionally a distinct representation problem:
`tensor.ranked` describes values, while a storage Module describes allocation,
layout, address space, and accesses. A storage pass consumes one Module and
returns another; device capacity or scheduling policy belongs in explicit
Module-declared inputs, not in the core IR. The `mem` vocabulary supplies open
reference, layout, space, alias, and effect contracts rather than claiming that
one memory model fits all accelerators.

## Current boundary

The core supports multi-Block SSA, dominance, reverse-use queries,
transactional edits, conversion legality, CFG-preserving clone, and
Module-owned data. The shipped precision transformation now uses the same clone
facility, but does not yet preserve calls between transformed Functions.
Symbol-aware interprocedural rewriting and a portable on-disk bundle for Module
data remain explicit implementation milestones.
